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

#ifndef REMOTE_SSHSESSIONWORKER_H
#define REMOTE_SSHSESSIONWORKER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

#include "ISshTransport.h"
#include "SshProfile.h"

class QSocketNotifier;
class QTimer;

namespace remote {

// --- ExecKind (FIX-2) --------------------------------------------------------
// Classifies an exec channel for the admission budget. ShortLived (git-exec) may
// use the reserved dynamic slot; LongLived (acp-exec) may not, preventing
// starvation of short-lived work under heavy long-lived load.
enum class ExecKind
{
    ShortLived, // git-exec: opens, runs, closes quickly
    LongLived,  // acp-exec: holds the channel for the session's lifetime
};

// One decoded remote directory entry, delivered to the UI thread for the file
// tree / readdir callback. Decoded from ISshTransport::SftpDirEntry on the
// worker thread (UTF-8 name, the size/mtime/isDir the tree needs) so the UI
// never touches a raw SFTP attribute struct. "." / ".." are filtered out on the
// worker, matching local readdir (QDir::NoDotAndDotDot).
struct RemoteDirEntry
{
    QString name;       // UTF-8-decoded filename, no path
    bool    isDir = false;
    qint64  size = 0;
    qint64  mtimeSecs = 0; // seconds since epoch (SFTP server clock), 0 if absent
};

// Lives on a dedicated worker QThread. Owns the LIBSSH2_SESSION via an injected
// ISshTransport and is the ONLY place libssh2 is ever touched. Drives:
//   - the connect/auth state machine (D2),
//   - the round-robin read pump (D3),
//   - the write path with the write-notifier invariant (D4),
//   - the FIFO channel-open queue with a cap (D5),
//   - connection-loss detection + ordered cleanup (D8),
//   - FIX-1 ChannelBusy exponential backoff,
//   - FIX-2 unified channel counter + budget + 2 sub-queues + reserve,
//   - FIX-3 keepalive timer + miss detection,
//   - FIX-4 non-blocking connect re-poll timer.
//
// The UI-thread facade (SshConnection) talks to it ONLY via queued
// signals/slots — no locks. Tests construct it directly on the test thread,
// inject a FakeSshTransport, and call pumpForTest() to drive the pump without a
// real QSocketNotifier.
class SshSessionWorker : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Idle,
        ConnectingSocket,
        Handshaking,
        AwaitingHostKey, // emitted hostKeyReceived; auth gated on accept/reject
        Authenticating,
        Ready,
        Disconnected,
        Failed,
    };
    Q_ENUM(State)

    struct ConnectParams
    {
        QString host;
        int port = 22;
        QString username;
        SshProfile::AuthMethod authMethod = SshProfile::AuthMethod::Agent;
        QString keyPath;
        // Secret providers invoked ON THE WORKER THREAD during auth so the UI
        // thread is never blocked on a keychain syscall (spec: "Secret fetched
        // off the UI thread"). Empty/unset → empty secret.
        std::function<QString()> passwordProvider;
        std::function<QString()> passphraseProvider;
    };

    // Takes ownership of the transport. Default cap = 8 concurrent channels (FIX-2).
    explicit SshSessionWorker(std::unique_ptr<ISshTransport> transport,
                              int channelCap = 8,
                              QObject *parent = nullptr);
    ~SshSessionWorker() override;

    State state() const { return m_state; }

    // --- test hooks ----------------------------------------------------------
    // Run exactly one bounded pump sweep (advance connect/auth, then if Ready
    // service channel setup + reads + writes). Returns nothing; observe via
    // signals + the accessors below.
    void pumpForTest() { pump(); }
    // Drive exactly one bounded SFTP service sweep (mirrors pumpForTest for the
    // D1a SFTP engine): advance the head op of BOTH lanes (metadata then bulk) as
    // far as each can go this pass, emitting their *Done signals on completion.
    // Tests script the fake transport then call this to step partial-read / EAGAIN
    // reassembly deterministically. Servicing metadata before bulk guarantees a
    // stalled bulk op never holds back a ready metadata op (D1a non-blocking).
    void serviceSftpForTest() { serviceSftp(); }
    // Drive exactly one bounded exec service sweep (mirrors serviceSftpForTest
    // for the D6 exec engine): advance every in-flight exec channel as far as
    // it can go this pass, emitting exec*Chunk / execDone as streams progress.
    void serviceExecForTest() { serviceExec(); }
    bool writeNotifierEnabledForTest() const { return m_writeNotifierWanted; }
    // Legacy test hook: counts PTY/shell channels in Opening..Open phase only
    // (does NOT include exec or SFTP). Kept for backward compat with existing
    // Batch-A tests that assert against a PTY-only cap.
    int liveChannelCountForTest() const { return livePtyChannelCount(); }
    int queuedChannelCountForTest() const { return static_cast<int>(m_shortQueue.size() + m_longQueue.size()); }
    // FIX-2: the single source of truth — PTY + exec + SFTP live channels.
    int unifiedChannelCountForTest() const { return unifiedLiveCount(); }
    // FIX-1: fire the maintenance timer slot (backoff retries + connect poll).
    void tickMaintenanceForTest() { onMaintenanceTick(); }
    // FIX-3: fire the keepalive timer slot.
    void tickKeepaliveForTest() { onKeepaliveTick(); }
    // FIX-3: simulate inbound activity (for tests that cannot trigger a real
    // socket-readable edge). Resets the keepalive miss counter.
    void markInboundActivityForTest() { m_sawInboundSinceKeepalive = true; }

public slots:
    // All posted from the UI thread (queued) in production; called directly in
    // tests. startConnect captures params; in production it also wires the
    // socket notifiers once the fd is valid.
    void startConnect(const remote::SshSessionWorker::ConnectParams &params);
    void acceptHostKey();
    void rejectHostKey();

    // logicalId is minted by the UI-thread facade and is stable for the
    // channel's whole life. wantPty + term/cols/rows + command describe the
    // channel setup (PTY shell for the remote terminal).
    void requestOpenChannel(int logicalId, bool wantPty, const QByteArray &term,
                            int cols, int rows, const QString &command);
    void requestResize(int logicalId, int cols, int rows);
    void requestWrite(int logicalId, const QByteArray &bytes);
    void requestCloseChannel(int logicalId);
    void requestDisconnect();

    // --- SFTP request slots (D1a) --------------------------------------------
    // Posted from the UI thread (queued) by SshConnection on behalf of
    // RemoteFsBackend; called directly in tests. reqId is minted UI-side and is
    // echoed back in the matching *Done signal so the backend can resolve the
    // right callback. Routing by kind (D1a): Read/Write enqueue on the BULK lane
    // (its own SFTP channel + FIFO), Stat/Readdir on the METADATA lane (its own
    // SFTP channel + FIFO), so a large bulk transfer never blocks a metadata op.
    // Each lane opens its channel once (sftpInit per lane, never per-op) and
    // drives as far as it can now; EAGAIN re-arms on the next socket edge
    // (production) or serviceSftpForTest (tests).
    void requestSftpRead(quint64 reqId, const QString &path);
    // Streaming variant: emits sftpReadChunk() per chunk instead of buffering.
    // On EOF emits sftpReadDone(reqId, true, {}, "") with empty data.
    void requestSftpStreamRead(quint64 reqId, const QString &path);
    void requestSftpWrite(quint64 reqId, const QString &path, const QByteArray &data);
    void requestSftpStat(quint64 reqId, const QString &path);
    void requestSftpReaddir(quint64 reqId, const QString &path);
    void requestSftpRename(quint64 reqId, const QString &oldPath, const QString &newPath);
    void requestSftpMkdir(quint64 reqId, const QString &path);
    void requestSftpUnlink(quint64 reqId, const QString &path);
    // Cancel a timed-out bulk SFTP read: shuts down the bulk SFTP lane (so its
    // stuck open_state is reset) and fails the op, unblocking the FIFO queue.
    void requestSftpCancelBulk(quint64 reqId);

    // --- exec request slots (D6/D8) ------------------------------------------
    // Posted from the UI thread (queued) by SshConnection on behalf of
    // RemoteGitProcessRunner (and, later, the ACP exec transport); called
    // directly in tests. reqId is minted UI-side and echoed in the matching
    // exec* result signals. Each requestExec opens its OWN non-PTY exec channel
    // (multiplexed on the live session), runs `command`, feeds stdinPayload (if
    // any), streams stdout/stderr back, and reports the channel exit status —
    // independent of and concurrent with other exec ops and the PTY channels.
    // requestExecCancel closes the channel for an in-flight op without emitting
    // a result (the runner already resolved its callback).
    //
    // FIX-2: `kind` classifies the exec op for the admission budget. Default is
    // ShortLived (git-exec); ACP passes LongLived. The 3-arg overload (no kind)
    // is kept for backward compat with existing QMetaObject::invokeMethod calls.
    void requestExec(quint64 reqId, const QString &command,
                     const QByteArray &stdinPayload,
                     remote::ExecKind kind);
    void requestExec(quint64 reqId, const QString &command, const QByteArray &stdinPayload);
    void requestExecCancel(quint64 reqId);
    // Append bytes to an in-flight exec op's stdin (D8). The one-shot
    // stdinPayload of requestExec covers the git runner (feed once, then read to
    // EOF); a long-lived ACP agent session instead streams JSON-RPC frames over
    // the channel's whole life, so the ACP exec transport posts each frame here.
    // Bytes are buffered and drained on the next service pass (EAGAIN re-arms the
    // write notifier). No EOF — stdin stays open until the op finishes/cancels.
    void requestExecWrite(quint64 reqId, const QByteArray &bytes);

signals:
    void stateChanged(remote::SshSessionWorker::State state);
    void hostKeyReceived(const QString &fingerprint, const QByteArray &key);
    void authFailed(const QString &reason);
    void connected();
    void channelOpened(int logicalId);
    void channelOpenFailed(int logicalId, const QString &reason);
    void dataReady(int logicalId, const QByteArray &bytes);
    void channelClosed(int logicalId, int exitStatus);
    // FIX-2: emitted when a channel open cannot be admitted immediately because
    // the dynamic budget is full. The opener waits in its sub-queue; the normal
    // channelOpened/channelReady path fires once a slot frees. SshConnection
    // relays this to the UI for the "Waiting for an available channel" banner.
    void channelQueued(int logicalId);
    // Connection lost / disconnected with a human reason.
    void disconnected(const QString &reason);

    // --- SFTP result signals (D1) --------------------------------------------
    void sftpReadDone(quint64 reqId, bool ok, const QByteArray &data, const QString &error);
    // Streaming read: each non-empty chunk emitted here as it arrives (StreamRead ops).
    void sftpReadChunk(quint64 reqId, const QByteArray &chunk);
    void sftpWriteDone(quint64 reqId, bool ok, const QString &error);
    void sftpStatDone(quint64 reqId, bool ok, bool exists, bool isDir,
                      qint64 size, qint64 mtimeSecs, const QString &error);
    void sftpReaddirDone(quint64 reqId, bool ok,
                         const QList<remote::RemoteDirEntry> &entries, const QString &error);
    void sftpRenameDone(quint64 reqId, bool ok, const QString &error);
    void sftpMkdirDone(quint64 reqId, bool ok, const QString &error);
    void sftpUnlinkDone(quint64 reqId, bool ok, const QString &error);

    // --- exec result signals (D6/D8) -----------------------------------------
    void execStdoutChunk(quint64 reqId, const QByteArray &chunk);
    void execStderrChunk(quint64 reqId, const QByteArray &chunk);
    void execDone(quint64 reqId, int exitStatus);

    // --- diagnostic signal ---------------------------------------------------
    // Low-volume debug events emitted from the worker thread. SshConnection
    // relays them into its appendDebugLog so they appear in the SSH debug dialog
    // alongside the existing UI-thread log entries. Only emitted when the event
    // is operationally significant (SFTP lane stall, abandoned exec drain, etc.)
    // — not on every socket edge.
    void debugEvent(const QString &line);

private slots:
    void onSocketActivity();
    void onMaintenanceTick();  // FIX-1 backoff retries + FIX-4 connect re-poll
    void onKeepaliveTick();    // FIX-3 keepalive send + miss detection

private:
    enum class ChPhase
    {
        Queued,    // waiting for a slot (counts against queue, not the cap)
        Opening,   // openChannel in flight (counts against the cap)
        NeedPty,   // channel open; requestPty pending
        NeedExec,  // pty done (or skipped); execOrShell pending
        Open,      // fully set up; in the read/write rotation
        Closed,    // terminal; pending removal
    };

    struct Channel
    {
        int logicalId = -1;
        int transportId = -1;
        ChPhase phase = ChPhase::Queued;
        bool wantPty = false;
        QByteArray term;
        int cols = 80;
        int rows = 24;
        QString command;     // empty -> interactive shell
        QByteArray pending;  // unsent write bytes (move-appended; no per-byte alloc)
        // FIX-1: backoff state for ChannelBusy retries.
        int backoffMs = 250;         // current backoff interval (doubles to max 2000)
        qint64 nextRetryMs = 0;      // monotonic msec timestamp for next retry (0 = ready now)
        // FIX-2: channelQueued emitted once when this opener first could not be
        // admitted (the signal is edge-triggered, not level-triggered, so the UI
        // banner is raised exactly once per waiting channel).
        bool queuedSignalled = false;
    };

    // --- FIX-2: admission budget constants -----------------------------------
    // Total cap (configurable per-profile): PTY + exec + SFTP combined.
    // Default 8 leaves 2 headroom under OpenSSH MaxSessions 10.
    static constexpr int kDefaultCap = 8;
    // Reserved for SFTP (bulk + metadata, both long-lived). The D1a two-SFTP
    // split fills both reserved slots — the bulk and metadata lanes each open
    // their own SFTP channel lazily on first use and hold it for the session.
    static constexpr int kSftpReserved = 2;
    // Within the dynamic budget (cap - sftpReserved):
    //   max kMaxLongLived long-lived (PTY + acp-exec)
    //   1 short-lived reserve (git-exec) — always available for short-lived.
    // For the default cap=8: dynamic=6, maxLong=5, shortReserve=1.
    static constexpr int kMaxLongLived = 5;
    // For small caps (e.g. tests with cap=2 where sftpReserved would exceed the
    // cap), the budget degrades gracefully — see admitPending() for the formula:
    // every dynamic slot is then usable by either kind (no reserve), preserving
    // the original FIFO-at-cap behavior the Batch-A tests assert.

    // --- FIX-2: pending-open entry (unified sub-queue item) ------------------
    // Represents a channel or exec op waiting for admission. Tagged by kind so
    // the admission pass can prioritize short-lived over long-lived.
    enum class OpenerSource { PtyChannel, ExecOp };
    struct PendingOpen
    {
        OpenerSource source = OpenerSource::PtyChannel;
        int logicalId = -1;    // for PtyChannel
        quint64 execReqId = 0; // for ExecOp
        ExecKind kind = ExecKind::ShortLived; // PTY is always LongLived
    };

    void setState(State s);
    void pump();                 // single bounded sweep (D3 + D4 + setup)
    void advanceConnect();       // socket->handshake->hostkey->auth->ready
    void advanceChannelSetup();  // open/pty/exec for not-yet-Open channels
    void readSweep();            // round-robin reads (D3)
    void flushPendingWrites();   // write path + notifier invariant (D4)
    void admitPending();         // FIX-2: dequeue while budget allows (replaces tryStartQueued)
    void enterConnectionLost(const QString &reason); // D8
    void finishChannel(int logicalId, int exitStatus);

    // FIX-2: unified live count (PTY Opening..Open + exec NeedOpen..Streaming + SFTP).
    int unifiedLiveCount() const;
    // Count of live dynamic channels (excludes SFTP reserved).
    int liveDynamicCount() const;
    // Count of live long-lived dynamic channels (PTY + acp-exec).
    int liveLongLivedCount() const;
    // Legacy: PTY/shell channels in Opening..Open only (for backward-compat test hook).
    int livePtyChannelCount() const;
    Channel *channelByTransportId(int transportId);

    void setupNotifiers();       // production only (real fd)
    void teardownNotifiers();
    void setWriteNotifierEnabled(bool on);
    void ensureTimers();         // lazy-create timers on the worker thread
    void armMaintenanceTimer();  // start/restart the short maintenance timer
    void stopMaintenanceTimer();

    // FIX-1: check if any opener is past its backoff deadline and ready to retry.
    bool hasBackoffReady() const;

    // --- SFTP engine (D1a): two independent lanes (bulk + metadata) -----------
    // Bulk lane: file read/write (editor open/save), where a single transfer can
    // be 100 MB+. Metadata lane: readdir/stat/poll-watch listings, all small and
    // latency-sensitive. Each lane has its own sftpInit (opening its own SFTP
    // channel on the multiplexed connection), its own FIFO queue, and its own
    // service function. Both are driven from onSocketActivity (after pump). Both
    // count toward the unified cap's 2-SFTP-reserved budget (FIX-2). A large bulk
    // read can never block tree-poll/readdir behind it.
    enum class SftpKind { Read, StreamRead, Write, Stat, Readdir, Rename, Mkdir, Unlink };
    enum class SftpPhase { NeedOpen, Transfer, NeedClose };
    enum class SftpLane { Bulk, Meta };

    struct SftpOp
    {
        quint64    reqId = 0;
        SftpKind   kind = SftpKind::Read;
        SftpPhase  phase = SftpPhase::NeedOpen;
        QString    path;
        QString    path2;                    // destination path for Rename
        int        handleId = -1;        // file/dir handle once opened
        QByteArray buffer;               // read accumulator / write source
        qint64     writeOffset = 0;      // bytes of `buffer` written so far
        QList<RemoteDirEntry> entries;   // readdir accumulator
    };

    static SftpLane laneForKind(SftpKind kind);
    // Map the worker's lane enum to the transport's lane enum (1:1).
    static ISshTransport::SftpLane transportLane(SftpLane lane);

    void serviceSftp();              // services both lanes (meta first, then bulk)
    void serviceSftpLane(SftpLane lane, QList<SftpOp> &queue, bool &inited);
    ISshTransport::Step ensureSftpLaneInited(SftpLane lane, bool &inited);
    bool advanceSftpOp(SftpLane lane, SftpOp &op);
    void enqueueSftpOp(SftpOp op);
    void failSftpOp(const SftpOp &op, const QString &reason);
    void failAllSftp(const QString &reason);

    // --- exec engine (D6): independent non-PTY channels, run concurrently -----
    enum class ExecPhase
    {
        Queued,     // FIX-2: waiting for admission (budget full)
        NeedOpen,   // admitted; openChannel in flight
        NeedExec,   // channel open; channel_exec(command) pending
        Streaming,  // exec started; draining stdout/stderr to EOF
        Done,       // terminal; pending removal
    };
    struct ExecOp
    {
        quint64    reqId = 0;
        int        transportId = -1;
        ExecPhase  phase = ExecPhase::Queued; // FIX-2: starts Queued, not NeedOpen
        ExecKind   kind = ExecKind::ShortLived; // FIX-2
        QString    command;
        QByteArray stdinPayload;   // fed once at Streaming start
        qint64     stdinOffset = 0;
        bool       stdinSent = false;
        bool       eofSent = false;
        QByteArray streamStdin;
        qint64     streamStdinOffset = 0;
        bool       stdoutEof = false;
        bool       stderrEof = false;
        // FIX-1: backoff state for ChannelBusy retries during NeedOpen.
        int        backoffMs = 250;
        qint64     nextRetryMs = 0;
    };
    void serviceExec();
    bool advanceExecOp(ExecOp &op);
    void failAllExec(int exitStatus);

    std::unique_ptr<ISshTransport> m_transport;
    int m_channelCap;
    State m_state = State::Idle;

    ConnectParams m_params;
    bool m_hostKeyEmitted = false;
    bool m_hostKeyAccepted = false;
    bool m_hostKeyRejected = false;

    // Live + queued channels. m_channels holds every non-removed channel keyed
    // by logicalId. FIX-2: the open queue is split into two sub-queues by kind.
    QHash<int, Channel> m_channels;
    // FIX-2: two sub-queues for pending opens. Short-lived (git-exec) is
    // admitted first; long-lived (PTY + acp-exec) second. FIFO within each.
    QList<PendingOpen> m_shortQueue;
    QList<PendingOpen> m_longQueue;
    // Stable rotation order for the round-robin read sweep (insertion order of
    // Open channels). Kept as a list so every sweep visits each exactly once.
    QList<int> m_rotation;

    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    bool m_writeNotifierWanted = false;
    bool m_lost = false;

    // FIX-3: keepalive timer state.
    QTimer *m_keepaliveTimer = nullptr;
    bool m_sawInboundSinceKeepalive = false;
    int m_keepaliveMissCount = 0;

    // FIX-1/FIX-4/FIX-5: maintenance timer (short interval) for backoff retries,
    // connect re-polling, and connect-phase deadline enforcement. Created lazily
    // on the worker thread.
    QTimer *m_maintenanceTimer = nullptr;
    // Monotonic clock base for backoff deadlines (QDateTime::currentMSecsSinceEpoch).
    qint64 monotonicMs() const;

    // FIX-5: overall connect-phase deadline (handshake + auth). Set in
    // startConnect; enforced by the maintenance timer. Covers the gap where the
    // server accepts TCP but never responds to SSH protocol traffic.
    static constexpr int kConnectPhaseTimeoutMs = 30000;
    qint64 m_connectDeadlineMs = 0;

    // SFTP engine state (D1a): two independent lanes.
    // Bulk lane (Read + Write) and metadata lane (Stat + Readdir). Each has its
    // own SFTP channel (sftpInit) + FIFO queue + inited flag, so a large bulk
    // transfer never blocks latency-sensitive metadata ops.
    bool m_sftpBulkInited = false;
    bool m_sftpMetaInited = false;
    QList<SftpOp> m_sftpBulkQueue;
    QList<SftpOp> m_sftpMetaQueue;

    // Exec engine state (D6).
    QList<ExecOp> m_execOps;
};

} // namespace remote

Q_DECLARE_METATYPE(remote::SshSessionWorker::ConnectParams)
Q_DECLARE_METATYPE(remote::RemoteDirEntry)
Q_DECLARE_METATYPE(QList<remote::RemoteDirEntry>)
Q_DECLARE_METATYPE(remote::ExecKind)

#endif // REMOTE_SSHSESSIONWORKER_H