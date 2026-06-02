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

#include "RemoteTransferManager.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QUuid>
#include <utility>

#include "RemoteExecutionContext.h"

namespace remote {

// ============================================================================
// Construction / destruction
// ============================================================================

RemoteTransferManager::RemoteTransferManager(RemoteFsBackend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
    // connectionLost wiring is done externally by setupTransferManager in
    // FolderAsWorkspaceDock, where RemoteExecutionContext is accessible (C1 fix).
    // Here we only wire the backend's destroyed signal as a safety net so the
    // QPointer nulls out and any in-flight callbacks no-op cleanly on teardown.
    if (backend) {
        connect(backend, &QObject::destroyed, this, [this]() {
            m_backend = nullptr;
            if (m_active) {
                onConnectionLost(tr("SSH backend destroyed"));
            }
        });
    }
}

// ============================================================================
// Public API — Download
// ============================================================================

void RemoteTransferManager::downloadFile(const QString &remotePath, const QString &localDestPath)
{
    PendingTransfer tx;
    tx.direction = Direction::Download;
    tx.remotePaths = { remotePath };
    tx.localPath = localDestPath;
    tx.isRecursive = false;
    tx.gitignore = GitignoreFilter::None;

    m_queue.append(tx);
    if (!m_active)
        startNextTransfer();
}

void RemoteTransferManager::downloadRecursive(const QStringList &remotePaths,
                                               const QString &localDestDir,
                                               GitignoreFilter filter)
{
    PendingTransfer tx;
    tx.direction = Direction::Download;
    tx.remotePaths = remotePaths;
    tx.localPath = localDestDir;
    tx.isRecursive = true;
    tx.gitignore = filter;

    m_queue.append(tx);
    if (!m_active)
        startNextTransfer();
}

// ============================================================================
// Public API — Upload
// ============================================================================

void RemoteTransferManager::uploadFiles(const QStringList &localFilePaths,
                                         const QString &remoteDestDir,
                                         const QHash<QString, ConflictResolution> &conflicts,
                                         bool overrideConflicts)
{
    PendingTransfer tx;
    tx.direction = Direction::Upload;
    tx.localPaths = localFilePaths;
    tx.localPath = remoteDestDir; // reuse localPath field for remote dest dir
    tx.isRecursive = false;
    tx.conflicts = conflicts;
    tx.overrideConflicts = overrideConflicts;

    m_queue.append(tx);
    if (!m_active)
        startNextTransfer();
}

void RemoteTransferManager::uploadFolder(const QString &localFolderPath,
                                          const QString &remoteDestDir,
                                          const QHash<QString, ConflictResolution> &conflicts,
                                          bool overrideConflicts)
{
    PendingTransfer tx;
    tx.direction = Direction::Upload;
    tx.localPaths = { localFolderPath };
    tx.localPath = remoteDestDir;
    tx.isRecursive = true;
    tx.conflicts = conflicts;
    tx.overrideConflicts = overrideConflicts;

    m_queue.append(tx);
    if (!m_active)
        startNextTransfer();
}

// ============================================================================
// Accessors
// ============================================================================

QString RemoteTransferManager::currentLocalDestination() const
{
    if (m_queue.isEmpty()) return QString();
    return m_queue.first().localPath;
}

// ============================================================================
// Cancel
// ============================================================================

void RemoteTransferManager::cancel()
{
    m_cancelled = true;
    // Cancel the in-flight bulk read (if any) so we don't wait for a large file.
    if (m_hasCurrentRead && m_backend) {
        m_backend->cancelReadAsync(m_currentReadReqId);
        m_hasCurrentRead = false;
    }
    // Clean up any in-progress archive streaming.
    if (m_archiveFile) {
        m_archiveFile->close();
        delete m_archiveFile;
        m_archiveFile = nullptr;
    }
    if (!m_localArchivePath.isEmpty()) {
        QFile::remove(m_localArchivePath);
        m_localArchivePath.clear();
    }
    cleanupRemoteArchive();
    // Clear queue.
    m_queue.clear();
    m_active = false;
    m_currentRemotePath.clear();
    m_downloadManifest.clear();
    m_uploadManifest.clear();
    emit transferCancelled();
}

void RemoteTransferManager::resolveUploadConflicts(
    const QHash<QString, ConflictResolution> &resolutions, bool overrideAll)
{
    if (m_cancelled) return;
    if (m_queue.isEmpty()) return;

    PendingTransfer &tx = m_queue.front();
    tx.conflicts = resolutions;
    tx.overrideConflicts = overrideAll;
    // Resume upload with resolved conflicts.
    uploadWithManifest(tx.conflicts, tx.overrideConflicts);
}

// ============================================================================
// Internal — Queue management
// ============================================================================

void RemoteTransferManager::startNextTransfer()
{
    if (m_queue.isEmpty()) {
        m_active = false;
        return;
    }
    m_active = true;
    m_cancelled = false;
    m_bytesTransferred = 0;
    m_totalBytes = 0;
    m_manifestIdx = 0;
    m_uploadIdx = 0;
    m_currentRemotePath.clear();
    m_downloadManifest.clear();
    m_uploadManifest.clear();
    m_gitignore.clear();
    m_createdDirs.clear();
    m_conflictDirsToCheck.clear();
    m_remoteFileSets.clear();
    m_pendingConflictDirs = 0;
    m_totalConflictDirs = 0;
    m_conflictDetectionDone = false;
    m_conflictResolutions.clear();
    m_overrideConflicts = false;
    m_pendingWalkDirs = 0;
    m_walkFailed = false;

    // Reset archive state (from previous transfer).
    m_remoteArchivePath.clear();
    m_localArchivePath.clear();
    m_archiveBytesWritten = 0;
    if (m_archiveFile) {
        m_archiveFile->close();
        delete m_archiveFile;
        m_archiveFile = nullptr;
    }

    const PendingTransfer &tx = m_queue.front();
    if (tx.direction == Direction::Download)
        startDownload(tx);
    else
        startUpload(tx);
}

// ============================================================================
// Download implementation
// ============================================================================

void RemoteTransferManager::startDownload(const PendingTransfer &tx)
{
    if (!m_backend) {
        m_queue.removeFirst();
        emit transferError(tr("No remote backend available."));
        startNextTransfer();
        return;
    }

    if (!tx.isRecursive && tx.remotePaths.size() == 1) {
        // Single-file fast path: skip manifest walk, go straight to download.
        // Size is unknown (0) — progress will show file count only.
        DownloadEntry entry;
        entry.remotePath  = tx.remotePaths.first();
        entry.localPath   = tx.localPath;
        entry.partialPath = tx.localPath + QStringLiteral(".partial");
        entry.size        = 0;
        m_downloadManifest.append(entry);
        downloadNextFile();
        return;
    }

    emit progressUpdated(0, 0, 0, 0, tr("Preparing download…"), m_queue.size() - 1);
    // Build the manifest asynchronously via recursive readdir walk.
    buildDownloadManifest(tx.remotePaths, tx.localPath, tx.gitignore);
}

void RemoteTransferManager::buildDownloadManifest(const QStringList &remotePaths,
                                                   const QString &localDestDir,
                                                   GitignoreFilter filter)
{
    if (m_cancelled || !m_backend) return;

    // --- Batch readdir approach ---
    // Group all requested paths by their parent directory so we can issue one
    // readdirAsync per unique parent instead of one statAsync per file.
    // This reduces preparation time from O(N * RTT) to O(D * RTT) where D is
    // the number of distinct parent directories (usually 1 for typical usage).

    // parentDir → list of (remotePath, baseName) pairs requested from that dir
    struct PathEntry {
        QString remotePath;
        QString baseName;
    };
    QHash<QString, QList<PathEntry>> byParent;
    byParent.reserve(remotePaths.size());

    for (const QString &rp : remotePaths) {
        const int slash = rp.lastIndexOf(QLatin1Char('/'));
        const QString parentDir = (slash > 0) ? rp.left(slash) : QStringLiteral("/");
        const QString baseName  = (slash >= 0) ? rp.mid(slash + 1) : rp;
        byParent[parentDir].append(PathEntry{ rp, baseName });
    }

    // pendingDirs counts outstanding readdirAsync + walkRemoteDir calls.
    // When it reaches zero, downloadWalkDone() is called.
    auto pendingDirs = std::make_shared<int>(byParent.size());
    auto anyFailed   = std::make_shared<bool>(false);

    for (auto it = byParent.cbegin(); it != byParent.cend(); ++it) {
        const QString parentDir       = it.key();
        const QList<PathEntry> wanted = it.value();

        QPointer<RemoteTransferManager> guard(this);
        const QString lDest = localDestDir;
        const GitignoreFilter gf = filter;

        m_backend->readdirAsync(parentDir,
            [guard, wanted, lDest, gf, pendingDirs, anyFailed]
            (bool ok, const QList<RemoteDirEntry> &entries, const QString & /*error*/) {
                if (guard.isNull()) return;
                if (guard->m_cancelled) {
                    --(*pendingDirs);
                    return;
                }

                if (!ok) {
                    *anyFailed = true;
                    if (--(*pendingDirs) == 0)
                        guard->downloadWalkDone();
                    return;
                }

                // Build a name→entry lookup from readdir results (O(N) build,
                // O(1) probe per requested path).
                QHash<QString, RemoteDirEntry> nameMap;
                nameMap.reserve(entries.size());
                for (const RemoteDirEntry &e : entries)
                    nameMap.insert(e.name, e);

                for (const auto &req : wanted) {
                    const auto mapIt = nameMap.find(req.baseName);
                    if (mapIt == nameMap.end()) {
                        // Path does not exist on remote — skip silently.
                        continue;
                    }

                    const RemoteDirEntry &e = mapIt.value();
                    if (e.isDir) {
                        // Recursive walk: mirrors original stat-path dir branch.
                        const QString localDir = lDest + QLatin1Char('/') + req.baseName;
                        QDir().mkpath(localDir);
                        ++(*pendingDirs);
                        guard->m_pendingWalkDirs++;
                        guard->walkRemoteDir(req.remotePath, localDir, req.baseName,
                                             gf, pendingDirs, anyFailed);
                    } else {
                        // Plain file: add directly to manifest.
                        const QString localPath = lDest + QLatin1Char('/') + req.baseName;
                        DownloadEntry entry;
                        entry.remotePath  = req.remotePath;
                        entry.localPath   = localPath;
                        entry.partialPath = localPath + QStringLiteral(".partial");
                        entry.size        = e.size;
                        guard->m_downloadManifest.append(entry);
                        guard->m_totalBytes += e.size;
                        emit guard->progressUpdated(0, 0, 0, 0,
                            tr("Preparing download… %1 files found")
                                .arg(guard->m_downloadManifest.size()),
                            guard->m_queue.size() - 1);
                    }
                }

                if (--(*pendingDirs) == 0)
                    guard->downloadWalkDone();
            });
    }
}

void RemoteTransferManager::walkRemoteDir(const QString &remoteDirPath,
                                          const QString &localDirPath,
                                          const QString &relBase,
                                          GitignoreFilter filter,
                                          std::shared_ptr<int> pending,
                                          std::shared_ptr<bool> failed)
{
    // Fast path: use a single `find` exec for the whole tree (one round-trip).
    // Requires filter==None (exec path doesn't read .gitignore files) and an
    // available RemoteExecutionContext with exec() support.
    if (filter == GitignoreFilter::None && m_backend) {
        auto *ctx = qobject_cast<RemoteExecutionContext *>(m_backend->parent());
        if (ctx) {
            walkRemoteDirExec(remoteDirPath, localDirPath, pending, failed);
            return;
        }
    }
    // Slow (SFTP readdir) path — used for gitignore filtering or when exec is
    // unavailable.
    walkRemoteDirSftp(remoteDirPath, localDirPath, relBase, filter, pending, failed);
}

void RemoteTransferManager::walkRemoteDirExec(const QString &remoteDirPath,
                                              const QString &localDirPath,
                                              std::shared_ptr<int> pending,
                                              std::shared_ptr<bool> failed)
{
    if (m_cancelled || !m_backend) {
        if (--(*pending) == 0) downloadWalkDone();
        return;
    }

    auto *ctx = qobject_cast<RemoteExecutionContext *>(m_backend->parent());
    if (!ctx) {
        // Context no longer available — fall back to SFTP walk.
        walkRemoteDirSftp(remoteDirPath, localDirPath, QString(),
                          GitignoreFilter::None, pending, failed);
        return;
    }

    // Build the find command as a single sh -c string to avoid shell-quoting
    // issues with -printf format strings. No parentheses needed: without explicit
    // grouping, `-type f -printf ...` and `-type d -printf ...` bind tighter than
    // -o, so the logic is correct.
    //
    // Output format per line:
    //   F <size> <path-relative-to-remoteDirPath>
    //   D 0 <path-relative-to-remoteDirPath>
    const QString findCmd =
        QStringLiteral("find ") + remoteDirPath +
        QStringLiteral(" -mindepth 1"
                       " -type f -printf 'F %s %P\\n'"
                       " -o -type d -printf 'D 0 %P\\n'");

    const QStringList argv = {
        QStringLiteral("sh"),
        QStringLiteral("-c"),
        findCmd
    };

    QPointer<RemoteTransferManager> guard(this);
    const QString rDir = remoteDirPath;
    const QString lDir = localDirPath;

    ctx->exec(QString(), argv, QByteArray(), 30000,
        [guard, rDir, lDir, pending, failed]
        (int exitCode, const QByteArray &out, const QByteArray & /*err*/) {
            if (guard.isNull()) { --(*pending); return; }
            if (guard->m_cancelled) { --(*pending); return; }

            if (exitCode != 0) {
                // `find` failed (e.g. not available). Fall back to SFTP walk.
                // We must re-increment pending first because walkRemoteDirSftp
                // will decrement it when it finishes.
                guard->walkRemoteDirSftp(rDir, lDir, QString(),
                                         GitignoreFilter::None, pending, failed);
                return;
            }

            // Parse stdout: one entry per line.
            // Each line: "F <size> <relPath>" or "D 0 <relPath>"
            const QList<QByteArray> lines = out.split('\n');
            for (const QByteArray &line : lines) {
                if (line.size() < 4) continue; // minimum "F 0 x"
                const char type = line.at(0);
                if (line.at(1) != ' ') continue; // guard: expected space at pos 1
                if (type != 'F' && type != 'D') continue;

                // Find the second space that separates <size> from <relPath>.
                const int secondSpace = line.indexOf(' ', 2);
                if (secondSpace < 0) continue;

                const QByteArray relPathBytes = line.mid(secondSpace + 1);
                if (relPathBytes.isEmpty()) continue;
                const QString relPath = QString::fromUtf8(relPathBytes);

                if (type == 'D') {
                    // Pre-create local directory so files inside can be written
                    // without parent-mkdir overhead per-file.
                    QDir().mkpath(lDir + QLatin1Char('/') + relPath);
                } else {
                    // F: parse size and add manifest entry.
                    const qint64 size = line.mid(2, secondSpace - 2).toLongLong();
                    DownloadEntry entry;
                    entry.remotePath  = rDir + QLatin1Char('/') + relPath;
                    entry.localPath   = lDir + QLatin1Char('/') + relPath;
                    entry.partialPath = entry.localPath + QStringLiteral(".partial");
                    entry.size        = size;
                    guard->m_downloadManifest.append(entry);
                    guard->m_totalBytes += size;
                }
            }

            if (!guard->m_cancelled && !guard->m_downloadManifest.isEmpty()) {
                emit guard->progressUpdated(0, 0, 0, 0,
                    tr("Preparing download… %1 files found")
                        .arg(guard->m_downloadManifest.size()),
                    guard->m_queue.size() - 1);
            }

            if (--(*pending) == 0)
                guard->downloadWalkDone();
        });
}

void RemoteTransferManager::walkRemoteDirSftp(const QString &remoteDirPath,
                                              const QString &localDirPath,
                                              const QString &relBase,
                                              GitignoreFilter filter,
                                              std::shared_ptr<int> pending,
                                              std::shared_ptr<bool> failed)
{
    if (m_cancelled || !m_backend) {
        if (--(*pending) == 0) downloadWalkDone();
        return;
    }

    QPointer<RemoteTransferManager> guard(this);
    const QString rDir = remoteDirPath;
    const QString lDir = localDirPath;
    const QString rel  = relBase;
    const GitignoreFilter gf = filter;

    m_backend->readdirAsync(remoteDirPath,
        [guard, rDir, lDir, rel, gf, pending, failed]
        (bool ok, const QList<RemoteDirEntry> &entries, const QString &error) {
            if (guard.isNull()) return;
            if (guard->m_cancelled) {
                --(*pending);
                return;
            }
            if (!ok) {
                *failed = true;
                if (--(*pending) == 0)
                    guard->downloadWalkDone();
                return;
            }

            // Fetch .gitignore from this directory if filtering is enabled.
            if (gf == GitignoreFilter::Apply) {
                // Check if there's a .gitignore in this listing.
                bool hasGitignore = false;
                for (const RemoteDirEntry &e : entries) {
                    if (!e.isDir && e.name == QLatin1String(".gitignore")) {
                        hasGitignore = true;
                        break;
                    }
                }
                if (hasGitignore) {
                    // Read .gitignore and add rules. This is async; we increment
                    // pending so walkDone waits for it.
                    ++(*pending);
                    const QString gitignorePath = rDir + QStringLiteral("/.gitignore");
                    guard->m_backend->readFileAsync(gitignorePath,
                        [guard, rDir, pending](bool readOk, const QByteArray &data, const QString &) {
                            if (guard.isNull()) { --(*pending); return; }
                            if (guard->m_cancelled) { --(*pending); return; }
                            if (readOk) {
                                guard->m_gitignore.addRules(rDir, QString::fromUtf8(data));
                            }
                            if (--(*pending) == 0)
                                guard->downloadWalkDone();
                        });
                }
            }

            // Process each entry.
            for (const RemoteDirEntry &e : entries) {
                const QString relPath = rel.isEmpty() ? e.name : (rel + QLatin1Char('/') + e.name);

                // Gitignore check.
                if (gf == GitignoreFilter::Apply && guard->m_gitignore.isIgnored(relPath, e.isDir))
                    continue;

                const QString remoteChild = rDir + QLatin1Char('/') + e.name;
                const QString localChild  = lDir  + QLatin1Char('/') + e.name;

                if (e.isDir) {
                    QDir().mkpath(localChild);
                    ++(*pending);
                    guard->m_pendingWalkDirs++;
                    guard->walkRemoteDirSftp(remoteChild, localChild, relPath, gf, pending, failed);
                } else {
                    RemoteTransferManager::DownloadEntry entry;
                    entry.remotePath = remoteChild;
                    entry.localPath  = localChild;
                    entry.partialPath = localChild + QStringLiteral(".partial");
                    entry.size       = e.size;
                    guard->m_downloadManifest.append(entry);
                    guard->m_totalBytes += e.size;
                }
            }

            if (!guard->m_cancelled && !guard->m_downloadManifest.isEmpty()) {
                emit guard->progressUpdated(0, 0, 0, 0,
                    tr("Preparing download… %1 files found").arg(guard->m_downloadManifest.size()),
                    guard->m_queue.size() - 1);
            }

            if (--(*pending) == 0)
                guard->downloadWalkDone();
        });
}

void RemoteTransferManager::downloadWalkDone()
{
    if (m_cancelled) return;
    if (m_downloadManifest.isEmpty()) {
        m_queue.removeFirst();
        emit transferCompleted(0);
        startNextTransfer();
        return;
    }
    // Archive fast path: 2+ files and a RemoteExecutionContext is available.
    if (m_downloadManifest.size() >= 2 && m_backend) {
        auto *ctx = qobject_cast<RemoteExecutionContext *>(m_backend->parent());
        if (ctx) {
            downloadViaArchive();
            return;
        }
    }
    // Fall back to per-file download.
    downloadNextFile();
}

// ============================================================================
// Archive-based batch download (tar fast path)
// ============================================================================

void RemoteTransferManager::downloadViaArchive()
{
    if (m_cancelled || !m_backend) return;

    if (m_tarProbed) {
        if (m_tarAvailable) {
            createRemoteArchive();
        } else {
            downloadNextFile(); // fallback: tar not available on remote
        }
        return;
    }

    // Probe once per session (m_tarProbed persists on the manager).
    probeRemoteTar([this](bool available) {
        if (m_cancelled) return;
        if (available) {
            createRemoteArchive();
        } else {
            downloadNextFile(); // fallback
        }
    });
}

void RemoteTransferManager::probeRemoteTar(std::function<void(bool)> callback)
{
    auto *ctx = qobject_cast<RemoteExecutionContext *>(m_backend ? m_backend->parent() : nullptr);
    if (!ctx) {
        callback(false);
        return;
    }

    QPointer<RemoteTransferManager> guard(this);
    ctx->exec(QString(), {QStringLiteral("sh"), QStringLiteral("-c"),
                          QStringLiteral("command -v tar")},
              QByteArray(), 5000,
        [guard, callback](int exitCode, const QByteArray &, const QByteArray &) {
            if (guard.isNull()) return;
            guard->m_tarProbed = true;
            guard->m_tarAvailable = (exitCode == 0);
            callback(guard->m_tarAvailable);
        });
}

void RemoteTransferManager::createRemoteArchive()
{
    auto *ctx = qobject_cast<RemoteExecutionContext *>(m_backend ? m_backend->parent() : nullptr);
    if (!ctx || m_cancelled) {
        downloadNextFile();
        return;
    }

    emit progressUpdated(0, 0, 0, 0,
        tr("Creating archive on remote\xe2\x80\xa6 (%1 files)").arg(m_downloadManifest.size()),
        m_queue.size() - 1);

    // Find longest common directory prefix of all remote paths.
    QString commonBase;
    for (const auto &entry : m_downloadManifest) {
        const QString dir = entry.remotePath.left(entry.remotePath.lastIndexOf(QLatin1Char('/')));
        if (commonBase.isEmpty()) {
            commonBase = dir;
        } else {
            int i = 0;
            while (i < commonBase.size() && i < dir.size() && commonBase[i] == dir[i])
                ++i;
            commonBase = commonBase.left(i);
            const int lastSlash = commonBase.lastIndexOf(QLatin1Char('/'));
            commonBase = (lastSlash > 0) ? commonBase.left(lastSlash) : QStringLiteral("/");
        }
    }
    if (commonBase.isEmpty()) commonBase = QStringLiteral("/");

    // Build null-separated relative path list for tar --null -T -.
    QByteArray stdinData;
    stdinData.reserve(m_downloadManifest.size() * 32);
    for (const auto &entry : m_downloadManifest) {
        QString rel = entry.remotePath.mid(commonBase.size());
        if (rel.startsWith(QLatin1Char('/'))) rel = rel.mid(1);
        stdinData.append(rel.toUtf8());
        stdinData.append('\0');
    }

    // Shell-safe single-quote escape for commonBase (for the -C argument).
    const QString safeCBase = QString(commonBase).replace(
        QLatin1Char('\''), QStringLiteral("'\\''"));

    const QString tarCmd =
        QStringLiteral("ARCHIVE=$(mktemp /tmp/notepadai-dl-XXXXXXXX.tar) && "
                       "tar cf \"$ARCHIVE\" -C '%1' --null -T - && "
                       "echo \"$ARCHIVE\"").arg(safeCBase);

    QPointer<RemoteTransferManager> guard(this);
    ctx->exec(QString(), {QStringLiteral("sh"), QStringLiteral("-c"), tarCmd},
              stdinData, 120000,
        [guard](int exitCode, const QByteArray &out, const QByteArray &err) {
            if (guard.isNull()) return;
            guard->onArchiveCreated(exitCode, out, err);
        });
}

void RemoteTransferManager::onArchiveCreated(int exitCode, const QByteArray &out,
                                              const QByteArray &err)
{
    Q_UNUSED(err)
    if (m_cancelled) return;

    if (exitCode != 0) {
        // tar creation failed — fall back to per-file download.
        if (!m_remoteArchivePath.isEmpty()) {
            cleanupRemoteArchive();
        }
        downloadNextFile();
        return;
    }

    m_remoteArchivePath = QString::fromUtf8(out.trimmed());
    if (m_remoteArchivePath.isEmpty()) {
        // Unexpected: no path in stdout — fall back.
        downloadNextFile();
        return;
    }

    streamArchiveToLocal();
}

void RemoteTransferManager::streamArchiveToLocal()
{
    if (m_cancelled || !m_backend) return;

    m_localArchivePath = QDir::tempPath()
        + QStringLiteral("/notepadai-dl-")
        + QUuid::createUuid().toString(QUuid::Id128)
        + QStringLiteral(".tar");

    m_archiveFile = new QFile(m_localArchivePath, this);
    if (!m_archiveFile->open(QIODevice::WriteOnly)) {
        delete m_archiveFile;
        m_archiveFile = nullptr;
        emit transferError(tr("Cannot create local temp file for archive."));
        cleanupRemoteArchive();
        m_queue.removeFirst();
        m_active = false;
        startNextTransfer();
        return;
    }

    m_archiveBytesWritten = 0;
    emit progressUpdated(0, 0, 0, 0,
        tr("Downloading archive\xe2\x80\xa6"), m_queue.size() - 1);

    m_hasCurrentRead = true;
    QPointer<RemoteTransferManager> guard(this);
    m_currentReadReqId = m_backend->readFileStreamAsync(m_remoteArchivePath,
        // Chunk callback: write to temp file + throttled progress.
        [guard](const QByteArray &chunk) {
            if (guard.isNull() || guard->m_cancelled) return;
            if (guard->m_archiveFile) {
                guard->m_archiveFile->write(chunk);
            }
            const qint64 prev = guard->m_archiveBytesWritten;
            guard->m_archiveBytesWritten += chunk.size();
            // Emit progress roughly every 64 KB to avoid signal-per-chunk overhead.
            if ((guard->m_archiveBytesWritten >> 16) != (prev >> 16)) {
                emit guard->progressUpdated(0, 0, 0, 0,
                    tr("Downloading archive\xe2\x80\xa6"),
                    guard->m_queue.size() - 1);
            }
        },
        // Done callback.
        [guard](bool ok, const QString &error) {
            if (guard.isNull()) return;
            guard->m_hasCurrentRead = false;
            guard->onArchiveStreamDone(ok, error);
        });
}

void RemoteTransferManager::onArchiveStreamDone(bool ok, const QString &error)
{
    if (m_archiveFile) {
        m_archiveFile->close();
        delete m_archiveFile;
        m_archiveFile = nullptr;
    }

    if (m_cancelled) {
        QFile::remove(m_localArchivePath);
        m_localArchivePath.clear();
        cleanupRemoteArchive();
        return;
    }

    if (!ok) {
        QFile::remove(m_localArchivePath);
        m_localArchivePath.clear();
        cleanupRemoteArchive();
        emit transferError(tr("Archive download failed: %1").arg(error));
        m_queue.removeFirst();
        m_active = false;
        startNextTransfer();
        return;
    }

    extractLocalArchive();
}

void RemoteTransferManager::extractLocalArchive()
{
    emit progressUpdated(0, 0, m_archiveBytesWritten, m_archiveBytesWritten,
        tr("Extracting\xe2\x80\xa6"), m_queue.size() - 1);

    // Recompute the common remote base (same algorithm as createRemoteArchive).
    QString commonBase;
    for (const auto &entry : m_downloadManifest) {
        const QString dir = entry.remotePath.left(entry.remotePath.lastIndexOf(QLatin1Char('/')));
        if (commonBase.isEmpty()) {
            commonBase = dir;
        } else {
            int i = 0;
            while (i < commonBase.size() && i < dir.size() && commonBase[i] == dir[i])
                ++i;
            commonBase = commonBase.left(i);
            const int lastSlash = commonBase.lastIndexOf(QLatin1Char('/'));
            commonBase = (lastSlash > 0) ? commonBase.left(lastSlash) : QStringLiteral("/");
        }
    }
    if (commonBase.isEmpty()) commonBase = QStringLiteral("/");

    // Derive local extraction base from the first manifest entry.
    // rel[0] = remotePath.mid(commonBase.size() + 1) is the relative path in the tar.
    // localBase = manifest[0].localPath with that suffix stripped.
    const QString firstRel = m_downloadManifest[0].remotePath.mid(commonBase.size() + 1);
    // chopped() removes n chars from the end; firstRel may be empty if the file is
    // directly under commonBase (no sub-directory). Guard against that case.
    const QString localBase = firstRel.isEmpty()
        ? m_downloadManifest[0].localPath.left(
              m_downloadManifest[0].localPath.lastIndexOf(QLatin1Char('/')))
        : m_downloadManifest[0].localPath.chopped(firstRel.size());

    QDir().mkpath(localBase);

    const QString localArchivePath = m_localArchivePath;
    QPointer<RemoteTransferManager> guard(this);

    QProcess *proc = new QProcess(this);
    proc->setProgram(QStringLiteral("tar"));
    proc->setArguments({QStringLiteral("xf"), localArchivePath,
                        QStringLiteral("-C"), localBase});

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, localArchivePath, guard](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        QFile::remove(localArchivePath);
        if (!guard.isNull()) m_localArchivePath.clear();

        if (guard.isNull()) return;

        if (exitCode != 0) {
            cleanupRemoteArchive();
            emit transferError(tr("Local extraction failed: %1")
                .arg(QString::fromUtf8(proc->readAllStandardError())));
            m_queue.removeFirst();
            m_active = false;
            startNextTransfer();
            return;
        }

        // Emit success status for each file.
        for (const auto &entry : m_downloadManifest) {
            emit fileTransferStatus(entry.remotePath, true, QString());
        }

        const int count = m_downloadManifest.size();
        cleanupRemoteArchive();
        m_queue.removeFirst();
        emit transferCompleted(count);
        startNextTransfer();
    });

    proc->start();
}

void RemoteTransferManager::cleanupRemoteArchive()
{
    if (m_remoteArchivePath.isEmpty()) return;
    auto *ctx = qobject_cast<RemoteExecutionContext *>(m_backend ? m_backend->parent() : nullptr);
    if (ctx) {
        const QString path = m_remoteArchivePath;
        ctx->exec(QString(), {QStringLiteral("rm"), QStringLiteral("-f"), path},
                  QByteArray(), 5000, nullptr);
    }
    m_remoteArchivePath.clear();
}

void RemoteTransferManager::downloadNextFile()
{
    if (m_cancelled) return;
    if (!m_backend) {
        emit transferError(tr("Remote backend lost during download."));
        m_active = false;
        return;
    }

    if (m_manifestIdx >= m_downloadManifest.size()) {
        // All done.
        const int count = m_downloadManifest.size();
        m_queue.removeFirst();
        emit transferCompleted(count);
        startNextTransfer();
        return;
    }

    const DownloadEntry &entry = m_downloadManifest.at(m_manifestIdx);

    m_currentRemotePath = entry.remotePath; // W1: track in-flight path

    emit progressUpdated(m_manifestIdx, m_downloadManifest.size(),
                         m_bytesTransferred, m_totalBytes,
                         QFileInfo(entry.localPath).fileName(),
                         m_queue.size() - 1);

    QPointer<RemoteTransferManager> guard(this);
    const QString remotePath  = entry.remotePath;
    const QString localPath   = entry.localPath;
    const QString partialPath = entry.partialPath;

    // Use tracked read so we can cancel via cancelReadAsync.
    m_hasCurrentRead = true;
    m_currentReadReqId = m_backend->readFileAsyncTracked(remotePath,
        [guard, remotePath, localPath, partialPath]
        (bool ok, const QByteArray &data, const QString &error) {
            if (guard.isNull()) return;
            guard->m_hasCurrentRead = false;
            guard->onFileDownloaded(remotePath, localPath, partialPath, ok, data, error);
        });
}

void RemoteTransferManager::onFileDownloaded(const QString &remotePath,
                                              const QString &localPath,
                                              const QString &partialPath,
                                              bool ok, const QByteArray &data,
                                              const QString &error)
{
    if (m_cancelled) {
        // Clean up partial file if it exists.
        QFile::remove(partialPath);
        return;
    }

    if (!ok) {
        // Delete any partial file and report error.
        QFile::remove(partialPath);
        emit fileTransferStatus(remotePath, false, error);
        emit transferError(tr("Failed to download \"%1\": %2")
                           .arg(QFileInfo(remotePath).fileName(), error));
        // Don't abort the whole transfer — skip and continue.
    } else {
        // Write to .partial, then rename to final name (D4).
        QFile partial(partialPath);
        // Ensure parent directory exists.
        QDir().mkpath(QFileInfo(partialPath).absolutePath());
        if (partial.open(QIODevice::WriteOnly)) {
            partial.write(data);
            partial.close();
            // Atomic-ish rename.
            QFile::remove(localPath);
            if (!partial.rename(localPath)) {
                // Rename failed — leave .partial, report error.
                emit fileTransferStatus(remotePath, false, tr("rename failed"));
                emit transferError(tr("Failed to save \"%1\".")
                                   .arg(QFileInfo(localPath).fileName()));
            } else {
                m_bytesTransferred += data.size();
                emit fileTransferStatus(remotePath, true, QString());
            }
        } else {
            QFile::remove(partialPath);
            emit fileTransferStatus(remotePath, false, tr("cannot write local file"));
            emit transferError(tr("Cannot write to \"%1\".").arg(localPath));
        }
    }

    ++m_manifestIdx;
    downloadNextFile();
}

// ============================================================================
// Upload implementation
// ============================================================================

void RemoteTransferManager::startUpload(const PendingTransfer &tx)
{
    if (!m_backend) {
        m_queue.removeFirst();
        emit transferError(tr("No remote backend available."));
        startNextTransfer();
        return;
    }

    emit progressUpdated(0, 0, 0, 0, tr("Preparing upload…"), m_queue.size() - 1);

    m_conflictResolutions = tx.conflicts;
    m_overrideConflicts   = tx.overrideConflicts;

    buildUploadManifest(tx.localPaths, tx.localPath);
}

void RemoteTransferManager::buildUploadManifest(const QStringList &localPaths,
                                                 const QString &remoteDestDir)
{
    m_uploadManifest.clear();

    for (const QString &lp : localPaths) {
        const QFileInfo fi(lp);
        if (!fi.exists()) continue;

        if (fi.isDir()) {
            // Recursive walk.
            const QString baseName = fi.fileName();
            const QString remoteBase = remoteDestDir + QLatin1Char('/') + baseName;
            // Walk the local dir.
            QDirIterator it(lp, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString filePath = it.next();
                const QString relPath = QDir(lp).relativeFilePath(filePath);
                const QString remotePath = remoteBase + QLatin1Char('/') + relPath;
                UploadEntry entry;
                entry.localPath  = filePath;
                entry.remotePath = remotePath;
                entry.size       = QFileInfo(filePath).size();
                m_uploadManifest.append(entry);
                m_totalBytes += entry.size;
            }
        } else if (fi.isFile()) {
            UploadEntry entry;
            entry.localPath  = lp;
            entry.remotePath = remoteDestDir + QLatin1Char('/') + fi.fileName();
            entry.size       = fi.size();
            m_uploadManifest.append(entry);
            m_totalBytes += entry.size;
        }
    }

    if (m_uploadManifest.isEmpty()) {
        m_queue.removeFirst();
        emit transferCompleted(0);
        startNextTransfer();
        return;
    }

    if (m_overrideConflicts) {
        // Bypass conflict detection entirely.
        uploadWithManifest({}, true);
    } else if (!m_conflictResolutions.isEmpty()) {
        // Already resolved (e.g. from a prior dialog call).
        uploadWithManifest(m_conflictResolutions, false);
    } else {
        // Run conflict detection.
        runConflictDetection(m_queue.front().localPath);
    }
}

void RemoteTransferManager::runConflictDetection(const QString &remoteDestDir)
{
    if (m_cancelled || !m_backend) return;

    // Collect unique parent directories from the upload manifest (D3).
    QSet<QString> parentDirs;
    for (const UploadEntry &e : std::as_const(m_uploadManifest)) {
        const int slash = e.remotePath.lastIndexOf(QLatin1Char('/'));
        if (slash > 0)
            parentDirs.insert(e.remotePath.left(slash));
        else
            parentDirs.insert(remoteDestDir);
    }

    m_pendingConflictDirs = parentDirs.size();
    m_totalConflictDirs = m_pendingConflictDirs; // W2: for scan progress
    if (m_pendingConflictDirs == 0) {
        uploadWithManifest({}, false);
        return;
    }

    // W2: emit progress to trigger the progress bar and show scanning state.
    // currentFile is set to a scan label (displayed by the dock's setStatusLabel).
    // We pass empty currentFile so the dock's setCurrentFile branch is skipped;
    // the dock lambda calls setStatusLabel separately when it detects scan phase.
    const QString scanLabel = tr("Scanning remote destination…");
    emit progressUpdated(0, m_pendingConflictDirs, 0, 0,
                         scanLabel,
                         m_queue.size() - 1);

    for (const QString &dir : std::as_const(parentDirs)) {
        QPointer<RemoteTransferManager> guard(this);
        const QString d = dir;
        m_backend->readdirAsync(dir,
            [guard, d](bool ok, const QList<RemoteDirEntry> &entries, const QString &error) {
                if (guard.isNull()) return;
                if (guard->m_cancelled) return;
                guard->onConflictReaddir(d, ok, entries, error);
            });
    }
}

void RemoteTransferManager::onConflictReaddir(const QString &dir, bool ok,
                                               const QList<RemoteDirEntry> &entries,
                                               const QString &error)
{
    Q_UNUSED(error)
    if (ok) {
        QSet<QString> names;
        names.reserve(entries.size());
        for (const RemoteDirEntry &e : entries) {
            if (!e.isDir) names.insert(e.name);
        }
        m_remoteFileSets.insert(dir, names);
    }
    // If dir doesn't exist yet, no conflict — empty set is fine.

    // W2: emit per-directory scan progress so the progress bar label updates.
    const int scanned = m_totalConflictDirs - m_pendingConflictDirs + 1;
    const QString scanLabelIncr = tr("Scanning remote destination… (%1/%2)")
                                      .arg(scanned).arg(m_totalConflictDirs);
    emit progressUpdated(scanned, m_totalConflictDirs, 0, 0,
                         scanLabelIncr,
                         m_queue.size() - 1);

    if (--m_pendingConflictDirs == 0) {
        // All readdir calls done. Find conflicts.
        QStringList conflicts;
        for (const UploadEntry &e : std::as_const(m_uploadManifest)) {
            const int slash = e.remotePath.lastIndexOf(QLatin1Char('/'));
            const QString dir2 = (slash > 0) ? e.remotePath.left(slash) : QString();
            const QString name = e.remotePath.section(QLatin1Char('/'), -1);
            if (m_remoteFileSets.contains(dir2) && m_remoteFileSets[dir2].contains(name))
                conflicts.append(e.remotePath);
        }

        if (conflicts.isEmpty()) {
            uploadWithManifest({}, false);
        } else {
            // Signal to the dock/MainWindow to show the conflict dialog.
            emit uploadConflictsDetected(conflicts, m_queue.front().localPath,
                                         m_queue.front().localPaths);
            // The caller calls resolveUploadConflicts() which resumes upload.
        }
    }
}

void RemoteTransferManager::uploadWithManifest(const QHash<QString, ConflictResolution> &conflicts,
                                                bool overrideConflicts)
{
    if (m_cancelled) return;

    m_conflictResolutions = conflicts;
    m_overrideConflicts   = overrideConflicts;
    m_uploadIdx = 0;
    uploadNextFile();
}

void RemoteTransferManager::uploadNextFile()
{
    if (m_cancelled) return;
    if (!m_backend) {
        emit transferError(tr("Remote backend lost during upload."));
        m_active = false;
        return;
    }

    // Skip files resolved as Skip (unless overrideConflicts).
    while (m_uploadIdx < m_uploadManifest.size()) {
        const UploadEntry &e = m_uploadManifest.at(m_uploadIdx);
        if (!m_overrideConflicts && m_conflictResolutions.contains(e.remotePath)) {
            if (m_conflictResolutions[e.remotePath] == ConflictResolution::Skip) {
                ++m_uploadIdx;
                continue;
            }
        }
        break;
    }

    if (m_uploadIdx >= m_uploadManifest.size()) {
        // All done.
        const int count = m_uploadManifest.size();
        m_queue.removeFirst();
        emit transferCompleted(count);
        startNextTransfer();
        return;
    }

    const UploadEntry &entry = m_uploadManifest.at(m_uploadIdx);

    m_currentRemotePath = entry.remotePath; // W1: track in-flight path

    emit progressUpdated(m_uploadIdx, m_uploadManifest.size(),
                         m_bytesTransferred, m_totalBytes,
                         QFileInfo(entry.localPath).fileName(),
                         m_queue.size() - 1);

    // Ensure remote parent directory exists.
    const int slash = entry.remotePath.lastIndexOf(QLatin1Char('/'));
    const QString remoteDir = (slash > 0) ? entry.remotePath.left(slash) : QString();

    QPointer<RemoteTransferManager> guard(this);
    const UploadEntry entryCopy = entry; // value copy — capture safe

    auto doUpload = [guard, entryCopy]() {
        if (guard.isNull() || guard->m_cancelled) return;
        if (!guard->m_backend) {
            emit guard->transferError(QObject::tr("Remote backend lost."));
            return;
        }

        QFile f(entryCopy.localPath);
        if (!f.open(QIODevice::ReadOnly)) {
            emit guard->transferError(
                QObject::tr("Cannot read local file \"%1\".").arg(entryCopy.localPath));
            ++guard->m_uploadIdx;
            guard->uploadNextFile();
            return;
        }
        const QByteArray data = f.readAll();
        f.close();

        const QString remotePath = entryCopy.remotePath;
        guard->m_backend->writeFileAsync(remotePath, data,
            [guard, remotePath, dataSize = data.size()](bool ok, const QString &error) {
                if (guard.isNull()) return;
                if (guard->m_cancelled) return;
                guard->onFileUploaded(remotePath, ok, error);
                if (ok) guard->m_bytesTransferred += dataSize;
            });
    };

    if (!remoteDir.isEmpty() && !m_createdDirs.contains(remoteDir)) {
        ensureRemoteDir(remoteDir, [guard, doUpload](bool /*ok*/) {
            if (guard.isNull() || guard->m_cancelled) return;
            doUpload();
        });
    } else {
        doUpload();
    }
}

void RemoteTransferManager::ensureRemoteDir(const QString &remoteDirPath,
                                             std::function<void(bool)> callback)
{
    if (m_createdDirs.contains(remoteDirPath)) {
        if (callback) callback(true);
        return;
    }

    QPointer<RemoteTransferManager> guard(this);
    const QString dir = remoteDirPath;

    // Check parent first (recursive ensure).
    const int slash = remoteDirPath.lastIndexOf(QLatin1Char('/'));
    if (slash > 0) {
        const QString parent = remoteDirPath.left(slash);
        ensureRemoteDir(parent, [guard, dir, callback](bool ok) {
            if (guard.isNull() || guard->m_cancelled) { if (callback) callback(false); return; }
            if (!ok) { if (callback) callback(false); return; }
            guard->m_backend->mkdirAsync(dir,
                [guard, dir, callback](bool mkOk, const QString & /*error*/) {
                    if (guard.isNull()) { if (callback) callback(false); return; }
                    // mkdir succeeds or fails (e.g. already exists); treat both as ok.
                    guard->m_createdDirs.insert(dir);
                    if (callback) callback(true);
                });
        });
    } else {
        m_backend->mkdirAsync(remoteDirPath,
            [guard, remoteDirPath, callback](bool /*ok*/, const QString & /*error*/) {
                if (guard.isNull()) { if (callback) callback(false); return; }
                guard->m_createdDirs.insert(remoteDirPath);
                if (callback) callback(true);
            });
    }
}

void RemoteTransferManager::onFileUploaded(const QString &remotePath, bool ok,
                                            const QString &error)
{
    if (m_cancelled) return;

    emit fileTransferStatus(remotePath, ok, error);

    if (!ok) {
        emit transferError(tr("Failed to upload \"%1\": %2")
                           .arg(remotePath.section(QLatin1Char('/'), -1), error));
        // Don't abort — continue with next file.
    }

    ++m_uploadIdx;
    uploadNextFile();
}

// ============================================================================
// Connection-lost handler
// ============================================================================

void RemoteTransferManager::onConnectionLost(const QString &reason)
{
    if (!m_active) return;

    // W1: Capture progress before clearing state so the error message is informative.
    const bool wasUpload = !m_queue.isEmpty() &&
                           m_queue.front().direction == Direction::Upload;
    const int completed = wasUpload ? m_uploadIdx : m_manifestIdx;
    const int total = wasUpload ? m_uploadManifest.size() : m_downloadManifest.size();
    const QString inFlight = m_currentRemotePath;

    m_cancelled = true;
    m_active = false;
    m_currentRemotePath.clear();
    // Clean up any in-progress archive streaming (connection is dead, no remote cleanup possible).
    if (m_archiveFile) {
        m_archiveFile->close();
        delete m_archiveFile;
        m_archiveFile = nullptr;
    }
    if (!m_localArchivePath.isEmpty()) {
        QFile::remove(m_localArchivePath);
        m_localArchivePath.clear();
    }
    m_remoteArchivePath.clear(); // connection is gone, no point trying rm -f
    m_queue.clear();
    m_downloadManifest.clear();
    m_uploadManifest.clear();

    QString msg = tr("Transfer interrupted: %1. %2 of %3 files transferred.")
                      .arg(reason)
                      .arg(completed)
                      .arg(total);
    if (wasUpload && !inFlight.isEmpty()) {
        msg += QLatin1Char('\n');
        msg += tr("File \"%1\" may be partially written on remote.")
                   .arg(inFlight.section(QLatin1Char('/'), -1));
    }
    emit transferError(msg);
}

} // namespace remote

