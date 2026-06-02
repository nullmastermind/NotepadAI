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

#include "RemoteFsBackend.h"

#include "SshConnection.h"

#include <QTimer>

#include <utility>

namespace remote {

namespace {

// D12 read-only auto-retry budget. Read-only ops are idempotent, so a TRANSIENT
// (connection/session-level) failure is retried up to kMaxRetries additional
// times (kMaxRetries + 1 = 3 attempts total) with a bounded exponential
// backoff: attempt 0→1 waits kBaseBackoffMs (200ms), attempt 1→2 waits
// 2*kBaseBackoffMs (400ms). Mutating ops (writeFileAsync) never retry.
constexpr int kMaxRetries = 2;       // additional attempts after the first
constexpr int kBaseBackoffMs = 200;  // 200ms, then 400ms

// Backoff before re-issuing the op that just failed on `attempt` (0-based).
// attempt 0 → 200ms, attempt 1 → 400ms (1 << attempt scaling).
inline int backoffForAttempt(int attempt)
{
    return kBaseBackoffMs << attempt;
}

} // namespace

RemoteFsBackend::RemoteFsBackend(SshConnection *connection, QObject *parent)
    : IFileSystemBackend(parent)
    , m_connection(connection)
{
    if (m_connection) {
        // Result signals are relayed queued from the worker thread by
        // SshConnection; resolve them to the right pending callback by reqId.
        connect(m_connection, &SshConnection::sftpReadResult,
                this, &RemoteFsBackend::onReadResult);
        connect(m_connection, &SshConnection::sftpReadChunkResult,
                this, &RemoteFsBackend::onStreamChunk);
        connect(m_connection, &SshConnection::sftpWriteResult,
                this, &RemoteFsBackend::onWriteResult);
        connect(m_connection, &SshConnection::sftpStatResult,
                this, &RemoteFsBackend::onStatResult);
        connect(m_connection, &SshConnection::sftpReaddirResult,
                this, &RemoteFsBackend::onReaddirResult);
        connect(m_connection, &SshConnection::sftpRenameResult,
                this, &RemoteFsBackend::onMutateResult);
        connect(m_connection, &SshConnection::sftpMkdirResult,
                this, &RemoteFsBackend::onMutateResult);
        connect(m_connection, &SshConnection::sftpUnlinkResult,
                this, &RemoteFsBackend::onMutateResult);
        connect(m_connection, &SshConnection::connectionLost,
                this, &RemoteFsBackend::onConnectionLost);
    }
}

RemoteFsBackend::~RemoteFsBackend()
{
    const QString reason = tr("Backend destroyed");
    for (auto it = m_readCallbacks.begin(); it != m_readCallbacks.end(); ++it) {
        if (it.value()) it.value()(false, QByteArray(), reason);
    }
    m_readCallbacks.clear();
    for (auto it = m_writeCallbacks.begin(); it != m_writeCallbacks.end(); ++it) {
        if (it.value()) it.value()(false, reason);
    }
    m_writeCallbacks.clear();
    for (auto it = m_statCallbacks.begin(); it != m_statCallbacks.end(); ++it) {
        if (it.value()) it.value()(false, FileStat(), reason);
    }
    m_statCallbacks.clear();
    for (auto it = m_readdirCallbacks.begin(); it != m_readdirCallbacks.end(); ++it) {
        if (it.value()) it.value()(false, QList<RemoteDirEntry>(), reason);
    }
    m_readdirCallbacks.clear();
    for (auto it = m_mutateCallbacks.begin(); it != m_mutateCallbacks.end(); ++it) {
        if (it.value()) it.value()(false, reason);
    }
    m_mutateCallbacks.clear();
    // Drain any in-flight streaming reads with a failure callback.
    for (auto it = m_streamDoneCallbacks.begin(); it != m_streamDoneCallbacks.end(); ++it) {
        if (it.value()) it.value()(false, reason);
    }
    m_streamChunkCallbacks.clear();
    m_streamDoneCallbacks.clear();
}

// --- async API ---------------------------------------------------------------
//
// D12 read-only auto-retry. readFileAsync/statAsync/readdirAsync delegate to
// their attempt-0 helper; on a TRANSIENT failure (isTransientError, defined
// inline in the header) with attempts remaining, the wrapped callback re-issues
// the SAME op (fresh reqId, same path) after a bounded backoff. writeFileAsync
// (mutating) is intentionally NOT retried — fail-no-replay.

void RemoteFsBackend::logEvent(const QString &message)
{
    if (m_connection) m_connection->appendDebugLog(message);
}

void RemoteFsBackend::cancelReadAsync(quint64 reqId)
{
    if (!m_connection || reqId == 0) return;
    // Remove the pending callback so a late sftpReadDone for this reqId is
    // silently discarded rather than firing after the timeout.
    m_readCallbacks.remove(reqId);
    // Also handle streaming read cancellations.
    m_streamChunkCallbacks.remove(reqId);
    m_streamDoneCallbacks.remove(reqId);
    m_connection->sftpCancelBulk(reqId);
}

void RemoteFsBackend::readFileAsync(const QString &path, ReadCallback cb)
{
    readFileAttempt(path, std::move(cb), 0);
}

quint64 RemoteFsBackend::readFileAsyncTracked(const QString &path, ReadCallback cb)
{
    if (!m_connection) {
        if (cb) cb(false, QByteArray(), tr("No SSH connection"));
        return 0;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) {
        ReadCallback userCb = std::move(cb);
        ReadCallback wrapped =
            [this, path, userCb = std::move(userCb)](
                bool ok, const QByteArray &data, const QString &error) mutable {
                if (!ok && isTransientError(error)) {
                    // On transient failure use the normal retry path (attempt=1
                    // since attempt=0 already ran). kMaxRetries still applies.
                    QTimer::singleShot(
                        backoffForAttempt(0), this,
                        [this, path, userCb = std::move(userCb)]() mutable {
                            readFileAttempt(path, std::move(userCb), 1);
                        });
                    return;
                }
                if (userCb) userCb(ok, data, error);
            };
        m_readCallbacks.insert(reqId, std::move(wrapped));
    }
    m_connection->sftpRead(reqId, path);
    return reqId;
}

quint64 RemoteFsBackend::readFileStreamAsync(const QString &path,
                                              StreamChunkCallback chunkCb,
                                              StreamDoneCallback doneCb)
{
    if (!m_connection) {
        if (doneCb) doneCb(false, tr("No SSH connection"));
        return 0;
    }
    const quint64 reqId = ++m_nextReqId;
    if (chunkCb) m_streamChunkCallbacks.insert(reqId, std::move(chunkCb));
    if (doneCb)  m_streamDoneCallbacks.insert(reqId, std::move(doneCb));
    m_connection->sftpStreamRead(reqId, path);
    return reqId;
}

void RemoteFsBackend::readFileAttempt(const QString &path, ReadCallback cb, int attempt)
{
    if (!m_connection) {
        if (cb) cb(false, QByteArray(), tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) {
        // Wrap the user callback: on a transient failure with attempts left,
        // re-issue the same read after a backoff; otherwise surface the result.
        // QTimer's `this` context-object form auto-cancels if the backend is
        // destroyed mid-backoff, so no QPointer guard is needed.
        ReadCallback userCb = std::move(cb);
        ReadCallback wrapped =
            [this, path, attempt, userCb = std::move(userCb)](
                bool ok, const QByteArray &data, const QString &error) mutable {
                if (!ok && attempt < kMaxRetries && isTransientError(error)) {
                    const int nextAttempt = attempt + 1;
                    QTimer::singleShot(
                        backoffForAttempt(attempt), this,
                        [this, path, nextAttempt, userCb = std::move(userCb)]() mutable {
                            readFileAttempt(path, std::move(userCb), nextAttempt);
                        });
                    return;
                }
                if (userCb) userCb(ok, data, error);
            };
        m_readCallbacks.insert(reqId, std::move(wrapped));
    }
    m_connection->sftpRead(reqId, path);
}

void RemoteFsBackend::writeFileAsync(const QString &path, const QByteArray &data,
                                     WriteCallback cb)
{
    if (!m_connection) {
        if (cb) cb(false, tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) m_writeCallbacks.insert(reqId, std::move(cb));
    m_connection->sftpWrite(reqId, path, data);
}

void RemoteFsBackend::statAsync(const QString &path, StatCallback cb)
{
    statAttempt(path, std::move(cb), 0);
}

void RemoteFsBackend::statAttempt(const QString &path, StatCallback cb, int attempt)
{
    if (!m_connection) {
        if (cb) cb(false, FileStat(), tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) {
        StatCallback userCb = std::move(cb);
        StatCallback wrapped =
            [this, path, attempt, userCb = std::move(userCb)](
                bool ok, const FileStat &stat, const QString &error) mutable {
                if (!ok && attempt < kMaxRetries && isTransientError(error)) {
                    const int nextAttempt = attempt + 1;
                    QTimer::singleShot(
                        backoffForAttempt(attempt), this,
                        [this, path, nextAttempt, userCb = std::move(userCb)]() mutable {
                            statAttempt(path, std::move(userCb), nextAttempt);
                        });
                    return;
                }
                if (userCb) userCb(ok, stat, error);
            };
        m_statCallbacks.insert(reqId, std::move(wrapped));
    }
    m_connection->sftpStat(reqId, path);
}

void RemoteFsBackend::readdirAsync(const QString &path, ReaddirCallback cb)
{
    readdirAttempt(path, std::move(cb), 0);
}

void RemoteFsBackend::readdirAttempt(const QString &path, ReaddirCallback cb, int attempt)
{
    if (!m_connection) {
        if (cb) cb(false, QList<RemoteDirEntry>(), tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) {
        ReaddirCallback userCb = std::move(cb);
        ReaddirCallback wrapped =
            [this, path, attempt, userCb = std::move(userCb)](
                bool ok, const QList<RemoteDirEntry> &entries, const QString &error) mutable {
                if (!ok && attempt < kMaxRetries && isTransientError(error)) {
                    const int nextAttempt = attempt + 1;
                    QTimer::singleShot(
                        backoffForAttempt(attempt), this,
                        [this, path, nextAttempt, userCb = std::move(userCb)]() mutable {
                            readdirAttempt(path, std::move(userCb), nextAttempt);
                        });
                    return;
                }
                if (userCb) userCb(ok, entries, error);
            };
        m_readdirCallbacks.insert(reqId, std::move(wrapped));
    }
    m_connection->sftpReaddir(reqId, path);
}

void RemoteFsBackend::renameAsync(const QString &oldPath, const QString &newPath,
                                   MutateCallback cb)
{
    if (!m_connection) {
        if (cb) cb(false, tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) m_mutateCallbacks.insert(reqId, std::move(cb));
    m_connection->sftpRename(reqId, oldPath, newPath);
}

void RemoteFsBackend::mkdirAsync(const QString &path, MutateCallback cb)
{
    if (!m_connection) {
        if (cb) cb(false, tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) m_mutateCallbacks.insert(reqId, std::move(cb));
    m_connection->sftpMkdir(reqId, path);
}

void RemoteFsBackend::unlinkAsync(const QString &path, MutateCallback cb)
{
    if (!m_connection) {
        if (cb) cb(false, tr("No SSH connection"));
        return;
    }
    const quint64 reqId = ++m_nextReqId;
    if (cb) m_mutateCallbacks.insert(reqId, std::move(cb));
    m_connection->sftpUnlink(reqId, path);
}

// --- result handlers (UI thread) ---------------------------------------------

void RemoteFsBackend::onStreamChunk(quint64 reqId, const QByteArray &chunk)
{
    auto it = m_streamChunkCallbacks.find(reqId);
    if (it == m_streamChunkCallbacks.end()) {
        return; // cancelled or not ours
    }
    if (it.value()) it.value()(chunk);
}

void RemoteFsBackend::onReadResult(quint64 reqId, bool ok, const QByteArray &data,
                                   const QString &error)
{
    // Check if this is a stream-read completion (StreamRead ops emit sftpReadDone
    // on EOF with empty data, having delivered all content via sftpReadChunk).
    {
        auto doneIt = m_streamDoneCallbacks.find(reqId);
        if (doneIt != m_streamDoneCallbacks.end()) {
            const StreamDoneCallback doneCb = std::move(doneIt.value());
            m_streamDoneCallbacks.erase(doneIt);
            m_streamChunkCallbacks.remove(reqId); // clean up paired chunk map
            if (doneCb) doneCb(ok, error);
            return;
        }
    }

    auto it = m_readCallbacks.find(reqId);
    if (it == m_readCallbacks.end()) {
        return; // not ours (or already resolved)
    }
    const ReadCallback cb = std::move(it.value());
    m_readCallbacks.erase(it);
    if (cb) cb(ok, data, error);
}

void RemoteFsBackend::onWriteResult(quint64 reqId, bool ok, const QString &error)
{
    auto it = m_writeCallbacks.find(reqId);
    if (it == m_writeCallbacks.end()) {
        return;
    }
    const WriteCallback cb = std::move(it.value());
    m_writeCallbacks.erase(it);
    if (cb) cb(ok, error);
}

void RemoteFsBackend::onStatResult(quint64 reqId, bool ok, bool exists, bool isDir,
                                   qint64 size, qint64 mtimeSecs, const QString &error)
{
    auto it = m_statCallbacks.find(reqId);
    if (it == m_statCallbacks.end()) {
        return;
    }
    const StatCallback cb = std::move(it.value());
    m_statCallbacks.erase(it);
    FileStat s;
    s.exists = exists;
    s.isDir = isDir;
    s.size = size;
    if (mtimeSecs > 0) {
        s.lastModified = QDateTime::fromSecsSinceEpoch(mtimeSecs);
    }
    if (cb) cb(ok, s, error);
}

void RemoteFsBackend::onReaddirResult(quint64 reqId, bool ok,
                                      const QList<RemoteDirEntry> &entries,
                                      const QString &error)
{
    auto it = m_readdirCallbacks.find(reqId);
    if (it == m_readdirCallbacks.end()) {
        return;
    }
    const ReaddirCallback cb = std::move(it.value());
    m_readdirCallbacks.erase(it);
    if (cb) cb(ok, entries, error);
}

void RemoteFsBackend::onMutateResult(quint64 reqId, bool ok, const QString &error)
{
    auto it = m_mutateCallbacks.find(reqId);
    if (it == m_mutateCallbacks.end()) {
        return;
    }
    const MutateCallback cb = std::move(it.value());
    m_mutateCallbacks.erase(it);
    if (cb) cb(ok, error);
}

void RemoteFsBackend::onConnectionLost(const QString &reason)
{
    const QString msg = tr("SSH connection lost: %1").arg(reason);
    QHash<quint64, ReadCallback> reads = std::move(m_readCallbacks);
    m_readCallbacks.clear();
    for (auto &cb : reads) {
        if (cb) cb(false, QByteArray(), msg);
    }
    QHash<quint64, WriteCallback> writes = std::move(m_writeCallbacks);
    m_writeCallbacks.clear();
    for (auto &cb : writes) {
        if (cb) cb(false, msg);
    }
    QHash<quint64, StatCallback> stats = std::move(m_statCallbacks);
    m_statCallbacks.clear();
    for (auto &cb : stats) {
        if (cb) cb(false, FileStat(), msg);
    }
    QHash<quint64, ReaddirCallback> dirs = std::move(m_readdirCallbacks);
    m_readdirCallbacks.clear();
    for (auto &cb : dirs) {
        if (cb) cb(false, QList<RemoteDirEntry>(), msg);
    }
    QHash<quint64, MutateCallback> mutates = std::move(m_mutateCallbacks);
    m_mutateCallbacks.clear();
    for (auto &cb : mutates) {
        if (cb) cb(false, msg);
    }
    // Drain in-flight streaming reads.
    m_streamChunkCallbacks.clear(); // chunks are not replayed — just discard
    QHash<quint64, StreamDoneCallback> streamDone = std::move(m_streamDoneCallbacks);
    m_streamDoneCallbacks.clear();
    for (auto &cb : streamDone) {
        if (cb) cb(false, msg);
    }
}

// --- synchronous overrides (fail-closed for remote) --------------------------
// A remote filesystem cannot be read synchronously without blocking the UI on
// the network, which the single-connection design forbids. Remote callers use
// the *Async variants; these exist only to satisfy the interface.

QByteArray RemoteFsBackend::readFile(const QString &path, bool *ok)
{
    Q_UNUSED(path);
    if (ok) *ok = false;
    return {};
}

bool RemoteFsBackend::writeFile(const QString &path, const QByteArray &data)
{
    Q_UNUSED(path);
    Q_UNUSED(data);
    return false;
}

FileStat RemoteFsBackend::stat(const QString &path)
{
    Q_UNUSED(path);
    return {}; // exists = false
}

QStringList RemoteFsBackend::readdir(const QString &path)
{
    Q_UNUSED(path);
    return {};
}

} // namespace remote
