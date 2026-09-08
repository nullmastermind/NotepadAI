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

#ifndef FOLDERZIPTRANSFER_H
#define FOLDERZIPTRANSFER_H

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>
#include <memory>

#include "remote/GitignoreMatcher.h"

class QFile;
class QWidget;
class ZipWriter;

namespace remote {
class RemoteFsBackend;
}

class FolderZipTransfer : public QObject
{
    Q_OBJECT

public:
    explicit FolderZipTransfer(QObject *parent = nullptr);
    ~FolderZipTransfer() override;

    bool isBusy() const { return m_busy; }
    void setRemoteBackend(remote::RemoteFsBackend *backend);
    void reportZipProgress(int current, int total, const QString &currentFile);

    void downloadLocal(const QString &workspaceRoot, const QString &selectedFolder,
                       const QString &destZip);
    void uploadLocal(const QString &zipPath, const QString &selectedFolder, QWidget *dialogParent);

    void downloadRemote(const QString &workspaceRootPosix, const QString &selectedFolderPosix,
                        const QString &destZip);
    void uploadRemote(const QString &zipPath, const QString &selectedFolderPosix,
                      QWidget *dialogParent);

public slots:
    void cancel();

signals:
    void progressUpdated(int current, int total, qint64 bytesTransferred, qint64 totalBytes,
                         const QString &currentFile, int queuedCount);
    void fileTransferStatus(const QString &relativePath, bool ok, const QString &error);
    void transferCompleted(int fileCount);
    void transferCancelled();
    void transferError(const QString &message);

private:
    struct RemoteFile {
        QString remotePath;
        QString entryName;
    };
    struct ExtractItem {
        QString zipEntry;
        QString destRel;
    };

    void finishIdle();
    void fail(const QString &message);
    void succeed(int n);
    bool beginBusy();
    void cleanupWriter();

    bool buildExtractManifest(const QString &zipPath, QList<ExtractItem> *items, QString *error);
    QList<ExtractItem> applyConflictDialog(const QList<ExtractItem> &items,
                                           const QStringList &conflicts,
                                           QWidget *dialogParent, bool *aborted);

    void remotePreloadGitignores();
    void remoteWalkDir(const QString &remoteDir, const QString &entryPrefix);
    void remoteWalkDone();
    void remotePackNext();
    void remoteUploadNext();
    void ensureRemoteParent(const QString &remoteFilePath, const std::function<void(bool)> &cb);

    bool m_busy = false;
    std::atomic<bool> m_cancelled{false};
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    std::shared_ptr<struct ZipProgressSink> m_progressSink;
    QPointer<remote::RemoteFsBackend> m_backend;

    QString m_partialPath;
    QString m_destZip;
    QString m_workspaceRoot;
    QString m_selectedFolder;
    QString m_zipPath;

    remote::GitignoreMatcher m_gitignore;
    QStringList m_gitignoreDirs;
    int m_gitignoreIdx = 0;
    int m_pendingWalk = 0;
    bool m_walkFailed = false;
    QList<RemoteFile> m_remoteFiles;
    int m_remoteIdx = 0;
    ZipWriter *m_writer = nullptr;
    QFile *m_streamFile = nullptr;

    QList<ExtractItem> m_extractItems;
    int m_extractIdx = 0;
    QSet<QString> m_ensuredDirs;
};

#endif // FOLDERZIPTRANSFER_H
