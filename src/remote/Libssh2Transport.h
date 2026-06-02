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

#ifndef REMOTE_LIBSSH2TRANSPORT_H
#define REMOTE_LIBSSH2TRANSPORT_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QString>

#include "ISshTransport.h"

// Forward-declare the libssh2 opaque types so this header does not require the
// vendored libssh2 sources to be present just to parse it. The .cpp includes
// <libssh2.h> for the real definitions.
struct _LIBSSH2_SESSION;
struct _LIBSSH2_CHANNEL;
struct _LIBSSH2_AGENT;
struct _LIBSSH2_SFTP;
struct _LIBSSH2_SFTP_HANDLE;

namespace remote {

// The thin production ISshTransport over libssh2 (1-2 libssh2 calls per method,
// no algorithm — all the state-machine logic lives in SshSessionWorker). The
// session is operated NON-BLOCKING; every step returns Again on
// LIBSSH2_ERROR_EAGAIN and is retried on the next socket-activity edge.
//
// Lives entirely on the worker thread (the worker is the only caller). Uses a
// raw TCP socket fd that the worker drives via QSocketNotifier.
class Libssh2Transport : public ISshTransport
{
public:
    Libssh2Transport();
    ~Libssh2Transport() override;

    Step connectSocket(const QString &host, int port) override;
    Step handshake() override;
    QByteArray hostKey() const override;
    Step authPassword(const QString &username, const QString &password) override;
    Step authPublicKey(const QString &username, const QString &keyPath,
                       const QString &passphrase) override;
    Step authAgent(const QString &username) override;

    OpenResult openChannel() override;
    Step requestPty(int channelId, const QByteArray &term, int cols, int rows) override;
    Step resizePty(int channelId, int cols, int rows) override;
    Step execOrShell(int channelId, const QString &command) override;
    qint64 chWrite(int channelId, const QByteArray &bytes) override;
    Step chSendEof(int channelId) override;
    ReadResult chRead(int channelId) override;
    ReadResult chReadStderr(int channelId) override;
    int chExitStatus(int channelId) override;
    void closeChannel(int channelId) override;

    SftpOpenResult sftpInit(SftpLane lane) override;
    void sftpShutdown() override;
    SftpOpenResult sftpOpen(SftpLane lane, const QString &path, bool forWrite) override;
    ReadResult sftpRead(SftpLane lane, int handleId) override;
    qint64 sftpWrite(SftpLane lane, int handleId, const QByteArray &bytes) override;
    void sftpClose(SftpLane lane, int handleId) override;
    SftpOpenResult sftpOpendir(SftpLane lane, const QString &path) override;
    SftpDirEntry sftpReaddir(SftpLane lane, int handleId) override;
    void sftpClosedir(SftpLane lane, int handleId) override;
    SftpStatResult sftpStat(SftpLane lane, const QString &path) override;
    Step sftpRename(SftpLane lane, const QString &srcPath, const QString &dstPath) override;
    Step sftpMkdir(SftpLane lane, const QString &path) override;
    Step sftpUnlink(SftpLane lane, const QString &path) override;

    int sendKeepalive() override;

    qintptr socketFd() const override { return m_sock; }
    void disconnect() override;

private:
    _LIBSSH2_CHANNEL *channel(int channelId) const { return m_channels.value(channelId, nullptr); }
    _LIBSSH2_SFTP_HANDLE *sftpHandle(int handleId) const { return m_sftpHandles.value(handleId, nullptr); }
    // D1a: select the LIBSSH2_SFTP session for a lane. Returns the address of the
    // member pointer so callers can both read it and lazily assign it in sftpInit.
    _LIBSSH2_SFTP *&sftpSession(SftpLane lane) { return lane == SftpLane::Bulk ? m_sftpBulk : m_sftpMeta; }

    _LIBSSH2_SESSION *m_session = nullptr;
    _LIBSSH2_AGENT *m_agent = nullptr;
    qintptr m_sock = -1;
    QByteArray m_hostKey;
    QHash<int, _LIBSSH2_CHANNEL *> m_channels;
    int m_nextChannelId = 1;
    void *m_agentPrevId = nullptr; // last-tried agent identity (auth walk)
    bool m_libssh2Inited = false;

    // SFTP state — D1a: TWO independent reused sessions (Bulk + Meta), each a
    // distinct LIBSSH2_SFTP channel on the multiplexed connection, so a large
    // bulk transfer never blocks a metadata op. File/dir handles are keyed by the
    // int ids this transport hands out from a single monotonic counter — the ids
    // never collide across lanes, and each LIBSSH2_SFTP_HANDLE* carries its own
    // session linkage, so sftpRead/sftpWrite/sftpClose resolve the right session
    // implicitly via the handle pointer regardless of the lane arg.
    _LIBSSH2_SFTP *m_sftpBulk = nullptr;
    _LIBSSH2_SFTP *m_sftpMeta = nullptr;
    QHash<int, _LIBSSH2_SFTP_HANDLE *> m_sftpHandles;
    int m_nextSftpHandleId = 1;

    // Non-blocking connect state (FIX-4). connectSocket is re-entrant: the first
    // call resolves + creates a non-blocking socket + issues ::connect (EINPROGRESS);
    // subsequent calls check writability until connected or the deadline expires.
    bool m_connecting = false;       // true while ::connect is in progress
    bool m_socketConnected = false;  // true once the TCP handshake completed
    qint64 m_connectDeadlineMs = 0;  // monotonic deadline (ms since epoch of m_elapsed)
    QElapsedTimer m_elapsed;         // started in ctor; used for connect deadline
};

} // namespace remote

#endif // REMOTE_LIBSSH2TRANSPORT_H
