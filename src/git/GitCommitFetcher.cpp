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

#include "GitCommitFetcher.h"

#include "GitNumstatParser.h"
#include "GitProcessRunner.h"
#include "GitWatcher.h"

#include <QByteArray>
#include <QStringList>
#include <QTimer>

#include <algorithm>

namespace {

constexpr int kCommitTimeoutMs = 60000;

// Format string for `git show --no-patch --format=...`.
// Fields in order:
//   %H = full SHA, %P = parents, %an = author name, %ae = author email,
//   %at = author epoch, %cn = committer name, %ce = committer email,
//   %ct = committer epoch, %s = subject, %B = full body
//   each separated by US (0x1f), terminated by RS (0x1e).
constexpr const char *kShowHeaderFormat =
    "--format=%H%x1f%P%x1f%an%x1f%ae%x1f%at%x1f%cn%x1f%ce%x1f%ct%x1f%s%x1f%B%x1e";

qint64 parseI64(const QByteArray &b)
{
    qint64 v = 0;
    bool neg = false;
    qsizetype i = 0;
    if (i < b.size() && b[i] == '-') { neg = true; ++i; }
    for (; i < b.size(); ++i) {
        const char c = b[i];
        if (c < '0' || c > '9') return 0;
        v = v * 10 + (c - '0');
    }
    return neg ? -v : v;
}

} // namespace

GitCommitFetcher::GitCommitFetcher(QObject *parent)
    : QObject(parent), m_runner(new GitProcessRunner(this))
{
    m_cache.setMaxCost(kCacheCapacity);
    m_runner->setMaxOutputBytes(kDefaultCapBytes);
}

GitCommitFetcher::~GitCommitFetcher() = default;

void GitCommitFetcher::setRepoRoot(const QString &repoToplevel)
{
    if (repoToplevel == m_repoRoot) return;
    cancel();
    invalidateAll();
    m_repoRoot = repoToplevel;
}

void GitCommitFetcher::invalidateAll()
{
    m_cache.clear();
}

void GitCommitFetcher::connectWatcher(GitWatcher *watcher)
{
    if (!watcher) return;
    connect(watcher, &GitWatcher::headChanged, this, &GitCommitFetcher::invalidateAll);
    connect(watcher, &GitWatcher::refsChanged, this, &GitCommitFetcher::invalidateAll);
}

void GitCommitFetcher::requestDetail(const QByteArray &sha, Callback cb)
{
    if (sha.isEmpty() || m_repoRoot.isEmpty()) {
        if (cb) cb({});
        return;
    }
    // Cache hit — fire callback asynchronously to keep call-site semantics
    // consistent (always "later" not "sometimes immediate").
    if (auto *hit = m_cache.object(sha)) {
        std::shared_ptr<const GitCommitDetail> shared = *hit;
        QTimer::singleShot(0, this, [cb = std::move(cb), shared]() {
            if (cb) cb(shared);
        });
        emit requestCompleted(sha, true);
        return;
    }

    cancel();
    ++m_generation;
    m_inflight = std::make_unique<InFlight>();
    m_inflight->sha = sha;
    m_inflight->cb = std::move(cb);
    m_inflight->partial = std::make_shared<GitCommitDetail>();
    m_inflight->generation = m_generation;
    m_inflight->uncapped = false;
    launchHeader(*m_inflight);
}

void GitCommitFetcher::requestDetailUncapped(const QByteArray &sha, Callback cb)
{
    if (sha.isEmpty() || m_repoRoot.isEmpty()) {
        if (cb) cb({});
        return;
    }
    cancel();
    // Evict any cached truncated copy.
    m_cache.remove(sha);
    ++m_generation;
    m_inflight = std::make_unique<InFlight>();
    m_inflight->sha = sha;
    m_inflight->cb = std::move(cb);
    m_inflight->partial = std::make_shared<GitCommitDetail>();
    m_inflight->generation = m_generation;
    m_inflight->uncapped = true;
    launchHeader(*m_inflight);
}

void GitCommitFetcher::cancel()
{
    if (m_runner && m_runner->isRunning()) m_runner->cancelAsync();
    ++m_generation;
    if (m_inflight) m_inflight.reset();
}

void GitCommitFetcher::launchHeader(InFlight &state)
{
    state.step = 0;
    const quint64 myGen = state.generation;
    const QStringList argv = {
        QStringLiteral("-c"), QStringLiteral("color.ui=never"),
        QStringLiteral("show"), QStringLiteral("--no-patch"),
        QString::fromLatin1(kShowHeaderFormat),
        QString::fromLatin1(state.sha)
    };
    m_runner->run(m_repoRoot, argv, QByteArray(), kCommitTimeoutMs, false,
                  [this, myGen](int exit, const QByteArray &out, const QByteArray &err) {
        if (myGen != m_generation) return;
        if (!m_inflight) return;
        if (exit != 0) {
            const QString msg = QString::fromUtf8(err).trimmed();
            qWarning("git show --no-patch failed: %s", qPrintable(msg));
            completeFail(*m_inflight);
            return;
        }
        parseHeader(out, *m_inflight->partial);
        parseTrailers(*m_inflight->partial);
        if (m_inflight->partial->sha.isEmpty()) {
            // Header parse failed — surface as fail.
            completeFail(*m_inflight);
            return;
        }
        launchNumstat(*m_inflight);
    });
}

void GitCommitFetcher::launchNumstat(InFlight &state)
{
    state.step = 1;
    const quint64 myGen = state.generation;
    // For merge commits, --first-parent diff is what the patch step shows.
    // Match here so the numstat row count agrees with the rendered diff.
    QStringList argv = {
        QStringLiteral("-c"), QStringLiteral("color.ui=never"),
        QStringLiteral("show"), QStringLiteral("--numstat"), QStringLiteral("-z"),
        QStringLiteral("--format=")
    };
    if (state.partial->isMerge()) argv << QStringLiteral("--first-parent");
    argv << QString::fromLatin1(state.sha);
    m_runner->run(m_repoRoot, argv, QByteArray(), kCommitTimeoutMs, false,
                  [this, myGen](int exit, const QByteArray &out, const QByteArray &err) {
        if (myGen != m_generation) return;
        if (!m_inflight) return;
        if (exit != 0) {
            qWarning("git show --numstat failed: %s",
                     qPrintable(QString::fromUtf8(err).trimmed()));
            // Don't fail — diff might still render; just skip numstat data.
        } else {
            const QHash<QString, GitNumstatParser::Stat> stats =
                GitNumstatParser::parse(out);
            QVector<GitCommitDetail::FileStat> fs;
            fs.reserve(stats.size());
            for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
                GitCommitDetail::FileStat e;
                e.path = it.key().toUtf8();
                e.added = it.value().added;
                e.deleted = it.value().deleted;
                e.isBinary = it.value().isBinary;
                fs.append(e);
            }
            // Stable order: alphabetical by path. Cheap (<200 entries
            // usually) and gives consistent UI for the same commit.
            std::sort(fs.begin(), fs.end(),
                      [](const GitCommitDetail::FileStat &a,
                         const GitCommitDetail::FileStat &b) {
                          return a.path < b.path;
                      });
            m_inflight->partial->fileStats = std::move(fs);
        }
        launchPatch(*m_inflight);
    });
}

void GitCommitFetcher::launchPatch(InFlight &state)
{
    state.step = 2;
    const quint64 myGen = state.generation;
    // Adjust runner cap before launch. Reset back to default afterwards in
    // completeOk/Fail.
    m_runner->setMaxOutputBytes(state.uncapped ? 0 : kDefaultCapBytes);
    QStringList argv = {
        QStringLiteral("-c"), QStringLiteral("color.ui=never"),
        QStringLiteral("show"), QStringLiteral("--no-color"),
        QStringLiteral("-p"), QStringLiteral("--format=")
    };
    if (state.partial->isMerge()) argv << QStringLiteral("--first-parent");
    argv << QString::fromLatin1(state.sha);
    m_runner->run(m_repoRoot, argv, QByteArray(), kCommitTimeoutMs, false,
                  [this, myGen](int exit, const QByteArray &out, const QByteArray &err) {
        if (myGen != m_generation) return;
        if (!m_inflight) return;
        const bool truncated = (exit == GitProcessRunner::kExitTruncated);
        if (exit != 0 && !truncated) {
            qWarning("git show -p failed: %s",
                     qPrintable(QString::fromUtf8(err).trimmed()));
            completeFail(*m_inflight);
            return;
        }
        m_inflight->partial->diffBytes = out;
        m_inflight->partial->truncated = truncated;
        completeOk(*m_inflight);
    });
}

void GitCommitFetcher::completeOk(InFlight &state)
{
    auto shared = std::shared_ptr<const GitCommitDetail>(std::move(state.partial));
    // Cache. QCache takes ownership of the heap pointer; wrap the shared_ptr
    // in another heap allocation so QCache can delete it.
    if (!state.uncapped) {
        m_cache.insert(state.sha,
                       new std::shared_ptr<const GitCommitDetail>(shared));
    }
    auto cb = std::move(state.cb);
    const QByteArray sha = state.sha;
    m_inflight.reset();
    m_runner->setMaxOutputBytes(kDefaultCapBytes);
    if (cb) cb(shared);
    emit requestCompleted(sha, true);
}

void GitCommitFetcher::completeFail(InFlight &state)
{
    auto cb = std::move(state.cb);
    const QByteArray sha = state.sha;
    m_inflight.reset();
    m_runner->setMaxOutputBytes(kDefaultCapBytes);
    if (cb) cb({});
    emit requestCompleted(sha, false);
}

void GitCommitFetcher::parseHeader(const QByteArray &bytes, GitCommitDetail &out)
{
    // Layout (10 US-separated fields, terminated by RS):
    //   sha, parents, an, ae, at, cn, ce, ct, subject, body
    const char *data = bytes.constData();
    const qsizetype n = bytes.size();
    qsizetype seps[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
    int sepN = 0;
    qsizetype rsEnd = n;
    for (qsizetype i = 0; i < n; ++i) {
        const char c = data[i];
        if (c == '\x1f' && sepN < 9) {
            seps[sepN++] = i;
        } else if (c == '\x1e') {
            rsEnd = i;
            break;
        }
    }
    if (sepN != 9) return;   // malformed → leave out.sha empty as fail signal
    out.sha             = QByteArray(data,             seps[0]);
    out.parents         = QByteArray(data + seps[0]+1, seps[1] - seps[0] - 1);
    out.authorName      = QByteArray(data + seps[1]+1, seps[2] - seps[1] - 1);
    out.authorEmail     = QByteArray(data + seps[2]+1, seps[3] - seps[2] - 1);
    out.authorTime      = parseI64(QByteArray(data + seps[3]+1, seps[4] - seps[3] - 1));
    out.committerName   = QByteArray(data + seps[4]+1, seps[5] - seps[4] - 1);
    out.committerEmail  = QByteArray(data + seps[5]+1, seps[6] - seps[5] - 1);
    out.commitTime      = parseI64(QByteArray(data + seps[6]+1, seps[7] - seps[6] - 1));
    out.subject         = QByteArray(data + seps[7]+1, seps[8] - seps[7] - 1);
    out.body            = QByteArray(data + seps[8]+1, rsEnd  - seps[8] - 1);
}

void GitCommitFetcher::parseTrailers(GitCommitDetail &out)
{
    // Look for a "trailer block" at the END of the body: one or more lines
    // matching ^[A-Z][A-Za-z-]+: .+ preceded by a blank line (or being the
    // very first line, for body-less commits — rare but possible).
    //
    // We scan back from the end, collecting matching lines into a vector,
    // then verify the block is preceded by a blank line OR is the entire
    // body. Otherwise, no trailers parsed (defensive against false matches
    // mid-body like "URL: https://...").
    const QByteArray &body = out.body;
    if (body.isEmpty()) return;

    QVector<QByteArrayView> lines;
    qsizetype lineStart = body.size();
    for (qsizetype i = body.size() - 1; i >= 0; --i) {
        if (body[i] == '\n') {
            lines.append(QByteArrayView(body.constData() + i + 1, lineStart - i - 1));
            lineStart = i;
        }
    }
    // First line (no leading \n).
    lines.append(QByteArrayView(body.constData(), lineStart));

    auto isTrailerLine = [](QByteArrayView ln) -> bool {
        if (ln.isEmpty()) return false;
        const char c0 = ln[0];
        if (c0 < 'A' || c0 > 'Z') return false;
        qsizetype i = 1;
        while (i < ln.size()) {
            const char c = ln[i];
            const bool letterOrDash =
                (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
            if (!letterOrDash) break;
            ++i;
        }
        if (i >= ln.size() || ln[i] != ':') return false;
        if (i + 1 >= ln.size() || ln[i+1] != ' ') return false;
        return ln.size() > i + 2;
    };

    // Walk from end (lines[0]) to find the first non-trailer line.
    int trailerCount = 0;
    while (trailerCount < lines.size() && isTrailerLine(lines[trailerCount])) {
        ++trailerCount;
    }
    if (trailerCount == 0) return;
    // The line just before the cluster (lines[trailerCount]) must be empty,
    // OR the cluster IS the whole body.
    const bool clusterIsAll = (trailerCount == lines.size());
    if (!clusterIsAll) {
        if (!lines[trailerCount].isEmpty()) return;   // false match — abort
    }

    out.trailers.reserve(trailerCount);
    // Restore order (we walked backwards). lines[trailerCount-1] is oldest.
    for (int i = trailerCount - 1; i >= 0; --i) {
        const auto &ln = lines[i];
        // Find ":"
        qsizetype colon = -1;
        for (qsizetype k = 0; k < ln.size(); ++k) {
            if (ln[k] == ':') { colon = k; break; }
        }
        if (colon < 0) continue;
        GitCommitDetail::Trailer t;
        t.key = QByteArray(ln.data(), colon);
        t.value = QByteArray(ln.data() + colon + 2, ln.size() - colon - 2);
        out.trailers.append(t);
    }
}
