#include "FolderSearchEngine.h"
#include "git/GitProcessRunner.h"
#include "git/FileEncodingDetector.h"
#include "remote/GitignoreMatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QTextCodec>
#include <QThread>
#include <QtConcurrent>

#include <cstring>

namespace {

constexpr int kLsFilesTimeoutMs = 30000;
constexpr int kBinaryCheckBytes = 8192;
constexpr qint64 kMaxFileSize = 100LL * 1024 * 1024; // 100 MB cap per file

QStringList splitGitOutput(const QByteArray &raw)
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

FolderSearchEngine::FolderSearchEngine(QObject *parent)
    : QObject(parent)
    , m_sink(std::make_shared<FolderSearchSink>())
{
    m_gitRunner = new GitProcessRunner(this);
}

FolderSearchEngine::~FolderSearchEngine()
{
    cancel();
}

void FolderSearchEngine::cancel()
{
    if (m_sink) {
        m_sink->cancelled.store(true, std::memory_order_relaxed);
        m_sink->generation.fetch_add(1, std::memory_order_relaxed);
    }
    if (m_gitRunner)
        m_gitRunner->cancelAsync();
    m_running.store(false, std::memory_order_relaxed);
}

bool FolderSearchEngine::isRunning() const
{
    return m_running.load(std::memory_order_relaxed);
}

void FolderSearchEngine::startSearch(const QString &folderPath,
                                     const QString &workspaceRoot,
                                     const QString &query,
                                     bool caseSensitive,
                                     bool wholeWord,
                                     bool regex)
{
    cancel();

    m_sink = std::make_shared<FolderSearchSink>();
    m_enumerationConsumed = false;
    m_folderPath = QDir::cleanPath(folderPath);
    m_workspaceRoot = QDir::cleanPath(workspaceRoot);

    QString pattern;
    if (regex) {
        pattern = query;
    } else {
        pattern = QRegularExpression::escape(query);
    }
    if (wholeWord) {
        pattern = QStringLiteral("\\b") + pattern + QStringLiteral("\\b");
    }

    QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
    if (!caseSensitive)
        opts |= QRegularExpression::CaseInsensitiveOption;
    m_regex = QRegularExpression(pattern, opts);
    if (!m_regex.isValid())
        return;

    m_running.store(true, std::memory_order_relaxed);
    const int gen = m_sink->generation.load(std::memory_order_relaxed);

    // Determine relative subfolder path for git ls-files scoping
    QString relFolder;
    if (!m_workspaceRoot.isEmpty() && m_folderPath.startsWith(m_workspaceRoot)) {
        relFolder = m_folderPath.mid(m_workspaceRoot.length());
        if (relFolder.startsWith(QLatin1Char('/')) || relFolder.startsWith(QLatin1Char('\\')))
            relFolder = relFolder.mid(1);
        if (!relFolder.isEmpty() && !relFolder.endsWith(QLatin1Char('/')))
            relFolder += QLatin1Char('/');
    }

    if (GitProcessRunner::gitAvailable() && !m_workspaceRoot.isEmpty()) {
        // Try git ls-files scoped to subfolder
        QStringList argv = {
            QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
            QStringLiteral("ls-files"), QStringLiteral("--recurse-submodules"),
        };
        if (!relFolder.isEmpty()) {
            argv << QStringLiteral("--") << relFolder;
        }

        auto sink = m_sink;
        auto wsRoot = m_workspaceRoot;
        auto folder = m_folderPath;
        auto re = m_regex;

        m_gitRunner->run(m_workspaceRoot, argv, QByteArray(), kLsFilesTimeoutMs, false,
            [this, gen, sink, wsRoot, folder, re, relFolder]
            (int exit, const QByteArray &out, const QByteArray &) {
            if (sink->generation.load(std::memory_order_relaxed) != gen) return;
            if (exit != 0) {
                // Not a git repo — DFS fallback
                sink->cancelled.store(false);
                // DFS runs on pool thread — must not capture raw `this`.
                // Use the sink's enumerationDone + a file list field instead.
                auto sinkCopy = sink;
                QtConcurrent::run([folder, gen, sinkCopy]() {
                    if (sinkCopy->generation.load(std::memory_order_relaxed) != gen) return;
                    auto cancel = std::make_shared<std::atomic<bool>>(false);
                    QStringList files = walkDfsFiltered(folder, cancel);
                    if (sinkCopy->generation.load(std::memory_order_relaxed) != gen) return;
                    // Store enumerated files in the sink for the UI timer to pick up
                    {
                        QMutexLocker lock(&sinkCopy->mutex);
                        sinkCopy->enumeratedFiles = std::move(files);
                    }
                    sinkCopy->enumerationDone.store(true, std::memory_order_release);
                });
                return;
            }
            QStringList relPaths = splitGitOutput(out);
            // Also get untracked files in the subfolder
            QStringList argv2 = {
                QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                QStringLiteral("ls-files"), QStringLiteral("--others"),
                QStringLiteral("--exclude-standard"),
            };
            if (!relFolder.isEmpty()) {
                argv2 << QStringLiteral("--") << relFolder;
            }
            // This callback fires on UI thread (QProcess::finished) — safe to use `this`
            m_gitRunner->run(wsRoot, argv2, QByteArray(), kLsFilesTimeoutMs, false,
                [this, gen, sink, wsRoot, relPaths, re]
                (int exit2, const QByteArray &out2, const QByteArray &) {
                if (sink->generation.load(std::memory_order_relaxed) != gen) return;
                QStringList allRel = relPaths;
                if (exit2 == 0) {
                    allRel += splitGitOutput(out2);
                    allRel.removeDuplicates();
                }
                // Convert to absolute paths
                QStringList absPaths;
                absPaths.reserve(allRel.size());
                for (const QString &rel : allRel) {
                    absPaths.append(wsRoot + QLatin1Char('/') + rel);
                }
                onFilesEnumerated(absPaths, gen);
            });
        });
    } else {
        // Non-git: DFS walk on pool thread — no raw `this` capture
        auto sink = m_sink;
        auto folder = m_folderPath;
        QtConcurrent::run([folder, gen, sink]() {
            if (sink->generation.load(std::memory_order_relaxed) != gen) return;
            auto cancel = std::make_shared<std::atomic<bool>>(false);
            QStringList files = walkDfsFiltered(folder, cancel);
            if (sink->generation.load(std::memory_order_relaxed) != gen) return;
            {
                QMutexLocker lock(&sink->mutex);
                sink->enumeratedFiles = std::move(files);
            }
            sink->enumerationDone.store(true, std::memory_order_release);
        });
    }
}

void FolderSearchEngine::pollEnumeration()
{
    if (m_enumerationConsumed || !m_sink) return;
    if (!m_sink->enumerationDone.load(std::memory_order_acquire)) return;

    m_enumerationConsumed = true;
    QStringList files;
    {
        QMutexLocker lock(&m_sink->mutex);
        files = std::move(m_sink->enumeratedFiles);
    }
    const int gen = m_sink->generation.load(std::memory_order_relaxed);
    onFilesEnumerated(files, gen);
}

void FolderSearchEngine::onFilesEnumerated(const QStringList &files, int generation)
{
    m_enumerationConsumed = true;
    if (!m_sink || m_sink->generation.load(std::memory_order_relaxed) != generation) {
        m_running.store(false, std::memory_order_relaxed);
        return;
    }
    if (files.isEmpty()) {
        m_running.store(false, std::memory_order_relaxed);
        m_sink->enumerationDone.store(true, std::memory_order_relaxed);
        m_sink->workersLaunched.store(true, std::memory_order_release);
        emit searchComplete(0, 0);
        return;
    }
    m_sink->enumerationDone.store(true, std::memory_order_relaxed);
    launchScanWorkers(files, generation);
}

void FolderSearchEngine::launchScanWorkers(const QStringList &absolutePaths, int generation)
{
    const int threadCount = qMax(1, QThread::idealThreadCount());
    const int filesPerChunk = qMax(1, absolutePaths.size() / threadCount);

    auto sink = m_sink;
    auto re = m_regex;
    sink->workersRemaining.store(0, std::memory_order_relaxed);

    int chunkStart = 0;
    int chunksLaunched = 0;
    while (chunkStart < absolutePaths.size()) {
        const int chunkEnd = qMin(chunkStart + filesPerChunk, absolutePaths.size());
        QStringList chunk = absolutePaths.mid(chunkStart, chunkEnd - chunkStart);
        chunkStart = chunkEnd;

        sink->workersRemaining.fetch_add(1, std::memory_order_relaxed);
        chunksLaunched++;

        QtConcurrent::run([chunk, re, sink, generation]() {
            for (const QString &filePath : chunk) {
                if (sink->cancelled.load(std::memory_order_relaxed)) break;
                if (sink->generation.load(std::memory_order_relaxed) != generation) break;

                QVector<FolderSearchMatch> matches = scanFile(filePath, re, sink, generation);
                if (!matches.isEmpty()) {
                    FolderSearchFileBatch batch;
                    batch.filePath = filePath;
                    batch.matches = std::move(matches);
                    QMutexLocker lock(&sink->mutex);
                    sink->pending.append(std::move(batch));
                }
            }
            const int rem = sink->workersRemaining.fetch_sub(1, std::memory_order_relaxed) - 1;
            Q_UNUSED(rem);
        });
    }
    sink->workersLaunched.store(true, std::memory_order_release);
}

QVector<FolderSearchMatch> FolderSearchEngine::scanFile(
    const QString &filePath,
    const QRegularExpression &re,
    const std::shared_ptr<FolderSearchSink> &sink,
    int generation)
{
    QVector<FolderSearchMatch> results;

    QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile())
        return results;
    const qint64 fileSize = fi.size();
    if (fileSize == 0 || fileSize > kMaxFileSize)
        return results;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return results;

    uchar *mapped = file.map(0, fileSize);
    if (!mapped) {
        file.close();
        return results;
    }

    const char *data = reinterpret_cast<const char *>(mapped);

    // Binary check: NUL byte in first 8KB
    const qint64 checkLen = qMin<qint64>(fileSize, kBinaryCheckBytes);
    if (std::memchr(data, '\0', static_cast<size_t>(checkLen)) != nullptr) {
        file.unmap(mapped);
        file.close();
        return results;
    }

    // Encoding sniff from first 64KB
    const qsizetype sniffLen = static_cast<qsizetype>(qMin<qint64>(fileSize, 64 * 1024));
    const QByteArray head = QByteArray::fromRawData(data, sniffLen);
    const FileEncodingDetector::SniffResult sniffResult = FileEncodingDetector::sniff(head);

    using Bom = FileEncodingDetector::Bom;
    const bool needsTranscode = (sniffResult.bom == Bom::Utf16LE ||
                                 sniffResult.bom == Bom::Utf16BE ||
                                 sniffResult.bom == Bom::Utf32LE ||
                                 sniffResult.bom == Bom::Utf32BE);

    QByteArray transcodedBuf;
    const char *scanData = data;
    qint64 scanSize = fileSize;

    if (needsTranscode) {
        const int bomLen = FileEncodingDetector::bomByteCount(sniffResult.bom);
        QTextCodec *codec = QTextCodec::codecForName(sniffResult.codecName);
        if (!codec) {
            file.unmap(mapped);
            file.close();
            return results;
        }
        transcodedBuf = codec->toUnicode(data + bomLen,
                            static_cast<int>(qMin<qint64>(fileSize - bomLen, INT_MAX))).toUtf8();
        scanData = transcodedBuf.constData();
        scanSize = transcodedBuf.size();
    } else {
        const int bomLen = FileEncodingDetector::bomByteCount(sniffResult.bom);
        scanData = data + bomLen;
        scanSize = fileSize - bomLen;
    }

    QTextCodec *lineCodec = nullptr;
    if (!needsTranscode && !sniffResult.codecName.isEmpty()) {
        lineCodec = QTextCodec::codecForName(sniffResult.codecName);
    }

    // Line-by-line scan over the mapped/transcoded bytes
    int lineNumber = 0;
    qint64 pos = 0;
    while (pos < scanSize) {
        if (sink->cancelled.load(std::memory_order_relaxed)) break;
        if (sink->generation.load(std::memory_order_relaxed) != generation) break;

        // Find end of line
        const char *lineStart = scanData + pos;
        const char *nl = static_cast<const char *>(
            std::memchr(lineStart, '\n', static_cast<size_t>(scanSize - pos)));
        qint64 lineLen;
        if (nl) {
            lineLen = nl - lineStart;
            // Strip \r
            if (lineLen > 0 && lineStart[lineLen - 1] == '\r')
                --lineLen;
        } else {
            lineLen = scanSize - pos;
        }

        // Decode line to QString for regex matching
        QString line;
        if (lineCodec) {
            line = lineCodec->toUnicode(lineStart, static_cast<int>(qMin<qint64>(lineLen, INT_MAX)));
        } else {
            line = QString::fromUtf8(lineStart, static_cast<int>(qMin<qint64>(lineLen, INT_MAX)));
        }

        QRegularExpressionMatchIterator it = re.globalMatch(line);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            FolderSearchMatch match;
            match.lineNumber = lineNumber;
            match.matchStart = static_cast<int>(m.capturedStart());
            match.matchEnd = static_cast<int>(m.capturedEnd());
            match.lineText = line;
            results.append(match);
        }

        pos += (nl ? (nl - lineStart + 1) : lineLen);
        ++lineNumber;
    }

    file.unmap(mapped);
    file.close();
    return results;
}

QStringList FolderSearchEngine::walkDfsFiltered(const QString &folder,
                                                const std::shared_ptr<std::atomic<bool>> &cancel)
{
    QStringList files;
    files.reserve(4096);

    QStringList stack;
    stack.append(QDir::cleanPath(folder));

    // Load .gitignore from the search root if present
    remote::GitignoreMatcher matcher;
    const QString rootGitignore = folder + QStringLiteral("/.gitignore");
    QFile gi(rootGitignore);
    if (gi.open(QIODevice::ReadOnly | QIODevice::Text)) {
        matcher.addRules(QDir::cleanPath(folder), QString::fromUtf8(gi.readAll()));
        gi.close();
    }

    const QString cleanRoot = QDir::cleanPath(folder);
    const int rootLen = cleanRoot.length() + 1;
    int processed = 0;

    while (!stack.isEmpty()) {
        if (cancel->load(std::memory_order_relaxed))
            return files;

        const QString dirPath = stack.takeLast();
        QDir dir(dirPath);

        // Load nested .gitignore
        const QString nestedGi = dirPath + QStringLiteral("/.gitignore");
        if (dirPath != cleanRoot) {
            QFile ngi(nestedGi);
            if (ngi.open(QIODevice::ReadOnly | QIODevice::Text)) {
                matcher.addRules(dirPath, QString::fromUtf8(ngi.readAll()));
                ngi.close();
            }
        }

        const QFileInfoList entries = dir.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);

        for (const QFileInfo &fi : entries) {
            if ((++processed % 1024) == 0 && cancel->load(std::memory_order_relaxed))
                return files;

            const QString abs = fi.absoluteFilePath();
            const QString rel = abs.mid(rootLen);

            if (fi.isDir()) {
                const QString name = fi.fileName();
                if (name == QLatin1String(".git") ||
                    name == QLatin1String("node_modules") ||
                    name.startsWith(QLatin1String("build")))
                    continue;
                if (!matcher.isEmpty() && matcher.isIgnored(rel, true))
                    continue;
                stack.append(abs);
            } else {
                if (!matcher.isEmpty() && matcher.isIgnored(rel, false))
                    continue;
                files.append(abs);
            }
        }
    }

    return files;
}
