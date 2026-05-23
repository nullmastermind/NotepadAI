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

#include "GitHistoryFetcher.h"

#include "GitLogParser.h"
#include "GitProcessRunner.h"
#include "GitWatcher.h"

#include <QDir>

namespace {
constexpr int kHistoryTimeoutMs = 60000;        // 60s — large repos can need it
constexpr qint64 kHistoryMaxStdoutBytes = 64 * 1024 * 1024;  // 64 MB safety cap
} // namespace

GitHistoryFetcher::GitHistoryFetcher(QObject *parent)
    : QObject(parent), m_runner(new GitProcessRunner(this))
{
    m_runner->setMaxOutputBytes(kHistoryMaxStdoutBytes);
}

GitHistoryFetcher::~GitHistoryFetcher() = default;

void GitHistoryFetcher::setRepoRoot(const QString &repoToplevel)
{
    const QString clean = QDir::cleanPath(repoToplevel);
    if (clean == m_repoRoot) return;
    cancel();
    m_repoRoot = clean;
    m_reachedEnd = false;
}

void GitHistoryFetcher::setAllBranches(bool all)
{
    if (m_allBranches == all) return;
    m_allBranches = all;
    // Caller (view) decides whether to immediately refetch — keep this setter
    // side-effect-free except for the flag.
}

void GitHistoryFetcher::connectWatcher(GitWatcher *watcher)
{
    if (!watcher) return;
    // refresh only on commit graph changes; index/working-tree don't affect
    // the log list itself.
    connect(watcher, &GitWatcher::headChanged, this, [this]() {
        if (!m_repoRoot.isEmpty()) refetch(m_pageSize);
    });
    connect(watcher, &GitWatcher::refsChanged, this, [this]() {
        if (!m_repoRoot.isEmpty()) refetch(m_pageSize);
    });
}

QStringList GitHistoryFetcher::buildArgv(int pageSize, int skip) const
{
    QStringList argv;
    argv << QStringLiteral("-c") << QStringLiteral("color.ui=never")
         << QStringLiteral("log")
         // Field separator US (0x1f), record separator RS (0x1e). Both
         // exceedingly rare in commit metadata and unaffected by shell
         // quoting (we pass argv directly to QProcess, no shell).
         << QStringLiteral("--pretty=format:%H%x1f%P%x1f%an%x1f%ae%x1f%ct%x1f%s%x1e")
         << QStringLiteral("--date-order")
         << QStringLiteral("--max-count=%1").arg(pageSize);
    if (skip > 0) argv << QStringLiteral("--skip=%1").arg(skip);
    if (m_allBranches) {
        argv << QStringLiteral("--all");
    } else {
        argv << QStringLiteral("HEAD");
    }
    return argv;
}

void GitHistoryFetcher::refetch(int pageSize)
{
    if (m_repoRoot.isEmpty()) return;
    if (pageSize <= 0) pageSize = 500;
    m_pageSize = pageSize;
    m_reachedEnd = false;
    if (m_runner->isRunning()) m_runner->cancelAsync();
    ++m_generation;
    const quint64 myGen = m_generation;
    m_mode = Mode::FirstPage;
    emit fetchStarted();

    const QStringList argv = buildArgv(pageSize, 0);
    m_runner->run(m_repoRoot, argv, /*stdinPayload=*/QByteArray(),
                  kHistoryTimeoutMs, /*readErrAsProgress=*/false,
                  [this, myGen](int exit, const QByteArray &out, const QByteArray &err) {
        if (myGen != m_generation) return;     // stale generation, drop
        onFinished(exit, out, err);
    });
}

void GitHistoryFetcher::loadMore(int alreadyLoaded, int pageSize)
{
    if (m_repoRoot.isEmpty()) return;
    if (m_reachedEnd) return;
    if (m_mode != Mode::Idle) return;          // already loading
    if (pageSize <= 0) pageSize = 500;
    m_pageSize = pageSize;
    if (m_runner->isRunning()) m_runner->cancelAsync();
    ++m_generation;
    const quint64 myGen = m_generation;
    m_mode = Mode::LoadMore;
    emit fetchStarted();

    const QStringList argv = buildArgv(pageSize, alreadyLoaded);
    m_runner->run(m_repoRoot, argv, QByteArray(),
                  kHistoryTimeoutMs, false,
                  [this, myGen](int exit, const QByteArray &out, const QByteArray &err) {
        if (myGen != m_generation) return;
        onFinished(exit, out, err);
    });
}

void GitHistoryFetcher::cancel()
{
    if (m_runner && m_runner->isRunning()) {
        m_runner->cancelAsync();
    }
    ++m_generation;
    m_mode = Mode::Idle;
}

void GitHistoryFetcher::onFinished(int exitCode, const QByteArray &stdoutBuf, const QByteArray &stderrBuf)
{
    m_mode = Mode::Idle;

    // Cancellation should be silent (generation token already filtered
    // staleness, but a same-gen cancel still funnels through here).
    if (exitCode == GitProcessRunner::kExitCancelled) {
        emit fetchFinished(false, QString());
        return;
    }

    if (exitCode != 0 && exitCode != GitProcessRunner::kExitTruncated) {
        // Empty repo flow: git log on a repo with no commits exits 128 and
        // prints "fatal: your current branch '...' does not have any commits
        // yet". Surface as success-with-zero-rows so the view shows the
        // empty-state placeholder instead of a generic error banner.
        const QByteArray noCommits = QByteArrayLiteral("does not have any commits yet");
        if (stderrBuf.contains(noCommits)) {
            m_reachedEnd = true;
            emit fetchFinished(true, QString());
            return;
        }
        const QString msg = QString::fromUtf8(stderrBuf).trimmed();
        emit fetchFinished(false, msg.isEmpty() ? QStringLiteral("git log failed") : msg);
        return;
    }

    // Parse the full stdout in one pass. For 500 commits ~60 KB, parse ~5 ms.
    QVector<GitCommitInfo> commits;
    commits.reserve(m_pageSize);
    GitLogParser parser;
    parser.feed(stdoutBuf, commits);
    parser.finish(commits);

    // reachedEnd when fewer than pageSize commits were returned — git short-
    // circuits when history is exhausted.
    if (commits.size() < m_pageSize) m_reachedEnd = true;

    if (!commits.isEmpty()) emit commitsAppended(commits);
    emit fetchFinished(m_reachedEnd, QString());
}
