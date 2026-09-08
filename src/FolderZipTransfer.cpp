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

#include "FolderZipTransfer.h"

#include "ZipArchive.h"
#include "ZipIgnoreWalk.h"
#include "ZipPath.h"
#include "dialogs/TransferConflictDialog.h"
#include "remote/RemoteFsBackend.h"
#include "remote/SshSessionWorker.h"

#include <memory>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QTemporaryFile>
#include <QtConcurrent>

namespace {

ZipPath::DestOs destOs()
{
#ifdef Q_OS_WIN
    return ZipPath::DestOs::Windows;
#else
    return ZipPath::DestOs::Posix;
#endif
}

QString posixJoin(const QString &a, const QString &b)
{
    if (a.isEmpty())
        return b;
    if (a.endsWith(QLatin1Char('/')))
        return a + b;
    return a + QLatin1Char('/') + b;
}

QString parentPosix(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return slash == 0 ? QStringLiteral("/") : QString();
    return path.left(slash);
}

bool shouldReportProgress(int n, int total)
{
    return n <= 1 || n == total || (n % 32) == 0;
}

QString relToRoot(const QString &root, const QString &abs)
{
    if (root.isEmpty() || !abs.startsWith(root))
        return abs;
    QString r = abs.mid(root.size());
    if (r.startsWith(QLatin1Char('/')))
        r.remove(0, 1);
    return r;
}

} // namespace

FolderZipTransfer::FolderZipTransfer(QObject *parent)
    : QObject(parent)
{
}

FolderZipTransfer::~FolderZipTransfer()
{
    m_cancelled.store(true);
    cleanupWriter();
}

void FolderZipTransfer::setRemoteBackend(remote::RemoteFsBackend *backend)
{
    m_backend = backend;
}

bool FolderZipTransfer::beginBusy()
{
    if (m_busy)
        return false;
    m_busy = true;
    m_cancelled.store(false);
    m_walkFailed = false;
    m_pendingWalk = 0;
    m_remoteFiles.clear();
    m_remoteIdx = 0;
    m_extractItems.clear();
    m_extractIdx = 0;
    m_ensuredDirs.clear();
    m_gitignore.clear();
    m_gitignoreDirs.clear();
    m_gitignoreIdx = 0;
    m_partialPath.clear();
    cleanupWriter();
    return true;
}

void FolderZipTransfer::cleanupWriter()
{
    if (m_writer) {
        m_writer->abort();
        delete m_writer;
        m_writer = nullptr;
    }
    if (m_streamFile) {
        m_streamFile->remove();
        delete m_streamFile;
        m_streamFile = nullptr;
    }
}

void FolderZipTransfer::finishIdle()
{
    cleanupWriter();
    m_busy = false;
}

void FolderZipTransfer::fail(const QString &message)
{
    if (!m_partialPath.isEmpty())
        QFile::remove(m_partialPath);
    finishIdle();
    emit transferError(message);
}

void FolderZipTransfer::succeed(int n)
{
    finishIdle();
    emit transferCompleted(n);
}

void FolderZipTransfer::cancel()
{
    if (!m_busy)
        return;
    m_cancelled.store(true);
    if (!m_partialPath.isEmpty())
        QFile::remove(m_partialPath);
    finishIdle();
    emit transferCancelled();
}

void FolderZipTransfer::downloadLocal(const QString &workspaceRoot, const QString &selectedFolder,
                                      const QString &destZip)
{
    if (!beginBusy())
        return;
    m_destZip = destZip;
    m_partialPath = destZip + QStringLiteral(".partial");
    QFile::remove(m_partialPath);
    emit progressUpdated(0, 0, 0, 0, tr("Preparing zip…"), 0);

    const QString root = workspaceRoot;
    const QString sel = selectedFolder;
    const QString dest = destZip;
    const QString partial = m_partialPath;
    QPointer<FolderZipTransfer> guard(this);

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, guard, watcher, dest, partial]() {
        watcher->deleteLater();
        if (guard.isNull())
            return;
        if (m_cancelled.load()) {
            QFile::remove(partial);
            return;
        }
        const QString err = watcher->result();
        if (!err.isEmpty()) {
            QFile::remove(partial);
            if (err == QLatin1String("empty")) {
                finishIdle();
                emit transferError(tr("Folder is empty or contains only ignored files"));
                return;
            }
            if (err == QLatin1String("cancelled")) {
                finishIdle();
                emit transferCancelled();
                return;
            }
            fail(err);
            return;
        }
        QFile::remove(dest);
        if (!QFile::rename(partial, dest)) {
            fail(tr("Could not write zip file"));
            return;
        }
        m_partialPath.clear();
        succeed(0);
    });
    watcher->setFuture(QtConcurrent::run([this, root, sel, dest, partial]() -> QString {
        const QList<ZipIgnoreWalk::File> files = ZipIgnoreWalk::walkLocalFolder(root, sel);
        QList<ZipIgnoreWalk::File> kept;
        kept.reserve(files.size());
        const QString destClean = QDir::cleanPath(dest);
        const QString partialClean = QDir::cleanPath(partial);
        for (const ZipIgnoreWalk::File &f : files) {
            const QString p = QDir::cleanPath(f.absPath);
            if (p == destClean || p == partialClean)
                continue;
            kept.append(f);
        }
        if (kept.isEmpty())
            return QStringLiteral("empty");
        if (m_cancelled.load())
            return QStringLiteral("cancelled");

        ZipWriter writer;
        if (!writer.open(partial))
            return writer.errorString();
        const int total = kept.size();
        for (int i = 0; i < total; ++i) {
            const ZipIgnoreWalk::File &f = kept.at(i);
            if (m_cancelled.load()) {
                writer.abort();
                QFile::remove(partial);
                return QStringLiteral("cancelled");
            }
            const int n = i + 1;
            const QString name = f.entryName;
            if (shouldReportProgress(n, total)) {
                QPointer<FolderZipTransfer> g(this);
                QMetaObject::invokeMethod(this, [g, n, total, name]() {
                    if (g.isNull() || g->m_cancelled.load())
                        return;
                    emit g->progressUpdated(n, total, 0, 0, name, 0);
                }, Qt::QueuedConnection);
            }
            if (!writer.addFile(f.absPath, f.entryName)) {
                writer.abort();
                return writer.errorString();
            }
        }
        if (!writer.finish())
            return writer.errorString();
        return {};
    }));
}

bool FolderZipTransfer::buildExtractManifest(const QString &zipPath, QList<ExtractItem> *items,
                                             QString *error)
{
    ZipReader reader;
    if (!reader.open(zipPath)) {
        if (error)
            *error = reader.errorString();
        return false;
    }
    const QStringList raw = reader.fileEntries();
    if (raw.isEmpty()) {
        if (error)
            *error = tr("ZIP is empty");
        return false;
    }
    const QString prefix = ZipPath::detectCommonPrefix(raw);
    const ZipPath::DestOs os = destOs();
    items->clear();
    for (const QString &rawName : raw) {
        QString stripped = ZipPath::normalizeZipEntryName(rawName);
        if (!prefix.isEmpty() && stripped.startsWith(prefix))
            stripped = stripped.mid(prefix.size());
        if (stripped.isEmpty())
            continue;
        const QString verr = ZipPath::validateExtractRelPath(stripped, os);
        if (!verr.isEmpty()) {
            emit fileTransferStatus(stripped, false, verr);
            continue;
        }
        items->append({rawName, stripped});
    }
    if (items->isEmpty()) {
        if (error)
            *error = tr("ZIP is empty");
        return false;
    }
    return true;
}

QList<FolderZipTransfer::ExtractItem> FolderZipTransfer::applyConflictDialog(
    const QList<ExtractItem> &items, const QStringList &conflicts, QWidget *dialogParent,
    bool *aborted)
{
    *aborted = false;
    if (conflicts.isEmpty())
        return items;
    TransferConflictDialog dlg(conflicts, dialogParent);
    if (dlg.exec() != QDialog::Accepted) {
        *aborted = true;
        return {};
    }
    if (dlg.overrideAll())
        return items;
    const auto res = dlg.resolutions();
    QList<ExtractItem> kept;
    kept.reserve(items.size());
    for (const ExtractItem &it : items) {
        const auto found = res.find(it.destRel);
        if (found != res.end() && found.value() == TransferConflictDialog::Skip)
            continue;
        kept.append(it);
    }
    return kept;
}

void FolderZipTransfer::uploadLocal(const QString &zipPath, const QString &selectedFolder,
                                    QWidget *dialogParent)
{
    if (!beginBusy())
        return;
    emit progressUpdated(0, 0, 0, 0, tr("Preparing zip…"), 0);
    QList<ExtractItem> items;
    QString err;
    if (!buildExtractManifest(zipPath, &items, &err)) {
        fail(err);
        return;
    }
    QStringList conflicts;
    for (const ExtractItem &it : items) {
        const QString dest = QDir(selectedFolder).filePath(it.destRel);
        if (QFileInfo::exists(dest))
            conflicts.append(it.destRel);
    }
    bool aborted = false;
    items = applyConflictDialog(items, conflicts, dialogParent, &aborted);
    if (aborted) {
        finishIdle();
        emit transferCancelled();
        return;
    }
    if (items.isEmpty()) {
        succeed(0);
        return;
    }

    const QString zip = zipPath;
    const QString sel = selectedFolder;
    QPointer<FolderZipTransfer> guard(this);
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, guard, watcher]() {
        watcher->deleteLater();
        if (guard.isNull() || m_cancelled.load())
            return;
        const QString err = watcher->result();
        if (!err.isEmpty()) {
            if (err == QLatin1String("cancelled")) {
                finishIdle();
                emit transferCancelled();
                return;
            }
            fail(err);
            return;
        }
        succeed(0);
    });
    watcher->setFuture(QtConcurrent::run([this, zip, sel, items]() -> QString {
        ZipReader reader;
        if (!reader.open(zip))
            return reader.errorString();
        const int total = items.size();
        for (int i = 0; i < total; ++i) {
            const ExtractItem &it = items.at(i);
            if (m_cancelled.load())
                return QStringLiteral("cancelled");
            const int n = i + 1;
            const QString name = it.destRel;
            if (shouldReportProgress(n, total)) {
                QPointer<FolderZipTransfer> g(this);
                QMetaObject::invokeMethod(this, [g, n, total, name]() {
                    if (g.isNull() || g->m_cancelled.load())
                        return;
                    emit g->progressUpdated(n, total, 0, 0, name, 0);
                }, Qt::QueuedConnection);
            }
            const QString dest = QDir::cleanPath(QDir(sel).filePath(it.destRel));
            if (!ZipPath::isSafeExtractDest(sel, dest))
                continue;
            QDir().mkpath(QFileInfo(dest).absolutePath());
            if (!ZipPath::isSafeExtractDest(sel, dest))
                continue;
            if (!reader.extractToFile(it.zipEntry, dest))
                return reader.errorString();
        }
        return {};
    }));
}

void FolderZipTransfer::downloadRemote(const QString &workspaceRootPosix,
                                       const QString &selectedFolderPosix,
                                       const QString &destZip)
{
    if (!beginBusy())
        return;
    if (!m_backend) {
        fail(tr("No remote filesystem"));
        return;
    }
    m_workspaceRoot = workspaceRootPosix;
    m_selectedFolder = selectedFolderPosix;
    m_destZip = destZip;
    m_partialPath = destZip + QStringLiteral(".partial");
    QFile::remove(m_partialPath);
    emit progressUpdated(0, 0, 0, 0, tr("Preparing zip…"), 0);

    QString walk = workspaceRootPosix;
    m_gitignoreDirs.append(walk);
    const QString rel = relToRoot(workspaceRootPosix, selectedFolderPosix);
    if (!rel.isEmpty()) {
        for (const QString &seg : rel.split(QLatin1Char('/'))) {
            walk = posixJoin(walk, seg);
            m_gitignoreDirs.append(walk);
        }
    }
    remotePreloadGitignores();
}

void FolderZipTransfer::remotePreloadGitignores()
{
    if (m_gitignoreIdx >= m_gitignoreDirs.size()) {
        m_pendingWalk = 1;
        remoteWalkDir(m_selectedFolder, QString());
        return;
    }
    QPointer<FolderZipTransfer> guard(this);
    const QString dir = m_gitignoreDirs.at(m_gitignoreIdx);
    ++m_gitignoreIdx;
    const QString gi = posixJoin(dir, QStringLiteral(".gitignore"));
    m_backend->readFileAsync(gi, [guard, dir](bool ok, const QByteArray &data, const QString &) {
        if (guard.isNull() || guard->m_cancelled.load())
            return;
        if (ok)
            guard->m_gitignore.addRules(dir, QString::fromUtf8(data));
        guard->remotePreloadGitignores();
    });
}

void FolderZipTransfer::remoteWalkDir(const QString &remoteDir, const QString &entryPrefix)
{
    QPointer<FolderZipTransfer> guard(this);
    QPointer<remote::RemoteFsBackend> backend = m_backend;
    m_backend->readdirAsync(remoteDir,
        [guard, backend, remoteDir, entryPrefix](bool ok, const QList<remote::RemoteDirEntry> &entries,
                                                 const QString &) {
            if (guard.isNull() || guard->m_cancelled.load())
                return;
            if (!ok) {
                guard->m_walkFailed = true;
                if (--guard->m_pendingWalk == 0)
                    guard->remoteWalkDone();
                return;
            }

            bool hasGi = false;
            for (const remote::RemoteDirEntry &e : entries) {
                if (!e.isDir && e.name == QLatin1String(".gitignore")) {
                    hasGi = true;
                    break;
                }
            }
            if (hasGi && backend) {
                ++guard->m_pendingWalk;
                const QString gi = posixJoin(remoteDir, QStringLiteral(".gitignore"));
                backend->readFileAsync(gi, [guard, remoteDir](bool readOk, const QByteArray &data,
                                                              const QString &) {
                    if (guard.isNull() || guard->m_cancelled.load())
                        return;
                    if (readOk)
                        guard->m_gitignore.addRules(remoteDir, QString::fromUtf8(data));
                    if (--guard->m_pendingWalk == 0)
                        guard->remoteWalkDone();
                });
            }

            for (const remote::RemoteDirEntry &e : entries) {
                if (ZipPath::isHardSkippedDirName(e.name))
                    continue;
                const QString childEntry = entryPrefix.isEmpty()
                    ? e.name
                    : posixJoin(entryPrefix, e.name);
                const QString relWs = relToRoot(guard->m_workspaceRoot, posixJoin(remoteDir, e.name));
                if (!relWs.isEmpty() && guard->m_gitignore.isIgnored(relWs, e.isDir))
                    continue;
                const QString remoteChild = posixJoin(remoteDir, e.name);
                if (e.isDir) {
                    ++guard->m_pendingWalk;
                    guard->remoteWalkDir(remoteChild, childEntry);
                } else {
                    guard->m_remoteFiles.append({remoteChild, childEntry});
                }
            }

            emit guard->progressUpdated(0, 0, 0, 0,
                tr("Preparing %1 files…").arg(guard->m_remoteFiles.size()), 0);

            if (--guard->m_pendingWalk == 0)
                guard->remoteWalkDone();
        });
}

void FolderZipTransfer::remoteWalkDone()
{
    if (m_cancelled.load())
        return;
    if (m_walkFailed) {
        fail(tr("Failed to list remote folder"));
        return;
    }
    if (m_remoteFiles.isEmpty()) {
        fail(tr("Folder is empty or contains only ignored files"));
        return;
    }
    m_writer = new ZipWriter();
    if (!m_writer->open(m_partialPath)) {
        fail(m_writer->errorString());
        return;
    }
    m_remoteIdx = 0;
    remotePackNext();
}

void FolderZipTransfer::remotePackNext()
{
    if (m_cancelled.load())
        return;
    if (m_remoteIdx >= m_remoteFiles.size()) {
        if (!m_writer->finish()) {
            fail(m_writer->errorString());
            return;
        }
        delete m_writer;
        m_writer = nullptr;
        QFile::remove(m_destZip);
        if (!QFile::rename(m_partialPath, m_destZip)) {
            fail(tr("Could not write zip file"));
            return;
        }
        m_partialPath.clear();
        succeed(m_remoteFiles.size());
        return;
    }

    const RemoteFile f = m_remoteFiles.at(m_remoteIdx);
    const int n = m_remoteIdx + 1;
    const int total = m_remoteFiles.size();
    if (shouldReportProgress(n, total))
        emit progressUpdated(n, total, 0, 0, f.entryName, 0);

    auto *tmp = new QTemporaryFile();
    tmp->setAutoRemove(true);
    if (!tmp->open()) {
        delete tmp;
        fail(tr("Could not create temp file"));
        return;
    }
    m_streamFile = tmp;
    QPointer<FolderZipTransfer> guard(this);
    QPointer<QTemporaryFile> tmpGuard(tmp);
    m_backend->readFileStreamAsync(
        f.remotePath,
        [tmpGuard](const QByteArray &chunk) {
            if (tmpGuard)
                tmpGuard->write(chunk);
        },
        [guard, tmpGuard, f](bool ok, const QString &error) {
            if (guard.isNull() || guard->m_cancelled.load())
                return;
            if (!ok || !tmpGuard) {
                if (guard)
                    guard->fail(error.isEmpty() ? tr("Remote read failed") : error);
                return;
            }
            tmpGuard->flush();
            const qint64 sz = tmpGuard->size();
            tmpGuard->seek(0);
            if (!guard->m_writer || !guard->m_writer->addFromFile(tmpGuard.data(), sz, f.entryName)) {
                guard->fail(guard->m_writer ? guard->m_writer->errorString()
                                            : tr("Zip write failed"));
                return;
            }
            tmpGuard->close();
            tmpGuard->remove();
            tmpGuard->deleteLater();
            guard->m_streamFile = nullptr;
            ++guard->m_remoteIdx;
            guard->remotePackNext();
        });
}

void FolderZipTransfer::uploadRemote(const QString &zipPath, const QString &selectedFolderPosix,
                                     QWidget *dialogParent)
{
    if (!beginBusy())
        return;
    if (!m_backend) {
        fail(tr("No remote filesystem"));
        return;
    }
    m_selectedFolder = selectedFolderPosix;
    m_zipPath = zipPath;
    emit progressUpdated(0, 0, 0, 0, tr("Preparing zip…"), 0);
    QString err;
    if (!buildExtractManifest(zipPath, &m_extractItems, &err)) {
        fail(err);
        return;
    }

    QSet<QString> parents;
    for (const ExtractItem &it : m_extractItems)
        parents.insert(parentPosix(posixJoin(selectedFolderPosix, it.destRel)));

    const QStringList parentList = parents.values();
    if (parentList.isEmpty()) {
        fail(tr("ZIP is empty"));
        return;
    }

    struct Scan {
        int pending = 0;
        QHash<QString, QSet<QString>> names;
    };
    auto scan = std::make_shared<Scan>();
    scan->pending = parentList.size();
    QPointer<FolderZipTransfer> guard(this);
    QPointer<remote::RemoteFsBackend> backend = m_backend;
    for (const QString &parent : parentList) {
        backend->readdirAsync(parent, [guard, scan, parent, dialogParent](
                                          bool ok, const QList<remote::RemoteDirEntry> &entries,
                                          const QString &) {
            if (guard.isNull() || guard->m_cancelled.load())
                return;
            if (ok) {
                QSet<QString> names;
                for (const remote::RemoteDirEntry &e : entries)
                    names.insert(e.name);
                scan->names.insert(parent, names);
            }
            if (--scan->pending > 0)
                return;

            QStringList conflicts;
            for (const ExtractItem &it : guard->m_extractItems) {
                const QString dest = posixJoin(guard->m_selectedFolder, it.destRel);
                const QString par = parentPosix(dest);
                const QString base = dest.section(QLatin1Char('/'), -1);
                if (scan->names.value(par).contains(base))
                    conflicts.append(it.destRel);
            }
            bool aborted = false;
            guard->m_extractItems = guard->applyConflictDialog(guard->m_extractItems, conflicts,
                                                               dialogParent, &aborted);
            if (aborted) {
                guard->finishIdle();
                emit guard->transferCancelled();
                return;
            }
            guard->m_extractIdx = 0;
            guard->remoteUploadNext();
        });
    }
}

void FolderZipTransfer::remoteUploadNext()
{
    if (m_cancelled.load())
        return;
    if (m_extractIdx >= m_extractItems.size()) {
        succeed(m_extractItems.size());
        return;
    }
    const ExtractItem it = m_extractItems.at(m_extractIdx);
    const int n = m_extractIdx + 1;
    const int total = m_extractItems.size();
    if (shouldReportProgress(n, total))
        emit progressUpdated(n, total, 0, 0, it.destRel, 0);
    const QString dest = posixJoin(m_selectedFolder, it.destRel);

    QPointer<FolderZipTransfer> guard(this);
    ensureRemoteParent(dest, [guard, it, dest](bool ok) {
        if (guard.isNull() || guard->m_cancelled.load())
            return;
        if (!ok) {
            guard->fail(tr("Could not create remote directory"));
            return;
        }
        ZipReader reader;
        if (!reader.open(guard->m_zipPath)) {
            guard->fail(reader.errorString());
            return;
        }
        QByteArray data;
        if (!reader.extractToBytes(it.zipEntry, &data)) {
            guard->fail(reader.errorString());
            return;
        }
        guard->m_backend->writeFileAsync(dest, data, [guard](bool wok, const QString &werr) {
            if (guard.isNull() || guard->m_cancelled.load())
                return;
            if (!wok) {
                guard->fail(werr.isEmpty() ? tr("Remote write failed") : werr);
                return;
            }
            ++guard->m_extractIdx;
            guard->remoteUploadNext();
        });
    });
}

void FolderZipTransfer::ensureRemoteParent(const QString &remoteFilePath,
                                           const std::function<void(bool)> &cb)
{
    const QString dir = parentPosix(remoteFilePath);
    auto ensureDir = std::make_shared<std::function<void(const QString &, const std::function<void(bool)> &)>>();
    QPointer<FolderZipTransfer> guard(this);
    *ensureDir = [guard, ensureDir](const QString &remoteDirPath,
                                    const std::function<void(bool)> &callback) {
        if (guard.isNull()) {
            callback(false);
            return;
        }
        if (remoteDirPath.isEmpty() || remoteDirPath == guard->m_selectedFolder
            || guard->m_ensuredDirs.contains(remoteDirPath)) {
            callback(true);
            return;
        }
        const int slash = remoteDirPath.lastIndexOf(QLatin1Char('/'));
        auto mkdirThis = [guard, remoteDirPath, callback]() {
            if (guard.isNull() || !guard->m_backend) {
                callback(false);
                return;
            }
            guard->m_backend->mkdirAsync(remoteDirPath, [guard, remoteDirPath, callback](bool, const QString &) {
                if (guard.isNull()) {
                    callback(false);
                    return;
                }
                guard->m_ensuredDirs.insert(remoteDirPath);
                callback(true);
            });
        };
        if (slash > 0) {
            (*ensureDir)(remoteDirPath.left(slash), [mkdirThis](bool ok) {
                if (!ok) {
                    mkdirThis();
                    return;
                }
                mkdirThis();
            });
        } else {
            mkdirThis();
        }
    };
    (*ensureDir)(dir, cb);
}
