/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// ssh.* profiles and connections, uploads/downloads, zip and croc transfers,
// Pandoc export, Show in Explorer and recent workspaces (docs/adr/0007),
// through the same managers the workspace tree and its dialogs use. Their
// questions (overwrite existing files? trust this host key?) are arguments.

#include "Registrar.h"

#include "CrocShareFlow.h"
#include "EditorManager.h"
#include "FolderZipTransfer.h"
#include "NotepadNextApplication.h"
#include "PandocExporter.h"
#include "RecentFilesListManager.h"
#include "ScintillaNext.h"
#include "ai/CredentialStore.h"
#include "dialogs/MainWindow.h"
#include "docks/FolderAsWorkspaceDock.h"
#include "remote/ExecutionContextRegistry.h"
#include "remote/RemoteExecutionContext.h"
#include "remote/RemoteFsBackend.h"
#include "remote/RemoteTransferManager.h"
#include "remote/SshConnection.h"
#include "remote/SshProfile.h"
#include "remote/SshProfileRegistry.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

namespace control::commands {

namespace {

constexpr int kConnectTimeoutMs = 60 * 1000;
constexpr int kTransferTimeoutMs = 60 * 60 * 1000;

// A path inside a workspace, as both forms its managers take.
struct WorkspacePath
{
    QString uri;   // what the tree uses: a local path, or ssh://<profile>/<posix path>
    QString posix; // a local path, or the remote posix path
    QString root;  // the workspace root in the same form as posix
};

// `path` (absolute, ssh://, or relative to the workspace root) inside `dock`.
bool workspacePath(const CommandContext &ctx, FolderAsWorkspaceDock *dock, const QString &path, WorkspacePath *out)
{
    const QString rootUri = dock->rootPath();
    if (!remote::isSshUri(rootUri)) {
        QString p = QDir::fromNativeSeparators(path);
        if (p.isEmpty())
            p = rootUri;
        else if (QDir::isRelativePath(p))
            p = rootUri + QLatin1Char('/') + p;
        p = QDir::cleanPath(p);
        *out = {p, p, rootUri};
        return true;
    }
    const remote::SshUri root = remote::parseSshUri(rootUri);
    QString posix;
    if (path.isEmpty())
        posix = root.remotePath;
    else if (remote::isSshUri(path))
        posix = remote::parseSshUri(path).remotePath;
    else if (path.startsWith(QLatin1Char('/')))
        posix = path;
    else
        posix = root.remotePath + QLatin1Char('/') + path;
    posix = QDir::cleanPath(posix);
    if (posix.isEmpty()) {
        ctx.fail(400, QStringLiteral("'%1' is not a path in this workspace").arg(path));
        return false;
    }
    *out = {remote::formatSshUri(root.profileId, posix), posix, root.remotePath};
    return true;
}

QString localPathArg(const CommandContext &ctx, const QString &arg)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(ctx.str(arg)));
}

QJsonObject profileJson(const remote::SshProfile &p, ai::CredentialStore *store)
{
    QJsonObject o{{QStringLiteral("id"), p.id},
                  {QStringLiteral("host"), p.host},
                  {QStringLiteral("port"), p.port},
                  {QStringLiteral("username"), p.username},
                  {QStringLiteral("auth"), p.authMethod == remote::SshProfile::AuthMethod::Password ? QStringLiteral("password")
                                           : p.authMethod == remote::SshProfile::AuthMethod::KeyFile ? QStringLiteral("key")
                                                                                                    : QStringLiteral("agent")},
                  {QStringLiteral("remotePath"), p.lastRemotePath}};
    if (!p.keyPath.isEmpty())
        o.insert(QStringLiteral("keyPath"), p.keyPath);
    if (store) {
        const QString kind = p.authMethod == remote::SshProfile::AuthMethod::KeyFile ? QStringLiteral("passphrase")
                                                                                      : QStringLiteral("password");
        o.insert(QStringLiteral("hasSecret"), !store->retrieveSecret(remote::sshSecretKey(p.id, kind)).isEmpty());
    }
    return o;
}

// The profile fields of ssh.addProfile / ssh.updateProfile.
QList<Arg> profileArgs(bool adding)
{
    auto field = adding ? &Registrar::required : &Registrar::optional;
    QList<Arg> args{
        field(QStringLiteral("host"), ArgType::String, QStringLiteral("host name or address")),
        Registrar::optional(QStringLiteral("port"), ArgType::Int, QStringLiteral("port; default 22")),
        Registrar::optional(QStringLiteral("username"), ArgType::String, QStringLiteral("user to log in as")),
        Registrar::oneOf(QStringLiteral("auth"), {QStringLiteral("agent"), QStringLiteral("key"), QStringLiteral("password")},
                         QStringLiteral("agent (ssh-agent / Pageant), key (a private key file) or password; default agent")),
        Registrar::optional(QStringLiteral("keyPath"), ArgType::String, QStringLiteral("private key file, for auth key")),
        Registrar::optional(QStringLiteral("remotePath"), ArgType::String, QStringLiteral("remote folder to start in"))};
    Arg secret = Registrar::optional(QStringLiteral("secret"), ArgType::String,
                                     QStringLiteral("the password (auth password) or key passphrase (auth key), kept in "
                                                    "the system keychain; never read back"));
    secret.secret = true;
    args.append(secret);
    return args;
}

void applyProfileArgs(const CommandContext &ctx, remote::SshProfile *p)
{
    if (ctx.has(QStringLiteral("host")))
        p->host = ctx.str(QStringLiteral("host")).trimmed();
    if (ctx.has(QStringLiteral("port")))
        p->port = int(ctx.integer(QStringLiteral("port")));
    if (ctx.has(QStringLiteral("username")))
        p->username = ctx.str(QStringLiteral("username")).trimmed();
    if (ctx.has(QStringLiteral("auth"))) {
        const QString auth = ctx.str(QStringLiteral("auth"));
        p->authMethod = auth == QLatin1String("password") ? remote::SshProfile::AuthMethod::Password
                        : auth == QLatin1String("key")    ? remote::SshProfile::AuthMethod::KeyFile
                                                          : remote::SshProfile::AuthMethod::Agent;
    }
    if (ctx.has(QStringLiteral("keyPath")))
        p->keyPath = QDir::fromNativeSeparators(ctx.str(QStringLiteral("keyPath")));
    if (ctx.has(QStringLiteral("remotePath")))
        p->lastRemotePath = ctx.str(QStringLiteral("remotePath"));
}

void storeProfileSecret(const CommandContext &ctx, ai::CredentialStore *store, const remote::SshProfile &p)
{
    if (!store || !ctx.has(QStringLiteral("secret")))
        return;
    const QString kind = p.authMethod == remote::SshProfile::AuthMethod::KeyFile ? QStringLiteral("passphrase")
                                                                                  : QStringLiteral("password");
    const QString secret = ctx.str(QStringLiteral("secret"));
    if (secret.isEmpty())
        store->clearSecret(remote::sshSecretKey(p.id, kind));
    else
        store->storeSecret(remote::sshSecretKey(p.id, kind), secret);
}

// Answers once a transfer manager's queue reports its outcome.
template <typename Manager>
void answerWhenTransferred(const CommandContext &ctx, Manager *manager, const std::function<QJsonObject()> &extra = {})
{
    auto done = std::make_shared<bool>(false);
    auto *guard = new QObject(manager);
    auto finish = [done, guard](const std::function<void()> &answer) {
        if (*done)
            return;
        *done = true;
        guard->deleteLater();
        answer();
    };
    QObject::connect(manager, &Manager::transferCompleted, guard, [ctx, finish, extra](int count) {
        finish([&] {
            QJsonObject o = extra ? extra() : QJsonObject{};
            if (count > 0) // writing a zip reports no count
                o.insert(QStringLiteral("files"), count);
            ctx.ok(o);
        });
    });
    QObject::connect(manager, &Manager::transferError, guard,
                     [ctx, finish](const QString &message) { finish([&] { ctx.fail(422, message); }); });
    QObject::connect(manager, &Manager::transferCancelled, guard,
                     [ctx, finish] { finish([&] { ctx.fail(422, QStringLiteral("the transfer was cancelled")); }); });
    QTimer::singleShot(kTransferTimeoutMs, guard, [ctx, finish] {
        finish([&] { ctx.fail(504, QStringLiteral("the transfer is still running after an hour")); });
    });
}

const Arg kConflicts = Registrar::oneOf(
    QStringLiteral("conflicts"), {QStringLiteral("overwrite"), QStringLiteral("skip")},
    QStringLiteral("what to do with files that already exist there: overwrite or skip; without it, existing files "
                   "refuse the transfer (409) and are listed"));

} // namespace

void Registrar::transfer()
{
    // --- Files ---------------------------------------------------------------

    add(QStringLiteral("files.pandoc"),
        QStringLiteral("Export a Markdown file through Pandoc to DOCX, HTML or EPUB (the tree's Export via Pandoc)"), true,
        {required(QStringLiteral("path"), ArgType::String, QStringLiteral("the Markdown file (local)")),
         oneOf(QStringLiteral("format"), {QStringLiteral("docx"), QStringLiteral("html"), QStringLiteral("epub")},
               QStringLiteral("docx, html or epub"), true),
         optional(QStringLiteral("to"), ArgType::String, QStringLiteral("output file; default: next to it with the format's extension")),
         oneOf(QStringLiteral("unsaved"), {QStringLiteral("save"), QStringLiteral("disk")},
               QStringLiteral("when the file has unsaved edits: save them first, or export the version on disk"))},
        [w = w](const CommandContext &ctx) {
            const QString path = pathArg(w, ctx.str(QStringLiteral("path")));
            if (remote::isSshUri(path) || !QFileInfo(path).isFile()) {
                ctx.fail(404, QStringLiteral("no local file %1").arg(displayPath(path)));
                return;
            }
            if (!PandocExporter::isAvailable()) {
                ctx.fail(409, QStringLiteral("Pandoc 2.19 or later is not installed (pandoc.org/installing.html)"));
                return;
            }
            if (ScintillaNext *editor = w->app->getEditorManager()->getEditorByFilePath(path);
                editor && editor->canSaveToDisk()) {
                const QString unsaved = ctx.str(QStringLiteral("unsaved"));
                if (unsaved.isEmpty()) {
                    ctx.fail(409, QStringLiteral("%1 has unsaved edits; pass unsaved: \"save\" or \"disk\"").arg(displayPath(path)),
                             {{QStringLiteral("needs"), QStringLiteral("unsaved")}});
                    return;
                }
                if (unsaved == QLatin1String("save") && editor->save() != QFileDevice::NoError) {
                    ctx.fail(422, QStringLiteral("could not save %1").arg(displayPath(path)));
                    return;
                }
            }
            const QString format = ctx.str(QStringLiteral("format"));
            const QFileInfo fi(path);
            const QString out = ctx.has(QStringLiteral("to"))
                                    ? pathArg(w, ctx.str(QStringLiteral("to")))
                                    : fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QLatin1Char('.') + format;
            const PandocExporter::Format f = format == QLatin1String("docx")   ? PandocExporter::Docx
                                             : format == QLatin1String("html") ? PandocExporter::Html
                                                                               : PandocExporter::Epub;
            auto *exporter = new PandocExporter(w, w);
            QObject::connect(exporter, &PandocExporter::finished, w, [ctx, exporter, out](bool ok, const QString &error) {
                exporter->deleteLater();
                if (ok)
                    ctx.ok({{QStringLiteral("path"), displayPath(out)}});
                else
                    ctx.fail(422, QStringLiteral("Pandoc failed: %1").arg(error));
            });
            exporter->exportFile(path, out, f);
        });

    add(QStringLiteral("workspace.showInExplorer"),
        QStringLiteral("Show a file or folder in the system file manager (the tree's Show in Explorer)"), true,
        {required(QStringLiteral("path"), ArgType::String, QStringLiteral("local file or folder"))},
        [w = w](const CommandContext &ctx) {
            const QString path = pathArg(w, ctx.str(QStringLiteral("path")));
            const QFileInfo fi(path);
            if (remote::isSshUri(path) || !fi.exists()) {
                ctx.fail(404, QStringLiteral("no local file or folder %1 (SSH paths live on the server)").arg(displayPath(path)));
                return;
            }
#ifdef Q_OS_WIN
            const bool ok = fi.isDir()
                                ? QProcess::startDetached(QStringLiteral("explorer"), {QDir::toNativeSeparators(path)})
                                : QProcess::startDetached(QStringLiteral("explorer"),
                                                          {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#elif defined(Q_OS_MACOS)
            const bool ok = fi.isDir() ? QProcess::startDetached(QStringLiteral("open"), {path})
                                       : QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
#else
            const bool ok = QProcess::startDetached(QStringLiteral("xdg-open"), {fi.isDir() ? path : fi.absolutePath()});
#endif
            if (!ok) {
                ctx.fail(422, QStringLiteral("the file manager did not start"));
                return;
            }
            ctx.ok({{QStringLiteral("path"), displayPath(path)}});
        });

    add(QStringLiteral("workspace.recent"), QStringLiteral("List the recently opened workspaces, newest first"), false, {},
        [w = w](const CommandContext &ctx) {
            QJsonArray list;
            for (const QString &p : w->app->getRecentWorkspacesListManager()->fileList())
                list.append(displayPath(p));
            ctx.ok({{QStringLiteral("workspaces"), list}});
        });

    // --- SSH profiles and connections ------------------------------------------

    add(QStringLiteral("ssh.addProfile"), QStringLiteral("Save a new SSH connection profile (the SSH Connections dialog)"),
        true, profileArgs(true), [w = w](const CommandContext &ctx) {
            remote::SshProfile p;
            p.id = remote::SshProfileRegistry::generateId();
            applyProfileArgs(ctx, &p);
            if (!p.isValid()) {
                ctx.fail(400, QStringLiteral("host is empty or port is outside 1..65535"));
                return;
            }
            remote::SshProfileRegistry *registry = w->app->getSshProfileRegistry();
            if (!registry->addProfile(p)) {
                ctx.fail(422, QStringLiteral("the profile was not saved"));
                return;
            }
            storeProfileSecret(ctx, w->app->getCredentialStore(), p);
            ctx.ok(profileJson(p, w->app->getCredentialStore()));
        });

    {
        QList<Arg> args = profileArgs(false);
        args.prepend(required(QStringLiteral("id"), ArgType::String, QStringLiteral("profile id from ssh.profiles")));
        add(QStringLiteral("ssh.updateProfile"),
            QStringLiteral("Change an SSH connection profile; a secret passed empty is removed"), true, args,
            [w = w](const CommandContext &ctx) {
                remote::SshProfileRegistry *registry = w->app->getSshProfileRegistry();
                const QString id = ctx.str(QStringLiteral("id"));
                if (!registry->contains(id)) {
                    ctx.fail(404, QStringLiteral("no SSH profile '%1'; ssh.profiles lists them").arg(id));
                    return;
                }
                remote::SshProfile p = registry->profile(id);
                applyProfileArgs(ctx, &p);
                if (!p.isValid()) {
                    ctx.fail(400, QStringLiteral("host is empty or port is outside 1..65535"));
                    return;
                }
                registry->updateProfile(p);
                storeProfileSecret(ctx, w->app->getCredentialStore(), p);
                ctx.ok(profileJson(p, w->app->getCredentialStore()));
            });
    }

    add(QStringLiteral("ssh.removeProfile"), QStringLiteral("Delete an SSH connection profile and its stored secret"), true,
        {required(QStringLiteral("id"), ArgType::String, QStringLiteral("profile id from ssh.profiles"))},
        [w = w](const CommandContext &ctx) {
            remote::SshProfileRegistry *registry = w->app->getSshProfileRegistry();
            const QString id = ctx.str(QStringLiteral("id"));
            if (!registry->contains(id)) {
                ctx.fail(404, QStringLiteral("no SSH profile '%1'; ssh.profiles lists them").arg(id));
                return;
            }
            if (ai::CredentialStore *store = w->app->getCredentialStore()) {
                store->clearSecret(remote::sshSecretKey(id, QStringLiteral("password")));
                store->clearSecret(remote::sshSecretKey(id, QStringLiteral("passphrase")));
            }
            registry->removeProfile(id);
            ctx.ok();
        });

    add(QStringLiteral("ssh.connect"),
        QStringLiteral("Connect an SSH profile (what Connect in the SSH dialog does), answering the host-key question"),
        true,
        {required(QStringLiteral("profile"), ArgType::String, QStringLiteral("profile id from ssh.profiles")),
         optional(QStringLiteral("trustHostKey"), ArgType::String,
                  QStringLiteral("the host key fingerprint the user accepted; an unknown or changed key without it "
                                 "is refused (409) and its fingerprint returned"))},
        [w = w](const CommandContext &ctx) {
            const QString id = ctx.str(QStringLiteral("profile"));
            if (!w->app->getSshProfileRegistry()->contains(id)) {
                ctx.fail(404, QStringLiteral("no SSH profile '%1'; ssh.profiles lists them").arg(id));
                return;
            }
            remote::RemoteExecutionContext *remote = w->app->getExecutionContextRegistry()->connect(id);
            remote::SshConnection *conn = remote ? remote->connection() : nullptr;
            if (!conn) {
                ctx.fail(422, QStringLiteral("could not start the connection"));
                return;
            }
            if (conn->isConnected()) {
                ctx.ok({{QStringLiteral("profile"), id}, {QStringLiteral("connected"), true}});
                return;
            }
            auto done = std::make_shared<bool>(false);
            auto *guard = new QObject(conn);
            auto finish = [done, guard](const std::function<void()> &answer) {
                if (*done)
                    return;
                *done = true;
                guard->deleteLater();
                answer();
            };
            const QString trust = ctx.str(QStringLiteral("trustHostKey")).trimmed();
            QPointer<remote::SshConnection> c(conn);
            auto onHostKey = [ctx, finish, trust, c](const QString &fingerprint, bool changed) {
                if (!c)
                    return;
                if (!trust.isEmpty() && trust == fingerprint.trimmed()) {
                    c->acceptHostKey();
                    return;
                }
                c->rejectHostKey();
                finish([&] {
                    ctx.fail(409,
                             changed ? QStringLiteral("the server's host key CHANGED since it was last accepted (possible "
                                                      "man-in-the-middle); only the user can decide to trust it")
                                     : QStringLiteral("unknown host key; ask the user to confirm the fingerprint, then "
                                                      "pass it as trustHostKey"),
                             {{QStringLiteral("needs"), QStringLiteral("trustHostKey")},
                              {QStringLiteral("fingerprint"), fingerprint},
                              {QStringLiteral("changed"), changed}});
                });
            };
            QObject::connect(conn, &remote::SshConnection::hostKeyReceived, guard,
                             [onHostKey](const QString &fingerprint, const QByteArray &) { onHostKey(fingerprint, false); });
            QObject::connect(conn, &remote::SshConnection::hostKeyChanged, guard,
                             [onHostKey](const QString &fingerprint, const QByteArray &) { onHostKey(fingerprint, true); });
            QObject::connect(conn, &remote::SshConnection::connected, guard, [ctx, finish, id] {
                finish([&] { ctx.ok({{QStringLiteral("profile"), id}, {QStringLiteral("connected"), true}}); });
            });
            QObject::connect(conn, &remote::SshConnection::authFailed, guard, [ctx, finish](const QString &reason) {
                finish([&] {
                    ctx.fail(422, QStringLiteral("authentication failed: %1 (ssh.updateProfile sets the secret)").arg(reason));
                });
            });
            QObject::connect(conn, &remote::SshConnection::connectionLost, guard, [ctx, finish](const QString &reason) {
                finish([&] { ctx.fail(422, QStringLiteral("the connection failed: %1").arg(reason)); });
            });
            QTimer::singleShot(kConnectTimeoutMs, guard, [ctx, finish] {
                finish([&] { ctx.fail(504, QStringLiteral("the connection did not finish within a minute")); });
            });
            // A connect started earlier may already be waiting on the question.
            if (!conn->pendingHostKeyFingerprint().isEmpty())
                onHostKey(conn->pendingHostKeyFingerprint(), conn->pendingHostKeyChanged());
        });

    add(QStringLiteral("ssh.reconnect"),
        QStringLiteral("Reconnect an SSH workspace whose connection dropped (its Reconnect button)"), true,
        {workspaceArgSpec()}, [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            if (dock->sshWorkspaceUri().isEmpty()) {
                ctx.fail(409, QStringLiteral("%1 is not an SSH workspace").arg(displayPath(dock->rootPath())));
                return;
            }
            w->reconnectSshWorkspace(dock);
            ctx.reveal(dock);
            ctx.ok(workspaceJson(w, dock));
        });

    add(QStringLiteral("ssh.debugLog"),
        QStringLiteral("Read an SSH connection's debug log (Help > Debug > SSH Debug Log in debug builds)"), false,
        {required(QStringLiteral("profile"), ArgType::String, QStringLiteral("profile id from ssh.profiles")),
         optional(QStringLiteral("last"), ArgType::Int, QStringLiteral("the last this many lines; default 200"))},
        [w = w](const CommandContext &ctx) {
            const QString id = ctx.str(QStringLiteral("profile"));
            remote::RemoteExecutionContext *remote = w->app->getExecutionContextRegistry()->remoteContext(id);
            if (!remote || !remote->connection()) {
                ctx.fail(409, QStringLiteral("profile '%1' has no connection; run ssh.connect first").arg(id));
                return;
            }
            QStringList lines = remote->connection()->debugLog();
            const qsizetype last = std::max<qint64>(1, ctx.integer(QStringLiteral("last"), 200));
            if (lines.size() > last)
                lines = lines.mid(lines.size() - last);
            ctx.ok({{QStringLiteral("lines"), QJsonArray::fromStringList(lines)}});
        });

    add(QStringLiteral("ssh.listDir"),
        QStringLiteral("List a remote folder over a connected SSH profile (the remote folder picker)"), false,
        {required(QStringLiteral("profile"), ArgType::String, QStringLiteral("profile id from ssh.profiles")),
         optional(QStringLiteral("path"), ArgType::String, QStringLiteral("remote folder; default /"))},
        [w = w](const CommandContext &ctx) {
            const QString id = ctx.str(QStringLiteral("profile"));
            remote::RemoteExecutionContext *remote = w->app->getExecutionContextRegistry()->remoteContext(id);
            if (!remote || !remote->connection() || !remote->connection()->isConnected()) {
                ctx.fail(409, QStringLiteral("profile '%1' is not connected; run ssh.connect first").arg(id));
                return;
            }
            auto *backend = qobject_cast<remote::RemoteFsBackend *>(remote->fsBackend());
            if (!backend) {
                ctx.fail(422, QStringLiteral("this connection has no SFTP"));
                return;
            }
            const QString path = ctx.str(QStringLiteral("path"), QStringLiteral("/"));
            backend->readdirAsync(path, [ctx, path](bool ok, const QList<remote::RemoteDirEntry> &entries, const QString &error) {
                if (!ok) {
                    ctx.fail(422, QStringLiteral("could not list %1: %2").arg(path, error));
                    return;
                }
                QJsonArray list;
                for (const remote::RemoteDirEntry &e : entries)
                    list.append(QJsonObject{{QStringLiteral("name"), e.name},
                                            {QStringLiteral("dir"), e.isDir},
                                            {QStringLiteral("size"), e.size}});
                ctx.ok({{QStringLiteral("path"), path}, {QStringLiteral("entries"), list}});
            });
        });

    // --- Transfers (the tree's Download / Upload / Zip menus) -------------------

    add(QStringLiteral("workspace.download"),
        QStringLiteral("Download files or folders from an SSH workspace to this computer"), true,
        {workspaceArgSpec(),
         required(QStringLiteral("paths"), ArgType::Array,
                  QStringLiteral("remote files/folders (absolute, ssh://, or relative to the workspace root)")),
         required(QStringLiteral("to"), ArgType::String,
                  QStringLiteral("local folder to download into (for one file: a folder, or the file path to write)")),
         optional(QStringLiteral("skipIgnored"), ArgType::Bool, QStringLiteral("leave out what .gitignore ignores"))},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            remote::RemoteTransferManager *tx = dock->transferManager();
            if (dock->sshWorkspaceUri().isEmpty() || !tx) {
                ctx.fail(409, QStringLiteral("downloads are for SSH workspaces; local files are already here"));
                return;
            }
            if (tx->isTransferring()) {
                ctx.fail(409, QStringLiteral("another transfer is running in this workspace"));
                return;
            }
            QStringList remotePaths;
            for (const QString &p : ctx.strings(QStringLiteral("paths"))) {
                WorkspacePath wp;
                if (!workspacePath(ctx, dock, p, &wp))
                    return;
                remotePaths.append(wp.posix);
            }
            if (remotePaths.isEmpty()) {
                ctx.fail(400, QStringLiteral("paths is empty"));
                return;
            }
            const QString to = localPathArg(ctx, QStringLiteral("to"));
            answerWhenTransferred(ctx, tx, [to] { return QJsonObject{{QStringLiteral("to"), displayPath(to)}}; });
            ctx.reveal(dock);
            if (remotePaths.size() == 1 && !QFileInfo(to).isDir() && !ctx.flag(QStringLiteral("skipIgnored"))
                && QFileInfo(to).suffix().size() > 0) {
                tx->downloadFile(remotePaths.first(), to);
            } else {
                QDir().mkpath(to);
                tx->downloadRecursive(remotePaths, to,
                                      ctx.flag(QStringLiteral("skipIgnored"))
                                          ? remote::RemoteTransferManager::GitignoreFilter::Apply
                                          : remote::RemoteTransferManager::GitignoreFilter::None);
            }
        });

    add(QStringLiteral("workspace.upload"),
        QStringLiteral("Upload local files or a local folder into a folder of an SSH workspace"), true,
        {workspaceArgSpec(),
         optional(QStringLiteral("files"), ArgType::Array, QStringLiteral("local files to upload")),
         optional(QStringLiteral("folder"), ArgType::String, QStringLiteral("instead: a local folder to upload (with its contents)")),
         required(QStringLiteral("into"), ArgType::String,
                  QStringLiteral("remote folder (absolute, ssh://, or relative to the workspace root)")),
         kConflicts},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            remote::RemoteTransferManager *tx = dock->transferManager();
            if (dock->sshWorkspaceUri().isEmpty() || !tx) {
                ctx.fail(409, QStringLiteral("uploads are for SSH workspaces; copy local files with the file manager"));
                return;
            }
            if (tx->isTransferring()) {
                ctx.fail(409, QStringLiteral("another transfer is running in this workspace"));
                return;
            }
            const bool folder = ctx.has(QStringLiteral("folder"));
            if (folder == ctx.has(QStringLiteral("files"))) {
                ctx.fail(400, QStringLiteral("pass either `files` or `folder`"));
                return;
            }
            WorkspacePath into;
            if (!workspacePath(ctx, dock, ctx.str(QStringLiteral("into")), &into))
                return;
            const QString conflicts = ctx.str(QStringLiteral("conflicts"));
            QStringList files;
            for (const QString &f : ctx.strings(QStringLiteral("files")))
                files.append(QDir::cleanPath(QDir::fromNativeSeparators(f)));
            const QString localFolder = localPathArg(ctx, QStringLiteral("folder"));
            for (const QString &f : files) {
                if (!QFileInfo(f).isFile()) {
                    ctx.fail(404, QStringLiteral("no local file %1").arg(f));
                    return;
                }
            }
            if (folder && !QFileInfo(localFolder).isDir()) {
                ctx.fail(404, QStringLiteral("no local folder %1").arg(localFolder));
                return;
            }
            if (conflicts != QLatin1String("overwrite")) {
                // Detection runs first; answer its question here instead of the dialog.
                QPointer<remote::RemoteTransferManager> mgr(tx);
                QObject::connect(tx, &remote::RemoteTransferManager::uploadConflictsDetected, tx,
                                 [ctx, mgr, conflicts](const QStringList &paths, const QString &, const QStringList &) {
                                     if (!mgr)
                                         return;
                                     if (conflicts == QLatin1String("skip")) {
                                         QHash<QString, remote::RemoteTransferManager::ConflictResolution> skip;
                                         for (const QString &p : paths)
                                             skip.insert(p, remote::RemoteTransferManager::ConflictResolution::Skip);
                                         mgr->resolveUploadConflicts(skip, false);
                                         return;
                                     }
                                     ctx.fail(409, QStringLiteral("%1 file(s) already exist there; pass conflicts: "
                                                                  "\"overwrite\" or \"skip\"").arg(paths.size()),
                                              {{QStringLiteral("needs"), QStringLiteral("conflicts")},
                                               {QStringLiteral("existing"), QJsonArray::fromStringList(paths)}});
                                     mgr->cancel();
                                 },
                                 Qt::SingleShotConnection);
            }
            answerWhenTransferred(ctx, tx, [into] { return QJsonObject{{QStringLiteral("into"), into.uri}}; });
            ctx.reveal(dock);
            const bool overwrite = conflicts == QLatin1String("overwrite");
            if (folder)
                tx->uploadFolder(localFolder, into.posix, {}, overwrite);
            else
                tx->uploadFiles(files, into.posix, {}, overwrite);
        });

    add(QStringLiteral("workspace.zip"),
        QStringLiteral("Save a workspace folder as a .zip on this computer (the tree's Zip > Download > Save)"), true,
        {workspaceArgSpec(),
         optional(QStringLiteral("path"), ArgType::String, QStringLiteral("folder in the workspace; default its root")),
         required(QStringLiteral("to"), ArgType::String, QStringLiteral("the .zip file to write (local)"))},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            FolderZipTransfer *zip = dock->zipTransfer();
            if (!zip || zip->isBusy()) {
                ctx.fail(409, QStringLiteral("a zip transfer is already running in this workspace"));
                return;
            }
            WorkspacePath folder;
            if (!workspacePath(ctx, dock, ctx.str(QStringLiteral("path")), &folder))
                return;
            const QString to = localPathArg(ctx, QStringLiteral("to"));
            answerWhenTransferred(ctx, zip, [to] { return QJsonObject{{QStringLiteral("zip"), displayPath(to)}}; });
            ctx.reveal(dock);
            if (dock->sshWorkspaceUri().isEmpty())
                zip->downloadLocal(folder.root, folder.posix, to);
            else
                zip->downloadRemote(folder.root, folder.posix, to);
        });

    add(QStringLiteral("workspace.unzip"),
        QStringLiteral("Extract a local .zip into a workspace folder (the tree's Zip > Upload > From file)"), true,
        {workspaceArgSpec(), required(QStringLiteral("zip"), ArgType::String, QStringLiteral("the .zip file (local)")),
         optional(QStringLiteral("into"), ArgType::String, QStringLiteral("folder in the workspace; default its root")),
         kConflicts},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            FolderZipTransfer *zip = dock->zipTransfer();
            if (!zip || zip->isBusy()) {
                ctx.fail(409, QStringLiteral("a zip transfer is already running in this workspace"));
                return;
            }
            const QString zipFile = localPathArg(ctx, QStringLiteral("zip"));
            if (!QFileInfo(zipFile).isFile()) {
                ctx.fail(404, QStringLiteral("no file %1").arg(zipFile));
                return;
            }
            WorkspacePath into;
            if (!workspacePath(ctx, dock, ctx.str(QStringLiteral("into")), &into))
                return;
            const QString conflicts = ctx.str(QStringLiteral("conflicts"));
            zip->setConflictPolicy(conflicts == QLatin1String("overwrite") ? FolderZipTransfer::ConflictPolicy::Overwrite
                                   : conflicts == QLatin1String("skip")    ? FolderZipTransfer::ConflictPolicy::Skip
                                                                           : FolderZipTransfer::ConflictPolicy::Refuse);
            QPointer<FolderZipTransfer> z(zip);
            auto *guard = new QObject(zip);
            // Refused conflicts arrive as a cancel: answer with what to pass.
            QObject::connect(zip, &FolderZipTransfer::transferCancelled, guard, [ctx, z, guard] {
                guard->deleteLater();
                if (z && !z->lastConflicts().isEmpty())
                    ctx.fail(409, QStringLiteral("%1 file(s) already exist there; pass conflicts: \"overwrite\" or \"skip\"")
                                      .arg(z->lastConflicts().size()),
                             {{QStringLiteral("needs"), QStringLiteral("conflicts")},
                              {QStringLiteral("existing"), QJsonArray::fromStringList(z->lastConflicts())}});
            });
            QObject::connect(zip, &FolderZipTransfer::transferCompleted, guard, [guard](int) { guard->deleteLater(); });
            QObject::connect(zip, &FolderZipTransfer::transferError, guard, [guard](const QString &) { guard->deleteLater(); });
            answerWhenTransferred(ctx, zip, [into] { return QJsonObject{{QStringLiteral("into"), into.uri}}; });
            ctx.reveal(dock);
            if (dock->sshWorkspaceUri().isEmpty())
                zip->uploadLocal(into.root, zipFile, into.posix, w);
            else
                zip->uploadRemote(into.root, zipFile, into.posix, w);
        });

    add(QStringLiteral("workspace.share"),
        QStringLiteral("Share a workspace folder with croc and answer with its code-phrase (the tree's Zip > Download > "
                       "Share); the user sees the phrase too, and the send runs until the other side receives it"),
        true,
        {workspaceArgSpec(),
         optional(QStringLiteral("path"), ArgType::String, QStringLiteral("folder in the workspace; default its root"))},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            FolderZipTransfer *zip = dock->zipTransfer();
            if (!zip || zip->isBusy()) {
                ctx.fail(409, QStringLiteral("a zip transfer is already running in this workspace"));
                return;
            }
            WorkspacePath folder;
            if (!workspacePath(ctx, dock, ctx.str(QStringLiteral("path")), &folder))
                return;
            QString name = folder.posix.section(QLatin1Char('/'), -1);
            if (name.isEmpty())
                name = QStringLiteral("project");
            auto *flow = new CrocShareFlow(w);
            flow->setQuiet(true);
            auto done = std::make_shared<bool>(false);
            QObject::connect(flow, &CrocShareFlow::phraseReady, w, [ctx, done](const QString &phrase) {
                if (std::exchange(*done, true))
                    return;
                ctx.ok({{QStringLiteral("phrase"), phrase}});
            });
            QObject::connect(flow, &CrocShareFlow::failed, w, [ctx, done](const QString &message) {
                if (std::exchange(*done, true))
                    return;
                ctx.fail(422, message);
            });
            ctx.reveal(dock);
            flow->shareFolder(zip, folder.root, folder.posix, name, !dock->sshWorkspaceUri().isEmpty());
        });

    add(QStringLiteral("workspace.receive"),
        QStringLiteral("Receive a folder shared with croc into a workspace folder (the tree's Zip > Upload > From "
                       "code-phrase)"),
        true,
        {workspaceArgSpec(), required(QStringLiteral("code"), ArgType::String, QStringLiteral("the croc code-phrase")),
         optional(QStringLiteral("into"), ArgType::String, QStringLiteral("folder in the workspace; default its root")),
         kConflicts},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            FolderZipTransfer *zip = dock->zipTransfer();
            if (!zip || zip->isBusy()) {
                ctx.fail(409, QStringLiteral("a zip transfer is already running in this workspace"));
                return;
            }
            WorkspacePath into;
            if (!workspacePath(ctx, dock, ctx.str(QStringLiteral("into")), &into))
                return;
            const QString conflicts = ctx.str(QStringLiteral("conflicts"));
            zip->setConflictPolicy(conflicts == QLatin1String("overwrite") ? FolderZipTransfer::ConflictPolicy::Overwrite
                                   : conflicts == QLatin1String("skip")    ? FolderZipTransfer::ConflictPolicy::Skip
                                                                           : FolderZipTransfer::ConflictPolicy::Refuse);
            auto *flow = new CrocShareFlow(w);
            flow->setQuiet(true);
            auto done = std::make_shared<bool>(false);
            QPointer<FolderZipTransfer> z(zip);
            QObject::connect(flow, &CrocShareFlow::finished, w, [ctx, done, into] {
                if (std::exchange(*done, true))
                    return;
                ctx.ok({{QStringLiteral("into"), into.uri}});
            });
            QObject::connect(flow, &CrocShareFlow::failed, w, [ctx, done, z](const QString &message) {
                if (std::exchange(*done, true))
                    return;
                if (z && !z->lastConflicts().isEmpty()) {
                    ctx.fail(409, QStringLiteral("%1 file(s) already exist there; pass conflicts: \"overwrite\" or \"skip\"")
                                      .arg(z->lastConflicts().size()),
                             {{QStringLiteral("needs"), QStringLiteral("conflicts")},
                              {QStringLiteral("existing"), QJsonArray::fromStringList(z->lastConflicts())}});
                    return;
                }
                ctx.fail(422, message);
            });
            ctx.reveal(dock);
            flow->receiveIntoFolder(zip, into.root, into.posix, !dock->sshWorkspaceUri().isEmpty(),
                                    ctx.str(QStringLiteral("code")).trimmed());
        });
}

} // namespace control::commands
