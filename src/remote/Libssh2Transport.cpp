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

// Thin production ISshTransport over libssh2 (non-blocking). All the
// state-machine logic lives in SshSessionWorker; here each method is 1-2
// libssh2 calls that translate LIBSSH2_ERROR_EAGAIN → Step::Again and a fatal
// errno → Step::Error. Runs only on the worker thread.

#include "Libssh2Transport.h"

#include <QtGlobal>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <cstring>
#include <cerrno>

#ifdef Q_OS_WIN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

namespace remote {

namespace {

inline ISshTransport::Step stepFromRc(int rc)
{
    if (rc == 0) return ISshTransport::Step::Ok;
    if (rc == LIBSSH2_ERROR_EAGAIN) return ISshTransport::Step::Again;
    return ISshTransport::Step::Error;
}

#ifdef Q_OS_WIN
void closeSock(qintptr s) { ::closesocket(static_cast<SOCKET>(s)); }
inline SOCKET sockHandle(qintptr s) { return static_cast<SOCKET>(s); }
void setNonBlocking(qintptr s)
{
    u_long mode = 1;
    ioctlsocket(sockHandle(s), FIONBIO, &mode);
}
bool connectInProgress()
{
    return (WSAGetLastError() == WSAEWOULDBLOCK);
}
#else
void closeSock(qintptr s) { ::close(static_cast<int>(s)); }
inline int sockHandle(qintptr s) { return static_cast<int>(s); }
void setNonBlocking(qintptr s)
{
    int flags = fcntl(static_cast<int>(s), F_GETFL, 0);
    if (flags >= 0) fcntl(static_cast<int>(s), F_SETFL, flags | O_NONBLOCK);
}
bool connectInProgress()
{
    return (errno == EINPROGRESS);
}
#endif

// Check if a non-blocking connect has completed. Returns true if the socket is
// writable with no pending error (connected), false otherwise.
bool isSocketConnected(qintptr s)
{
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sockHandle(s), &wfds);
    struct timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    const int sel = ::select(static_cast<int>(s) + 1, nullptr, &wfds, nullptr, &tv);
    if (sel <= 0) return false;
    // Check SO_ERROR to confirm the connect succeeded (not just writable).
    int err = 0;
#ifdef Q_OS_WIN
    int errLen = sizeof(err);
#else
    socklen_t errLen = sizeof(err);
#endif
    getsockopt(sockHandle(s), SOL_SOCKET, SO_ERROR,
               reinterpret_cast<char *>(&err), &errLen);
    return (err == 0);
}

// Decode the libssh2 SFTP attribute flags into our flat SftpAttrs. Only the
// fields the file tree + editor consume (size, mtime, dir bit) are extracted.
inline ISshTransport::SftpAttrs attrsFromLibssh2(const LIBSSH2_SFTP_ATTRIBUTES &a)
{
    ISshTransport::SftpAttrs out;
    if (a.flags & LIBSSH2_SFTP_ATTR_SIZE) {
        out.hasSize = true;
        out.size = static_cast<quint64>(a.filesize);
    }
    if (a.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
        out.hasMtime = true;
        out.mtime = static_cast<quint64>(a.mtime);
    }
    if (a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
        out.permissions = static_cast<quint32>(a.permissions);
        out.isDir = LIBSSH2_SFTP_S_ISDIR(a.permissions);
    }
    return out;
}

} // namespace

Libssh2Transport::Libssh2Transport()
{
    m_elapsed.start();
    // libssh2_init is refcounted; safe to call per transport. CRYPTO backend is
    // the vendored mbedTLS (selected at build time).
    if (libssh2_init(0) == 0) {
        m_libssh2Inited = true;
    }
}

Libssh2Transport::~Libssh2Transport()
{
    disconnect();
    if (m_libssh2Inited) {
        libssh2_exit();
    }
}

Libssh2Transport::Step Libssh2Transport::connectSocket(const QString &host, int port)
{
    // FIX-4: re-entrant, non-blocking connect. The first call resolves the host
    // and issues a non-blocking ::connect (expecting EINPROGRESS / WSAEWOULDBLOCK).
    // Subsequent calls poll for writability with a zero timeout and return Again
    // until the TCP handshake completes or a 15 s deadline expires. Returning
    // Again (never blocking) lets the worker preempt with a queued Cancel.
    if (m_session) {
        return Step::Ok; // session already created → socket fully connected
    }

#ifdef Q_OS_WIN
    {
        // WSAStartup is idempotent across calls; we leave it started for the
        // app's lifetime (no matching WSACleanup for a long-lived process).
        static bool wsaStarted = false;
        if (!wsaStarted) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
                return Step::Error;
            }
            wsaStarted = true;
        }
    }
#endif

    // --- second-and-later calls: the connect is in flight, poll writability ---
    if (m_connecting && m_sock >= 0) {
        if (isSocketConnected(m_sock)) {
            m_connecting = false;
            m_socketConnected = true;
            // fall through to create the session below
        } else {
            if (m_elapsed.elapsed() >= m_connectDeadlineMs) {
                closeSock(m_sock);
                m_sock = -1;
                m_connecting = false;
                return Step::Error; // 15 s timeout → "Could not reach host"
            }
            return Step::Again; // still connecting; worker re-polls on the next edge/tick
        }
    }

    // --- first call: resolve + kick off the non-blocking connect --------------
    if (!m_socketConnected) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        const QByteArray hostUtf8 = host.toUtf8();
        const QByteArray portStr = QByteArray::number(port);
        addrinfo *res = nullptr;
        // NOTE: getaddrinfo is the OS default (potentially blocking) resolver. The
        // overall connect path still returns Again immediately after this call so a
        // queued Cancel preempts the *connect* wait; a fully non-blocking resolver
        // (getaddrinfo_a / a resolver thread) is a future refinement (D0 FIX-4).
        if (getaddrinfo(hostUtf8.constData(), portStr.constData(), &hints, &res) != 0 || !res) {
            return Step::Error;
        }

        qintptr sock = -1;
        for (addrinfo *ai = res; ai; ai = ai->ai_next) {
            const qintptr s = static_cast<qintptr>(
                ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
            if (s < 0) {
                continue;
            }
            // Set the socket non-blocking BEFORE connect so ::connect returns
            // immediately with EINPROGRESS / WSAEWOULDBLOCK rather than blocking.
            setNonBlocking(s);
            const int rc = ::connect(sockHandle(s), ai->ai_addr,
                                     static_cast<int>(ai->ai_addrlen));
            if (rc == 0) {
                // Rare: connected synchronously (e.g. localhost).
                sock = s;
                m_socketConnected = true;
                break;
            }
            if (connectInProgress()) {
                sock = s;
                m_connecting = true;
                break;
            }
            closeSock(s);
        }
        freeaddrinfo(res);

        if (sock < 0) {
            return Step::Error;
        }
        m_sock = sock;
        m_connectDeadlineMs = m_elapsed.elapsed() + 15000; // 15 s connect timeout
        if (m_connecting) {
            return Step::Again; // wait for writable; worker re-polls
        }
        // else: synchronously connected → fall through to session creation
    }

    // --- TCP connected: create the libssh2 session ----------------------------
    m_session = libssh2_session_init();
    if (!m_session) {
        closeSock(m_sock);
        m_sock = -1;
        m_socketConnected = false;
        return Step::Error;
    }
    // Non-blocking from here on — the worker drives via QSocketNotifier.
    libssh2_session_set_blocking(m_session, 0);
    // FIX-3: configure server-side keepalive so an idle TCP drop is detected in
    // seconds, not the OS TCP-keepalive default (~2 h). want_reply=1 asks the
    // server to answer each probe; the worker drives sendKeepalive() on a timer.
    libssh2_keepalive_config(m_session, 1, 15);
    return Step::Ok;
}

Libssh2Transport::Step Libssh2Transport::handshake()
{
    if (!m_session || m_sock < 0) {
        return Step::Error;
    }
    const int rc = libssh2_session_handshake(m_session, sockHandle(m_sock));
    const Step s = stepFromRc(rc);
    if (s == Step::Ok && m_hostKey.isEmpty()) {
        size_t len = 0;
        int type = 0;
        const char *key = libssh2_session_hostkey(m_session, &len, &type);
        if (key && len > 0) {
            m_hostKey = QByteArray(key, static_cast<int>(len));
        }
    }
    return s;
}

QByteArray Libssh2Transport::hostKey() const
{
    return m_hostKey;
}

Libssh2Transport::Step Libssh2Transport::authPassword(const QString &username,
                                                      const QString &password)
{
    if (!m_session) return Step::Error;
    const QByteArray u = username.toUtf8();
    const QByteArray p = password.toUtf8();
    const int rc = libssh2_userauth_password_ex(
        m_session, u.constData(), static_cast<unsigned int>(u.size()),
        p.constData(), static_cast<unsigned int>(p.size()), nullptr);
    return stepFromRc(rc);
}

Libssh2Transport::Step Libssh2Transport::authPublicKey(const QString &username,
                                                       const QString &keyPath,
                                                       const QString &passphrase)
{
    if (!m_session) return Step::Error;
    const QByteArray u = username.toUtf8();
    const QByteArray priv = keyPath.toUtf8();
    const QByteArray pass = passphrase.toUtf8();
    // Public-key path left null → libssh2 derives "<priv>.pub".
    const int rc = libssh2_userauth_publickey_fromfile_ex(
        m_session, u.constData(), static_cast<unsigned int>(u.size()),
        nullptr, priv.constData(), pass.isEmpty() ? nullptr : pass.constData());
    return stepFromRc(rc);
}

Libssh2Transport::Step Libssh2Transport::authAgent(const QString &username)
{
    if (!m_session) return Step::Error;
    const QByteArray u = username.toUtf8();

    if (!m_agent) {
        m_agent = libssh2_agent_init(m_session);
        if (!m_agent) {
            return Step::Error;
        }
        if (libssh2_agent_connect(m_agent) != 0) {
            return Step::Error;
        }
        if (libssh2_agent_list_identities(m_agent) != 0) {
            return Step::Error;
        }
        m_agentPrevId = nullptr;
    }

    // Walk identities, trying each until one authenticates. Non-blocking auth
    // can return EAGAIN; surface that so the worker retries on the next edge.
    for (;;) {
        struct libssh2_agent_publickey *identity = nullptr;
        const int rc = libssh2_agent_get_identity(
            m_agent, &identity, static_cast<struct libssh2_agent_publickey *>(m_agentPrevId));
        if (rc == 1) {
            return Step::Error; // exhausted all identities without success
        }
        if (rc < 0) {
            return Step::Error;
        }
        const int authRc = libssh2_agent_userauth(m_agent, u.constData(), identity);
        if (authRc == 0) {
            return Step::Ok;
        }
        if (authRc == LIBSSH2_ERROR_EAGAIN) {
            return Step::Again; // retry this same identity next edge
        }
        m_agentPrevId = identity; // this key failed; advance to the next
    }
}

Libssh2Transport::OpenResult Libssh2Transport::openChannel()
{
    OpenResult out;
    if (!m_session) {
        out.step = Step::Error;
        return out;
    }
    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(m_session);
    if (ch) {
        const int id = m_nextChannelId++;
        m_channels.insert(id, ch);
        out.step = Step::Ok;
        out.channelId = id;
        return out;
    }
    const int err = libssh2_session_last_errno(m_session);
    if (err == LIBSSH2_ERROR_EAGAIN) {
        out.step = Step::Again;
    } else if (err == LIBSSH2_ERROR_CHANNEL_FAILURE) {
        // FIX-1: server denied the channel (MaxSessions hit) — transient
        // back-pressure, NOT a dead connection. The worker re-queues with backoff.
        out.step = Step::ChannelBusy;
    } else {
        out.step = Step::Error;
    }
    return out;
}

Libssh2Transport::Step Libssh2Transport::requestPty(int channelId, const QByteArray &term,
                                                    int cols, int rows)
{
    LIBSSH2_CHANNEL *ch = channel(channelId);
    if (!ch) return Step::Error;
    const QByteArray t = term.isEmpty() ? QByteArrayLiteral("xterm-256color") : term;
    const int rc = libssh2_channel_request_pty_ex(
        ch, t.constData(), static_cast<unsigned int>(t.size()),
        nullptr, 0, cols, rows, 0, 0);
    return stepFromRc(rc);
}

Libssh2Transport::Step Libssh2Transport::resizePty(int channelId, int cols, int rows)
{
    LIBSSH2_CHANNEL *ch = channel(channelId);
    if (!ch) return Step::Error;
    const int rc = libssh2_channel_request_pty_size_ex(ch, cols, rows, 0, 0);
    return stepFromRc(rc);
}

Libssh2Transport::Step Libssh2Transport::execOrShell(int channelId, const QString &command)
{
    LIBSSH2_CHANNEL *ch = channel(channelId);
    if (!ch) return Step::Error;
    int rc;
    if (command.isEmpty()) {
        rc = libssh2_channel_shell(ch);
    } else {
        const QByteArray c = command.toUtf8();
        rc = libssh2_channel_exec(ch, c.constData());
    }
    return stepFromRc(rc);
}

qint64 Libssh2Transport::chWrite(int channelId, const QByteArray &bytes)
{
    LIBSSH2_CHANNEL *ch = channel(channelId);
    if (!ch) return kWriteError;
    const ssize_t n = libssh2_channel_write(ch, bytes.constData(),
                                            static_cast<size_t>(bytes.size()));
    if (n == LIBSSH2_ERROR_EAGAIN) return kWriteAgain;
    if (n < 0) return kWriteError;
    return static_cast<qint64>(n);
}

ISshTransport::Step Libssh2Transport::chSendEof(int channelId)
{
    LIBSSH2_CHANNEL *ch = channel(channelId);
    if (!ch) return Step::Error;
    const int rc = libssh2_channel_send_eof(ch);
    if (rc == LIBSSH2_ERROR_EAGAIN) return Step::Again;
    if (rc < 0) return Step::Error;
    return Step::Ok;
}

Libssh2Transport::ReadResult Libssh2Transport::chRead(int channelId)
{
    ReadResult out;
    LIBSSH2_CHANNEL *ch = channel(channelId);
    if (!ch) {
        out.error = true;
        return out;
    }
    char buf[32 * 1024];
    const ssize_t n = libssh2_channel_read(ch, buf, sizeof(buf));
    if (n == LIBSSH2_ERROR_EAGAIN) {
        out.again = true;
        return out;
    }
    if (n < 0) {
        out.error = true;
        return out;
    }
    if (n > 0) {
        out.data = QByteArray(buf, static_cast<int>(n));
    }
    // libssh2_channel_eof is cheap; report EOF so the worker can finish + read
    // the exit status.
    if (libssh2_channel_eof(ch)) {
        out.eof = true;
    }
    return out;
}

Libssh2Transport::ReadResult Libssh2Transport::chReadStderr(int channelId)
{
    ReadResult out;
    LIBSSH2_CHANNEL *ch = channel(channelId);
    if (!ch) {
        out.error = true;
        return out;
    }
    char buf[32 * 1024];
    const ssize_t n =
        libssh2_channel_read_ex(ch, SSH_EXTENDED_DATA_STDERR, buf, sizeof(buf));
    if (n == LIBSSH2_ERROR_EAGAIN) {
        out.again = true;
        return out;
    }
    if (n < 0) {
        out.error = true;
        return out;
    }
    if (n > 0) {
        out.data = QByteArray(buf, static_cast<int>(n));
    }
    // EOF on stderr tracks the same channel-level EOF as stdout.
    if (libssh2_channel_eof(ch)) {
        out.eof = true;
    }
    return out;
}

int Libssh2Transport::chExitStatus(int channelId)
{
    LIBSSH2_CHANNEL *ch = channel(channelId);
    if (!ch) return -1;
    return libssh2_channel_get_exit_status(ch);
}

void Libssh2Transport::closeChannel(int channelId)
{
    LIBSSH2_CHANNEL *ch = channel(channelId);
    if (!ch) return;
    libssh2_channel_close(ch);
    libssh2_channel_free(ch);
    m_channels.remove(channelId);
}

Libssh2Transport::SftpOpenResult Libssh2Transport::sftpInit(SftpLane lane)
{
    SftpOpenResult out;
    if (!m_session) {
        out.step = Step::Error;
        return out;
    }
    _LIBSSH2_SFTP *&sftp = sftpSession(lane);
    if (sftp) {
        out.step = Step::Ok; // already established for this lane; reuse
        return out;
    }
    LIBSSH2_SFTP *s = libssh2_sftp_init(m_session);
    if (s) {
        sftp = s;
        out.step = Step::Ok;
        return out;
    }
    const int err = libssh2_session_last_errno(m_session);
    out.step = (err == LIBSSH2_ERROR_EAGAIN) ? Step::Again : Step::Error;
    return out;
}

void Libssh2Transport::sftpShutdown()
{
    // Free any still-open file/dir handles before either session goes away.
    for (auto it = m_sftpHandles.begin(); it != m_sftpHandles.end(); ++it) {
        if (it.value()) {
            libssh2_sftp_close_handle(it.value());
        }
    }
    m_sftpHandles.clear();
    // D1a: tear BOTH lane sessions down.
    if (m_sftpBulk) {
        libssh2_sftp_shutdown(m_sftpBulk);
        m_sftpBulk = nullptr;
    }
    if (m_sftpMeta) {
        libssh2_sftp_shutdown(m_sftpMeta);
        m_sftpMeta = nullptr;
    }
}

Libssh2Transport::SftpOpenResult Libssh2Transport::sftpOpen(SftpLane lane,
                                                            const QString &path, bool forWrite)
{
    SftpOpenResult out;
    _LIBSSH2_SFTP *sftp = sftpSession(lane);
    if (!sftp) {
        out.step = Step::Error;
        return out;
    }
    const QByteArray p = path.toUtf8();
    const unsigned long flags = forWrite
        ? (LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC)
        : LIBSSH2_FXF_READ;
    const long mode = forWrite
        ? (LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR
           | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH) // 0644
        : 0;
    LIBSSH2_SFTP_HANDLE *h = libssh2_sftp_open_ex(
        sftp, p.constData(), static_cast<unsigned int>(p.size()),
        flags, mode, LIBSSH2_SFTP_OPENFILE);
    if (h) {
        const int id = m_nextSftpHandleId++;
        m_sftpHandles.insert(id, h);
        out.step = Step::Ok;
        out.handleId = id;
        return out;
    }
    const int err = libssh2_session_last_errno(m_session);
    out.step = (err == LIBSSH2_ERROR_EAGAIN) ? Step::Again : Step::Error;
    return out;
}

Libssh2Transport::ReadResult Libssh2Transport::sftpRead(SftpLane lane, int handleId)
{
    Q_UNUSED(lane); // the handle carries its own session linkage
    ReadResult out;
    LIBSSH2_SFTP_HANDLE *h = sftpHandle(handleId);
    if (!h) {
        out.error = true;
        return out;
    }
    char buf[32 * 1024]; // 32 KiB chunk (D1 tunable)
    const ssize_t n = libssh2_sftp_read(h, buf, sizeof(buf));
    if (n == LIBSSH2_ERROR_EAGAIN) {
        out.again = true;
        return out;
    }
    if (n < 0) {
        out.error = true;
        return out;
    }
    if (n == 0) {
        out.eof = true; // SFTP read returns 0 at end-of-file
        return out;
    }
    out.data = QByteArray(buf, static_cast<int>(n));
    return out;
}

qint64 Libssh2Transport::sftpWrite(SftpLane lane, int handleId, const QByteArray &bytes)
{
    Q_UNUSED(lane); // the handle carries its own session linkage
    LIBSSH2_SFTP_HANDLE *h = sftpHandle(handleId);
    if (!h) return kWriteError;
    const ssize_t n = libssh2_sftp_write(h, bytes.constData(),
                                         static_cast<size_t>(bytes.size()));
    if (n == LIBSSH2_ERROR_EAGAIN) return kWriteAgain;
    if (n < 0) return kWriteError;
    return static_cast<qint64>(n);
}

void Libssh2Transport::sftpClose(SftpLane lane, int handleId)
{
    Q_UNUSED(lane); // the handle carries its own session linkage
    LIBSSH2_SFTP_HANDLE *h = sftpHandle(handleId);
    if (!h) return;
    libssh2_sftp_close_handle(h);
    m_sftpHandles.remove(handleId);
}

Libssh2Transport::SftpOpenResult Libssh2Transport::sftpOpendir(SftpLane lane, const QString &path)
{
    SftpOpenResult out;
    _LIBSSH2_SFTP *sftp = sftpSession(lane);
    if (!sftp) {
        out.step = Step::Error;
        return out;
    }
    const QByteArray p = path.toUtf8();
    LIBSSH2_SFTP_HANDLE *h = libssh2_sftp_open_ex(
        sftp, p.constData(), static_cast<unsigned int>(p.size()),
        0, 0, LIBSSH2_SFTP_OPENDIR);
    if (h) {
        const int id = m_nextSftpHandleId++;
        m_sftpHandles.insert(id, h);
        out.step = Step::Ok;
        out.handleId = id;
        return out;
    }
    const int err = libssh2_session_last_errno(m_session);
    out.step = (err == LIBSSH2_ERROR_EAGAIN) ? Step::Again : Step::Error;
    return out;
}

Libssh2Transport::SftpDirEntry Libssh2Transport::sftpReaddir(SftpLane lane, int handleId)
{
    Q_UNUSED(lane); // the handle carries its own session linkage
    SftpDirEntry out;
    LIBSSH2_SFTP_HANDLE *h = sftpHandle(handleId);
    if (!h) {
        out.kind = SftpDirEntry::Kind::Error;
        return out;
    }
    char name[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    const int rc = libssh2_sftp_readdir_ex(h, name, sizeof(name),
                                           nullptr, 0, &attrs);
    if (rc == LIBSSH2_ERROR_EAGAIN) {
        out.kind = SftpDirEntry::Kind::Again;
        return out;
    }
    if (rc < 0) {
        out.kind = SftpDirEntry::Kind::Error;
        return out;
    }
    if (rc == 0) {
        out.kind = SftpDirEntry::Kind::Done; // no more entries
        return out;
    }
    out.kind = SftpDirEntry::Kind::Entry;
    out.name = QByteArray(name, rc); // rc = filename length
    out.attrs = attrsFromLibssh2(attrs);
    return out;
}

void Libssh2Transport::sftpClosedir(SftpLane lane, int handleId)
{
    sftpClose(lane, handleId); // handle close is identical for files and dirs
}

Libssh2Transport::SftpStatResult Libssh2Transport::sftpStat(SftpLane lane, const QString &path)
{
    SftpStatResult out;
    _LIBSSH2_SFTP *sftp = sftpSession(lane);
    if (!sftp) {
        out.step = Step::Error;
        return out;
    }
    const QByteArray p = path.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    const int rc = libssh2_sftp_stat_ex(
        sftp, p.constData(), static_cast<unsigned int>(p.size()),
        LIBSSH2_SFTP_STAT, &attrs);
    out.step = stepFromRc(rc);
    if (out.step == Step::Ok) {
        out.attrs = attrsFromLibssh2(attrs);
    }
    return out;
}

ISshTransport::Step Libssh2Transport::sftpRename(SftpLane lane,
                                                  const QString &srcPath,
                                                  const QString &dstPath)
{
    _LIBSSH2_SFTP *sftp = sftpSession(lane);
    if (!sftp) return Step::Error;
    const QByteArray src = srcPath.toUtf8();
    const QByteArray dst = dstPath.toUtf8();
    const int rc = libssh2_sftp_rename_ex(
        sftp,
        src.constData(), static_cast<unsigned int>(src.size()),
        dst.constData(), static_cast<unsigned int>(dst.size()),
        LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC | LIBSSH2_SFTP_RENAME_NATIVE);
    return stepFromRc(rc);
}

ISshTransport::Step Libssh2Transport::sftpMkdir(SftpLane lane, const QString &path)
{
    _LIBSSH2_SFTP *sftp = sftpSession(lane);
    if (!sftp) return Step::Error;
    const QByteArray p = path.toUtf8();
    const int rc = libssh2_sftp_mkdir_ex(
        sftp, p.constData(), static_cast<unsigned int>(p.size()),
        LIBSSH2_SFTP_S_IRWXU | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IXGRP
            | LIBSSH2_SFTP_S_IROTH | LIBSSH2_SFTP_S_IXOTH); // 0755
    return stepFromRc(rc);
}

ISshTransport::Step Libssh2Transport::sftpUnlink(SftpLane lane, const QString &path)
{
    _LIBSSH2_SFTP *sftp = sftpSession(lane);
    if (!sftp) return Step::Error;
    const QByteArray p = path.toUtf8();
    const int rc = libssh2_sftp_unlink_ex(
        sftp, p.constData(), static_cast<unsigned int>(p.size()));
    return stepFromRc(rc);
}

int Libssh2Transport::sendKeepalive()
{
    if (!m_session) return -1;
    int secondsToNext = 0;
    const int rc = libssh2_keepalive_send(m_session, &secondsToNext);
    if (rc == LIBSSH2_ERROR_EAGAIN) {
        return 0; // non-fatal; retry on the next edge
    }
    if (rc < 0) {
        return -1; // fatal socket error
    }
    return secondsToNext;
}

void Libssh2Transport::disconnect()
{
    // Tear the SFTP layer down first (frees its handles + the implicit channel)
    // so the channel sweep below does not double-free anything SFTP owns.
    sftpShutdown();

    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (it.value()) {
            libssh2_channel_free(it.value());
        }
    }
    m_channels.clear();

    if (m_agent) {
        libssh2_agent_disconnect(m_agent);
        libssh2_agent_free(m_agent);
        m_agent = nullptr;
    }
    if (m_session) {
        libssh2_session_disconnect(m_session, "NotepadAI closing");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    if (m_sock >= 0) {
        closeSock(m_sock);
        m_sock = -1;
    }
    m_connecting = false;
    m_socketConnected = false;
}

} // namespace remote
