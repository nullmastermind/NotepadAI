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

#include "GitProcessRunner.h"

#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

GitProcessRunner::GitProcessRunner(QObject *parent) : QObject(parent)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &GitProcessRunner::onTimeout);
}

GitProcessRunner::~GitProcessRunner()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
}

bool GitProcessRunner::gitAvailable()
{
    return !gitExecutable().isEmpty();
}

QString GitProcessRunner::gitExecutable()
{
    static QString cached;
    static bool resolved = false;
    if (!resolved) {
        cached = QStandardPaths::findExecutable(QStringLiteral("git"));
        resolved = true;
    }
    return cached;
}

QProcessEnvironment GitProcessRunner::baseEnv()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C"));
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_ADVICE"), QStringLiteral("0"));
    // Defensive: make sure no leftover askpass leaks in until Phase 2 wires one.
    env.remove(QStringLiteral("GIT_ASKPASS"));
    env.remove(QStringLiteral("SSH_ASKPASS"));
    return env;
}

bool GitProcessRunner::isRunning() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

void GitProcessRunner::cancel()
{
    if (!isRunning()) return;
    m_cancelled = true;
    m_proc->terminate();
    if (!m_proc->waitForFinished(2000)) {
        m_proc->kill();
        m_proc->waitForFinished(500);
    }
}

void GitProcessRunner::cancelAsync()
{
    // Detach the in-flight process from this runner instance and SIGKILL it
    // without waiting. Used by hot-path callers (history refresh) where a
    // blocking waitForFinished is unacceptable. The previous callback is
    // dropped — callers must guard with a generation token.
    if (!m_proc) return;
    m_proc->disconnect(this);
    if (m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
    }
    // Hand ownership of the dying QProcess to the event loop. Its destructor
    // closes file descriptors / pipes when Qt reaps it on the next iteration.
    QProcess *dying = m_proc;
    m_proc = nullptr;
    dying->deleteLater();
    m_cb = nullptr;
    m_stdoutBuf.clear();
    m_stderrBuf.clear();
    m_stderrLineBuf.clear();
    m_cancelled = false;
    m_progressMode = false;
    m_argv.clear();
    if (m_timeout) m_timeout->stop();
}

void GitProcessRunner::reset()
{
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_stdoutBuf.clear();
    m_stderrBuf.clear();
    m_stderrLineBuf.clear();
    m_cancelled = false;
    m_progressMode = false;
    m_argv.clear();
    m_cb = nullptr;
    // m_maxOutputBytes is a config (set by caller once) — don't clear here.
    if (m_timeout) m_timeout->stop();
}

void GitProcessRunner::run(const QString &cwd,
                           const QStringList &argv,
                           const QByteArray &stdinPayload,
                           int timeoutMs,
                           bool readErrAsProgress,
                           Callback cb)
{
    if (isRunning()) {
        // Caller violation — refuse rather than queue.
        if (cb) cb(-1, {}, QByteArray("runner busy"));
        return;
    }

    reset();
    m_cb = std::move(cb);
    m_progressMode = readErrAsProgress;
    m_argv = argv;
    m_inflightTimeoutMs = timeoutMs;

    const QString exe = gitExecutable();
    if (exe.isEmpty()) {
        if (m_cb) m_cb(-1, {}, QByteArray("git executable not found in PATH"));
        return;
    }

    m_proc = new QProcess(this);
    m_proc->setProgram(exe);
    m_proc->setArguments(argv);
    m_proc->setWorkingDirectory(cwd);
    m_proc->setProcessEnvironment(baseEnv());

#ifdef Q_OS_WIN
    // Same flag as AcpConnection::spawn — own process group so we can kill child
    // processes (ssh, curl, askpass helpers) along with git itself.
    m_proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        if (a) a->flags |= CREATE_NEW_PROCESS_GROUP;
    });
#endif

    connect(m_proc, &QProcess::readyReadStandardOutput,
            this, &GitProcessRunner::onReadyReadStdout);
    connect(m_proc, &QProcess::readyReadStandardError,
            this, &GitProcessRunner::onReadyReadStderr);
    connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &GitProcessRunner::onFinished);
    connect(m_proc, &QProcess::errorOccurred,
            this, &GitProcessRunner::onProcessError);

    m_proc->start(QIODevice::ReadWrite);
    if (!m_proc->waitForStarted(5000)) {
        const QByteArray err = QString::fromLatin1("failed to start git: %1")
            .arg(m_proc->errorString()).toUtf8();
        Callback cb2 = std::move(m_cb);
        reset();
        if (cb2) cb2(-1, {}, err);
        return;
    }

    if (!stdinPayload.isEmpty()) {
        m_proc->write(stdinPayload);
    }
    m_proc->closeWriteChannel();

    if (timeoutMs > 0) m_timeout->start(timeoutMs);
}

void GitProcessRunner::onReadyReadStdout()
{
    if (!m_proc) return;
    m_stdoutBuf.append(m_proc->readAllStandardOutput());
    // Output cap (history / commit-show paths set this to bound RAM).
    // Kill the process when exceeded; onFinished will fire with code -4
    // and the partial bytes already in m_stdoutBuf.
    if (m_maxOutputBytes > 0 && m_stdoutBuf.size() > m_maxOutputBytes) {
        if (!m_cancelled) {
            m_cancelled = true;
            // Reuse the cancellation sentinel but tag stderr so onFinished
            // can override exit to kExitTruncated.
            m_stderrBuf.append(QByteArrayLiteral("\n__GIT_TRUNCATED__"));
            m_proc->kill();
        }
        return;
    }
    // keepalive on activity
    if (m_inflightTimeoutMs > 0) m_timeout->start(m_inflightTimeoutMs);
}

void GitProcessRunner::onReadyReadStderr()
{
    if (!m_proc) return;
    const QByteArray chunk = m_proc->readAllStandardError();
    m_stderrBuf.append(chunk);
    if (m_inflightTimeoutMs > 0) m_timeout->start(m_inflightTimeoutMs);

    if (m_progressMode) {
        m_stderrLineBuf.append(chunk);
        // git uses \r for in-place progress updates and \n at line end; split on both.
        for (;;) {
            int nl = -1;
            for (int i = 0; i < m_stderrLineBuf.size(); ++i) {
                if (m_stderrLineBuf[i] == '\r' || m_stderrLineBuf[i] == '\n') { nl = i; break; }
            }
            if (nl < 0) break;
            const QByteArray line = m_stderrLineBuf.left(nl);
            m_stderrLineBuf.remove(0, nl + 1);
            const QString text = QString::fromUtf8(line).trimmed();
            if (!text.isEmpty()) emit progressLine(text);
        }
    }
}

void GitProcessRunner::onFinished(int code, QProcess::ExitStatus status)
{
    if (m_timeout) m_timeout->stop();

    // Drain any final buffered bytes.
    if (m_proc) {
        m_stdoutBuf.append(m_proc->readAllStandardOutput());
        m_stderrBuf.append(m_proc->readAllStandardError());
    }

    int exit = code;
    QByteArray err = m_stderrBuf;
    if (err.endsWith(QByteArrayLiteral("\n__GIT_TRUNCATED__"))) {
        exit = kExitTruncated;
        err.chop(static_cast<int>(qstrlen("\n__GIT_TRUNCATED__")));
    } else if (m_cancelled) {
        exit = kExitCancelled;
        if (err.isEmpty()) err = QByteArrayLiteral("cancelled");
    } else if (status == QProcess::CrashExit) {
        exit = exit ? exit : kExitCrash;
    }

    Callback cb = std::move(m_cb);
    QByteArray out = m_stdoutBuf;
    reset();
    if (cb) cb(exit, out, err);
}

void GitProcessRunner::onProcessError(QProcess::ProcessError e)
{
    Q_UNUSED(e);
    if (!m_proc) return;
    // FailedToStart is the only state where onFinished won't fire.
    if (e == QProcess::FailedToStart) {
        const QByteArray err = m_proc->errorString().toUtf8();
        Callback cb = std::move(m_cb);
        reset();
        if (cb) cb(-1, {}, err);
    }
}

void GitProcessRunner::onTimeout()
{
    if (!isRunning()) return;
    cancel();
    // cancel() sets m_cancelled then waits; onFinished will fire with exit=-2.
    // Re-tag the error as timeout via stderr override before reset happens —
    // but reset() runs after onFinished. To distinguish, we append a marker.
    m_stderrBuf.append(QByteArrayLiteral("\n__GIT_TIMEOUT__"));
}
