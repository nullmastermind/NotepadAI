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

#ifndef REMOTE_SSHPROFILE_H
#define REMOTE_SSHPROFILE_H

#include <QJsonObject>
#include <QString>

namespace remote {

// Plain data describing one saved SSH connection profile. Intentionally not a
// QObject so it can be copied and stored by value (pattern: AcpAgentDefinition).
// Secrets (password / passphrase) are NEVER stored here — they live only in the
// OS keychain under `ssh-profile:<id>:<kind>` (see CredentialStore). This struct
// is persisted in the `Ssh/Profiles` settings array (SshProfileRegistry).
struct SshProfile
{
    enum class AuthMethod : quint8
    {
        Password = 0,
        KeyFile = 1,   // private key file (+ optional passphrase in keychain)
        Agent = 2,     // ssh-agent / Pageant — no stored secret
    };

    QString    id;
    QString    host;
    int        port = 22;
    QString    username;
    AuthMethod authMethod = AuthMethod::Agent;
    QString    keyPath;          // used when authMethod == KeyFile
    QString    lastRemotePath;   // remote cwd to default to on connect
    qint64     lastConnectedMs = 0;

    bool isValid() const
    {
        return !id.isEmpty() && !host.isEmpty() && port > 0 && port <= 65535;
    }
};

inline QString authMethodToString(SshProfile::AuthMethod m)
{
    switch (m) {
    case SshProfile::AuthMethod::Password: return QStringLiteral("password");
    case SshProfile::AuthMethod::KeyFile:  return QStringLiteral("keyfile");
    case SshProfile::AuthMethod::Agent:    return QStringLiteral("agent");
    }
    return QStringLiteral("agent");
}

inline SshProfile::AuthMethod authMethodFromString(const QString &s)
{
    if (s == QLatin1String("password")) return SshProfile::AuthMethod::Password;
    if (s == QLatin1String("keyfile"))  return SshProfile::AuthMethod::KeyFile;
    return SshProfile::AuthMethod::Agent;
}

inline QJsonObject sshProfileToJson(const SshProfile &p)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), p.id);
    obj.insert(QStringLiteral("host"), p.host);
    obj.insert(QStringLiteral("port"), p.port);
    obj.insert(QStringLiteral("username"), p.username);
    obj.insert(QStringLiteral("authMethod"), authMethodToString(p.authMethod));
    obj.insert(QStringLiteral("keyPath"), p.keyPath);
    obj.insert(QStringLiteral("lastRemotePath"), p.lastRemotePath);
    obj.insert(QStringLiteral("lastConnectedMs"), p.lastConnectedMs);
    return obj;
}

inline SshProfile sshProfileFromJson(const QJsonObject &obj)
{
    SshProfile p;
    p.id = obj.value(QStringLiteral("id")).toString();
    p.host = obj.value(QStringLiteral("host")).toString();
    p.port = obj.value(QStringLiteral("port")).toInt(22);
    p.username = obj.value(QStringLiteral("username")).toString();
    p.authMethod = authMethodFromString(obj.value(QStringLiteral("authMethod")).toString());
    p.keyPath = obj.value(QStringLiteral("keyPath")).toString();
    p.lastRemotePath = obj.value(QStringLiteral("lastRemotePath")).toString();
    p.lastConnectedMs = static_cast<qint64>(obj.value(QStringLiteral("lastConnectedMs")).toDouble(0));
    return p;
}

// Keychain key schema for a profile secret: `ssh-profile:<profileId>:<kind>`
// where kind is "password" or "passphrase". Used with
// CredentialStore::storeSecret/retrieveSecret/clearSecret.
inline QString sshSecretKey(const QString &profileId, const QString &kind)
{
    return QStringLiteral("ssh-profile:") + profileId + QLatin1Char(':') + kind;
}

// --- ssh:// workspace/recent URI sentinel ------------------------------------
//
// SSH workspaces are stored as `ssh://<profileId><remoteAbsPath>` strings inside
// the EXISTING FolderAsWorkspace/Workspaces QStringList. A remote absolute path
// always begins with '/', so the URI is `ssh://` + profileId + that path, e.g.
//   ssh://9f3c1a/home/alice/project
// Local paths never begin with `ssh://`, so the legacy format parses unchanged
// (zero migration). The profileId must not contain '/' (registry mints
// slash-free ids), so the first '/' after the scheme cleanly splits id|path.

struct SshUri
{
    QString profileId;
    QString remotePath;
    bool    valid = false;
};

inline bool isSshUri(const QString &s)
{
    return s.startsWith(QLatin1String("ssh://"));
}

inline QString formatSshUri(const QString &profileId, const QString &remotePath)
{
    QString path = remotePath;
    if (path.isEmpty()) {
        path = QStringLiteral("/");
    } else if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }
    return QStringLiteral("ssh://") + profileId + path;
}

inline SshUri parseSshUri(const QString &s)
{
    SshUri out;
    if (!isSshUri(s)) {
        return out;
    }
    const QString rest = s.mid(6); // strip "ssh://"
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash < 0) {
        // "ssh://<id>" with no path → default to remote root.
        out.profileId = rest;
        out.remotePath = QStringLiteral("/");
    } else {
        out.profileId = rest.left(slash);
        out.remotePath = rest.mid(slash);
    }
    out.valid = !out.profileId.isEmpty();
    return out;
}

inline QString posixParentPath(const QString &path)
{
    if (path.isEmpty())
        return QString();
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? path.left(slash) : QStringLiteral("/");
}

} // namespace remote

#endif // REMOTE_SSHPROFILE_H
