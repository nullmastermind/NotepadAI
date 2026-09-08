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

#include "ZipUploadWipe.h"

#include "ZipIgnoreWalk.h"
#include "ZipPath.h"
#include "ZipArchive.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace ZipUploadWipe {

QStringList orphanRelPaths(const QStringList &walked, const QStringList &retain)
{
    const QSet<QString> keep(retain.cbegin(), retain.cend());
    QStringList out;
    out.reserve(walked.size());
    for (const QString &rel : walked) {
        if (!keep.contains(rel))
            out.append(rel);
    }
    out.sort();
    return out;
}

static void pruneEmptyParents(const QString &selectedFolder, const QStringList &deletedRels)
{
    QStringList dirs;
    for (const QString &rel : deletedRels) {
        QString parent = QFileInfo(rel).path();
        parent.replace(QLatin1Char('\\'), QLatin1Char('/'));
        while (!parent.isEmpty() && parent != QLatin1String(".") && parent != QLatin1String("/")) {
            dirs.append(parent);
            const int slash = parent.lastIndexOf(QLatin1Char('/'));
            if (slash <= 0)
                break;
            parent = parent.left(slash);
        }
    }
    dirs.removeDuplicates();
    std::sort(dirs.begin(), dirs.end(), [](const QString &a, const QString &b) {
        const int da = a.count(QLatin1Char('/'));
        const int db = b.count(QLatin1Char('/'));
        if (da != db)
            return da > db;
        return a > b;
    });

    const QString root = QDir::cleanPath(selectedFolder);
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    for (const QString &drel : dirs) {
        const QString dabs = QDir::cleanPath(QDir(selectedFolder).filePath(drel));
        if (dabs.compare(root, cs) == 0)
            continue;
        if (!ZipPath::isSafeExtractDest(selectedFolder, dabs))
            continue;
        QDir().rmdir(dabs);
    }
}

QString removeLocalOrphans(const QString &selectedFolder, const QStringList &orphanRels,
                           std::atomic<bool> *cancel, const Progress &progress)
{
    const int total = orphanRels.size();
    QStringList deletedRels;
    deletedRels.reserve(total);
    int n = 0;
    for (const QString &rel : orphanRels) {
        if (cancel && cancel->load())
            return QStringLiteral("cancelled");
        const QString dest = QDir::cleanPath(QDir(selectedFolder).filePath(rel));
        ++n;
        if (!ZipPath::isSafeExtractDest(selectedFolder, dest)) {
            if (progress)
                progress(n, total, rel);
            continue;
        }
        if (QFileInfo::exists(dest) && !QFile::remove(dest))
            return QStringLiteral("Failed to delete %1").arg(rel);
        deletedRels.append(rel);
        if (progress)
            progress(n, total, rel);
    }
    pruneEmptyParents(selectedFolder, deletedRels);
    return {};
}

QString wipeLocalFolder(const QString &workspaceRoot, const QString &selectedFolder,
                        const QStringList &retainRelPaths, std::atomic<bool> *cancel,
                        const Progress &progress)
{
    const auto walked = ZipIgnoreWalk::walkLocalFolder(workspaceRoot, selectedFolder);
    QStringList names;
    names.reserve(walked.size());
    for (const auto &f : walked)
        names.append(f.entryName);
    return removeLocalOrphans(selectedFolder, orphanRelPaths(names, retainRelPaths), cancel,
                              progress);
}

bool isSafeRemoteDest(const QString &selectedFolderPosix, const QString &destPosix)
{
    QString root = selectedFolderPosix;
    QString dest = destPosix;
    root.replace(QLatin1Char('\\'), QLatin1Char('/'));
    dest.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (root.endsWith(QLatin1Char('/')) && root.size() > 1)
        root.chop(1);
    while (dest.endsWith(QLatin1Char('/')) && dest.size() > 1)
        dest.chop(1);
    if (root.isEmpty() || dest.isEmpty())
        return false;
    const QStringList parts = dest.split(QLatin1Char('/'));
    for (const QString &p : parts) {
        if (p == QLatin1String(".."))
            return false;
    }
    return dest == root || dest.startsWith(root + QLatin1Char('/'));
}

QString extractThenWipeLocal(const QString &zipPath, const QString &workspaceRoot,
                             const QString &selectedFolder, const QList<ExtractFile> &items,
                             const QStringList &retainRelPaths, std::atomic<bool> *cancel,
                             const Progress &progress)
{
    if (!items.isEmpty()) {
        ZipReader reader;
        if (!reader.open(zipPath))
            return reader.errorString();
        const int total = items.size();
        for (int i = 0; i < total; ++i) {
            if (cancel && cancel->load())
                return QStringLiteral("cancelled");
            const ExtractFile &it = items.at(i);
            const QString dest = QDir::cleanPath(QDir(selectedFolder).filePath(it.destRel));
            if (!ZipPath::isSafeExtractDest(selectedFolder, dest))
                continue;
            QDir().mkpath(QFileInfo(dest).absolutePath());
            if (!ZipPath::isSafeExtractDest(selectedFolder, dest))
                continue;
            if (!reader.extractToFile(it.zipEntry, dest))
                return reader.errorString();
            if (progress)
                progress(i + 1, total, it.destRel);
        }
    }
    if (cancel && cancel->load())
        return QStringLiteral("cancelled");
    return wipeLocalFolder(workspaceRoot, selectedFolder, retainRelPaths, cancel, progress);
}

} // namespace ZipUploadWipe
