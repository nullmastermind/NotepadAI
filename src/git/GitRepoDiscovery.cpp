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

#include "GitRepoDiscovery.h"

#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace {
// String literals, not QByteArray: a throwing static initializer trips
// bugprone-throwing-static-initialization, and these compares are allocation-free.
constexpr char kWorktreePrefix[] = "worktree ";
constexpr qsizetype kWorktreePrefixLen = qsizetype(sizeof(kWorktreePrefix) - 1);
constexpr char kBranchPrefix[] = "branch ";
constexpr qsizetype kBranchPrefixLen = qsizetype(sizeof(kBranchPrefix) - 1);
constexpr char kHeadsPrefix[] = "refs/heads/";
constexpr qsizetype kHeadsPrefixLen = qsizetype(sizeof(kHeadsPrefix) - 1);
constexpr char kDetached[] = "detached";
} // namespace

GitRepoInfos GitRepoDiscovery::parseSubmoduleStatus(const QByteArray &out, const QString &rootToplevel)
{
    GitRepoInfos result;
    const QString rootClean = QDir::cleanPath(rootToplevel);

    const QList<QByteArray> lines = out.split('\n');
    for (const QByteArray &raw : lines) {
        QString line = QString::fromUtf8(raw);
        if (line.isEmpty()) continue;

        // first char is status marker (' ', '+', '-', 'U'), skip it
        line = line.mid(1);
        // next: 40-char sha + space, skip 41 chars
        if (line.size() < 41) continue;
        line = line.mid(41);
        // trim trailing " (ref)" annotation
        const int paren = line.lastIndexOf(QLatin1Char(' '));
        if (paren > 0 && line.endsWith(QLatin1Char(')')))
            line = line.left(paren);

        const QString relPath = line.trimmed();
        if (relPath.isEmpty()) continue;

        const QString abs = QDir::cleanPath(rootClean + QLatin1Char('/') + relPath);
        const int depth = relPath.count(QLatin1Char('/')) + 1;

        GitRepoInfo info;
        info.toplevel = abs;
        info.displayName = relPath;
        info.depth = depth;
        info.isSubmodule = true;
        result.append(info);
    }
    return result;
}

GitRepoInfos GitRepoDiscovery::parseWorktreeList(const QByteArray &out, const QString &rootToplevel)
{
    return parseWorktrees(out, rootToplevel).linked;
}

GitWorktreeParse GitRepoDiscovery::parseWorktrees(const QByteArray &out, const QString &rootToplevel)
{
    GitWorktreeParse parsed;
    const QString rootClean = QDir::cleanPath(rootToplevel);

    QString path;
    QString branch;
    bool have = false;
    bool primary = true;

    const auto flush = [&]() {
        if (!have) return;
        const QString clean = QDir::cleanPath(path);
        const bool isPrimary = primary;
        const QString savedBranch = branch;
        primary = false;
        have = false;
        path.clear();
        branch.clear();
        if (clean.isEmpty()) return;
        if (isPrimary) {
            parsed.mainToplevel = clean;
            parsed.mainBranch = savedBranch;
        }
        if (clean == rootClean) return;
        if (isGitDirPath(clean)) return;

        GitRepoInfo info;
        info.toplevel = clean;
        info.displayName = QFileInfo(clean).fileName();
        info.isSubmodule = false;
        info.isWorktree = !isPrimary;
        info.depth = info.isWorktree ? 1 : 0;
        info.branch = savedBranch;
        if (info.isWorktree)
            info.parentToplevel = rootClean;
        parsed.linked.append(info);
    };

    const QList<QByteArray> lines = out.split('\n');
    for (QByteArray raw : lines) {
        if (raw.endsWith('\r')) raw.chop(1);
        if (raw.isEmpty()) {
            flush();
            continue;
        }
        if (raw.startsWith(QByteArrayView(kWorktreePrefix, kWorktreePrefixLen))) {
            flush();
            path = QString::fromUtf8(raw.mid(kWorktreePrefixLen));
            have = true;
            continue;
        }
        if (raw.startsWith(QByteArrayView(kBranchPrefix, kBranchPrefixLen))) {
            QByteArray ref = raw.mid(kBranchPrefixLen);
            if (ref.startsWith(QByteArrayView(kHeadsPrefix, kHeadsPrefixLen)))
                ref = ref.mid(kHeadsPrefixLen);
            branch = QString::fromUtf8(ref);
            continue;
        }
        if (raw == QByteArrayView(kDetached))
            branch.clear();
    }
    flush();
    return parsed;
}

QString GitRepoDiscovery::parseMainWorktreePath(const QByteArray &out)
{
    return parseWorktrees(out, QString()).mainToplevel;
}

QString GitRepoDiscovery::worktreeRegistryDir(const QString &gitDir)
{
    QString clean = QDir::cleanPath(gitDir);
    const QString suffix = QStringLiteral("/worktrees");
    if (clean.endsWith(suffix))
        return clean;
    const QString marker = suffix + QLatin1Char('/');
    const int at = clean.lastIndexOf(marker);
    if (at >= 0)
        return clean.left(at + suffix.size());
    return clean + suffix;
}

bool GitRepoDiscovery::isGitDirPath(const QString &path)
{
    return QDir::cleanPath(path).contains(QLatin1String("/.git/"));
}

bool GitRepoDiscovery::isWorktreeRegistryPath(const QString &path)
{
    return QDir::cleanPath(path).endsWith(QStringLiteral("/worktrees"));
}

QStringList GitRepoDiscovery::worktreeWatchDirs(const QString &gitDir)
{
    const QString registry = worktreeRegistryDir(gitDir);
    return { registry, QFileInfo(registry).path() };
}

QString GitRepoDiscovery::resolveGitDir(const QString &toplevel)
{
    const QString root = QDir::cleanPath(toplevel);
    if (root.isEmpty()) return {};
    QString dotGitEntry = root + QStringLiteral("/.git");
    QFileInfo fi(dotGitEntry);
    if (fi.isDir())
        return dotGitEntry;
    if (fi.isFile()) {
        QFile f(dotGitEntry);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString contents = QString::fromUtf8(f.readAll()).trimmed();
            const QString prefix = QStringLiteral("gitdir: ");
            if (contents.startsWith(prefix)) {
                QString gitDir = contents.mid(prefix.size()).trimmed();
                if (!QFileInfo(gitDir).isAbsolute())
                    gitDir = QDir::cleanPath(root + QLatin1Char('/') + gitDir);
                return QDir::cleanPath(gitDir);
            }
        }
    }
    return dotGitEntry;
}

bool GitRepoDiscovery::worktreesUnchanged(const GitRepoInfos &existing, const GitRepoInfos &linked,
                                          const QString &ownerToplevel)
{
    const QString owner = QDir::cleanPath(ownerToplevel);
    QStringList a;
    QStringList b;
    a.reserve(existing.size());
    b.reserve(linked.size());
    for (const auto &r : existing) {
        if (!r.isWorktree) continue;
        if (!owner.isEmpty() && QDir::cleanPath(r.parentToplevel) != owner) continue;
        a.append(r.toplevel + QLatin1Char('\0') + r.branch);
    }
    for (const auto &r : linked) {
        if (!r.isWorktree) continue;
        b.append(r.toplevel + QLatin1Char('\0') + r.branch);
    }
    a.sort();
    b.sort();
    return a == b;
}

GitRepoInfos GitRepoDiscovery::replaceOwnedWorktrees(const GitRepoInfos &existing,
                                                     const QString &ownerToplevel,
                                                     const GitRepoInfos &linked)
{
    const QString owner = QDir::cleanPath(ownerToplevel);
    int ownerDepth = 0;
    for (const auto &r : existing) {
        if (QDir::cleanPath(r.toplevel) == owner)
            ownerDepth = r.depth;
    }

    const auto annotate = [&](GitRepoInfo row) {
        row.depth = ownerDepth + 1;
        if (row.parentToplevel.isEmpty())
            row.parentToplevel = owner;
        return row;
    };

    GitRepoInfos kept;
    kept.reserve(existing.size() + linked.size());
    bool inserted = false;
    for (const auto &r : existing) {
        if (r.isWorktree && QDir::cleanPath(r.parentToplevel) == owner)
            continue;
        kept.append(r);
        if (!inserted && QDir::cleanPath(r.toplevel) == owner) {
            for (const auto &item : linked)
                kept.append(annotate(item));
            inserted = true;
        }
    }
    if (!inserted) {
        for (const auto &item : linked)
            kept.append(annotate(item));
    }
    return kept;
}

QString GitRepoDiscovery::ownerCwdForWorktree(const GitRepoInfos &repos, const QString &path,
                                              const QString &fallback)
{
    const QString clean = QDir::cleanPath(path);
    for (const auto &r : repos) {
        if (QDir::cleanPath(r.toplevel) == clean && !r.parentToplevel.isEmpty())
            return QDir::cleanPath(r.parentToplevel);
    }
    return QDir::cleanPath(fallback);
}

bool GitRepoDiscovery::worktreeMatchesFilter(const GitRepoInfo &info, const QString &needle)
{
    if (!info.isWorktree) return false;
    if (needle.isEmpty()) return true;
    if (info.displayName.contains(needle, Qt::CaseInsensitive)
        || info.branch.contains(needle, Qt::CaseInsensitive)
        || info.toplevel.contains(needle, Qt::CaseInsensitive))
        return true;
    if (info.parentToplevel.isEmpty()) return false;
    if (info.parentToplevel.contains(needle, Qt::CaseInsensitive)) return true;
    return QFileInfo(info.parentToplevel).fileName().contains(needle, Qt::CaseInsensitive);
}

GitRepoInfos GitRepoDiscovery::filteredWorktrees(const GitRepoInfos &repos, const QString &needle)
{
    const QString q = needle.trimmed();
    GitRepoInfos out;
    out.reserve(repos.size());
    for (const auto &r : repos) {
        if (worktreeMatchesFilter(r, q))
            out.append(r);
    }
    return out;
}

QString GitRepoDiscovery::worktreeMergeTargetName(const GitRepoInfo &info, const QString &fallback)
{
    if (!info.parentToplevel.isEmpty())
        return QFileInfo(info.parentToplevel).fileName();
    return fallback;
}

QStringList GitRepoDiscovery::worktreeRemoveArgv(const QString &mainCwd, const QString &path, bool force)
{
    QStringList argv{ QStringLiteral("-C"), QDir::cleanPath(mainCwd),
                      QStringLiteral("worktree"), QStringLiteral("remove") };
    if (force) argv.append(QStringLiteral("--force"));
    argv.append(QDir::cleanPath(path));
    return argv;
}

QStringList GitRepoDiscovery::worktreeMergeArgv(const QString &mainCwd, const QString &branch)
{
    return { QStringLiteral("-C"), QDir::cleanPath(mainCwd),
             QStringLiteral("merge"), QStringLiteral("--no-edit"), branch };
}

QStringList GitRepoDiscovery::statusArgv(const QString &cwd)
{
    return { QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
             QStringLiteral("-C"), QDir::cleanPath(cwd),
             QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
             QStringLiteral("--branch"), QStringLiteral("-z"),
             QStringLiteral("--untracked-files=all"), QStringLiteral("--renames") };
}

QStringList GitRepoDiscovery::stageArgv(const QString &cwd)
{
    return { QStringLiteral("-C"), QDir::cleanPath(cwd),
             QStringLiteral("add"), QStringLiteral("--") };
}

QStringList GitRepoDiscovery::commitArgv(const QString &cwd)
{
    return { QStringLiteral("-c"), QStringLiteral("i18n.commitEncoding=UTF-8"),
             QStringLiteral("-C"), QDir::cleanPath(cwd),
             QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-") };
}

QStringList GitRepoDiscovery::diffArgv(const QString &cwd, bool stagedSide)
{
    QStringList argv{ QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                      QStringLiteral("-C"), QDir::cleanPath(cwd),
                      QStringLiteral("diff") };
    if (stagedSide) argv.append(QStringLiteral("--cached"));
    argv.append({ QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
                  QStringLiteral("--src-prefix=a/"), QStringLiteral("--dst-prefix=b/"),
                  QStringLiteral("--") });
    return argv;
}

QString GitRepoDiscovery::nextRepoAfterRemove(const QString &current, const QString &removed, const QString &main)
{
    if (QDir::cleanPath(current) == QDir::cleanPath(removed))
        return QDir::cleanPath(main);
    return QDir::cleanPath(current);
}

QString GitRepoDiscovery::nextRepoAfterMergeConflict(const QString &main)
{
    return QDir::cleanPath(main);
}

QString GitRepoDiscovery::nextRepoAfterFailedRemove(const QString &current)
{
    return QDir::cleanPath(current);
}

bool GitRepoDiscovery::worktreeFailureSwitchesToMain(GitError::Kind kind)
{
    return kind == GitError::MergeConflict;
}

QString GitRepoDiscovery::fallbackRepo(const GitRepoInfos &repos, const QString &current, const QString &main)
{
    QString cur = QDir::cleanPath(current);
    for (const auto &r : repos) {
        if (r.toplevel == cur) return cur;
    }
    return QDir::cleanPath(main);
}

GitRepoInfos GitRepoDiscovery::dropMissingLocalCheckouts(const GitRepoInfos &repos)
{
    GitRepoInfos out;
    out.reserve(repos.size());
    for (const auto &r : repos) {
        if (!r.toplevel.isEmpty() && QDir(r.toplevel).exists())
            out.append(r);
    }
    return out;
}
