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

#ifndef ZIPUPLOADWIPE_H
#define ZIPUPLOADWIPE_H

#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace ZipUploadWipe {

using Progress = std::function<void(int current, int total, const QString &rel)>;

struct ExtractFile {
    QString zipEntry;
    QString destRel;
};

// Walked entry names minus retain (kept extract dests ∪ skipped dests). Sorted.
QStringList orphanRelPaths(const QStringList &walked, const QStringList &retain);

// True iff destPosix stays under selectedFolderPosix (POSIX, no `..` escape).
bool isSafeRemoteDest(const QString &selectedFolderPosix, const QString &destPosix);

// Delete orphan rels under selectedFolder. Unsafe paths (escape) are skipped, not
// deleted. Returns {} on success, "cancelled", or an error naming the path.
QString removeLocalOrphans(const QString &selectedFolder, const QStringList &orphanRels,
                           std::atomic<bool> *cancel, const Progress &progress);

QString wipeLocalFolder(const QString &workspaceRoot, const QString &selectedFolder,
                        const QStringList &retainRelPaths, std::atomic<bool> *cancel,
                        const Progress &progress);

// Extract kept zip entries then wipe. Extract failure/cancel does not wipe.
QString extractThenWipeLocal(const QString &zipPath, const QString &workspaceRoot,
                             const QString &selectedFolder, const QList<ExtractFile> &items,
                             const QStringList &retainRelPaths, std::atomic<bool> *cancel,
                             const Progress &progress);

} // namespace ZipUploadWipe

#endif // ZIPUPLOADWIPE_H
