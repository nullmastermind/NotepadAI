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

#include "TerminalCwdResolver.h"

#include "remote/ExecutionContext.h"
#include "remote/SshProfile.h"

#include <QDir>
#include <QFileInfo>

static bool directoryExists(const QString &path)
{
    if (path.isEmpty() || remote::isSshUri(path)) {
        return false;
    }
    QFileInfo info(path);
    return info.exists() && info.isDir();
}

bool TerminalCwdResolver::canOpenInWorkspace(const QString &workspaceRoot)
{
    return directoryExists(workspaceRoot);
}

bool TerminalCwdResolver::canOpenInFolder(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    if (activeBufferIsFile && !activeFilePath.isEmpty() && !remote::isSshUri(activeFilePath)) {
        const QString parent = QFileInfo(activeFilePath).absolutePath();
        if (directoryExists(parent)) {
            return true;
        }
    }
    return directoryExists(workspaceRoot);
}

QString TerminalCwdResolver::resolveWorkspace(const QString &workspaceRoot)
{
    if (directoryExists(workspaceRoot)) {
        return QDir::cleanPath(workspaceRoot);
    }
    return QString();
}

QString TerminalCwdResolver::resolveFolder(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    if (activeBufferIsFile && !activeFilePath.isEmpty() && !remote::isSshUri(activeFilePath)) {
        const QString parent = QFileInfo(activeFilePath).absolutePath();
        if (directoryExists(parent)) {
            return QDir::cleanPath(parent);
        }
    }
    if (directoryExists(workspaceRoot)) {
        return QDir::cleanPath(workspaceRoot);
    }
    return QString();
}

static QString posixParentPath(QString path)
{
    if (path.isEmpty()) {
        return QString();
    }
    if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? path.left(slash) : QStringLiteral("/");
}

static QString remoteWorkspacePath(const QString &workspaceRoot)
{
    const remote::SshUri uri = remote::parseSshUri(workspaceRoot);
    return uri.valid ? uri.remotePath : QString();
}

static QString remoteFolderPath(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    const remote::SshUri workspaceUri = remote::parseSshUri(workspaceRoot);
    if (!workspaceUri.valid) {
        return QString();
    }
    if (activeBufferIsFile && remote::isSshUri(activeFilePath)) {
        const remote::SshUri fileUri = remote::parseSshUri(activeFilePath);
        if (fileUri.valid && fileUri.profileId == workspaceUri.profileId) {
            return posixParentPath(fileUri.remotePath);
        }
    }
    return workspaceUri.remotePath;
}

bool TerminalCwdResolver::canOpenInWorkspaceForContext(remote::ExecutionContext *ctx, const QString &workspaceRoot)
{
    if (!ctx || !ctx->isRemote()) {
        return canOpenInWorkspace(workspaceRoot);
    }
    return !resolveWorkspaceForContext(ctx, workspaceRoot).isEmpty();
}

bool TerminalCwdResolver::canOpenInFolderForContext(remote::ExecutionContext *ctx, const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    if (!ctx || !ctx->isRemote()) {
        return canOpenInFolder(activeFilePath, activeBufferIsFile, workspaceRoot);
    }
    return !resolveFolderForContext(ctx, activeFilePath, activeBufferIsFile, workspaceRoot).isEmpty();
}

QString TerminalCwdResolver::resolveWorkspaceForContext(remote::ExecutionContext *ctx, const QString &workspaceRoot)
{
    if (!ctx || !ctx->isRemote()) {
        return resolveWorkspace(workspaceRoot);
    }
    return resolveForContext(ctx, remoteWorkspacePath(workspaceRoot));
}

QString TerminalCwdResolver::resolveFolderForContext(remote::ExecutionContext *ctx, const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    if (!ctx || !ctx->isRemote()) {
        return resolveFolder(activeFilePath, activeBufferIsFile, workspaceRoot);
    }
    return resolveForContext(ctx, remoteFolderPath(activeFilePath, activeBufferIsFile, workspaceRoot));
}

QString TerminalCwdResolver::resolveForContext(remote::ExecutionContext *ctx, const QString &requested)
{
    // Local / no context → the existing local behavior verbatim. (A null ctx is
    // treated as local; this keeps callers that have no context wired yet on
    // the identical path.)
    if (!ctx || !ctx->isRemote()) {
        return resolveWorkspace(requested);
    }

    // Remote: never stat the path against the LOCAL disk (it lives on another
    // machine). Require the connection to be live and the path non-empty, then
    // delegate to the context's own POSIX normalization (design D11). The caller
    // supplies the remote default (lastRemotePath or "~") as `requested`.
    if (ctx->state() != remote::ExecutionContext::State::Connected) {
        return QString();
    }
    if (requested.isEmpty()) {
        return QString();
    }
    return ctx->resolveCwd(requested);
}

static Qt::CaseSensitivity localPathCs()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

static bool pathIsUnder(const QString &child, const QString &root, Qt::CaseSensitivity cs)
{
    if (root.isEmpty() || child.isEmpty())
        return false;
    if (child.compare(root, cs) == 0)
        return true;
    if (root == QLatin1Char('/'))
        return child.startsWith(QLatin1Char('/'));
    return child.startsWith(root + QLatin1Char('/'), cs);
}

static QString posixComparable(const QString &path)
{
    if (remote::isSshUri(path)) {
        const remote::SshUri uri = remote::parseSshUri(path);
        return uri.valid ? QDir::cleanPath(uri.remotePath) : QString();
    }
    return QDir::cleanPath(path);
}

bool TerminalCwdResolver::workspaceRootsEqual(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    if (remote::isSshUri(a) || remote::isSshUri(b)) {
        const remote::SshUri ua = remote::parseSshUri(a);
        const remote::SshUri ub = remote::parseSshUri(b);
        return ua.valid && ub.valid && ua.profileId == ub.profileId
            && QDir::cleanPath(ua.remotePath) == QDir::cleanPath(ub.remotePath);
    }
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), localPathCs()) == 0;
}


bool TerminalCwdResolver::cwdBelongsToWorkspace(const QString &cwd, CwdSpace space, const QString &workspaceRoot)
{
    if (cwd.isEmpty() || workspaceRoot.isEmpty())
        return false;
    if (space == CwdSpace::Local) {
        if (remote::isSshUri(workspaceRoot) || remote::isSshUri(cwd))
            return false;
        return pathIsUnder(QDir::cleanPath(cwd), QDir::cleanPath(workspaceRoot), localPathCs());
    }
    if (!remote::isSshUri(workspaceRoot))
        return false;
    const remote::SshUri uri = remote::parseSshUri(workspaceRoot);
    if (!uri.valid)
        return false;
    return pathIsUnder(posixComparable(cwd), QDir::cleanPath(uri.remotePath), Qt::CaseSensitive);
}

QString TerminalCwdResolver::matchingWorkspace(const QString &cwd, CwdSpace space, const QStringList &workspaceRoots)
{
    QString best;
    int bestLen = -1;
    for (const QString &root : workspaceRoots) {
        if (!cwdBelongsToWorkspace(cwd, space, root))
            continue;
        const QString comparable = (space == CwdSpace::Remote)
            ? posixComparable(root)
            : QDir::cleanPath(root);
        const int len = comparable.size();
        if (len > bestLen) {
            bestLen = len;
            best = root;
        }
    }
    return best;
}

QList<int> TerminalCwdResolver::visibleTerminalIndices(const QStringList &cwds,
                                                       const QList<CwdSpace> &spaces,
                                                       const QStringList &workspaceRoots,
                                                       const QString &activeWorkspaceRoot)
{
    const int n = cwds.size();
    QList<int> all;
    all.reserve(n);
    for (int i = 0; i < n; ++i)
        all.append(i);
    if (n == 0 || activeWorkspaceRoot.isEmpty() || workspaceRoots.isEmpty() || spaces.size() != n)
        return all;

    bool anyUnmatched = false;
    QList<int> matchedActive;
    matchedActive.reserve(n);
    for (int i = 0; i < n; ++i) {
        const QString owner = matchingWorkspace(cwds.at(i), spaces.at(i), workspaceRoots);
        if (owner.isEmpty()) {
            anyUnmatched = true;
            continue;
        }
        if (workspaceRootsEqual(owner, activeWorkspaceRoot))
            matchedActive.append(i);
    }
    if (anyUnmatched)
        return all;
    return matchedActive;
}

bool TerminalCwdResolver::terminalTabWanted(const QString &cwd, CwdSpace space,
                                            const QStringList &openWorkspaceRoots,
                                            const QString &activeWorkspaceRoot,
                                            bool anyUnmatched)
{
    if (anyUnmatched || activeWorkspaceRoot.isEmpty() || openWorkspaceRoots.isEmpty())
        return true;
    return workspaceRootsEqual(matchingWorkspace(cwd, space, openWorkspaceRoots),
                               activeWorkspaceRoot);
}


QList<int> TerminalCwdResolver::closeTerminalIndices(const QStringList &cwds,
                                                     const QList<CwdSpace> &spaces,
                                                     const QStringList &openWorkspaceRoots,
                                                     const QString &closingWorkspaceRoot)
{
    QList<int> out;
    const int n = cwds.size();
    if (closingWorkspaceRoot.isEmpty() || n == 0 || spaces.size() != n)
        return out;

    const QStringList *roots = &openWorkspaceRoots;
    QStringList withClosing;
    bool hasClosing = false;
    for (const QString &root : openWorkspaceRoots) {
        if (workspaceRootsEqual(root, closingWorkspaceRoot)) {
            hasClosing = true;
            break;
        }
    }
    if (!hasClosing) {
        withClosing = openWorkspaceRoots;
        withClosing.append(closingWorkspaceRoot);
        roots = &withClosing;
    }

    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const QString owner = matchingWorkspace(cwds.at(i), spaces.at(i), *roots);
        if (workspaceRootsEqual(owner, closingWorkspaceRoot))
            out.append(i);
    }
    return out;
}

