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

#ifndef REMOTE_REMOTEEXECUTIONCONTEXT_H
#define REMOTE_REMOTEEXECUTIONCONTEXT_H

#include "ExecutionContext.h"

#include "SshProfile.h"

namespace remote {

class SshConnection;
class RemoteFsBackend;

// An SSH host as an ExecutionContext. Holds an SshConnection (whose worker
// thread does all libssh2 I/O) and forwards connection state + connectionLost /
// reconnected. createPty() returns an SshPtyProcess on a channel of this
// connection. createGitRunner()/exec()/fsBackend() are the Phase 3/4/2 seams:
// declared, but stubbed (null / no-op) with // TODO markers — implementing them
// here is explicitly out of Phase-1 scope.
class RemoteExecutionContext : public ExecutionContext
{
    Q_OBJECT

public:
    RemoteExecutionContext(const SshProfile &profile, SshConnection *connection,
                           QObject *parent = nullptr);
    ~RemoteExecutionContext() override;

    SshConnection *connection() const { return m_connection; }
    const SshProfile &profile() const { return m_profile; }

    bool isRemote() const override { return true; }
    QString displayName() const override;
    State state() const override { return m_state; }
    QString sshProfileId() const override { return m_profile.id; }

    IPtyProcess *createPty(QObject *parent) override;

    // Git over SSH (D6): a RemoteGitProcessRunner over this context's
    // multiplexed connection (one exec channel per invocation).
    IGitProcessRunner *createGitRunner(QObject *parent) override;
    // One-shot remote exec over an SSH exec channel (D8): `cd <cwd> && <argv...>`
    // shell-quoted, stdout/stderr captured, exit status returned to cb exactly
    // once. Used by the ACP binary probe (`command -v`) and other capture-style
    // commands; multiplexed on this context's single connection.
    void exec(const QString &cwd, const QStringList &argv,
              const QByteArray &stdinPayload, int timeoutMs, ExecCallback cb) override;
    // Phase 2 — remote (SFTP) filesystem backend. Lazily creates a
    // RemoteFsBackend over this context's connection (D1).
    IFileSystemBackend *fsBackend() override;

    QString resolveCwd(const QString &requested) const override;

private:
    SshProfile m_profile;
    SshConnection *m_connection; // owned by the registry, not by this context
    State m_state = State::Disconnected;
    RemoteFsBackend *m_fsBackend = nullptr; // lazily created, child of this
};

} // namespace remote

#endif // REMOTE_REMOTEEXECUTIONCONTEXT_H
