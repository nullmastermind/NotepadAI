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

#include "GitWatcher.h"

#include "GitRepoDiscovery.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

#ifdef Q_OS_WIN
#include "RecursiveTreeWatcher.h"
#endif

GitWatcher::GitWatcher(QObject *parent) : QObject(parent)
{
    m_fs = new QFileSystemWatcher(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(200);

    connect(m_fs, &QFileSystemWatcher::fileChanged, this, &GitWatcher::onFileChanged);
    connect(m_fs, &QFileSystemWatcher::directoryChanged, this, &GitWatcher::onDirChanged);
    connect(m_debounce, &QTimer::timeout, this, &GitWatcher::onDebounce);

#ifdef Q_OS_WIN
    m_treeWatcher = new RecursiveTreeWatcher(this);
    connect(m_treeWatcher, &RecursiveTreeWatcher::changed, this, [this]() {
        m_treeWatcher->ackNotify();
        m_pending |= PTree;
        m_debounce->start();
    });
#endif
}

GitWatcher::~GitWatcher() = default;

void GitWatcher::clear()
{
    if (!m_fs->files().isEmpty()) m_fs->removePaths(m_fs->files());
    if (!m_fs->directories().isEmpty()) m_fs->removePaths(m_fs->directories());
#ifdef Q_OS_WIN
    m_treeWatcher->stop();
    // Drop the prior repo's gitignore prefixes — they are repo-relative and
    // would be misapplied against a different root until the next refresh
    // repopulates them. Unfiltered for that brief window is the safe default.
    m_treeWatcher->setIgnoredPrefixes(QStringList());
#endif
    m_repoRoot.clear();
    m_gitDir.clear();
    m_auxGitDirs.clear();
    m_pending = 0;
}

void GitWatcher::setRepo(const QString &toplevel)
{
    const QStringList aux = m_auxGitDirs;
    clear();
    m_auxGitDirs = aux;
    if (toplevel.isEmpty()) return;
    m_repoRoot = QDir::cleanPath(toplevel);
    m_gitDir = GitRepoDiscovery::resolveGitDir(m_repoRoot);
    if (m_gitDir.isEmpty())
        m_gitDir = m_repoRoot + QStringLiteral("/.git");
    rewatch();
}

void GitWatcher::setAuxiliaryGitDirs(const QStringList &gitDirs)
{
    m_auxGitDirs.clear();
    m_auxGitDirs.reserve(gitDirs.size());
    for (const QString &d : gitDirs) {
        const QString clean = QDir::cleanPath(d);
        if (!clean.isEmpty() && clean != m_gitDir)
            m_auxGitDirs.append(clean);
    }
    if (m_gitDir.isEmpty()) return;
    if (!m_fs->directories().isEmpty())
        m_fs->removePaths(m_fs->directories());
    const QStringList dirs = currentWatchedDirs();
    if (!dirs.isEmpty()) m_fs->addPaths(dirs);
}

void GitWatcher::setIgnoredPrefixes(const QStringList &prefixes)
{
#ifdef Q_OS_WIN
    if (m_treeWatcher) m_treeWatcher->setIgnoredPrefixes(prefixes);
#else
    Q_UNUSED(prefixes);
#endif
}

QStringList GitWatcher::currentWatchedFiles() const
{
    QStringList files;
    if (m_gitDir.isEmpty()) return files;
    files.append(m_gitDir + QStringLiteral("/HEAD"));
    files.append(m_gitDir + QStringLiteral("/index"));
    files.append(m_gitDir + QStringLiteral("/packed-refs"));
    files.append(m_gitDir + QStringLiteral("/MERGE_HEAD"));
    files.append(m_gitDir + QStringLiteral("/REBASE_HEAD"));
    // Trim missing files; QFileSystemWatcher will warn loudly on misses.
    QStringList existing;
    for (const QString &f : files)
        if (QFile::exists(f)) existing.append(f);
    return existing;
}

QStringList GitWatcher::currentWatchedDirs() const
{
    QStringList dirs;
    if (m_gitDir.isEmpty()) return dirs;
    dirs.append(m_gitDir + QStringLiteral("/refs/heads"));
    dirs.append(m_gitDir + QStringLiteral("/refs/remotes"));
    dirs.append(m_gitDir + QStringLiteral("/rebase-merge"));
    dirs.append(m_gitDir + QStringLiteral("/rebase-apply"));
    const auto appendWatchDirs = [&](const QString &gitDir) {
        for (const QString &d : GitRepoDiscovery::worktreeWatchDirs(gitDir))
            dirs.append(d);
    };
    appendWatchDirs(m_gitDir);
    for (const QString &aux : m_auxGitDirs)
        appendWatchDirs(aux);
#ifndef Q_OS_WIN
    // On non-Windows, fall back to watching the repo root (non-recursive).
    dirs.append(m_repoRoot);
#endif
    QStringList existing;
    for (const QString &d : dirs)
        if (QFileInfo(d).isDir()) existing.append(d);
    return existing;
}

void GitWatcher::rewatch()
{
    const QStringList files = currentWatchedFiles();
    const QStringList dirs = currentWatchedDirs();
    if (!files.isEmpty()) m_fs->addPaths(files);
    if (!dirs.isEmpty()) m_fs->addPaths(dirs);
#ifdef Q_OS_WIN
    if (!m_repoRoot.isEmpty())
        m_treeWatcher->start(m_repoRoot);
#endif
}

void GitWatcher::onFileChanged(const QString &path)
{
    if (path.endsWith(QStringLiteral("/HEAD")))              m_pending |= PHead;
    else if (path.endsWith(QStringLiteral("/index")))        m_pending |= PIndex;
    else if (path.endsWith(QStringLiteral("/packed-refs")))  m_pending |= PRefs;
    else if (path.endsWith(QStringLiteral("/MERGE_HEAD")) ||
             path.endsWith(QStringLiteral("/REBASE_HEAD")))  m_pending |= POpState;
    else                                                     m_pending |= PTree;
    // QFileSystemWatcher stops watching a file after it's atomically replaced
    // (common with git); re-add to keep tracking.
    if (!m_fs->files().contains(path) && QFile::exists(path)) m_fs->addPath(path);
    m_debounce->start();
}

void GitWatcher::onDirChanged(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (GitRepoDiscovery::isWorktreeRegistryPath(clean)) {
        m_pending |= PWorktrees;
    } else if (path.endsWith(QStringLiteral("/refs/heads")) ||
               path.endsWith(QStringLiteral("/refs/remotes"))) {
        m_pending |= PRefs;
    } else if (path.endsWith(QStringLiteral("/rebase-merge")) ||
               path.endsWith(QStringLiteral("/rebase-apply"))) {
        m_pending |= POpState;
    } else {
        QString matchedRegistry;
        const auto consider = [&](const QString &gitDir) {
            if (gitDir.isEmpty() || !matchedRegistry.isEmpty()) return;
            const QString registry = GitRepoDiscovery::worktreeRegistryDir(gitDir);
            if (clean == QFileInfo(registry).path())
                matchedRegistry = registry;
        };
        consider(m_gitDir);
        for (const QString &aux : m_auxGitDirs)
            consider(aux);
        if (!matchedRegistry.isEmpty()) {
            bool watched = false;
            for (const QString &d : m_fs->directories()) {
                if (QDir::cleanPath(d) == matchedRegistry) { watched = true; break; }
            }
            const bool exists = QFileInfo(matchedRegistry).isDir();
            if (exists && !watched) {
                m_fs->addPath(matchedRegistry);
                m_pending |= PWorktrees;
            } else if (!exists && watched) {
                m_pending |= PWorktrees;
            }
        } else {
            m_pending |= PTree;
        }
    }
    if (m_pending) m_debounce->start();
}

void GitWatcher::onDebounce()
{
    const int p = m_pending;
    m_pending = 0;
    if (p & PHead)    emit headChanged();
    if (p & PIndex)   emit indexChanged();
    if (p & PRefs)    emit refsChanged();
    if (p & PTree)    emit workingTreeChanged();
    if (p & POpState) emit operationStateFileChanged();
    if (p & PWorktrees) emit worktreesChanged();
}
