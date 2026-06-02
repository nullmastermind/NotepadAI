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

#include "SshConnection.h"

#include "Libssh2Transport.h"
#include "SshChannel.h"

#include "ai/CredentialStore.h"

#include <QTime>

namespace {
const char *sshStateLabel(remote::SshSessionWorker::State s)
{
    switch (s) {
    case remote::SshSessionWorker::State::Idle:             return "Idle";
    case remote::SshSessionWorker::State::ConnectingSocket: return "ConnectingSocket";
    case remote::SshSessionWorker::State::Handshaking:      return "Handshaking";
    case remote::SshSessionWorker::State::AwaitingHostKey:  return "AwaitingHostKey";
    case remote::SshSessionWorker::State::Authenticating:   return "Authenticating";
    case remote::SshSessionWorker::State::Ready:            return "Ready";
    case remote::SshSessionWorker::State::Disconnected:     return "Disconnected";
    case remote::SshSessionWorker::State::Failed:           return "Failed";
    }
    return "Unknown";
}
} // namespace

#include <QThread>

namespace remote {

SshConnection::SshConnection(const SshProfile &profile,
                             ai::CredentialStore *credentialStore, QObject *parent)
    : QObject(parent)
    , m_profile(profile)
    , m_credentialStore(credentialStore)
{
    init(std::make_unique<Libssh2Transport>());
}

SshConnection::SshConnection(const SshProfile &profile,
                             std::unique_ptr<ISshTransport> transport,
                             ai::CredentialStore *credentialStore, QObject *parent)
    : QObject(parent)
    , m_profile(profile)
    , m_credentialStore(credentialStore)
{
    init(std::move(transport));
}

void SshConnection::init(std::unique_ptr<ISshTransport> transport)
{
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("SshWorker:%1").arg(m_profile.id));

    // The worker owns the transport and lives on the worker thread. It has no
    // parent so moveToThread is clean; it is deleted when the thread finishes.
    m_worker = new SshSessionWorker(std::move(transport));
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    // --- worker → facade relays (queued: cross-thread) -----------------------
    connect(m_worker, &SshSessionWorker::stateChanged, this,
            [this](SshSessionWorker::State s) {
                m_state = s;
                appendDebugLog(QStringLiteral("state: %1")
                                   .arg(QString::fromLatin1(sshStateLabel(s))));
                emit stateChanged(s);
            });
    connect(m_worker, &SshSessionWorker::hostKeyReceived, this,
            &SshConnection::handleHostKey);
    connect(m_worker, &SshSessionWorker::authFailed, this,
            [this](const QString &reason) {
                appendDebugLog(QStringLiteral("auth-failed: %1").arg(reason));
                emit authFailed(reason);
            });
    connect(m_worker, &SshSessionWorker::connected, this, [this]() {
        appendDebugLog(QStringLiteral("connected"));
        emit connected();
    });
    connect(m_worker, &SshSessionWorker::disconnected, this,
            [this](const QString &reason) {
                appendDebugLog(QStringLiteral("connection-lost: %1").arg(reason));
                emit connectionLost(reason);
            });

    connect(m_worker, &SshSessionWorker::channelOpened, this, [this](int logicalId) {
        if (m_channels.contains(logicalId)) {
            appendDebugLog(QStringLiteral("channel-ready: id=%1").arg(logicalId));
            emit channelReady(logicalId);
        }
    });
    connect(m_worker, &SshSessionWorker::channelOpenFailed, this,
            [this](int logicalId, const QString &reason) {
                appendDebugLog(QStringLiteral("channel-open-failed: id=%1 %2")
                                   .arg(logicalId).arg(reason));
                if (auto *ch = m_channels.value(logicalId, nullptr)) {
                    ch->markClosed(-1);
                }
                emit channelOpenFailed(logicalId, reason);
            });
    // FIX-2: relay the worker's channelQueued signal (queued: cross-thread) so
    // the UI can raise the "Waiting for an available channel…" banner.
    connect(m_worker, &SshSessionWorker::channelQueued, this,
            [this](int logicalId) {
                appendDebugLog(QStringLiteral("channel-queued: id=%1").arg(logicalId));
                emit channelQueued(logicalId);
            });
    connect(m_worker, &SshSessionWorker::dataReady, this,
            [this](int logicalId, const QByteArray &bytes) {
                if (auto *ch = m_channels.value(logicalId, nullptr)) {
                    ch->appendIncoming(bytes);
                }
            });
    connect(m_worker, &SshSessionWorker::channelClosed, this,
            [this](int logicalId, int exitStatus) {
                appendDebugLog(QStringLiteral("channel-closed: id=%1 exit=%2")
                                   .arg(logicalId).arg(exitStatus));
                if (auto *ch = m_channels.value(logicalId, nullptr)) {
                    ch->markClosed(exitStatus);
                }
            });

    // SFTP results (D1): relays with debug logging, worker thread → UI thread
    // (queued). RemoteFsBackend resolves the reqId to its pending callback.
    connect(m_worker, &SshSessionWorker::sftpReadDone, this,
            [this](quint64 reqId, bool ok, const QByteArray &data, const QString &error) {
                appendDebugLog(ok
                    ? QStringLiteral("sftp-read-result: req=%1 bytes=%2").arg(reqId).arg(data.size())
                    : QStringLiteral("sftp-read-result: req=%1 ERROR=%2").arg(reqId).arg(error));
                emit sftpReadResult(reqId, ok, data, error);
            });
    connect(m_worker, &SshSessionWorker::sftpWriteDone, this,
            [this](quint64 reqId, bool ok, const QString &error) {
                appendDebugLog(ok
                    ? QStringLiteral("sftp-write-result: req=%1").arg(reqId)
                    : QStringLiteral("sftp-write-result: req=%1 ERROR=%2").arg(reqId).arg(error));
                emit sftpWriteResult(reqId, ok, error);
            });
    connect(m_worker, &SshSessionWorker::sftpStatDone, this,
            [this](quint64 reqId, bool ok, bool exists, bool isDir,
                   qint64 size, qint64 mtimeSecs, const QString &error) {
                appendDebugLog(ok
                    ? QStringLiteral("sftp-stat-result: req=%1 exists=%2 isDir=%3 size=%4")
                          .arg(reqId).arg(exists).arg(isDir).arg(size)
                    : QStringLiteral("sftp-stat-result: req=%1 ERROR=%2").arg(reqId).arg(error));
                emit sftpStatResult(reqId, ok, exists, isDir, size, mtimeSecs, error);
            });
    connect(m_worker, &SshSessionWorker::sftpReaddirDone, this,
            [this](quint64 reqId, bool ok, const QList<remote::RemoteDirEntry> &entries,
                   const QString &error) {
                appendDebugLog(ok
                    ? QStringLiteral("sftp-readdir-result: req=%1 entries=%2")
                          .arg(reqId).arg(entries.size())
                    : QStringLiteral("sftp-readdir-result: req=%1 ERROR=%2").arg(reqId).arg(error));
                emit sftpReaddirResult(reqId, ok, entries, error);
            });
    connect(m_worker, &SshSessionWorker::sftpRenameDone, this,
            [this](quint64 reqId, bool ok, const QString &error) {
                appendDebugLog(ok
                    ? QStringLiteral("sftp-rename-result: req=%1").arg(reqId)
                    : QStringLiteral("sftp-rename-result: req=%1 ERROR=%2").arg(reqId).arg(error));
                emit sftpRenameResult(reqId, ok, error);
            });
    connect(m_worker, &SshSessionWorker::sftpMkdirDone, this,
            [this](quint64 reqId, bool ok, const QString &error) {
                appendDebugLog(ok
                    ? QStringLiteral("sftp-mkdir-result: req=%1").arg(reqId)
                    : QStringLiteral("sftp-mkdir-result: req=%1 ERROR=%2").arg(reqId).arg(error));
                emit sftpMkdirResult(reqId, ok, error);
            });
    connect(m_worker, &SshSessionWorker::sftpUnlinkDone, this,
            [this](quint64 reqId, bool ok, const QString &error) {
                appendDebugLog(ok
                    ? QStringLiteral("sftp-unlink-result: req=%1").arg(reqId)
                    : QStringLiteral("sftp-unlink-result: req=%1 ERROR=%2").arg(reqId).arg(error));
                emit sftpUnlinkResult(reqId, ok, error);
            });

    // Exec results (D6): relays with debug logging, worker thread → UI thread
    // (queued). RemoteGitProcessRunner resolves the reqId to its in-flight op.
    // stdout/stderr chunks are left as direct relays (raw data, too noisy to log).
    connect(m_worker, &SshSessionWorker::execStdoutChunk, this,
            &SshConnection::execStdout);
    connect(m_worker, &SshSessionWorker::execStderrChunk, this,
            &SshConnection::execStderr);
    connect(m_worker, &SshSessionWorker::execDone, this,
            [this](quint64 reqId, int exitStatus) {
                appendDebugLog(QStringLiteral("exec-done: req=%1 exit=%2")
                                   .arg(reqId).arg(exitStatus));
                emit execDone(reqId, exitStatus);
            });

    m_thread->start();
}

SshConnection::~SshConnection()
{
    // Ask the worker to tear down libssh2 on its own thread, then stop the loop.
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "requestDisconnect", Qt::QueuedConnection);
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
    // m_worker was deleteLater'd via QThread::finished; channels are children
    // of this and clean up normally.
}

SshSessionWorker::ConnectParams SshConnection::buildConnectParams() const
{
    SshSessionWorker::ConnectParams p;
    p.host = m_profile.host;
    p.port = m_profile.port;
    p.username = m_profile.username;
    p.authMethod = m_profile.authMethod;
    p.keyPath = m_profile.keyPath;

    // Secret providers run ON THE WORKER THREAD during auth so the UI thread is
    // never blocked on a keychain syscall. Capture by value what they need.
    ai::CredentialStore *store = m_credentialStore;
    const QString id = m_profile.id;
    if (store) {
        p.passwordProvider = [store, id]() {
            return store->retrieveSecret(sshSecretKey(id, QStringLiteral("password")));
        };
        p.passphraseProvider = [store, id]() {
            return store->retrieveSecret(sshSecretKey(id, QStringLiteral("passphrase")));
        };
    }
    return p;
}

void SshConnection::connectToHost()
{
    appendDebugLog(QStringLiteral("connect: %1@%2:%3")
                       .arg(m_profile.username, m_profile.host).arg(m_profile.port));
    QMetaObject::invokeMethod(m_worker, "startConnect", Qt::QueuedConnection,
                              Q_ARG(remote::SshSessionWorker::ConnectParams,
                                    buildConnectParams()));
}

void SshConnection::acceptHostKey()
{
    appendDebugLog(QStringLiteral("host-key-accepted"));
    // Persist the now-trusted key in the app-managed known_hosts (design D9 —
    // not the system ~/.ssh/known_hosts) before letting the worker authenticate.
    if (!m_pendingHostKey.isEmpty()) {
        m_hostKeyStore.add(m_profile.host, m_profile.port, m_pendingHostKey);
        m_pendingHostKey.clear();
    }
    QMetaObject::invokeMethod(m_worker, "acceptHostKey", Qt::QueuedConnection);
}

void SshConnection::rejectHostKey()
{
    appendDebugLog(QStringLiteral("host-key-rejected"));
    m_pendingHostKey.clear();
    QMetaObject::invokeMethod(m_worker, "rejectHostKey", Qt::QueuedConnection);
}

void SshConnection::handleHostKey(const QString &fingerprint, const QByteArray &key)
{
    // Consult the app-managed known_hosts on the UI thread (the worker stays
    // pure transport). Three cases (D9):
    //   - known + matching → auto-accept, no prompt.
    //   - known + changed  → loud warning (hostKeyChanged); do NOT auto-trust.
    //   - unknown          → prompt (hostKeyReceived) — first-connect is never
    //                        silently trusted.
    const QByteArray known = m_hostKeyStore.lookup(m_profile.host, m_profile.port);
    const char *status = known.isEmpty() ? "new" : (known == key ? "match" : "changed");
    appendDebugLog(QStringLiteral("host-key: fingerprint=%1 status=%2")
                       .arg(fingerprint, QString::fromLatin1(status)));
    if (!known.isEmpty() && known == key) {
        QMetaObject::invokeMethod(m_worker, "acceptHostKey", Qt::QueuedConnection);
        return;
    }
    m_pendingHostKey = key;
    if (!known.isEmpty() && known != key) {
        emit hostKeyChanged(fingerprint, key);
    } else {
        emit hostKeyReceived(fingerprint, key);
    }
}

void SshConnection::disconnectFromHost()
{
    appendDebugLog(QStringLiteral("disconnect: requested"));
    QMetaObject::invokeMethod(m_worker, "requestDisconnect", Qt::QueuedConnection);
}

SshChannel *SshConnection::openChannel(bool wantPty, const QByteArray &term,
                                       int cols, int rows, const QString &command)
{
    const int logicalId = m_nextLogicalId++;
    appendDebugLog(QStringLiteral("channel-open: id=%1 pty=%2 cmd=%3")
                       .arg(logicalId).arg(wantPty).arg(command.left(80)));
    auto *ch = new SshChannel(this, logicalId, this);
    m_channels.insert(logicalId, ch);
    connect(ch, &SshChannel::closed, this, [this, logicalId](int) {
        m_channels.remove(logicalId);
    });
    QMetaObject::invokeMethod(m_worker, "requestOpenChannel", Qt::QueuedConnection,
                              Q_ARG(int, logicalId), Q_ARG(bool, wantPty),
                              Q_ARG(QByteArray, term), Q_ARG(int, cols),
                              Q_ARG(int, rows), Q_ARG(QString, command));
    return ch;
}

void SshConnection::writeToChannel(int logicalId, const QByteArray &bytes)
{
    QMetaObject::invokeMethod(m_worker, "requestWrite", Qt::QueuedConnection,
                              Q_ARG(int, logicalId), Q_ARG(QByteArray, bytes));
}

void SshConnection::resizeChannel(int logicalId, int cols, int rows)
{
    QMetaObject::invokeMethod(m_worker, "requestResize", Qt::QueuedConnection,
                              Q_ARG(int, logicalId), Q_ARG(int, cols), Q_ARG(int, rows));
}

void SshConnection::closeChannel(int logicalId)
{
    appendDebugLog(QStringLiteral("channel-close: id=%1").arg(logicalId));
    QMetaObject::invokeMethod(m_worker, "requestCloseChannel", Qt::QueuedConnection,
                              Q_ARG(int, logicalId));
}

// --- SFTP posting methods (D1) — queued onto the worker thread ---------------

void SshConnection::sftpRead(quint64 reqId, const QString &path)
{
    appendDebugLog(QStringLiteral("sftp-read: req=%1 %2").arg(reqId).arg(path));
    QMetaObject::invokeMethod(m_worker, "requestSftpRead", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QString, path));
}

void SshConnection::sftpWrite(quint64 reqId, const QString &path, const QByteArray &data)
{
    appendDebugLog(QStringLiteral("sftp-write: req=%1 %2 bytes=%3")
                       .arg(reqId).arg(path).arg(data.size()));
    QMetaObject::invokeMethod(m_worker, "requestSftpWrite", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QString, path),
                              Q_ARG(QByteArray, data));
}

void SshConnection::sftpStat(quint64 reqId, const QString &path)
{
    appendDebugLog(QStringLiteral("sftp-stat: req=%1 %2").arg(reqId).arg(path));
    QMetaObject::invokeMethod(m_worker, "requestSftpStat", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QString, path));
}

void SshConnection::sftpReaddir(quint64 reqId, const QString &path)
{
    appendDebugLog(QStringLiteral("sftp-readdir: req=%1 %2").arg(reqId).arg(path));
    QMetaObject::invokeMethod(m_worker, "requestSftpReaddir", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QString, path));
}

void SshConnection::sftpRename(quint64 reqId, const QString &oldPath, const QString &newPath)
{
    appendDebugLog(QStringLiteral("sftp-rename: req=%1 %2 -> %3").arg(reqId).arg(oldPath).arg(newPath));
    QMetaObject::invokeMethod(m_worker, "requestSftpRename", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QString, oldPath),
                              Q_ARG(QString, newPath));
}

void SshConnection::sftpMkdir(quint64 reqId, const QString &path)
{
    appendDebugLog(QStringLiteral("sftp-mkdir: req=%1 %2").arg(reqId).arg(path));
    QMetaObject::invokeMethod(m_worker, "requestSftpMkdir", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QString, path));
}

void SshConnection::sftpUnlink(quint64 reqId, const QString &path)
{
    appendDebugLog(QStringLiteral("sftp-unlink: req=%1 %2").arg(reqId).arg(path));
    QMetaObject::invokeMethod(m_worker, "requestSftpUnlink", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QString, path));
}

// --- exec posting methods (D6) — queued onto the worker thread ---------------

quint64 SshConnection::execStart(const QString &command, const QByteArray &stdinPayload)
{
    // Backward-compat overload: git-exec is the common case → ShortLived.
    return execStart(command, stdinPayload, ExecKind::ShortLived);
}

quint64 SshConnection::execStart(const QString &command, const QByteArray &stdinPayload,
                                 remote::ExecKind kind)
{
    const quint64 reqId = ++m_nextExecReqId;
    appendDebugLog(QStringLiteral("exec-start: req=%1 kind=%2 cmd=%3")
                       .arg(reqId)
                       .arg(kind == ExecKind::LongLived ? QStringLiteral("long") : QStringLiteral("short"))
                       .arg(command.left(120)));
    QMetaObject::invokeMethod(m_worker, "requestExec", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QString, command),
                              Q_ARG(QByteArray, stdinPayload),
                              Q_ARG(remote::ExecKind, kind));
    return reqId;
}

void SshConnection::execCancel(quint64 reqId)
{
    appendDebugLog(QStringLiteral("exec-cancel: req=%1").arg(reqId));
    QMetaObject::invokeMethod(m_worker, "requestExecCancel", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId));
}

void SshConnection::execWrite(quint64 reqId, const QByteArray &bytes)
{
    QMetaObject::invokeMethod(m_worker, "requestExecWrite", Qt::QueuedConnection,
                              Q_ARG(quint64, reqId), Q_ARG(QByteArray, bytes));
}

// --- SSH debug log -----------------------------------------------------------

void SshConnection::clearDebugLog()
{
    m_debugLog.clear();
}

void SshConnection::appendDebugLog(QString line)
{
    if (line.size() > kDebugLogLineMaxChars) {
        line.truncate(kDebugLogLineMaxChars);
        line.append(QStringLiteral("… [truncated]"));
    }
    const QString prefixed =
        QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), line);
    m_debugLog.append(prefixed);
    if (m_debugLog.size() > kDebugLogMaxLines) {
        m_debugLog.erase(m_debugLog.begin(),
                         m_debugLog.begin() + (m_debugLog.size() - kDebugLogMaxLines));
    }
    emit debugLogAppended(prefixed);
}

} // namespace remote
