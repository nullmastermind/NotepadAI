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

#ifndef GIT_HISTORY_FETCHER_H
#define GIT_HISTORY_FETCHER_H

#include "GitCommitInfo.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

class GitProcessRunner;
class GitWatcher;

// Owns the streaming `git log` lifecycle for one repository / branch scope.
//
// Independent from GitController: keeps its own GitProcessRunner so a long
// streaming log never blocks status/fetch/pull operations. Refresh uses a
// generation token + GitProcessRunner::cancelAsync() so spam-refresh never
// blocks the UI thread.
//
// The fetcher does NOT own the model — it emits chunks of CommitInfo and
// the model appends them. Generation token is the SOLE correctness mechanism;
// receivers should not rely on Qt disconnect alone.
class GitHistoryFetcher : public QObject
{
    Q_OBJECT
public:
    explicit GitHistoryFetcher(QObject *parent = nullptr);
    ~GitHistoryFetcher() override;

    // Configure the repository toplevel (absolute path). Reconfiguring with a
    // different value cancels any in-flight fetch and resets state.
    void setRepoRoot(const QString &repoToplevel);
    QString repoRoot() const { return m_repoRoot; }

    // Branch scope toggle: false = current branch (HEAD), true = --all.
    void setAllBranches(bool all);
    bool allBranches() const { return m_allBranches; }

    // Optional GitWatcher hookup. When set, headChanged / refsChanged trigger
    // refetch() (debounced by the watcher itself). Caller still owns the
    // watcher object.
    void connectWatcher(GitWatcher *watcher);

    // Begin (or restart) a fetch of the first page of commits. Caller passes
    // page size; subsequent loadMore() calls fetch the next page.
    //
    // Spawn pattern:
    //   git -c color.ui=never log
    //     --pretty=format:%H%x1f%P%x1f%an%x1f%ae%x1f%ct%x1f%s%x1e
    //     --max-count=<pageSize>
    //     [--all | <branch>]
    void refetch(int pageSize = 500);

    // Append-fetch the next page using `--skip=<already-loaded>`.
    // Caller must pass the count of commits already in the model so the
    // fetcher knows where to skip from. Idempotent / no-op if already loading.
    void loadMore(int alreadyLoaded, int pageSize = 500);

    // True after the most recent fetch finished with no commits in the last
    // chunk — there are no more pages to load.
    bool reachedEnd() const { return m_reachedEnd; }

    // Cancel any in-flight fetch. Safe to call mid-stream.
    void cancel();

signals:
    // Chunked emission during streaming. Append-only — the receiver adds these
    // rows to the model. Emitted per ~50-commit slice as bytes arrive.
    void commitsAppended(const QVector<GitCommitInfo> &chunk);

    // Fired when the underlying `git log` process completes (success or
    // failure). `reachedEnd` mirrors reachedEnd(); `errorMessage` empty on
    // success, otherwise stderr line.
    void fetchFinished(bool reachedEnd, const QString &errorMessage);

    // Lifecycle hooks for UI spinner / banner.
    void fetchStarted();

private:
    // Build argv for the current configuration + pageSize / skip.
    QStringList buildArgv(int pageSize, int skip) const;

    // Final callback when QProcess finishes — handles success/error/cancel,
    // parses the full stdout in one pass, emits commitsAppended once.
    void onFinished(int exitCode, const QByteArray &stdoutBuf, const QByteArray &stderrBuf);

    // Returns the active "page-load mode" — first page or load-more.
    enum class Mode : std::uint8_t { Idle, FirstPage, LoadMore };

    QString          m_repoRoot;
    bool             m_allBranches = false;
    bool             m_reachedEnd  = false;

    GitProcessRunner *m_runner = nullptr;

    // Generation token bumped on every refetch / loadMore / cancel. Pending
    // callbacks check against this and drop themselves if stale.
    quint64 m_generation = 0;

    Mode    m_mode      = Mode::Idle;
    int     m_pageSize  = 500;
};

#endif // GIT_HISTORY_FETCHER_H
