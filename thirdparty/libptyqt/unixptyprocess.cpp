/*
 * Adapted from kafeg/ptyqt (MIT). Qt6 rewrite using forkpty (POSIX).
 * See LICENSE.
 */

#include "unixptyprocess.h"

#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#include <util.h>
#elif defined(__linux__)
#include <pty.h>
#endif

UnixPtyProcess::UnixPtyProcess()
    : IPtyProcess()
    , m_masterFd(-1)
    , m_childPid(-1)
    , m_readNotifier(nullptr)
{
}

UnixPtyProcess::~UnixPtyProcess()
{
    kill();
}

bool UnixPtyProcess::startProcess(const QString &shellPath, const QString &workingDir, QStringList environment, qint16 cols, qint16 rows)
{
    if (m_childPid > 0) {
        return false;
    }

    QFileInfo fi(shellPath);
    if (fi.isRelative() || !QFile::exists(shellPath)) {
        m_lastError = QStringLiteral("UnixPty Error: shell file path must be absolute and existing");
        return false;
    }

    m_shellPath = shellPath;
    m_size = QPair<qint16, qint16>(cols, rows);

    struct winsize winp;
    winp.ws_col = cols;
    winp.ws_row = rows;
    winp.ws_xpixel = 0;
    winp.ws_ypixel = 0;

    int master = -1;
    pid_t pid = forkpty(&master, nullptr, nullptr, &winp);
    if (pid < 0) {
        m_lastError = QStringLiteral("UnixPty Error: forkpty failed (%1)").arg(strerror(errno));
        return false;
    }

    if (pid == 0) {
        if (!workingDir.isEmpty()) {
            (void)chdir(workingDir.toLocal8Bit().constData());
        }

        setenv("TERM", "xterm-256color", 1);
        setenv("COLORTERM", "truecolor", 1);

        for (const QString &kv : environment) {
            const int eq = kv.indexOf(QLatin1Char('='));
            if (eq > 0) {
                const QByteArray k = kv.left(eq).toLocal8Bit();
                const QByteArray v = kv.mid(eq + 1).toLocal8Bit();
                setenv(k.constData(), v.constData(), 1);
            }
        }

        const QByteArray shellPath = m_shellPath.toLocal8Bit();
        const QString base = QFileInfo(m_shellPath).fileName();
        const bool wantsInteractive =
            base == QLatin1String("bash") ||
            base == QLatin1String("zsh") ||
            base == QLatin1String("fish") ||
            base == QLatin1String("ksh");
        if (wantsInteractive) {
            execl(shellPath.constData(), shellPath.constData(), "-i", reinterpret_cast<char *>(nullptr));
        } else {
            execl(shellPath.constData(), shellPath.constData(), reinterpret_cast<char *>(nullptr));
        }
        _exit(127);
    }

    m_masterFd = master;
    m_childPid = pid;
    m_pid = pid;

    int flags = fcntl(m_masterFd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);
    }

    m_readNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &UnixPtyProcess::onMasterReadable);
    m_readNotifier->setEnabled(true);

    return true;
}

void UnixPtyProcess::onMasterReadable()
{
    char buffer[4096];
    for (;;) {
        ssize_t n = ::read(m_masterFd, buffer, sizeof(buffer));
        if (n > 0) {
            m_readBuffer.append(buffer, static_cast<int>(n));
        } else if (n < 0) {
            if (errno == EINTR) continue;
            break;
        } else {
            break;
        }
    }
    if (!m_readBuffer.isEmpty()) {
        m_notifier.emitReadyRead();
    }
}

bool UnixPtyProcess::resize(qint16 cols, qint16 rows)
{
    if (m_masterFd < 0) return false;
    struct winsize winp;
    winp.ws_col = cols;
    winp.ws_row = rows;
    winp.ws_xpixel = 0;
    winp.ws_ypixel = 0;
    bool ok = (ioctl(m_masterFd, TIOCSWINSZ, &winp) != -1);
    if (ok) m_size = QPair<qint16, qint16>(cols, rows);
    return ok;
}

bool UnixPtyProcess::kill()
{
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        delete m_readNotifier;
        m_readNotifier = nullptr;
    }

    if (m_childPid > 0) {
        ::kill(m_childPid, SIGHUP);
        int status = 0;
        for (int i = 0; i < 25; ++i) {
            pid_t r = waitpid(m_childPid, &status, WNOHANG);
            if (r == m_childPid || r < 0) break;
            QThread::msleep(10);
        }
        pid_t r2 = waitpid(m_childPid, &status, WNOHANG);
        if (r2 == 0) {
            ::kill(m_childPid, SIGKILL);
            waitpid(m_childPid, &status, 0);
        }
        m_childPid = -1;
        m_pid = 0;
    }

    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
    return true;
}

IPtyProcess::PtyType UnixPtyProcess::type() const
{
    return PtyType::UnixPty;
}

QString UnixPtyProcess::dumpDebugInfo()
{
    return QStringLiteral("UnixPty PID: %1, master: %2, Cols: %3, Rows: %4")
        .arg(m_pid).arg(m_masterFd).arg(m_size.first).arg(m_size.second);
}

QIODevice *UnixPtyProcess::notifier()
{
    return &m_notifier;
}

QByteArray UnixPtyProcess::readAll()
{
    QByteArray out = m_readBuffer;
    m_readBuffer.clear();
    return out;
}

qint64 UnixPtyProcess::write(const QByteArray &byteArray)
{
    if (m_masterFd < 0) return -1;
    ssize_t n = ::write(m_masterFd, byteArray.constData(), byteArray.size());
    return n;
}

bool UnixPtyProcess::isAvailable()
{
    return true;
}

void UnixPtyProcess::moveToThread(QThread *targetThread)
{
    Q_UNUSED(targetThread);
}
