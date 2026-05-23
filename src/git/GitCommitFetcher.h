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

#ifndef GIT_COMMIT_FETCHER_H
#define GIT_COMMIT_FETCHER_H

#include "GitCommitDetail.h"

#include <QByteArray>
#include <QCache>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

class GitProcessRunner;
class GitWatcher;

// Two-stage fetch for the commit detail tab:
//   1. `git -c color.ui=never show --no-patch --format=…<sha>` for the header
//      and full message body.
//   2. `git -c color.ui=never show --numstat -z --format= <sha>` for the
//      file list with binary detection (numstat uses `-\t-\t<path>` for
//      binaries — re-uses GitNumstatParser).
//   3. `git -c color.ui=never show --first-parent --no-color -p
//      --format= <sha>` for the actual unified diff bytes.
//
// One in-flight fetch at a time per fetcher. Generation token guards stale
// callbacks. LRU cache holds up to 16 fully-built CommitDetail; in-memory
// only (no disk persistence per J2).
class GitCommitFetcher : public QObject
{
    Q_OBJECT
public:
    explicit GitCommitFetcher(QObject *parent = nullptr);
    ~GitCommitFetcher() override;

    using Callback = std::function<void(std::shared_ptr<const GitCommitDetail>)>;

    // Configure the repository toplevel. Reconfiguring with a different value
    // cancels any in-flight fetch and invalidates the cache (cache is keyed
    // per-fetcher, so this means "fresh cache for new repo").
    void setRepoRoot(const QString &repoToplevel);
    QString repoRoot() const { return m_repoRoot; }

    // Invalidate the cache. Called when GitWatcher reports HEAD or refs
    // change (force-push/rebase can leave SHAs valid but pointing to GC'd
    // objects in a stale cache).
    void invalidateAll();

    // Fetch a single commit detail. On cache hit, callback fires in the next
    // event-loop tick with the cached object. On miss, spawns 3 git commands
    // sequentially and invokes the callback with either a complete detail or
    // an empty (sha-cleared) detail on failure.
    //
    // truncated=true if the diff bytes hit the 5MB cap.
    void requestDetail(const QByteArray &sha, Callback cb);

    // Force re-fetch ignoring the cap (called when user clicks "Show all" on
    // a truncated detail).
    void requestDetailUncapped(const QByteArray &sha, Callback cb);

    // Cancel the in-flight fetch (no callback fires).
    void cancel();

    // Hook GitWatcher → invalidateAll on HEAD/refs change.
    void connectWatcher(GitWatcher *watcher);

signals:
    // Diagnostic — most callers use the per-request Callback. Emitted on
    // completion (success or failure) of any request.
    void requestCompleted(const QByteArray &sha, bool ok);

private:
    // Per-request inflight state (one at a time).
    struct InFlight {
        QByteArray sha;
        Callback   cb;
        std::shared_ptr<GitCommitDetail> partial;  // built across the 3 calls
        int        step = 0;                       // 0=show-header,1=numstat,2=patch
        bool       uncapped = false;
        quint64    generation = 0;
    };
    void launchHeader(InFlight &state);
    void launchNumstat(InFlight &state);
    void launchPatch(InFlight &state);
    void completeOk(InFlight &state);
    void completeFail(InFlight &state);

    // git -c color.ui=never show --no-patch --format=<fmt> <sha>
    // Format string parsed by parseHeader:
    //   %H%x1f%P%x1f%an%x1f%ae%x1f%at%x1f%cn%x1f%ce%x1f%ct%x1f%s%x1f%B%x1e
    static void parseHeader(const QByteArray &bytes, GitCommitDetail &out);
    // Body trailer parsing — split body into main message + trailing
    // "Key: value" cluster.
    static void parseTrailers(GitCommitDetail &out);

    QString          m_repoRoot;
    GitProcessRunner *m_runner = nullptr;

    // LRU cache. QCache uses a "cost" budget; here every entry costs 1 so
    // the budget is the row count.
    QCache<QByteArray, std::shared_ptr<const GitCommitDetail>> m_cache;
    static constexpr int kCacheCapacity = 16;
    static constexpr qint64 kDefaultCapBytes = 5 * 1024 * 1024;

    quint64  m_generation = 0;
    std::unique_ptr<InFlight> m_inflight;
};

#endif // GIT_COMMIT_FETCHER_H
