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

#ifndef REMOTE_EXECUTIONCONTEXT_H
#define REMOTE_EXECUTIONCONTEXT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class IPtyProcess;
class IGitProcessRunner;

namespace remote {

class IFileSystemBackend;

// A single abstraction that both the local machine and a remote SSH host
// implement, so a subsystem (terminal now; file tree / git / agents later) can
// obtain an execution primitive without knowing whether "here" is local or an
// SSH host. Local execution is just another context.
//
// Phase 1 wires only createPty() + resolveCwd() + state/signals end-to-end.
// EVERY Phase 2-4 seam is declared here so later phases plug in without
// re-architecting:
//   - createGitRunner()  Phase 3  (local: GitProcessRunner; remote: TODO P3)
//   - exec()             Phase 4  (local: QProcess;        remote: TODO P4)
//   - fsBackend()        Phase 2  (local: QFile-backed now; remote: TODO P2)
//
// Threading: an ExecutionContext lives on the UI thread. A remote context owns
// an SshConnection whose worker thread does all libssh2 I/O; the context only
// relays state via queued signals. Long-lived consumers MUST capture the
// context* at creation and never re-resolve it (a workspace switch must not
// silently rebase a running terminal/agent — same rule as
// AiAgentDock::m_workingDirectory).
class ExecutionContext : public QObject
{
    Q_OBJECT

public:
    enum class State : quint8
    {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting,
        Failed,
    };
    Q_ENUM(State)

    using ExecCallback =
        std::function<void(int exitCode, const QByteArray &out, const QByteArray &err)>;

    explicit ExecutionContext(QObject *parent = nullptr) : QObject(parent) {}
    ~ExecutionContext() override = default;

    virtual bool isRemote() const = 0;
    virtual QString displayName() const = 0;
    virtual State state() const = 0;

    // PTY for a terminal. Caller takes ownership (passes a parent). Local
    // returns the exact same PtyQt object used before this change; remote
    // returns an SshPtyProcess backed by a channel on the connection.
    virtual IPtyProcess *createPty(QObject *parent) = 0;

    // Git runner. Phase 3. Local returns a GitProcessRunner; remote returns
    // nullptr (// TODO P3) until git-over-SSH lands.
    virtual IGitProcessRunner *createGitRunner(QObject *parent) = 0;

    // One-shot command exec (capture stdout/stderr/exit). Phase 4. Local runs a
    // QProcess; remote is an unimplemented stub (// TODO P4).
    virtual void exec(const QString &cwd,
                      const QStringList &argv,
                      const QByteArray &stdinPayload,
                      int timeoutMs,
                      ExecCallback cb) = 0;

    // Filesystem backend for the workspace tree / editor I/O. Phase 2 for
    // remote. Local returns a QFile-backed backend NOW so the seam is real and
    // 0-regression; remote returns nullptr (// TODO P2). Ownership stays with
    // the context.
    virtual IFileSystemBackend *fsBackend() = 0;

    // Normalize a requested working directory. Local cleans a local path;
    // remote normalizes a POSIX path and requires Connected (see D11).
    virtual QString resolveCwd(const QString &requested) const = 0;

signals:
    void stateChanged(remote::ExecutionContext::State state);
    void connectionLost(const QString &reason);
    void reconnected();
};

} // namespace remote

#endif // REMOTE_EXECUTIONCONTEXT_H
