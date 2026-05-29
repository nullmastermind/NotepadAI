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

#ifndef WORKSPACE_FILE_ENUMERATOR_H
#define WORKSPACE_FILE_ENUMERATOR_H

#include <QByteArray>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

#include "FileIndexCache.h"

class GitProcessRunner;

// Enumerates a workspace's files OFF the UI thread and publishes an immutable
// FileIndexCache snapshot via indexReady().
//
// Two enumeration strategies, chosen at run time:
//
//   Git (GitProcessRunner::gitAvailable() && root is a git repo):
//     Three independent git streams, merged + deduped:
//       1. tracked:               git ls-files --recurse-submodules
//       2. untracked superproject: git ls-files --others --exclude-standard
//       3. untracked submodules:   git submodule --quiet foreach --recursive
//                                   '<ls-files --others>, each line prefixed
//                                    with $displaypath'
//     --recurse-submodules is incompatible with --others (git docs), so the
//     tracked and untracked passes are SEPARATE invocations. Gitignored files
//     never appear. Each git child process runs async (UI thread never blocks);
//     the merge+arena build runs on a worker thread.
//
//   Non-git fallback (first-class): a cancelable manual DFS that prunes
//     .git / node_modules / build* BEFORE descending (not after), checks an
//     atomic cancel flag every K entries, and builds the identical
//     FileIndexCache with isGitRepo=false.
//
// Lifetime / threading: indexReady is emitted on the UI thread (from the
// QFutureWatcher completion or a git callback). Connect it QUEUED. The worker
// task captures only value/shared_ptr copies — never `this` — so destroying
// this object mid-enumeration is safe (the detached task finishes harmlessly
// and its result is dropped via the generation token / dead watcher).
class WorkspaceFileEnumerator : public QObject
{
    Q_OBJECT
public:
    explicit WorkspaceFileEnumerator(QObject *parent = nullptr);
    ~WorkspaceFileEnumerator() override;

    // Begin a (re)enumeration of `root`. The emitted rootKey is
    // QDir::cleanPath(root). Cancels any prior in-flight run for this object.
    void enumerate(const QString &root);

    // Merge the three git streams and deduplicate. Exposed static + pure so it
    // is unit-testable without live git. A `--others` pass reports an untracked
    // submodule DIRECTORY as a single entry while the recursive stream yields
    // its contents; such directory placeholders are dropped (an entry E is
    // dropped iff another entry begins with E + "/"). Result is normalized to
    // '/' separators, exact-deduped, and sorted for deterministic ordering.
    static QStringList mergeAndDedupe(const QStringList &tracked,
                                      const QStringList &othersSuperproject,
                                      const QStringList &othersSubmodules);

signals:
    // QUEUED. snapshot is immutable (shared_ptr<const>).
    void indexReady(const QString &rootKey, std::shared_ptr<const FileIndexCache> snapshot);

private:
    struct Result {
        int generation = 0;
        QString rootKey;
        std::shared_ptr<const FileIndexCache> snapshot;
    };

    // git step chain
    void launchTracked();
    void launchOthersSuperproject();
    void launchOthersSubmodules();
    void finishGitOnWorker();

    // non-git fallback
    void launchDfs();
    static QStringList walkDfs(const QString &root,
                               const std::shared_ptr<std::atomic<bool>> &cancel);

    void startWorker(std::function<Result()> task);
    void onWorkerFinished();

    GitProcessRunner *m_runner = nullptr;
    QFutureWatcher<Result> *m_watcher = nullptr;

    int m_generation = 0;
    QString m_rootKey;
    QString m_rootPath;

    // Raw git stream outputs for the current generation (split off-thread).
    QByteArray m_rawTracked;
    QByteArray m_rawOthersSuper;
    QByteArray m_rawOthersSub;

    // Cancel token for the active DFS worker (shared with the detached task).
    std::shared_ptr<std::atomic<bool>> m_cancel;
};

Q_DECLARE_METATYPE(std::shared_ptr<const FileIndexCache>)

#endif // WORKSPACE_FILE_ENUMERATOR_H
