/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "SshSessionWorker.h"

#include "SshHostKeyStore.h"

#include <QDateTime>
#include <QMetaType>
#include <QSocketNotifier>
#include <QTimer>

#include <utility>

namespace remote {

SshSessionWorker::SshSessionWorker(std::unique_ptr<ISshTransport> transport,
                                   int channelCap, QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
    , m_channelCap(channelCap > 0 ? channelCap : kDefaultCap)
{
    qRegisterMetaType<remote::SshSessionWorker::State>("remote::SshSessionWorker::State");
    qRegisterMetaType<remote::SshSessionWorker::ConnectParams>(
        "remote::SshSessionWorker::ConnectParams");
    qRegisterMetaType<remote::RemoteDirEntry>("remote::RemoteDirEntry");
    qRegisterMetaType<QList<remote::RemoteDirEntry>>("QList<remote::RemoteDirEntry>");
    qRegisterMetaType<remote::ExecKind>("remote::ExecKind");
}

SshSessionWorker::~SshSessionWorker()
{
    teardownNotifiers();
    if (m_keepaliveTimer) {
        m_keepaliveTimer->stop();
    }
    if (m_maintenanceTimer) {
        m_maintenanceTimer->stop();
    }
    if (m_transport) {
        m_transport->disconnect();
    }
}

// --- monotonic clock ---------------------------------------------------------

qint64 SshSessionWorker::monotonicMs() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

// --- state + timers ----------------------------------------------------------

void SshSessionWorker::setState(State s)
{
    if (m_state == s) {
        return;
    }
    const State prev = m_state;
    m_state = s;

    // FIX-3: start keepalive timer when entering Ready; stop otherwise.
    if (s == State::Ready) {
        ensureTimers();
        // Start fresh: a healthy idle connection never accrues 2 consecutive
        // misses because each keepalive send draws a server reply (a readable
        // edge → onSocketActivity sets the flag) well within the 15 s interval.
        // A silent drop accrues miss=1 at 15 s, miss=2 at 30 s → lost (~30 s),
        // matching D0 FIX-3.
        m_sawInboundSinceKeepalive = false;
        m_keepaliveMissCount = 0;
        m_keepaliveTimer->start(15000);
    } else if (prev == State::Ready) {
        if (m_keepaliveTimer) {
            m_keepaliveTimer->stop();
        }
    }

    emit stateChanged(s);
}

void SshSessionWorker::ensureTimers()
{
    if (!m_keepaliveTimer) {
        m_keepaliveTimer = new QTimer(this);
        m_keepaliveTimer->setSingleShot(false);
        connect(m_keepaliveTimer, &QTimer::timeout, this, &SshSessionWorker::onKeepaliveTick);
    }
    if (!m_maintenanceTimer) {
        m_maintenanceTimer = new QTimer(this);
        m_maintenanceTimer->setSingleShot(false);
        m_maintenanceTimer->setInterval(75); // 75 ms tick for connect poll + backoff
        connect(m_maintenanceTimer, &QTimer::timeout, this, &SshSessionWorker::onMaintenanceTick);
    }
}

void SshSessionWorker::armMaintenanceTimer()
{
    ensureTimers();
    if (!m_maintenanceTimer->isActive()) {
        m_maintenanceTimer->start();
    }
}

void SshSessionWorker::stopMaintenanceTimer()
{
    if (m_maintenanceTimer && m_maintenanceTimer->isActive()) {
        m_maintenanceTimer->stop();
    }
}

// --- FIX-2: unified channel counters ----------------------------------------

int SshSessionWorker::unifiedLiveCount() const
{
    int n = livePtyChannelCount(); // PTY Opening..Open
    // Exec ops that have been admitted (past Queued phase).
    for (const ExecOp &op : m_execOps) {
        if (op.phase != ExecPhase::Queued && op.phase != ExecPhase::Done) {
            ++n;
        }
    }
    // D1a: count each initialized SFTP lane as one live channel.
    if (m_sftpBulkInited) {
        ++n;
    }
    if (m_sftpMetaInited) {
        ++n;
    }
    return n;
}

int SshSessionWorker::liveDynamicCount() const
{
    // Dynamic = unified minus SFTP reserved slots actually in use.
    int sftp = (m_sftpBulkInited ? 1 : 0) + (m_sftpMetaInited ? 1 : 0);
    return unifiedLiveCount() - sftp;
}

int SshSessionWorker::liveLongLivedCount() const
{
    int n = 0;
    // All PTY channels in Opening..Open are long-lived.
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
        const ChPhase p = it.value().phase;
        if (p != ChPhase::Queued && p != ChPhase::Closed) {
            ++n;
        }
    }
    // Exec ops classified as LongLived that have been admitted.
    for (const ExecOp &op : m_execOps) {
        if (op.kind == ExecKind::LongLived
            && op.phase != ExecPhase::Queued && op.phase != ExecPhase::Done) {
            ++n;
        }
    }
    return n;
}

int SshSessionWorker::livePtyChannelCount() const
{
    int n = 0;
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
        const ChPhase p = it.value().phase;
        if (p != ChPhase::Queued && p != ChPhase::Closed) {
            ++n;
        }
    }
    return n;
}

SshSessionWorker::Channel *SshSessionWorker::channelByTransportId(int transportId)
{
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (it.value().transportId == transportId) {
            return &it.value();
        }
    }
    return nullptr;
}

// --- connection entry points -------------------------------------------------

void SshSessionWorker::startConnect(const ConnectParams &params)
{
    m_params = params;
    m_lost = false;
    m_hostKeyEmitted = false;
    m_hostKeyAccepted = false;
    m_hostKeyRejected = false;
    m_sftpBulkInited = false;
    m_sftpMetaInited = false;
    m_connectDeadlineMs = monotonicMs() + kConnectPhaseTimeoutMs;
    setState(State::ConnectingSocket);
    pump();
    // FIX-4/FIX-5: arm the maintenance timer for the entire connect phase
    // (socket poll + handshake/auth deadline enforcement). It stays running until
    // Ready or connection-lost.
    armMaintenanceTimer();
}

void SshSessionWorker::acceptHostKey()
{
    m_hostKeyAccepted = true;
    // Reset the deadline — the user may have spent minutes on the host-key prompt.
    m_connectDeadlineMs = monotonicMs() + kConnectPhaseTimeoutMs;
    pump();
}

void SshSessionWorker::rejectHostKey()
{
    m_hostKeyRejected = true;
    enterConnectionLost(tr("Host key rejected"));
}

void SshSessionWorker::requestDisconnect()
{
    if (m_state == State::Disconnected || m_state == State::Failed) {
        teardownNotifiers();
        stopMaintenanceTimer();
        if (m_keepaliveTimer) m_keepaliveTimer->stop();
        if (m_transport) m_transport->disconnect();
        failAllSftp(tr("Disconnected"));
        failAllExec(-1);
        m_sftpBulkInited = false;
        m_sftpMetaInited = false;
        return;
    }
    teardownNotifiers();
    stopMaintenanceTimer();
    if (m_keepaliveTimer) m_keepaliveTimer->stop();
    if (m_transport) {
        m_transport->disconnect();
    }
    // Resolve any pending SFTP callbacks before tearing the session down, so a
    // RemoteFsBackend op never hangs on a deliberate disconnect (D1).
    failAllSftp(tr("Disconnected"));
    failAllExec(-1);
    m_sftpBulkInited = false;
    m_sftpMetaInited = false;
    setState(State::Disconnected);
}

// --- channel request slots ---------------------------------------------------

void SshSessionWorker::requestOpenChannel(int logicalId, bool wantPty,
                                          const QByteArray &term, int cols, int rows,
                                          const QString &command)
{
    if (m_lost || m_state == State::Disconnected || m_state == State::Failed) {
        emit channelOpenFailed(logicalId, tr("Not connected"));
        return;
    }
    Channel ch;
    ch.logicalId = logicalId;
    ch.phase = ChPhase::Queued;   // promoted to Opening by admitPending under the budget
    ch.wantPty = wantPty;
    ch.term = term.isEmpty() ? QByteArrayLiteral("xterm-256color") : term;
    ch.cols = cols > 0 ? cols : 80;
    ch.rows = rows > 0 ? rows : 24;
    ch.command = command;
    m_channels.insert(logicalId, ch);
    // FIX-2: PTY channels are always LongLived; enqueue on the long sub-queue.
    PendingOpen po;
    po.source = OpenerSource::PtyChannel;
    po.logicalId = logicalId;
    po.kind = ExecKind::LongLived;
    m_longQueue.append(po);   // FIFO within kind: never dropped
    pump();
}

void SshSessionWorker::requestResize(int logicalId, int cols, int rows)
{
    auto it = m_channels.find(logicalId);
    if (it == m_channels.end()) {
        return;
    }
    it->cols = cols > 0 ? cols : it->cols;
    it->rows = rows > 0 ? rows : it->rows;
    if (it->phase == ChPhase::Open && !m_lost) {
        m_transport->resizePty(it->transportId, it->cols, it->rows);
    }
}

void SshSessionWorker::requestWrite(int logicalId, const QByteArray &bytes)
{
    auto it = m_channels.find(logicalId);
    if (it == m_channels.end() || it->phase == ChPhase::Closed || m_lost) {
        return; // dead-guard: nothing posted to a gone channel/session
    }
    it->pending.append(bytes);   // move-append, no per-byte allocation
    flushPendingWrites();
}

void SshSessionWorker::requestCloseChannel(int logicalId)
{
    auto it = m_channels.find(logicalId);
    if (it == m_channels.end()) {
        return;
    }
    finishChannel(logicalId, -1);
}

// --- the pump: a single bounded sweep (D3) -----------------------------------

void SshSessionWorker::pump()
{
    if (m_lost) {
        return;
    }

    if (m_state == State::ConnectingSocket || m_state == State::Handshaking
        || m_state == State::AwaitingHostKey || m_state == State::Authenticating) {
        advanceConnect();
    }

    if (m_state != State::Ready) {
        return;
    }

    // Ready: progress any channels still being set up, drain reads round-robin,
    // then flush writes (which toggles the write notifier per D4).
    admitPending();
    advanceChannelSetup();
    readSweep();
    flushPendingWrites();
    // The exec engine runs its own non-PTY channels off to the side; advance
    // them after the interactive pump so a large git output never starves the
    // terminal channels (and SFTP is serviced via onSocketActivity/serviceSftp).
    serviceExec();
}

void SshSessionWorker::advanceConnect()
{
    // Socket connect.
    if (m_state == State::ConnectingSocket) {
        const ISshTransport::Step s = m_transport->connectSocket(m_params.host, m_params.port);
        if (s == ISshTransport::Step::Again) return;
        if (s == ISshTransport::Step::Error) {
            stopMaintenanceTimer();
            enterConnectionLost(tr("Could not reach %1:%2").arg(m_params.host).arg(m_params.port));
            return;
        }
        // TCP connected; notifiers take over for data-driven progress, but the
        // maintenance timer stays armed for FIX-5 deadline enforcement.
        setupNotifiers();
        setState(State::Handshaking);
    }

    // SSH handshake (key exchange).
    if (m_state == State::Handshaking) {
        const ISshTransport::Step s = m_transport->handshake();
        if (s == ISshTransport::Step::Again) return;
        if (s == ISshTransport::Step::Error) {
            stopMaintenanceTimer();
            enterConnectionLost(tr("SSH handshake failed"));
            return;
        }
        setState(State::AwaitingHostKey);
    }

    // Host-key verification gate.
    if (m_state == State::AwaitingHostKey) {
        if (m_hostKeyRejected) {
            return; // already aborting
        }
        if (!m_hostKeyEmitted) {
            m_hostKeyEmitted = true;
            const QByteArray key = m_transport->hostKey();
            emit hostKeyReceived(SshHostKeyStore::sha256Fingerprint(key), key);
        }
        if (!m_hostKeyAccepted) {
            return; // wait for the UI / test to accept
        }
        setState(State::Authenticating);
    }

    // Authentication.
    if (m_state == State::Authenticating) {
        ISshTransport::Step s = ISshTransport::Step::Error;
        switch (m_params.authMethod) {
        case SshProfile::AuthMethod::Password: {
            const QString pw = m_params.passwordProvider ? m_params.passwordProvider() : QString();
            s = m_transport->authPassword(m_params.username, pw);
            break;
        }
        case SshProfile::AuthMethod::KeyFile: {
            const QString pass = m_params.passphraseProvider ? m_params.passphraseProvider() : QString();
            s = m_transport->authPublicKey(m_params.username, m_params.keyPath, pass);
            break;
        }
        case SshProfile::AuthMethod::Agent:
            s = m_transport->authAgent(m_params.username);
            break;
        }
        if (s == ISshTransport::Step::Again) return;
        if (s == ISshTransport::Step::Error) {
            stopMaintenanceTimer();
            const QString reason = tr("Authentication failed");
            emit authFailed(reason);
            enterConnectionLost(reason);
            return;
        }
        stopMaintenanceTimer();
        setState(State::Ready);
        emit connected();
    }
}

// --- FIX-2: admission with budget + sub-queues (replaces tryStartQueued) ------

void SshSessionWorker::admitPending()
{
    // Budget: total cap minus SFTP reserved = dynamic budget.
    // For small caps (tests with cap=2), degrade gracefully: if the cap is not
    // larger than the SFTP reservation, use the full cap as the dynamic budget
    // (SFTP channels then compete with PTY/exec instead of being reserved).
    const int dynamicBudget = (m_channelCap > kSftpReserved)
                                  ? (m_channelCap - kSftpReserved)
                                  : m_channelCap;
    // Max long-lived. The short-lived reserve (1 slot) only kicks in once the
    // dynamic budget exceeds kMaxLongLived — that is exactly when long-lived
    // work could otherwise starve short-lived (5+ long-lived competing). For the
    // default cap=8: dynamic=6 > 5 => maxLong=5, leaving 1 short-lived reserve.
    // For smaller budgets (incl. test caps) every dynamic slot is usable by
    // either kind, so the existing FIFO-at-cap behavior is preserved.
    const int maxLong = (dynamicBudget > kMaxLongLived) ? kMaxLongLived : dynamicBudget;

    // Pass 1: admit short-lived (git-exec) — they get priority and can use any
    // free dynamic slot including the 1 reserved.
    while (!m_shortQueue.isEmpty() && liveDynamicCount() < dynamicBudget) {
        const PendingOpen po = m_shortQueue.takeFirst();
        if (po.source == OpenerSource::ExecOp) {
            for (ExecOp &op : m_execOps) {
                if (op.reqId == po.execReqId && op.phase == ExecPhase::Queued) {
                    op.phase = ExecPhase::NeedOpen;
                    break;
                }
            }
        }
        if (po.source == OpenerSource::PtyChannel) {
            auto it = m_channels.find(po.logicalId);
            if (it != m_channels.end() && it->phase == ChPhase::Queued) {
                it->phase = ChPhase::Opening;
            }
        }
    }

    // Pass 2: admit long-lived (PTY + acp-exec) — only if liveLongLived < max
    // AND there is a free dynamic slot.
    while (!m_longQueue.isEmpty()
           && liveDynamicCount() < dynamicBudget
           && liveLongLivedCount() < maxLong) {
        const PendingOpen po = m_longQueue.takeFirst();
        if (po.source == OpenerSource::PtyChannel) {
            auto it = m_channels.find(po.logicalId);
            if (it == m_channels.end()) {
                continue; // closed before it got a slot
            }
            if (it->phase != ChPhase::Queued) {
                continue; // already promoted
            }
            it->phase = ChPhase::Opening; // now counts against the budget
        } else {
            for (ExecOp &op : m_execOps) {
                if (op.reqId == po.execReqId && op.phase == ExecPhase::Queued) {
                    op.phase = ExecPhase::NeedOpen;
                    break;
                }
            }
        }
    }

    // FIX-2: emit channelQueued (edge-triggered) for any PTY channel still
    // waiting after the admission passes. The per-channel `queuedSignalled` flag
    // ensures the UX banner is raised exactly once per waiting channel — not on
    // every pump — and is reset if the channel is later admitted (below).
    for (const PendingOpen &po : m_longQueue) {
        if (po.source != OpenerSource::PtyChannel) {
            continue;
        }
        auto it = m_channels.find(po.logicalId);
        if (it != m_channels.end() && !it->queuedSignalled) {
            it->queuedSignalled = true;
            emit channelQueued(po.logicalId);
        }
    }
}

// --- FIX-1: ChannelBusy backoff in channel setup -----------------------------

void SshSessionWorker::advanceChannelSetup()
{
    // Drive every not-yet-Open channel through open -> (pty) -> exec/shell.
    // Iterate over a snapshot of ids so finishChannel-driven removals are safe.
    const qint64 now = monotonicMs();
    const QList<int> ids = m_channels.keys();
    for (int logicalId : ids) {
        auto it = m_channels.find(logicalId);
        if (it == m_channels.end()) continue;
        Channel &ch = it.value();

        if (ch.phase == ChPhase::Opening) {
            // FIX-1: skip if backoff deadline has not elapsed yet.
            if (ch.nextRetryMs > 0 && now < ch.nextRetryMs) {
                continue;
            }
            const ISshTransport::OpenResult r = m_transport->openChannel();
            if (r.step == ISshTransport::Step::Again) {
                continue;
            }
            if (r.step == ISshTransport::Step::ChannelBusy) {
                // FIX-1: transient back-pressure. Keep the opener pending and
                // retry with exponential backoff 250ms -> max 2s.
                ch.nextRetryMs = now + ch.backoffMs;
                ch.backoffMs = qMin(ch.backoffMs * 2, 2000);
                armMaintenanceTimer(); // ensure we wake to retry
                continue;
            }
            if (r.step == ISshTransport::Step::Error) {
                emit channelOpenFailed(logicalId, tr("Could not open channel"));
                finishChannel(logicalId, -1);
                continue;
            }
            // Ok: channel opened successfully. Reset backoff state.
            ch.transportId = r.channelId;
            ch.backoffMs = 250;
            ch.nextRetryMs = 0;
            ch.phase = ch.wantPty ? ChPhase::NeedPty : ChPhase::NeedExec;
        }

        if (ch.phase == ChPhase::NeedPty) {
            const ISshTransport::Step s =
                m_transport->requestPty(ch.transportId, ch.term, ch.cols, ch.rows);
            if (s == ISshTransport::Step::Again) continue;
            if (s == ISshTransport::Step::Error) {
                emit channelOpenFailed(logicalId, tr("Could not allocate a PTY"));
                finishChannel(logicalId, -1);
                continue;
            }
            ch.phase = ChPhase::NeedExec;
        }

        if (ch.phase == ChPhase::NeedExec) {
            const ISshTransport::Step s = m_transport->execOrShell(ch.transportId, ch.command);
            if (s == ISshTransport::Step::Again) continue;
            if (s == ISshTransport::Step::Error) {
                emit channelOpenFailed(logicalId, tr("Could not start the remote shell"));
                finishChannel(logicalId, -1);
                continue;
            }
            ch.phase = ChPhase::Open;
            if (!m_rotation.contains(logicalId)) {
                m_rotation.append(logicalId);
            }
            emit channelOpened(logicalId);
        }
    }
}

// --- FIX-1: backoff readiness check ------------------------------------------

bool SshSessionWorker::hasBackoffReady() const
{
    const qint64 now = monotonicMs();
    for (auto it = m_channels.constBegin(); it != m_channels.constEnd(); ++it) {
        if (it->phase == ChPhase::Opening && it->nextRetryMs > 0 && now >= it->nextRetryMs) {
            return true;
        }
    }
    for (const ExecOp &op : m_execOps) {
        if (op.phase == ExecPhase::NeedOpen && op.nextRetryMs > 0 && now >= op.nextRetryMs) {
            return true;
        }
    }
    return false;
}

// --- round-robin read sweep (D3) ---------------------------------------------

void SshSessionWorker::readSweep()
{
    const QList<int> order = m_rotation; // stable snapshot; safe across removals
    for (int logicalId : order) {
        auto it = m_channels.find(logicalId);
        if (it == m_channels.end() || it->phase != ChPhase::Open) {
            continue;
        }
        const int transportId = it->transportId;
        for (;;) {
            ISshTransport::ReadResult r = m_transport->chRead(transportId);
            if (r.error) {
                enterConnectionLost(tr("SSH connection lost"));
                return;
            }
            if (!r.data.isEmpty()) {
                m_sawInboundSinceKeepalive = true; // FIX-3: track activity
                emit dataReady(logicalId, std::move(r.data));
            }
            if (r.eof) {
                m_sawInboundSinceKeepalive = true; // FIX-3
                finishChannel(logicalId, m_transport->chExitStatus(transportId));
                break;
            }
            if (r.again) {
                break; // channel drained; move to the next
            }
        }
    }
}

// --- write path + write-notifier invariant (D4) ------------------------------

void SshSessionWorker::flushPendingWrites()
{
    if (m_lost) {
        return;
    }
    bool anyPending = false;
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        Channel &ch = it.value();
        if (ch.phase != ChPhase::Open || ch.pending.isEmpty()) {
            continue;
        }
        while (!ch.pending.isEmpty()) {
            const qint64 n = m_transport->chWrite(ch.transportId, ch.pending);
            if (n == ISshTransport::kWriteAgain) {
                anyPending = true;
                break;
            }
            if (n == ISshTransport::kWriteError) {
                enterConnectionLost(tr("SSH connection lost"));
                return;
            }
            if (n <= 0) {
                break;
            }
            ch.pending.remove(0, static_cast<int>(n));
        }
    }
    setWriteNotifierEnabled(anyPending);
}

// --- connection-loss cleanup (D8) --------------------------------------------

void SshSessionWorker::enterConnectionLost(const QString &reason)
{
    if (m_lost) {
        return;
    }
    m_lost = true; // mark dead FIRST — no further libssh2 calls on a dead session

    teardownNotifiers();
    stopMaintenanceTimer();
    if (m_keepaliveTimer) m_keepaliveTimer->stop();

    // Close each channel reporting an abnormal exit (-1).
    const QList<int> ids = m_channels.keys();
    for (int logicalId : ids) {
        auto it = m_channels.find(logicalId);
        if (it == m_channels.end()) continue;
        it->phase = ChPhase::Closed;
        emit channelClosed(logicalId, -1);
    }
    m_channels.clear();
    m_rotation.clear();
    m_shortQueue.clear();
    m_longQueue.clear();

    failAllSftp(reason);
    failAllExec(-1);
    m_sftpBulkInited = false;
    m_sftpMetaInited = false;

    if (m_transport) {
        m_transport->disconnect();
    }
    setState(State::Disconnected);
    emit disconnected(reason);
}

void SshSessionWorker::finishChannel(int logicalId, int exitStatus)
{
    auto it = m_channels.find(logicalId);
    if (it == m_channels.end()) {
        return;
    }
    if (it->transportId >= 0 && !m_lost) {
        m_transport->closeChannel(it->transportId);
    }
    m_channels.erase(it);
    m_rotation.removeAll(logicalId);
    // Remove from sub-queues if still queued (closed before admitted).
    for (int i = 0; i < m_longQueue.size(); ++i) {
        if (m_longQueue[i].source == OpenerSource::PtyChannel
            && m_longQueue[i].logicalId == logicalId) {
            m_longQueue.removeAt(i);
            break;
        }
    }
    emit channelClosed(logicalId, exitStatus);

    // A freed slot may let a queued open proceed (PTY *or* exec). Only
    // meaningful while connected. admitPending() promotes queued openers of
    // either kind; advanceChannelSetup() + serviceExec() then drive them.
    if (!m_lost && m_state == State::Ready) {
        admitPending();
        advanceChannelSetup();
        serviceExec();
    }
}

// --- FIX-3: keepalive tick ---------------------------------------------------

void SshSessionWorker::onKeepaliveTick()
{
    if (m_lost || m_state != State::Ready) {
        return;
    }

    // Check inbound activity since last tick.
    if (m_sawInboundSinceKeepalive) {
        m_keepaliveMissCount = 0;
    } else {
        ++m_keepaliveMissCount;
    }
    m_sawInboundSinceKeepalive = false;

    // 2 consecutive misses (~30s with no inbound) -> connection lost.
    if (m_keepaliveMissCount >= 2) {
        enterConnectionLost(tr("SSH connection lost (keepalive timeout)"));
        return;
    }

    // Send the keepalive probe.
    const int result = m_transport->sendKeepalive();
    if (result < 0) {
        // Fatal socket error during send.
        enterConnectionLost(tr("SSH connection lost (keepalive timeout)"));
        return;
    }
    // A successful send counts as activity (the server will reply, which
    // triggers a socket-readable edge that sets m_sawInboundSinceKeepalive).
    // We do NOT set the flag here — we rely on the reply arriving before the
    // next tick to clear the miss counter.
}

// --- FIX-4 + FIX-1: maintenance tick (connect poll + backoff retries) --------

void SshSessionWorker::onMaintenanceTick()
{
    if (m_lost) {
        stopMaintenanceTimer();
        return;
    }

    // FIX-5: enforce the overall connect-phase deadline (handshake + auth).
    // This fires when the server accepts TCP but never responds to SSH traffic,
    // or when auth hangs on an unresponsive agent/server. AwaitingHostKey is
    // excluded — that's user interaction time, not a server timeout.
    if (m_state == State::ConnectingSocket || m_state == State::Handshaking
        || m_state == State::Authenticating) {
        if (m_connectDeadlineMs > 0 && monotonicMs() >= m_connectDeadlineMs) {
            stopMaintenanceTimer();
            enterConnectionLost(tr("Connection timed out (server did not respond within %1 s)")
                                    .arg(kConnectPhaseTimeoutMs / 1000));
            return;
        }
    }

    // FIX-4: while connecting, re-poll the transport.
    if (m_state == State::ConnectingSocket) {
        pump();
        return;
    }

    // Handshaking/Authenticating/AwaitingHostKey: the maintenance timer stays
    // armed solely for deadline enforcement above. No pump needed — socket
    // notifiers drive progress (and AwaitingHostKey waits on user input).
    if (m_state == State::Handshaking || m_state == State::Authenticating
        || m_state == State::AwaitingHostKey) {
        return;
    }

    // FIX-1: retry openers whose backoff deadline has elapsed.
    if (m_state == State::Ready) {
        if (hasBackoffReady()) {
            advanceChannelSetup();
            serviceExec(); // exec ops also have backoff
        }
        // Stop the timer if no more backoff retries are pending.
        if (!hasBackoffReady()) {
            stopMaintenanceTimer();
        }
    }
}

// --- notifiers (production only; tests drive pumpForTest) --------------------

void SshSessionWorker::setupNotifiers()
{
    const qintptr fd = m_transport->socketFd();
    if (fd < 0) {
        return; // fd not valid yet (or a fake transport in tests)
    }
    if (!m_readNotifier) {
        m_readNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(m_readNotifier, &QSocketNotifier::activated, this,
                &SshSessionWorker::onSocketActivity);
    }
    if (!m_writeNotifier) {
        m_writeNotifier = new QSocketNotifier(fd, QSocketNotifier::Write, this);
        m_writeNotifier->setEnabled(false); // D4: off until bytes pending
        connect(m_writeNotifier, &QSocketNotifier::activated, this,
                &SshSessionWorker::onSocketActivity);
    }
}

void SshSessionWorker::teardownNotifiers()
{
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        m_readNotifier->deleteLater();
        m_readNotifier = nullptr;
    }
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
        m_writeNotifier->deleteLater();
        m_writeNotifier = nullptr;
    }
    m_writeNotifierWanted = false;
}

void SshSessionWorker::setWriteNotifierEnabled(bool on)
{
    m_writeNotifierWanted = on; // observable in tests
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(on);
    }
}

void SshSessionWorker::onSocketActivity()
{
    // FIX-3: a socket-readable edge means the server is alive (could be a
    // keepalive reply, channel data, or SSH transport traffic).
    m_sawInboundSinceKeepalive = true;
    // A single readable/writable edge -> one bounded pump sweep. Not recursive.
    pump();
    // The SFTP layer is multiplexed on the SAME socket.
    serviceSftp();
    // Exec channels share the same socket too.
    serviceExec();
}

// --- SFTP engine (D1a): two independent lanes (bulk + metadata) --------------

SshSessionWorker::SftpLane SshSessionWorker::laneForKind(SftpKind kind)
{
    switch (kind) {
    case SftpKind::Read:
    case SftpKind::Write:
        return SftpLane::Bulk;
    case SftpKind::Stat:
    case SftpKind::Readdir:
    case SftpKind::Rename:
    case SftpKind::Mkdir:
    case SftpKind::Unlink:
        return SftpLane::Meta;
    }
    return SftpLane::Meta; // unreachable
}

ISshTransport::SftpLane SshSessionWorker::transportLane(SftpLane lane)
{
    return lane == SftpLane::Bulk ? ISshTransport::SftpLane::Bulk
                                  : ISshTransport::SftpLane::Meta;
}

void SshSessionWorker::requestSftpRead(quint64 reqId, const QString &path)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Read;
    op.path = path;
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::requestSftpWrite(quint64 reqId, const QString &path,
                                        const QByteArray &data)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Write;
    op.path = path;
    op.buffer = data;
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::requestSftpStat(quint64 reqId, const QString &path)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Stat;
    op.path = path;
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::requestSftpReaddir(quint64 reqId, const QString &path)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Readdir;
    op.path = path;
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::requestSftpRename(quint64 reqId, const QString &oldPath,
                                          const QString &newPath)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Rename;
    op.path = oldPath;
    op.path2 = newPath;
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::requestSftpMkdir(quint64 reqId, const QString &path)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Mkdir;
    op.path = path;
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::requestSftpUnlink(quint64 reqId, const QString &path)
{
    SftpOp op;
    op.reqId = reqId;
    op.kind = SftpKind::Unlink;
    op.path = path;
    enqueueSftpOp(std::move(op));
}

void SshSessionWorker::enqueueSftpOp(SftpOp op)
{
    if (m_lost || m_state == State::Disconnected || m_state == State::Failed) {
        failSftpOp(op, tr("Not connected"));
        return;
    }
    // Route to the appropriate lane by kind (D1a).
    if (laneForKind(op.kind) == SftpLane::Bulk) {
        m_sftpBulkQueue.append(std::move(op));
    } else {
        m_sftpMetaQueue.append(std::move(op));
    }
    serviceSftp();
}

ISshTransport::Step SshSessionWorker::ensureSftpLaneInited(SftpLane lane, bool &inited)
{
    if (inited) {
        return ISshTransport::Step::Ok;
    }
    const ISshTransport::SftpOpenResult r = m_transport->sftpInit(transportLane(lane));
    if (r.step == ISshTransport::Step::Ok) {
        inited = true;
    }
    return r.step;
}

void SshSessionWorker::serviceSftp()
{
    if (m_lost || m_state != State::Ready) {
        return;
    }
    // Service metadata lane first (latency-sensitive: readdir/stat for the tree),
    // then bulk lane (large file reads/writes). This interleaving guarantees a
    // stalled bulk op never holds back a ready metadata op (D1a non-blocking).
    serviceSftpLane(SftpLane::Meta, m_sftpMetaQueue, m_sftpMetaInited);
    serviceSftpLane(SftpLane::Bulk, m_sftpBulkQueue, m_sftpBulkInited);
}

void SshSessionWorker::serviceSftpLane(SftpLane lane, QList<SftpOp> &queue, bool &inited)
{
    if (m_lost || m_state != State::Ready) {
        return;
    }
    while (!queue.isEmpty()) {
        const ISshTransport::Step initStep = ensureSftpLaneInited(lane, inited);
        if (initStep == ISshTransport::Step::Again) {
            return;
        }
        if (initStep == ISshTransport::Step::Error) {
            const SftpOp failed = queue.takeFirst();
            failSftpOp(failed, tr("Could not open SFTP session"));
            continue;
        }
        SftpOp &op = queue.first();
        if (!advanceSftpOp(lane, op)) {
            return; // EAGAIN mid-op: resume on the next socket edge
        }
        queue.removeFirst(); // finished (a *Done signal was emitted)
    }
}

bool SshSessionWorker::advanceSftpOp(SftpLane lane, SftpOp &op)
{
    // D1a: every transport SFTP call runs against this op's lane session/handle.
    const ISshTransport::SftpLane tl = transportLane(lane);
    switch (op.kind) {
    case SftpKind::Stat: {
        const ISshTransport::SftpStatResult r = m_transport->sftpStat(tl, op.path);
        if (r.step == ISshTransport::Step::Again) {
            return false;
        }
        m_sawInboundSinceKeepalive = true; // FIX-3
        if (r.step == ISshTransport::Step::Error) {
            emit sftpStatDone(op.reqId, /*ok=*/true, /*exists=*/false, /*isDir=*/false,
                              /*size=*/0, /*mtime=*/0, QString());
            return true;
        }
        const ISshTransport::SftpAttrs &a = r.attrs;
        emit sftpStatDone(op.reqId, /*ok=*/true, /*exists=*/true, a.isDir,
                          static_cast<qint64>(a.hasSize ? a.size : 0),
                          static_cast<qint64>(a.hasMtime ? a.mtime : 0), QString());
        return true;
    }

    case SftpKind::Read: {
        if (op.phase == SftpPhase::NeedOpen) {
            const ISshTransport::SftpOpenResult r = m_transport->sftpOpen(tl, op.path, /*forWrite=*/false);
            if (r.step == ISshTransport::Step::Again) {
                return false;
            }
            if (r.step == ISshTransport::Step::Error) {
                failSftpOp(op, tr("Could not open remote file for reading"));
                return true;
            }
            op.handleId = r.handleId;
            op.phase = SftpPhase::Transfer;
        }
        for (;;) {
            ISshTransport::ReadResult rr = m_transport->sftpRead(tl, op.handleId);
            if (rr.error) {
                m_transport->sftpClose(tl, op.handleId);
                op.handleId = -1;
                failSftpOp(op, tr("Remote read failed"));
                return true;
            }
            if (!rr.data.isEmpty()) {
                m_sawInboundSinceKeepalive = true; // FIX-3
                op.buffer.append(rr.data);
            }
            if (rr.eof) {
                m_transport->sftpClose(tl, op.handleId);
                op.handleId = -1;
                emit sftpReadDone(op.reqId, /*ok=*/true, std::move(op.buffer), QString());
                return true;
            }
            if (rr.again) {
                return false;
            }
        }
    }

    case SftpKind::Write: {
        if (op.phase == SftpPhase::NeedOpen) {
            const ISshTransport::SftpOpenResult r = m_transport->sftpOpen(tl, op.path, /*forWrite=*/true);
            if (r.step == ISshTransport::Step::Again) {
                return false;
            }
            if (r.step == ISshTransport::Step::Error) {
                failSftpOp(op, tr("Could not open remote file for writing"));
                return true;
            }
            op.handleId = r.handleId;
            op.phase = SftpPhase::Transfer;
        }
        while (op.writeOffset < op.buffer.size()) {
            const QByteArray slice = QByteArray::fromRawData(
                op.buffer.constData() + op.writeOffset,
                static_cast<int>(op.buffer.size() - op.writeOffset));
            const qint64 n = m_transport->sftpWrite(tl, op.handleId, slice);
            if (n == ISshTransport::kWriteAgain) {
                setWriteNotifierEnabled(true);
                return false;
            }
            if (n == ISshTransport::kWriteError) {
                m_transport->sftpClose(tl, op.handleId);
                op.handleId = -1;
                failSftpOp(op, tr("Remote write failed"));
                return true;
            }
            if (n <= 0) {
                return false;
            }
            op.writeOffset += n;
        }
        m_transport->sftpClose(tl, op.handleId);
        op.handleId = -1;
        emit sftpWriteDone(op.reqId, /*ok=*/true, QString());
        return true;
    }

    case SftpKind::Readdir: {
        if (op.phase == SftpPhase::NeedOpen) {
            const ISshTransport::SftpOpenResult r = m_transport->sftpOpendir(tl, op.path);
            if (r.step == ISshTransport::Step::Again) {
                return false;
            }
            if (r.step == ISshTransport::Step::Error) {
                failSftpOp(op, tr("Could not open remote directory"));
                return true;
            }
            op.handleId = r.handleId;
            op.phase = SftpPhase::Transfer;
        }
        for (;;) {
            ISshTransport::SftpDirEntry e = m_transport->sftpReaddir(tl, op.handleId);
            if (e.kind == ISshTransport::SftpDirEntry::Kind::Again) {
                return false;
            }
            if (e.kind == ISshTransport::SftpDirEntry::Kind::Error) {
                m_transport->sftpClosedir(tl, op.handleId);
                op.handleId = -1;
                failSftpOp(op, tr("Remote directory listing failed"));
                return true;
            }
            if (e.kind == ISshTransport::SftpDirEntry::Kind::Done) {
                m_transport->sftpClosedir(tl, op.handleId);
                op.handleId = -1;
                emit sftpReaddirDone(op.reqId, /*ok=*/true, op.entries, QString());
                return true;
            }
            // Kind::Entry — decode + filter "." / ".." (matches local readdir).
            m_sawInboundSinceKeepalive = true; // FIX-3
            const QString name = QString::fromUtf8(e.name);
            if (name == QLatin1String(".") || name == QLatin1String("..")) {
                continue;
            }
            RemoteDirEntry de;
            de.name = name;
            de.isDir = e.attrs.isDir;
            de.size = static_cast<qint64>(e.attrs.hasSize ? e.attrs.size : 0);
            de.mtimeSecs = static_cast<qint64>(e.attrs.hasMtime ? e.attrs.mtime : 0);
            op.entries.append(std::move(de));
        }
    }

    case SftpKind::Rename: {
        const ISshTransport::Step s = m_transport->sftpRename(tl, op.path, op.path2);
        if (s == ISshTransport::Step::Again) return false;
        m_sawInboundSinceKeepalive = true;
        emit sftpRenameDone(op.reqId, s == ISshTransport::Step::Ok, QString());
        return true;
    }

    case SftpKind::Mkdir: {
        const ISshTransport::Step s = m_transport->sftpMkdir(tl, op.path);
        if (s == ISshTransport::Step::Again) return false;
        m_sawInboundSinceKeepalive = true;
        emit sftpMkdirDone(op.reqId, s == ISshTransport::Step::Ok, QString());
        return true;
    }

    case SftpKind::Unlink: {
        const ISshTransport::Step s = m_transport->sftpUnlink(tl, op.path);
        if (s == ISshTransport::Step::Again) return false;
        m_sawInboundSinceKeepalive = true;
        emit sftpUnlinkDone(op.reqId, s == ISshTransport::Step::Ok, QString());
        return true;
    }

    }
    return true; // unreachable
}

void SshSessionWorker::failSftpOp(const SftpOp &op, const QString &reason)
{
    switch (op.kind) {
    case SftpKind::Read:
        emit sftpReadDone(op.reqId, /*ok=*/false, QByteArray(), reason);
        break;
    case SftpKind::Write:
        emit sftpWriteDone(op.reqId, /*ok=*/false, reason);
        break;
    case SftpKind::Stat:
        emit sftpStatDone(op.reqId, /*ok=*/false, /*exists=*/false, /*isDir=*/false,
                          0, 0, reason);
        break;
    case SftpKind::Readdir:
        emit sftpReaddirDone(op.reqId, /*ok=*/false, QList<RemoteDirEntry>(), reason);
        break;
    case SftpKind::Rename:
        emit sftpRenameDone(op.reqId, /*ok=*/false, reason);
        break;
    case SftpKind::Mkdir:
        emit sftpMkdirDone(op.reqId, /*ok=*/false, reason);
        break;
    case SftpKind::Unlink:
        emit sftpUnlinkDone(op.reqId, /*ok=*/false, reason);
        break;
    }
}

void SshSessionWorker::failAllSftp(const QString &reason)
{
    // Fail both lanes (D1a).
    const QList<SftpOp> bulkPending = std::move(m_sftpBulkQueue);
    m_sftpBulkQueue.clear();
    for (const SftpOp &op : bulkPending) {
        failSftpOp(op, reason);
    }
    const QList<SftpOp> metaPending = std::move(m_sftpMetaQueue);
    m_sftpMetaQueue.clear();
    for (const SftpOp &op : metaPending) {
        failSftpOp(op, reason);
    }
}

// --- exec engine (D6) --------------------------------------------------------

void SshSessionWorker::requestExec(quint64 reqId, const QString &command,
                                   const QByteArray &stdinPayload,
                                   ExecKind kind)
{
    if (m_lost || m_state == State::Disconnected || m_state == State::Failed) {
        emit execDone(reqId, -1);
        return;
    }
    ExecOp op;
    op.reqId = reqId;
    op.command = command;
    op.stdinPayload = stdinPayload;
    op.kind = kind;
    op.phase = ExecPhase::Queued; // FIX-2: starts queued, admitted by admitPending
    m_execOps.append(std::move(op));

    // FIX-2: enqueue on the appropriate sub-queue for admission.
    PendingOpen po;
    po.source = OpenerSource::ExecOp;
    po.execReqId = reqId;
    po.kind = kind;
    if (kind == ExecKind::ShortLived) {
        m_shortQueue.append(po);
    } else {
        m_longQueue.append(po);
    }

    // Try to admit immediately if budget allows.
    if (m_state == State::Ready) {
        admitPending();
        serviceExec();
    }
}

void SshSessionWorker::requestExec(quint64 reqId, const QString &command,
                                   const QByteArray &stdinPayload)
{
    // Backward-compat overload: default to ShortLived (git-exec is the common case).
    requestExec(reqId, command, stdinPayload, ExecKind::ShortLived);
}

void SshSessionWorker::requestExecCancel(quint64 reqId)
{
    // Remove from sub-queues if still queued.
    for (int i = 0; i < m_shortQueue.size(); ++i) {
        if (m_shortQueue[i].source == OpenerSource::ExecOp && m_shortQueue[i].execReqId == reqId) {
            m_shortQueue.removeAt(i);
            break;
        }
    }
    for (int i = 0; i < m_longQueue.size(); ++i) {
        if (m_longQueue[i].source == OpenerSource::ExecOp && m_longQueue[i].execReqId == reqId) {
            m_longQueue.removeAt(i);
            break;
        }
    }

    for (int i = 0; i < m_execOps.size(); ++i) {
        if (m_execOps[i].reqId != reqId) {
            continue;
        }
        if (m_execOps[i].transportId >= 0 && !m_lost) {
            m_transport->closeChannel(m_execOps[i].transportId);
        }
        m_execOps.removeAt(i);
        // A freed slot may let a queued open proceed.
        if (!m_lost && m_state == State::Ready) {
            admitPending();
        }
        return;
    }
}

void SshSessionWorker::requestExecWrite(quint64 reqId, const QByteArray &bytes)
{
    for (int i = 0; i < m_execOps.size(); ++i) {
        if (m_execOps[i].reqId != reqId) {
            continue;
        }
        m_execOps[i].streamStdin.append(bytes);
        serviceExec();
        return;
    }
}

void SshSessionWorker::serviceExec()
{
    if (m_lost || m_state != State::Ready) {
        return;
    }
    for (int i = 0; i < m_execOps.size();) {
        // Skip ops still waiting for admission.
        if (m_execOps[i].phase == ExecPhase::Queued) {
            ++i;
            continue;
        }
        if (advanceExecOp(m_execOps[i])) {
            m_execOps.removeAt(i);
            // A freed slot may let a queued open proceed.
            if (!m_lost && m_state == State::Ready) {
                admitPending();
            }
        } else {
            ++i;
        }
    }
}

bool SshSessionWorker::advanceExecOp(ExecOp &op)
{
    if (op.phase == ExecPhase::NeedOpen) {
        // FIX-1: skip if backoff deadline has not elapsed yet.
        const qint64 now = monotonicMs();
        if (op.nextRetryMs > 0 && now < op.nextRetryMs) {
            return false;
        }
        const ISshTransport::OpenResult r = m_transport->openChannel();
        if (r.step == ISshTransport::Step::Again) {
            return false;
        }
        if (r.step == ISshTransport::Step::ChannelBusy) {
            // FIX-1: transient back-pressure. Keep pending, retry with backoff.
            op.nextRetryMs = now + op.backoffMs;
            op.backoffMs = qMin(op.backoffMs * 2, 2000);
            armMaintenanceTimer();
            return false;
        }
        if (r.step == ISshTransport::Step::Error) {
            emit execDone(op.reqId, -1);
            return true;
        }
        op.transportId = r.channelId;
        op.backoffMs = 250;
        op.nextRetryMs = 0;
        op.phase = ExecPhase::NeedExec;
    }

    if (op.phase == ExecPhase::NeedExec) {
        const ISshTransport::Step s = m_transport->execOrShell(op.transportId, op.command);
        if (s == ISshTransport::Step::Again) {
            return false;
        }
        if (s == ISshTransport::Step::Error) {
            m_transport->closeChannel(op.transportId);
            op.transportId = -1;
            emit execDone(op.reqId, -1);
            return true;
        }
        op.phase = ExecPhase::Streaming;
    }

    // Streaming: feed stdin once, then drain stdout + stderr toward EOF.
    if (op.phase == ExecPhase::Streaming) {
        if (!op.stdinSent && !op.stdinPayload.isEmpty()) {
            while (op.stdinOffset < op.stdinPayload.size()) {
                const QByteArray slice = QByteArray::fromRawData(
                    op.stdinPayload.constData() + op.stdinOffset,
                    static_cast<int>(op.stdinPayload.size() - op.stdinOffset));
                const qint64 n = m_transport->chWrite(op.transportId, slice);
                if (n == ISshTransport::kWriteAgain) {
                    setWriteNotifierEnabled(true);
                    break;
                }
                if (n == ISshTransport::kWriteError) {
                    m_transport->closeChannel(op.transportId);
                    op.transportId = -1;
                    emit execDone(op.reqId, -1);
                    return true;
                }
                if (n <= 0) {
                    break;
                }
                op.stdinOffset += n;
            }
            if (op.stdinOffset >= op.stdinPayload.size()) {
                op.stdinSent = true;
            }
        } else {
            op.stdinSent = true;
        }

        // Drain streamed stdin (D8).
        if (op.stdinSent && op.streamStdinOffset < op.streamStdin.size()) {
            while (op.streamStdinOffset < op.streamStdin.size()) {
                const QByteArray slice = QByteArray::fromRawData(
                    op.streamStdin.constData() + op.streamStdinOffset,
                    static_cast<int>(op.streamStdin.size() - op.streamStdinOffset));
                const qint64 n = m_transport->chWrite(op.transportId, slice);
                if (n == ISshTransport::kWriteAgain) {
                    setWriteNotifierEnabled(true);
                    break;
                }
                if (n == ISshTransport::kWriteError) {
                    m_transport->closeChannel(op.transportId);
                    op.transportId = -1;
                    emit execDone(op.reqId, -1);
                    return true;
                }
                if (n <= 0) {
                    break;
                }
                op.streamStdinOffset += n;
            }
            if (op.streamStdinOffset >= op.streamStdin.size()) {
                op.streamStdin.clear();
                op.streamStdinOffset = 0;
            }
        }

        // ShortLived ops: send EOF after all stdin is written so the remote
        // process sees end-of-input (e.g. git commit -F - reading message from
        // stdin). LongLived ops (ACP) keep stdin open for execWrite frames.
        if (op.stdinSent && !op.eofSent && op.kind == ExecKind::ShortLived) {
            const ISshTransport::Step eof = m_transport->chSendEof(op.transportId);
            if (eof == ISshTransport::Step::Again) {
                setWriteNotifierEnabled(true);
                return false;
            }
            if (eof == ISshTransport::Step::Error) {
                m_transport->closeChannel(op.transportId);
                op.transportId = -1;
                emit execDone(op.reqId, -1);
                return true;
            }
            op.eofSent = true;
        }

        // Drain stdout.
        if (!op.stdoutEof) {
            for (;;) {
                ISshTransport::ReadResult r = m_transport->chRead(op.transportId);
                if (r.error) {
                    m_transport->closeChannel(op.transportId);
                    op.transportId = -1;
                    emit execDone(op.reqId, -1);
                    return true;
                }
                if (!r.data.isEmpty()) {
                    m_sawInboundSinceKeepalive = true; // FIX-3
                    emit execStdoutChunk(op.reqId, std::move(r.data));
                }
                if (r.eof) {
                    op.stdoutEof = true;
                    break;
                }
                if (r.again) {
                    break;
                }
            }
        }

        // Drain stderr.
        if (!op.stderrEof) {
            for (;;) {
                ISshTransport::ReadResult r = m_transport->chReadStderr(op.transportId);
                if (r.error) {
                    m_transport->closeChannel(op.transportId);
                    op.transportId = -1;
                    emit execDone(op.reqId, -1);
                    return true;
                }
                if (!r.data.isEmpty()) {
                    m_sawInboundSinceKeepalive = true; // FIX-3
                    emit execStderrChunk(op.reqId, std::move(r.data));
                }
                if (r.eof) {
                    op.stderrEof = true;
                    break;
                }
                if (r.again) {
                    break;
                }
            }
        }

        if (op.stdoutEof) {
            const int exitStatus = m_transport->chExitStatus(op.transportId);
            m_transport->closeChannel(op.transportId);
            op.transportId = -1;
            emit execDone(op.reqId, exitStatus);
            return true;
        }
        return false;
    }

    return false;
}

void SshSessionWorker::failAllExec(int exitStatus)
{
    const QList<ExecOp> pending = std::move(m_execOps);
    m_execOps.clear();
    m_shortQueue.clear(); // clear exec entries from sub-queues
    m_longQueue.clear();  // PTY entries are already cleared in enterConnectionLost
    for (const ExecOp &op : pending) {
        emit execDone(op.reqId, exitStatus);
    }
}

} // namespace remote

