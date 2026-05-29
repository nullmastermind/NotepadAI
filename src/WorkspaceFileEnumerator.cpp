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

#include "WorkspaceFileEnumerator.h"

#include "git/GitProcessRunner.h"

#include <QDir>
#include <QFileInfo>
#include <QtConcurrent>

#include <algorithm>

namespace {

constexpr int kLsFilesTimeoutMs = 30000;
constexpr int kSubmoduleTimeoutMs = 60000;
constexpr int kCancelCheckStride = 1024;

// Split git's newline-delimited stdout into a clean QStringList: trim a
// trailing '\r' (defensive on Windows), drop empty lines, decode as UTF-8.
QStringList splitLines(const QByteArray &raw)
{
    QStringList out;
    int start = 0;
    const int n = raw.size();
    out.reserve(n / 24 + 8);
    for (int i = 0; i <= n; ++i) {
        if (i == n || raw[i] == '\n') {
            int end = i;
            if (end > start && raw[end - 1] == '\r')
                --end;
            if (end > start)
                out.append(QString::fromUtf8(raw.constData() + start, end - start));
            start = i + 1;
        }
    }
    return out;
}

} // namespace

WorkspaceFileEnumerator::WorkspaceFileEnumerator(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<std::shared_ptr<const FileIndexCache>>();

    m_runner = new GitProcessRunner(this);
    m_watcher = new QFutureWatcher<Result>(this);
    connect(m_watcher, &QFutureWatcher<Result>::finished,
            this, &WorkspaceFileEnumerator::onWorkerFinished);
}

WorkspaceFileEnumerator::~WorkspaceFileEnumerator()
{
    // Never block on shutdown: signal any running DFS to bail and detach the
    // git child process. The QtConcurrent task (if any) keeps running on the
    // pool but its result is dropped (the watcher is destroyed as our child).
    if (m_cancel)
        m_cancel->store(true);
    if (m_runner)
        m_runner->cancelAsync();
}

void WorkspaceFileEnumerator::enumerate(const QString &root)
{
    m_rootPath = root;
    m_rootKey = QDir::cleanPath(root);

    // Supersede any in-flight run: bump the generation so a stale worker
    // result is dropped, cancel a running DFS, and detach a running git child.
    ++m_generation;
    if (m_cancel) {
        m_cancel->store(true);
        m_cancel.reset();
    }
    if (m_runner)
        m_runner->cancelAsync();

    m_rawTracked.clear();
    m_rawOthersSuper.clear();
    m_rawOthersSub.clear();

    if (m_rootPath.isEmpty()) {
        // Nothing to enumerate; publish an empty snapshot so the UI clears.
        Result r;
        r.generation = m_generation;
        r.rootKey = m_rootKey;
        r.snapshot = std::make_shared<const FileIndexCache>(
            FileIndexCache::build(QStringList(), false));
        emit indexReady(r.rootKey, r.snapshot);
        return;
    }

    if (GitProcessRunner::gitAvailable())
        launchTracked();   // a non-zero exit here means "not a git repo" → DFS
    else
        launchDfs();
}

// --- Git path: tracked stream --------------------------------------------

void WorkspaceFileEnumerator::launchTracked()
{
    const int gen = m_generation;
    const QStringList argv = {
        QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
        QStringLiteral("ls-files"), QStringLiteral("--recurse-submodules"),
    };
    m_runner->run(m_rootPath, argv, QByteArray(), kLsFilesTimeoutMs, false,
                  [this, gen](int exit, const QByteArray &out, const QByteArray &) {
        if (gen != m_generation) return;   // superseded
        if (exit != 0) {
            // Not a git repository (or git error) → first-class DFS fallback.
            launchDfs();
            return;
        }
        m_rawTracked = out;
        launchOthersSuperproject();
    });
}

void WorkspaceFileEnumerator::launchOthersSuperproject()
{
    const int gen = m_generation;
    const QStringList argv = {
        QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
        QStringLiteral("ls-files"), QStringLiteral("--others"),
        QStringLiteral("--exclude-standard"),
    };
    m_runner->run(m_rootPath, argv, QByteArray(), kLsFilesTimeoutMs, false,
                  [this, gen](int exit, const QByteArray &out, const QByteArray &) {
        if (gen != m_generation) return;
        // Untracked enumeration is best-effort; on failure just use empty.
        if (exit == 0)
            m_rawOthersSuper = out;
        launchOthersSubmodules();
    });
}

void WorkspaceFileEnumerator::launchOthersSubmodules()
{
    const int gen = m_generation;
    // Each submodule's untracked-not-ignored files, prefixed with the
    // submodule's $displaypath (path relative to the top working dir) so the
    // emitted paths are workspace-root-relative. The inner command runs in
    // git's bundled sh; `printf` is a shell builtin (portable on Windows too).
    const QString inner = QStringLiteral(
        "git -c core.quotepath=false ls-files --others --exclude-standard | "
        "while IFS= read -r f; do printf '%s/%s\\n' \"$displaypath\" \"$f\"; done");
    const QStringList argv = {
        QStringLiteral("submodule"), QStringLiteral("--quiet"),
        QStringLiteral("foreach"), QStringLiteral("--recursive"),
        inner,
    };
    m_runner->run(m_rootPath, argv, QByteArray(), kSubmoduleTimeoutMs, false,
                  [this, gen](int exit, const QByteArray &out, const QByteArray &) {
        if (gen != m_generation) return;
        if (exit == 0)
            m_rawOthersSub = out;
        finishGitOnWorker();
    });
}

void WorkspaceFileEnumerator::finishGitOnWorker()
{
    const int gen = m_generation;
    const QString key = m_rootKey;
    // Copy raw outputs into the worker by value — the lambda must NOT touch
    // `this` so destroying the enumerator mid-build is safe.
    const QByteArray rawTracked = m_rawTracked;
    const QByteArray rawSuper = m_rawOthersSuper;
    const QByteArray rawSub = m_rawOthersSub;

    startWorker([gen, key, rawTracked, rawSuper, rawSub]() -> Result {
        Result r;
        r.generation = gen;
        r.rootKey = key;
        const QStringList merged = mergeAndDedupe(splitLines(rawTracked),
                                                  splitLines(rawSuper),
                                                  splitLines(rawSub));
        r.snapshot = std::make_shared<const FileIndexCache>(
            FileIndexCache::build(merged, /*isGitRepo=*/true));
        return r;
    });
}

// --- Static merge + dedupe (unit-testable without git) -------------------

QStringList WorkspaceFileEnumerator::mergeAndDedupe(const QStringList &tracked,
                                                    const QStringList &othersSuperproject,
                                                    const QStringList &othersSubmodules)
{
    QStringList all;
    all.reserve(tracked.size() + othersSuperproject.size() + othersSubmodules.size());
    all += tracked;
    all += othersSuperproject;
    all += othersSubmodules;

    // Exact-dedupe + deterministic order via sort.
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());

    // Drop directory-placeholder entries: a `--others` pass reports an
    // untracked submodule DIRECTORY as a single entry "E" while the recursive
    // stream yields its contents "E/...". After sorting, any descendant of E
    // is contiguous immediately after E, so E is a placeholder iff the next
    // entry begins with E + "/". (The "+/" guard prevents false positives like
    // "foo.cpp" vs "foo.cpp.bak".)
    QStringList result;
    result.reserve(all.size());
    const int n = all.size();
    for (int i = 0; i < n; ++i) {
        if (i + 1 < n) {
            const QString prefix = all[i] + QLatin1Char('/');
            if (all[i + 1].startsWith(prefix))
                continue;   // placeholder directory, drop it
        }
        result.append(all[i]);
    }
    return result;
}

// --- Non-git fallback: cancelable DFS ------------------------------------

void WorkspaceFileEnumerator::launchDfs()
{
    const int gen = m_generation;
    const QString key = m_rootKey;
    const QString root = m_rootPath;
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    auto cancel = m_cancel;

    startWorker([gen, key, root, cancel]() -> Result {
        Result r;
        r.generation = gen;
        r.rootKey = key;
        const QStringList files = walkDfs(root, cancel);
        r.snapshot = std::make_shared<const FileIndexCache>(
            FileIndexCache::build(files, /*isGitRepo=*/false));
        return r;
    });
}

QStringList WorkspaceFileEnumerator::walkDfs(const QString &root,
                                             const std::shared_ptr<std::atomic<bool>> &cancel)
{
    QStringList files;
    files.reserve(4096);

    const QString cleanRoot = QDir::cleanPath(root);
    const int rootLen = cleanRoot.length() + 1;   // +1 for the trailing '/'

    // Explicit work stack — lets us PRUNE heavy/control directories BEFORE
    // descending (the original QDirIterator still stat()'d node_modules).
    QStringList stack;
    stack.append(cleanRoot);

    int processed = 0;
    while (!stack.isEmpty()) {
        const QString dirPath = stack.takeLast();
        QDir dir(dirPath);
        const QFileInfoList entries = dir.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);

        for (const QFileInfo &fi : entries) {
            if ((++processed % kCancelCheckStride) == 0 && cancel->load())
                return files;   // promptly bail; caller drops the stale result

            if (fi.isDir()) {
                const QString name = fi.fileName();
                // Prune .git / node_modules / build* before descending.
                if (name == QLatin1String(".git") ||
                    name == QLatin1String("node_modules") ||
                    name.startsWith(QLatin1String("build")))
                    continue;
                stack.append(fi.absoluteFilePath());
            } else {
                const QString abs = fi.absoluteFilePath();
                files.append(abs.mid(rootLen));   // workspace-relative, '/'-sep
            }
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

// --- Worker plumbing ------------------------------------------------------

void WorkspaceFileEnumerator::startWorker(std::function<Result()> task)
{
    // setFuture on a watcher already tracking a running future silently stops
    // watching the old one — the detached task finishes harmlessly on the pool
    // and its result is dropped (it captured only values, never `this`).
    m_watcher->setFuture(QtConcurrent::run(std::move(task)));
}

void WorkspaceFileEnumerator::onWorkerFinished()
{
    const Result r = m_watcher->result();
    if (r.generation != m_generation)
        return;   // superseded by a newer enumerate()
    emit indexReady(r.rootKey, r.snapshot);
}
