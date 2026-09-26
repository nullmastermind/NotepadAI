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

#ifndef GIT_REPO_DISCOVERY_H
#define GIT_REPO_DISCOVERY_H

#include "GitError.h"
#include "GitRepoInfo.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

struct GitWorktreeParse
{
    QString mainToplevel;
    QString mainBranch;
    GitRepoInfos linked;
};

class GitRepoDiscovery
{
public:
    // Parses output of `git submodule status --recursive`. Each line:
    //   " 1234abcd path/to/sub (heads/main)"     # in sync
    //   "+1234abcd path/to/sub (heads/main)"     # different commit checked out
    //   "-1234abcd path/to/sub"                  # not initialised
    //   "U1234abcd path/to/sub"                  # merge conflict
    // "path/to/sub" is RELATIVE to the root repo.
    static GitRepoInfos parseSubmoduleStatus(const QByteArray &out, const QString &rootToplevel);

    // Parses `git worktree list --porcelain`. Returns every linked worktree
    // except <rootToplevel> (already shown as the combo root). The first
    // porcelain record is the primary checkout.
    static GitRepoInfos parseWorktreeList(const QByteArray &out, const QString &rootToplevel);
    static GitWorktreeParse parseWorktrees(const QByteArray &out, const QString &rootToplevel);
    static QString parseMainWorktreePath(const QByteArray &out);
    // Git's linked-worktree registry (`<common-git-dir>/worktrees`), whether
    // `gitDir` is the main `.git` or a linked checkout's gitdir.
    static QString worktreeRegistryDir(const QString &gitDir);
    static bool isGitDirPath(const QString &path);
    static bool isWorktreeRegistryPath(const QString &path);
    static QStringList worktreeWatchDirs(const QString &gitDir);
    static QString resolveGitDir(const QString &toplevel);
    static bool worktreesUnchanged(const GitRepoInfos &existing, const GitRepoInfos &linked,
                                  const QString &ownerToplevel = {});
    // Drop worktrees owned by `ownerToplevel`, then append `linked`. Other
    // checkouts and other parents' worktrees stay.
    static GitRepoInfos replaceOwnedWorktrees(const GitRepoInfos &existing,
                                             const QString &ownerToplevel,
                                             const GitRepoInfos &linked);
    static QString ownerCwdForWorktree(const GitRepoInfos &repos, const QString &path,
                                      const QString &fallback);
    static bool worktreeMatchesFilter(const GitRepoInfo &info, const QString &needle);
    static GitRepoInfos filteredWorktrees(const GitRepoInfos &repos, const QString &needle);
    static QString worktreeMergeTargetName(const GitRepoInfo &info, const QString &fallback);

    static QStringList worktreeRemoveArgv(const QString &mainCwd, const QString &path, bool force);
    static QStringList worktreeMergeArgv(const QString &mainCwd, const QString &branch);
    static QStringList statusArgv(const QString &cwd);
    static QStringList stageArgv(const QString &cwd);
    static QStringList commitArgv(const QString &cwd);
    static QStringList diffArgv(const QString &cwd, bool stagedSide);
    static QString nextRepoAfterRemove(const QString &current, const QString &removed, const QString &main);
    static QString nextRepoAfterMergeConflict(const QString &main);
    static QString nextRepoAfterFailedRemove(const QString &current);
    static bool worktreeFailureSwitchesToMain(GitError::Kind kind);

    // If `current` is still in `repos`, return it; otherwise return `main`.
    static QString fallbackRepo(const GitRepoInfos &repos, const QString &current, const QString &main);

    // Drop checkouts whose directory is gone. `git worktree list` keeps listing
    // them until prune; the combo must not.
    static GitRepoInfos dropMissingLocalCheckouts(const GitRepoInfos &repos);
};

#endif // GIT_REPO_DISCOVERY_H
