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

#ifndef REMOTE_TRANSFER_MANAGER_H
#define REMOTE_TRANSFER_MANAGER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

#include "GitignoreMatcher.h"
#include "RemoteFsBackend.h"

class QFile;

namespace remote {

// RemoteTransferManager — Orchestrates SFTP downloads and uploads for a single
// SSH workspace. Owned by FolderAsWorkspaceDock as a QObject child.
//
// Design decisions (from design.md):
//   D1: QObject child of FolderAsWorkspaceDock; holds QPointer<RemoteFsBackend>
//   D2: All callbacks capture QPointer<RemoteTransferManager> + value types only
//   D3: Readdir-based conflict detection for uploads
//   D4: Download writes to <name>.partial, renames on success, deletes on failure
//   D5: Cancel = logical only (m_cancelled flag + cancelReadAsync for bulk lane)
//   D6: Queue model — one active transfer at a time
class RemoteTransferManager : public QObject
{
    Q_OBJECT

public:
    // Transfer direction enum for queue entries.
    enum class Direction : quint8 { Download, Upload };

    // How to handle gitignore during download.
    enum class GitignoreFilter : quint8 { None, Apply };

    // Upload conflict resolution.
    enum class ConflictResolution : quint8 { Skip, Replace };

    // A pending transfer in the queue.
    struct PendingTransfer
    {
        Direction direction;
        QStringList remotePaths;      // Source remote paths (download) or empty (upload)
        QString localPath;            // Destination local path (download) or source (upload)
        QStringList localPaths;       // Multiple local source paths (upload multi-file)
        bool isRecursive = false;
        GitignoreFilter gitignore = GitignoreFilter::None;
        QHash<QString, ConflictResolution> conflicts; // pre-resolved conflicts from dialog
        bool overrideConflicts = false;               // bypass conflict detection
    };

    explicit RemoteTransferManager(RemoteFsBackend *backend, QObject *parent = nullptr);

    bool isTransferring() const { return m_active; }
    int queuedCount() const { return m_queue.size(); }
    QString currentLocalDestination() const;

    // --- Download API --------------------------------------------------------

    // Download a single remote file to localDestPath.
    void downloadFile(const QString &remotePath, const QString &localDestPath);

    // Download remote path(s) recursively to localDestDir.
    // If filter == GitignoreFilter::Apply, .gitignore files are read during the walk.
    void downloadRecursive(const QStringList &remotePaths, const QString &localDestDir,
                           GitignoreFilter filter = GitignoreFilter::None);

    // --- Upload API ----------------------------------------------------------

    // Upload local files to the remote directory at remoteDestDir.
    // Conflict detection runs first (readdir-based); dialog shown if any found.
    void uploadFiles(const QStringList &localFilePaths, const QString &remoteDestDir,
                     const QHash<QString, ConflictResolution> &conflicts = {},
                     bool overrideConflicts = false);

    // Upload a local directory (recursively) to remoteDestDir.
    void uploadFolder(const QString &localFolderPath, const QString &remoteDestDir,
                      const QHash<QString, ConflictResolution> &conflicts = {},
                      bool overrideConflicts = false);

signals:
    // Emitted periodically during transfer.
    // `current` = files done, `total` = files in manifest, `currentFile` = name being transferred.
    void progressUpdated(int current, int total, qint64 bytesTransferred, qint64 totalBytes,
                         const QString &currentFile, int queuedCount);

    // Emitted per file after a download/upload finishes (success or failure).
    // `relativePath` is the path shown to the user (remote path for download,
    // local filename for upload). `ok` = file transferred successfully.
    void fileTransferStatus(const QString &relativePath, bool ok, const QString &error);

    // Emitted when the active transfer completes successfully.
    void transferCompleted(int fileCount);

    // Emitted when the transfer is cancelled by the user.
    void transferCancelled();

    // Emitted when a transfer fails (partial or total failure).
    void transferError(const QString &message);

    // Emitted before upload starts when conflicts are detected — caller shows
    // TransferConflictDialog and calls resolveUploadConflicts().
    void uploadConflictsDetected(const QStringList &conflictPaths,
                                 const QString &remoteDestDir,
                                 const QStringList &localPaths);

public slots:
    // Cancel the current transfer. Sets m_cancelled; the .partial file (if any)
    // will be deleted in the next download callback.
    void cancel();

    // Called by caller after conflict dialog returns. Either override=true (skip detection)
    // or a per-path resolution map. Resumes the queued upload.
    void resolveUploadConflicts(const QHash<QString, ConflictResolution> &resolutions,
                                bool overrideAll);

    // Called when the SSH connection drops (wired from RemoteExecutionContext in
    // FolderAsWorkspaceDock::setupTransferManager). Aborts any active transfer and
    // emits transferError with partial-progress details (C1/W1 fix).
    void onConnectionLost(const QString &reason);

private:
    // --- Internal download implementation ------------------------------------

    // A flat file entry in the download manifest (built during recursive walk).
    struct DownloadEntry
    {
        QString remotePath;
        QString localPath;        // Full local destination path (including filename)
        QString partialPath;      // localPath + ".partial"
        qint64  size = 0;
    };

    void startNextTransfer();
    void startDownload(const PendingTransfer &tx);
    void startUpload(const PendingTransfer &tx);

    // Recursive readdir walk to build the download manifest.
    // Calls downloadWalkDone() when the full manifest is known.
    void buildDownloadManifest(const QStringList &remotePaths, const QString &localDestDir,
                               GitignoreFilter filter);
    // Dispatch: uses exec fast-path (walkRemoteDirExec) when filter==None and
    // RemoteExecutionContext is available; falls back to SFTP walk otherwise.
    void walkRemoteDir(const QString &remoteDirPath, const QString &localDirPath,
                       const QString &relBase, GitignoreFilter filter,
                       const std::shared_ptr<int> &pending, const std::shared_ptr<bool> &failed);
    // Fast path: single SSH exec of `find` — one round-trip for the whole tree.
    // Only called for GitignoreFilter::None.
    void walkRemoteDirExec(const QString &remoteDirPath, const QString &localDirPath,
                           const std::shared_ptr<int> &pending, const std::shared_ptr<bool> &failed);
    // Slow (SFTP) path: serial readdirAsync calls, one per directory.
    // Used when gitignore filtering is needed or exec is unavailable.
    void walkRemoteDirSftp(const QString &remoteDirPath, const QString &localDirPath,
                           const QString &relBase, GitignoreFilter filter,
                           const std::shared_ptr<int> &pending, const std::shared_ptr<bool> &failed);
    void downloadWalkDone();

    // Kick off downloading the next file in m_downloadManifest at m_manifestIdx.
    void downloadNextFile();
    void onFileDownloaded(const QString &remotePath, const QString &localPath,
                          const QString &partialPath, bool ok,
                          const QByteArray &data, const QString &error);

    // --- Archive-based batch download (fast path for 2+ files when tar is available) ---
    void downloadViaArchive();
    void probeRemoteTar(const std::function<void(bool)> &callback);
    void createRemoteArchive();
    void onArchiveCreated(int exitCode, const QByteArray &out, const QByteArray &err);
    void streamArchiveToLocal();
    void onArchiveStreamDone(bool ok, const QString &error);
    void extractLocalArchive();
    void cleanupRemoteArchive();

    // --- Internal upload implementation --------------------------------------

    // A flat file entry in the upload manifest.
    struct UploadEntry
    {
        QString localPath;
        QString remotePath;
        qint64  size = 0;
    };

    // Build the upload manifest from local file/dir list and remote dest.
    void buildUploadManifest(const QStringList &localPaths, const QString &remoteDestDir);
    void uploadWithManifest(const QHash<QString, ConflictResolution> &conflicts,
                            bool overrideConflicts);
    void runConflictDetection(const QString &remoteDestDir);
    void onConflictReaddir(const QString &dir, bool ok,
                           const QList<RemoteDirEntry> &entries, const QString &error);
    void uploadNextFile();
    void onFileUploaded(const QString &remotePath, bool ok, const QString &error);
    void ensureRemoteDir(const QString &remoteDirPath, const std::function<void(bool)> &callback);

    // --- State ---------------------------------------------------------------

    QPointer<RemoteFsBackend> m_backend;

    bool m_active = false;
    bool m_cancelled = false;

    // Queue of pending transfers (head is the one currently running).
    QList<PendingTransfer> m_queue;

    // Download state.
    QList<DownloadEntry> m_downloadManifest;
    int m_manifestIdx = 0;
    qint64 m_bytesTransferred = 0;
    qint64 m_totalBytes = 0;
    // In-flight read reqId for the bulk lane (to cancel on user cancel).
    quint64 m_currentReadReqId = 0;
    bool m_hasCurrentRead = false;
    // Gitignore state for the current download.
    GitignoreMatcher m_gitignore;
    // Track in-flight directory reads during walk so we know when done.
    // pendingDirs: number of outstanding readdirAsync calls.
    int m_pendingWalkDirs = 0;
    bool m_walkFailed = false;

    // Archive-based batch download state.
    bool m_tarAvailable = false;
    bool m_tarProbed = false;
    QString m_remoteArchivePath;  // path of the tar created on the remote
    QString m_localArchivePath;   // path of the downloaded tar on local disk
    QFile  *m_archiveFile = nullptr; // open temp file receiving the archive stream
    qint64  m_archiveBytesWritten = 0;

    // Upload state.
    QList<UploadEntry> m_uploadManifest;
    int m_uploadIdx = 0;
    // Remote path of the file currently being written (for connection-loss report).
    QString m_currentRemotePath;
    // Conflict detection state.
    QStringList m_conflictDirsToCheck;
    QHash<QString, QSet<QString>> m_remoteFileSets; // dir -> set of filenames
    int m_pendingConflictDirs = 0;
    int m_totalConflictDirs = 0;    // W2: total dirs for conflict scan progress
    bool m_conflictDetectionDone = false;
    // Conflict resolution from dialog.
    QHash<QString, ConflictResolution> m_conflictResolutions;
    bool m_overrideConflicts = false;
    // Remote directories we've already ensured exist (to avoid duplicate mkdirAsync).
    QSet<QString> m_createdDirs;
};

} // namespace remote

#endif // REMOTE_TRANSFER_MANAGER_H
