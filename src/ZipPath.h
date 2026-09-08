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

#ifndef ZIPPATH_H
#define ZIPPATH_H

#include <QString>
#include <QStringList>
#include <QStringView>

namespace ZipPath {

enum class DestOs : quint8 { Windows, Posix };

// POSIX slashes, no '\\'. Does not strip a leading '/' (validate catches absolute).
QString normalizeZipEntryName(QString name);

// Shared first directory component including trailing '/', or empty if mixed/top-level.
QString detectCommonPrefix(const QStringList &entryNames);

// Empty string = valid. Otherwise a short reason. `relPath` is prefix-stripped.
QString validateExtractRelPath(const QString &relPath, DestOs dest);

// True iff destPath stays under targetDir after cleanPath and no existing
// path component from dest up to target is a symlink.
bool isSafeExtractDest(const QString &targetDir, const QString &destPath);

inline bool isHardSkippedDirName(QStringView name)
{
    if (name.compare(QLatin1String(".git"), Qt::CaseInsensitive) == 0)
        return true;
    if (name.compare(QLatin1String("node_modules"), Qt::CaseInsensitive) == 0)
        return true;
    if (name.compare(QLatin1String(".cpm-cache"), Qt::CaseInsensitive) == 0)
        return true;
    // Exact CMake output dirs — not every folder whose name starts with "build".
    if (name.compare(QLatin1String("build-debug"), Qt::CaseInsensitive) == 0)
        return true;
    if (name.compare(QLatin1String("build-release"), Qt::CaseInsensitive) == 0)
        return true;
    return false;
}

} // namespace ZipPath

#endif // ZIPPATH_H
