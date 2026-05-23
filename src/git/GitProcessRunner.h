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

#ifndef GIT_PROCESS_RUNNER_H
#define GIT_PROCESS_RUNNER_H

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>

class QTimer;

class IGitProcessRunner
{
public:
    using Callback = std::function<void(int exit, const QByteArray &out, const QByteArray &err)>;
    virtual ~IGitProcessRunner() = default;

    virtual void run(const QString &cwd,
                     const QStringList &argv,
                     const QByteArray &stdinPayload,
                     int timeoutMs,
                     bool readErrAsProgress,
                     Callback cb) = 0;
    virtual void cancel() = 0;
    virtual bool isRunning() const = 0;
};

class GitProcessRunner : public QObject, public IGitProcessRunner
{
    Q_OBJECT
public:
    explicit GitProcessRunner(QObject *parent = nullptr);
    ~GitProcessRunner() override;

    void run(const QString &cwd,
             const QStringList &argv,
             const QByteArray &stdinPayload,
             int timeoutMs,
             bool readErrAsProgress,
             Callback cb) override;
    void cancel() override;
    bool isRunning() const override;

    // Non-blocking cancel for hot-path / spam-friendly callers (history
    // refresh). Detaches the in-flight process from this runner instance and
    // SIGKILLs it without waiting. The callback (if any) is dropped; callers
    // who care about late chunks must guard with a generation token.
    // After cancelAsync() returns, isRunning() == false and a new run() can
    // be issued in the same event-loop tick.
    void cancelAsync();

    // Optional stdout byte cap. When the buffered stdout exceeds this size
    // the process is killed and the callback is invoked with the truncated
    // bytes + a special exit code (kExitTruncated). 0 = unlimited (default).
    void setMaxOutputBytes(qint64 bytes) { m_maxOutputBytes = bytes; }
    qint64 maxOutputBytes() const { return m_maxOutputBytes; }

    // Sentinel exit codes (in addition to the process's own).
    static constexpr int kExitCancelled = -2;   // cancel() was invoked
    static constexpr int kExitCrash     = -3;   // QProcess::CrashExit
    static constexpr int kExitTruncated = -4;   // maxOutputBytes exceeded

    static bool gitAvailable();
    static QString gitExecutable();
    static QProcessEnvironment baseEnv();

signals:
    void progressLine(const QString &line);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onFinished(int code, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError e);
    void onTimeout();

private:
    void reset();

    QProcess *m_proc = nullptr;
    QByteArray m_stdoutBuf;
    QByteArray m_stderrBuf;
    QByteArray m_stderrLineBuf;     // for line-by-line progress emission
    Callback m_cb;
    bool m_progressMode = false;
    bool m_cancelled = false;
    QTimer *m_timeout = nullptr;
    QStringList m_argv;
    int m_inflightTimeoutMs = 0;
    qint64 m_maxOutputBytes = 0;  // 0 = unlimited
};

#endif // GIT_PROCESS_RUNNER_H
