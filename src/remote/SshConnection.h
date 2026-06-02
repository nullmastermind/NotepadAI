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

#ifndef REMOTE_SSHCONNECTION_H
#define REMOTE_SSHCONNECTION_H

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

#include "SshProfile.h"
#include "SshHostKeyStore.h"
#include "SshSessionWorker.h"

class QThread;

namespace ai { class CredentialStore; }

namespace remote {

class ISshTransport;
class SshChannel;

// UI-thread facade over one SSH connection. Spawns and owns the worker QThread
// (the worker runs Libssh2Transport / FakeSshTransport), owns the per-channel
// SshChannel proxies, and relays state. ALL UI↔worker communication is via
// queued connections — no locks. One SshConnection == one TCP connection ==
// one LIBSSH2_SESSION (multiplexing many channels).
class SshConnection : public QObject
{
    Q_OBJECT

public:
    using State = SshSessionWorker::State;

    // Production: builds a Libssh2Transport internally.
    SshConnection(const SshProfile &profile,
                  ai::CredentialStore *credentialStore,
                  QObject *parent = nullptr);
    // Test / advanced: inject a transport (e.g. FakeSshTransport). Takes
    // ownership and moves it onto the worker thread.
    SshConnection(const SshProfile &profile,
                  std::unique_ptr<ISshTransport> transport,
                  ai::CredentialStore *credentialStore,
                  QObject *parent = nullptr);
    ~SshConnection() override;

    const SshProfile &profile() const { return m_profile; }
    State state() const { return m_state; }
    bool isConnected() const { return m_state == State::Ready; }

    // Begin the staged connect. Host-key prompts surface via hostKeyReceived;
    // the caller MUST respond with acceptHostKey()/rejectHostKey(). A key that
    // matches the app-managed known_hosts is auto-accepted (no prompt); a key
    // that DIFFERS from the stored one surfaces hostKeyChanged (MITM guard) and
    // is NOT auto-trusted.
    void connectToHost();
    void acceptHostKey();
    void rejectHostKey();
    void disconnectFromHost();

    // Open a new logical channel. Returns an owned proxy immediately; the actual
    // SSH channel opens asynchronously (channel becomes usable on channelReady).
    // wantPty + term/cols/rows + command describe a remote PTY shell.
    SshChannel *openChannel(bool wantPty, const QByteArray &term, int cols, int rows,
                            const QString &command);

    // Posted to the worker (queued). Called by SshChannel.
    void writeToChannel(int logicalId, const QByteArray &bytes);
    void resizeChannel(int logicalId, int cols, int rows);
    void closeChannel(int logicalId);

    // --- SFTP (D1a) ----------------------------------------------------------
    // Posted to the worker (queued). Called by RemoteFsBackend. reqId is minted
    // by the backend and echoed in the matching sftp*Result signal so it can
    // resolve the right pending callback. The worker routes by kind: read/write
    // go to the bulk SFTP lane, stat/readdir to the metadata lane (D1a two-
    // channel split). Results arrive on the UI thread via the signals below.
    void sftpRead(quint64 reqId, const QString &path);
    // Streaming variant: sftpReadChunkResult fires per chunk; sftpReadResult fires on EOF/error.
    void sftpStreamRead(quint64 reqId, const QString &path);
    void sftpWrite(quint64 reqId, const QString &path, const QByteArray &data);
    void sftpStat(quint64 reqId, const QString &path);
    void sftpReaddir(quint64 reqId, const QString &path);
    void sftpRename(quint64 reqId, const QString &oldPath, const QString &newPath);
    void sftpMkdir(quint64 reqId, const QString &path);
    void sftpUnlink(quint64 reqId, const QString &path);

    // --- exec (D6) -----------------------------------------------------------
    // Posted to the worker (queued). Called by RemoteGitProcessRunner (and the
    // ACP exec transport later). execStart mints a process-wide-unique reqId,
    // posts the exec request, and returns the reqId so the caller can match the
    // exec* result signals (multiple runners share one connection, so the id
    // must be unique across them — minted here, not per-runner). execCancel
    // tears down an in-flight op's channel without a result.
    //
    // FIX-2: the kind-carrying overload classifies the exec op for the admission
    // budget — ShortLived (git-exec) may use the reserved short-lived slot,
    // LongLived (acp-exec) may not. The 2-arg overload defaults to ShortLived so
    // existing callers/tests are unaffected.
    quint64 execStart(const QString &command, const QByteArray &stdinPayload);
    quint64 execStart(const QString &command, const QByteArray &stdinPayload,
                      remote::ExecKind kind);
    void execCancel(quint64 reqId);
    // Cancel a pending bulk SFTP read. Reinitializes the bulk lane so subsequent
    // reads are not blocked behind the timed-out op (FIFO wedge fix).
    void sftpCancelBulk(quint64 reqId);
    // Append more bytes to an in-flight exec op's stdin (D8). Unlike the one-shot
    // execStart(stdinPayload) feed used by the git runner, a long-lived ACP agent
    // session keeps writing JSON-RPC frames over the channel's whole life, so the
    // ACP exec transport posts each frame here. No EOF primitive — stdin stays
    // open until the op finishes / is cancelled.
    void execWrite(quint64 reqId, const QByteArray &bytes);

    // --- SSH debug log -------------------------------------------------------
    // Bounded timestamped ring buffer of SSH transport events. Safe to read on
    // the UI thread only (all writes happen there via queued relay lambdas and
    // UI-thread call sites). Mirrors AcpConnection::m_debugLog exactly.
    QStringList debugLog() const { return m_debugLog; }
    void clearDebugLog();
    void appendDebugLog(QString line);

signals:
    void stateChanged(remote::SshConnection::State state);
    // Unknown host: prompt the user (first-connect TOFU is NOT silent).
    void hostKeyReceived(const QString &fingerprint, const QByteArray &key);
    // Known host whose key CHANGED since last accepted — loud warning, no
    // silent proceed (MITM guard, design D9).
    void hostKeyChanged(const QString &fingerprint, const QByteArray &key);
    void authFailed(const QString &reason);
    void connected();
    void connectionLost(const QString &reason);
    void channelReady(int logicalId);
    void channelOpenFailed(int logicalId, const QString &reason);
    // FIX-2: relayed from the worker when a channel open cannot be admitted
    // immediately (the dynamic budget is full). The UI raises a "Waiting for an
    // available channel…" banner; the normal channelReady fires once a slot frees.
    void channelQueued(int logicalId);
    // Emitted when a new SSH debug log line is appended. Dialog connects here
    // for live tailing; zero cost when no listener is connected.
    void debugLogAppended(const QString &line);

    // --- SFTP results (D1) — relayed queued from the worker ------------------
    void sftpReadResult(quint64 reqId, bool ok, const QByteArray &data, const QString &error);
    // Streaming read: each chunk relayed here as it arrives from the worker.
    void sftpReadChunkResult(quint64 reqId, const QByteArray &chunk);
    void sftpWriteResult(quint64 reqId, bool ok, const QString &error);
    void sftpStatResult(quint64 reqId, bool ok, bool exists, bool isDir,
                        qint64 size, qint64 mtimeSecs, const QString &error);
    void sftpReaddirResult(quint64 reqId, bool ok,
                           const QList<remote::RemoteDirEntry> &entries, const QString &error);
    void sftpRenameResult(quint64 reqId, bool ok, const QString &error);
    void sftpMkdirResult(quint64 reqId, bool ok, const QString &error);
    void sftpUnlinkResult(quint64 reqId, bool ok, const QString &error);

    // --- exec results (D6) — relayed queued from the worker ------------------
    void execStdout(quint64 reqId, const QByteArray &chunk);
    void execStderr(quint64 reqId, const QByteArray &chunk);
    void execDone(quint64 reqId, int exitStatus);

private:
    void init(std::unique_ptr<ISshTransport> transport);
    void handleHostKey(const QString &fingerprint, const QByteArray &key);
    SshSessionWorker::ConnectParams buildConnectParams() const;

    SshProfile m_profile;
    ai::CredentialStore *m_credentialStore;
    SshHostKeyStore m_hostKeyStore;
    QByteArray m_pendingHostKey;          // key awaiting user accept (then persisted)
    QThread *m_thread = nullptr;
    SshSessionWorker *m_worker = nullptr; // lives on m_thread; deleted via thread teardown
    State m_state = State::Idle;
    QHash<int, SshChannel *> m_channels;  // logicalId → proxy (UI thread owns)
    int m_nextLogicalId = 1;
    // Monotonic reqId for exec ops (D6). Shared across every RemoteGitProcessRunner
    // (and ACP exec) on this connection so ids never collide between runners.
    quint64 m_nextExecReqId = 0;

    // SSH debug log ring buffer. UI-thread-only. appendDebugLog() prefixes a
    // timestamp, appends, trims overflow, and emits debugLogAppended.
    static constexpr int kDebugLogMaxLines     = 5000;
    static constexpr int kDebugLogLineMaxChars = 4096;
    QStringList m_debugLog;
};

} // namespace remote

#endif // REMOTE_SSHCONNECTION_H
