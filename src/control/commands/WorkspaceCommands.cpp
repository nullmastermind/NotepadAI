/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// workspace.*, ssh.* (docs/adr/0007).

#include "Registrar.h"

#include "FileIndexCache.h"
#include "NotepadNextApplication.h"
#include "ScintillaNext.h"
#include "WorkspaceFileEnumerator.h"
#include "dialogs/MainWindow.h"
#include "docks/FolderAsWorkspaceDock.h"
#include "git/GitTabWidget.h"
#include "remote/RemoteFsBackend.h"
#include "remote/SshProfile.h"
#include "remote/SshProfileRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTimer>

namespace control::commands {

namespace {

// A path inside the workspace: absolute, or relative to its root.
QString inWorkspace(FolderAsWorkspaceDock *dock, const QString &path)
{
    const QString root = dock->rootPath();
    if (remote::isSshUri(path))
        return path;
    if (remote::isSshUri(root)) {
        if (path.startsWith(QLatin1Char('/'))) {
            const remote::SshUri uri = remote::parseSshUri(root);
            return remote::formatSshUri(uri.profileId, path);
        }
        return root + QLatin1Char('/') + path;
    }
    const QString p = QDir::fromNativeSeparators(path);
    return QDir::cleanPath(QDir::isRelativePath(p) ? root + QLatin1Char('/') + p : p);
}

bool underRoot(const QString &root, const QString &path)
{
    const QString r = QDir::cleanPath(root);
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return path.compare(r, cs) == 0 || path.startsWith(r + QLatin1Char('/'), cs);
}

QJsonObject filesResult(const FileIndexCache &index, const QString &query, int limit)
{
    QJsonArray files;
    int matched = 0;
    for (const QString &p : index.displayPaths) {
        if (!query.isEmpty() && !p.contains(query, Qt::CaseInsensitive))
            continue;
        if (++matched <= limit)
            files.append(p);
    }
    return {{QStringLiteral("files"), files},
            {QStringLiteral("matched"), matched},
            {QStringLiteral("total"), index.count()},
            {QStringLiteral("truncated"), matched > limit},
            {QStringLiteral("git"), index.isGitRepo}};
}

} // namespace

void Registrar::workspace()
{
    add(QStringLiteral("workspace.list"), QStringLiteral("List the open workspaces (project folders) and the active one"),
        false, {}, [w = w](const CommandContext &ctx) {
            QJsonArray list;
            const QList<FolderAsWorkspaceDock *> docks =
                w->findChildren<FolderAsWorkspaceDock *>(QString(), Qt::FindDirectChildrenOnly);
            for (FolderAsWorkspaceDock *dock : docks) {
                if (!dock->rootPath().isEmpty())
                    list.append(workspaceJson(w, dock));
            }
            ctx.ok({{QStringLiteral("workspaces"), list},
                    {QStringLiteral("active"), displayPath(w->currentWorkspaceRoot())}});
        });

    add(QStringLiteral("workspace.open"),
        QStringLiteral("Open a folder as a workspace (or make it active if open) and show its file tree"), true,
        {required(QStringLiteral("path"), ArgType::String,
                  QStringLiteral("folder path, or ssh://<profile id>/<remote path> (ssh.profiles lists profiles)")),
         optional(QStringLiteral("git"), ArgType::Bool, QStringLiteral("show its Git tab"))},
        [w = w](const CommandContext &ctx) {
            const QString path = pathArg(w, ctx.str(QStringLiteral("path")));
            if (!remote::isSshUri(path)) {
                if (!QFileInfo(path).isDir()) {
                    ctx.fail(404, QStringLiteral("no folder %1").arg(path));
                    return;
                }
            } else if (const remote::SshUri uri = remote::parseSshUri(path);
                       !uri.valid || !w->app->getSshProfileRegistry()->contains(uri.profileId)) {
                ctx.fail(404, QStringLiteral("no SSH profile for %1; ssh.profiles lists them").arg(path));
                return;
            }
            w->openFolderAsWorkspacePath(path, ctx.flag(QStringLiteral("git")));
            FolderAsWorkspaceDock *dock = workspaceFor(w, path);
            if (!dock) {
                ctx.fail(422, QStringLiteral("the workspace did not open"));
                return;
            }
            ctx.reveal(dock);
            ctx.ok(workspaceJson(w, dock));
        });

    add(QStringLiteral("workspace.activate"),
        QStringLiteral("Make an open workspace the active one (its tree in front; new terminals and agents start there)"),
        true, {required(QStringLiteral("workspace"), ArgType::String, QStringLiteral("workspace root from workspace.list"))},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            dock->setVisible(true);
            dock->raise();
            w->setActiveWorkspace(dock);
            ctx.reveal(dock);
            ctx.ok(workspaceJson(w, dock));
        });

    add(QStringLiteral("workspace.close"),
        QStringLiteral("Close a workspace (its terminals close with it; open files stay open)"), true,
        {required(QStringLiteral("workspace"), ArgType::String, QStringLiteral("workspace root from workspace.list"))},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            const QString root = displayPath(dock->rootPath());
            dock->close();
            ctx.ok({{QStringLiteral("closed"), root}});
        });

    add(QStringLiteral("workspace.show"), QStringLiteral("Show a workspace's Files or Git tab"), true,
        {workspaceArgSpec(),
         oneOf(QStringLiteral("view"), {QStringLiteral("files"), QStringLiteral("git")},
               QStringLiteral("files or git"), true)},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            dock->setVisible(true);
            dock->raise();
            if (ctx.str(QStringLiteral("view")) == QLatin1String("git")) {
                dock->showGitTab();
                if (GitTabWidget *git = dock->gitTabWidget())
                    ctx.reveal(git);
            } else {
                dock->revealAndSelectPath(dock->rootPath());
                ctx.reveal(dock);
            }
            ctx.ok(workspaceJson(w, dock));
        });

    add(QStringLiteral("workspace.reveal"),
        QStringLiteral("Select a file or folder in the workspace tree (expanding its folders)"), true,
        {required(QStringLiteral("path"), ArgType::String, QStringLiteral("absolute path, or relative to the active workspace"))},
        [w = w](const CommandContext &ctx) {
            const QString path = pathArg(w, ctx.str(QStringLiteral("path")));
            if (!w->revealInWorkspace(path)) {
                ctx.fail(404, QStringLiteral("no open workspace contains %1").arg(path));
                return;
            }
            if (FolderAsWorkspaceDock *dock = w->resolveShowInWorkspaceDock(path, true))
                ctx.reveal(dock);
            ctx.ok({{QStringLiteral("path"), path}});
        });

    add(QStringLiteral("workspace.files"),
        QStringLiteral("List the workspace's files (git-tracked and untracked, ignoring ignored ones), as Ctrl+P sees them"),
        false,
        {workspaceArgSpec(),
         optional(QStringLiteral("query"), ArgType::String, QStringLiteral("only paths containing this text (case-insensitive)")),
         optional(QStringLiteral("limit"), ArgType::Int, QStringLiteral("at most this many paths; default 500"))},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            const QString rootKey = QDir::cleanPath(dock->rootPath());
            const QString query = ctx.str(QStringLiteral("query"));
            const int limit = int(qBound(qint64(1), ctx.integer(QStringLiteral("limit"), 500), qint64(100000)));
            if (const auto it = w->m_fileIndexCache.constFind(rootKey); it != w->m_fileIndexCache.cend() && *it) {
                ctx.ok(filesResult(**it, query, limit));
                return;
            }
            if (!w->m_fileIndexEnumerator) {
                w->m_fileIndexEnumerator = new WorkspaceFileEnumerator(w);
                QObject::connect(w->m_fileIndexEnumerator, &WorkspaceFileEnumerator::indexReady, w,
                                 &MainWindow::onFileIndexReady);
            }
            // Answer from the index once it is built (a large repo takes a moment).
            auto *wait = new QObject(w);
            QObject::connect(w->m_fileIndexEnumerator, &WorkspaceFileEnumerator::indexReady, wait,
                             [ctx, rootKey, query, limit, wait](const QString &key, std::shared_ptr<const FileIndexCache> snapshot) {
                                 if (key != rootKey)
                                     return;
                                 wait->deleteLater();
                                 if (snapshot)
                                     ctx.ok(filesResult(*snapshot, query, limit));
                                 else
                                     ctx.fail(422, QStringLiteral("could not list the workspace's files"));
                             });
            QTimer::singleShot(60000, wait, [ctx, wait] {
                wait->deleteLater();
                ctx.fail(504, QStringLiteral("listing the files took over a minute"));
            });
            w->m_fileIndexEnumerator->enumerate(rootKey);
        });

    add(QStringLiteral("workspace.createFile"), QStringLiteral("Create a file in a workspace (and open it)"), true,
        {required(QStringLiteral("path"), ArgType::String, QStringLiteral("path relative to the workspace root, or absolute")),
         workspaceArgSpec(),
         optional(QStringLiteral("text"), ArgType::String, QStringLiteral("initial content; default empty")),
         optional(QStringLiteral("open"), ArgType::Bool, QStringLiteral("open it in a tab; default true"))},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            const QString path = inWorkspace(dock, ctx.str(QStringLiteral("path")));
            const QByteArray data = ctx.str(QStringLiteral("text")).toUtf8();
            const bool open = ctx.flag(QStringLiteral("open"), true);
            if (remote::isSshUri(path)) {
                remote::RemoteFsBackend *backend = dock->remoteBackend();
                if (!backend) {
                    ctx.fail(409, QStringLiteral("the SSH workspace is not connected"));
                    return;
                }
                const remote::SshUri uri = remote::parseSshUri(path);
                backend->writeFileAsync(uri.remotePath, data, [ctx, w = QPointer<MainWindow>(w), path, open](bool ok, const QString &error) {
                    if (!ok) {
                        ctx.fail(422, QStringLiteral("could not create %1: %2").arg(path, error));
                        return;
                    }
                    if (w && open)
                        w->openFile(path);
                    ctx.ok({{QStringLiteral("path"), path}});
                });
                return;
            }
            if (!underRoot(dock->rootPath(), path)) {
                ctx.fail(400, QStringLiteral("%1 is outside the workspace").arg(path));
                return;
            }
            if (QFileInfo::exists(path)) {
                ctx.fail(409, QStringLiteral("%1 already exists").arg(path));
                return;
            }
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
                ctx.fail(422, QStringLiteral("could not create %1: %2").arg(path, f.errorString()));
                return;
            }
            f.close();
            if (open) {
                w->openFile(path);
                if (ScintillaNext *editor = w->currentEditor())
                    ctx.reveal(editor);
            } else {
                dock->revealAndSelectPath(path);
                ctx.reveal(dock);
            }
            ctx.ok({{QStringLiteral("path"), path}});
        });

    add(QStringLiteral("workspace.createFolder"), QStringLiteral("Create a folder in a workspace"), true,
        {required(QStringLiteral("path"), ArgType::String, QStringLiteral("path relative to the workspace root, or absolute")),
         workspaceArgSpec()},
        [w = w](const CommandContext &ctx) {
            FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
            if (!dock)
                return;
            const QString path = inWorkspace(dock, ctx.str(QStringLiteral("path")));
            if (remote::isSshUri(path)) {
                remote::RemoteFsBackend *backend = dock->remoteBackend();
                if (!backend) {
                    ctx.fail(409, QStringLiteral("the SSH workspace is not connected"));
                    return;
                }
                backend->mkdirAsync(remote::parseSshUri(path).remotePath, [ctx, path](bool ok, const QString &error) {
                    if (ok)
                        ctx.ok({{QStringLiteral("path"), path}});
                    else
                        ctx.fail(422, QStringLiteral("could not create %1: %2").arg(path, error));
                });
                return;
            }
            if (!underRoot(dock->rootPath(), path)) {
                ctx.fail(400, QStringLiteral("%1 is outside the workspace").arg(path));
                return;
            }
            if (QFileInfo::exists(path)) {
                ctx.fail(409, QStringLiteral("%1 already exists").arg(path));
                return;
            }
            if (!QDir().mkpath(path)) {
                ctx.fail(422, QStringLiteral("could not create %1").arg(path));
                return;
            }
            dock->revealAndSelectPath(path);
            ctx.reveal(dock);
            ctx.ok({{QStringLiteral("path"), path}});
        });

    add(QStringLiteral("workspace.rename"),
        QStringLiteral("Rename or move a file or folder in a local workspace (open tabs follow it)"), true,
        {required(QStringLiteral("path"), ArgType::String, QStringLiteral("current path (absolute, or relative to the active workspace)")),
         required(QStringLiteral("to"), ArgType::String, QStringLiteral("new path (absolute, or relative to the active workspace)"))},
        [w = w](const CommandContext &ctx) {
            const QString from = pathArg(w, ctx.str(QStringLiteral("path")));
            const QString to = pathArg(w, ctx.str(QStringLiteral("to")));
            if (remote::isSshUri(from) || remote::isSshUri(to)) {
                ctx.fail(409, QStringLiteral("renaming in an SSH workspace is not supported by commands yet"));
                return;
            }
            const QFileInfo info(from);
            if (!info.exists()) {
                ctx.fail(404, QStringLiteral("no file or folder %1").arg(from));
                return;
            }
            QString error;
            if (!w->renameWorkspaceEntry(from, to, info.isDir(), &error)) {
                ctx.fail(422, error);
                return;
            }
            w->revealInWorkspace(to);
            if (FolderAsWorkspaceDock *dock = w->resolveShowInWorkspaceDock(to, true))
                ctx.reveal(dock);
            ctx.ok({{QStringLiteral("path"), to}});
        });

    add(QStringLiteral("workspace.delete"), QStringLiteral("Delete a file or folder in a workspace"), true,
        {required(QStringLiteral("path"), ArgType::String, QStringLiteral("path (absolute, or relative to the active workspace)")),
         optional(QStringLiteral("trash"), ArgType::Bool,
                  QStringLiteral("move it to the recycle bin instead of deleting (local only); default true"))},
        [w = w](const CommandContext &ctx) {
            const QString path = pathArg(w, ctx.str(QStringLiteral("path")));
            FolderAsWorkspaceDock *owner = nullptr;
            const QList<FolderAsWorkspaceDock *> docks =
                w->findChildren<FolderAsWorkspaceDock *>(QString(), Qt::FindDirectChildrenOnly);
            for (FolderAsWorkspaceDock *dock : docks) {
                const QString root = dock->rootPath();
                if (!root.isEmpty() && (remote::isSshUri(path) ? path.startsWith(root) : underRoot(root, path)))
                    owner = dock;
            }
            if (!owner) {
                ctx.fail(400, QStringLiteral("%1 is in no open workspace").arg(path));
                return;
            }
            if (QDir::cleanPath(owner->rootPath()) == path) {
                ctx.fail(400, QStringLiteral("that is the workspace root; close it with workspace.close"));
                return;
            }
            if (remote::isSshUri(path)) {
                remote::RemoteFsBackend *backend = owner->remoteBackend();
                if (!backend) {
                    ctx.fail(409, QStringLiteral("the SSH workspace is not connected"));
                    return;
                }
                const QString remotePath = remote::parseSshUri(path).remotePath;
                backend->statAsync(remotePath, [ctx, backend, remotePath, path](bool ok, const remote::FileStat &st,
                                                                              const QString &error) {
                    if (!ok) {
                        ctx.fail(404, QStringLiteral("%1: %2").arg(path, error));
                        return;
                    }
                    auto done = [ctx, path](bool removed, const QString &why) {
                        if (removed)
                            ctx.ok({{QStringLiteral("deleted"), path}});
                        else
                            ctx.fail(422, QStringLiteral("could not delete %1: %2").arg(path, why));
                    };
                    if (st.isDir)
                        backend->rmdirAsync(remotePath, done);
                    else
                        backend->unlinkAsync(remotePath, done);
                });
                return;
            }
            const QFileInfo info(path);
            if (!info.exists()) {
                ctx.fail(404, QStringLiteral("no file or folder %1").arg(path));
                return;
            }
            const bool ok = ctx.flag(QStringLiteral("trash"), true) ? QFile::moveToTrash(path)
                                                                     : owner->removeLocalPath(path, info.isDir());
            if (!ok) {
                ctx.fail(422, QStringLiteral("could not delete %1").arg(path));
                return;
            }
            ctx.reveal(owner);
            ctx.ok({{QStringLiteral("deleted"), path}});
        });

    add(QStringLiteral("ssh.profiles"), QStringLiteral("List the saved SSH connection profiles"), false, {},
        [w = w](const CommandContext &ctx) {
            QJsonArray list;
            for (const remote::SshProfile &p : w->app->getSshProfileRegistry()->profiles()) {
                list.append(QJsonObject{{QStringLiteral("id"), p.id},
                                        {QStringLiteral("host"), p.host},
                                        {QStringLiteral("port"), p.port},
                                        {QStringLiteral("user"), p.username},
                                        {QStringLiteral("lastPath"), p.lastRemotePath},
                                        {QStringLiteral("workspaceUri"),
                                         remote::formatSshUri(p.id, p.lastRemotePath.isEmpty() ? QStringLiteral("/")
                                                                                               : p.lastRemotePath)}});
            }
            ctx.ok({{QStringLiteral("profiles"), list}});
        });
}

} // namespace control::commands
