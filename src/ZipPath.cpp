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

#include "ZipPath.h"

#include <QDir>
#include <QFileInfo>
#include <QStringView>

namespace ZipPath {

QString normalizeZipEntryName(QString name)
{
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (name.startsWith(QLatin1String("./")))
        name.remove(0, 2);
    return name;
}

QString detectCommonPrefix(const QStringList &entryNames)
{
    if (entryNames.isEmpty())
        return {};
    const QString first = normalizeZipEntryName(entryNames.front());
    const int slash = first.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return {};
    const QString candidate = first.left(slash + 1);
    for (const QString &raw : entryNames) {
        if (!normalizeZipEntryName(raw).startsWith(candidate))
            return {};
    }
    return candidate;
}

static bool isWindowsReservedStem(QStringView stem)
{
    static const char *const kReserved[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    for (const char *r : kReserved) {
        if (stem.compare(QLatin1String(r), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString validateExtractRelPath(const QString &relPath, DestOs dest)
{
    const QString n = normalizeZipEntryName(relPath);
    if (n.isEmpty())
        return QStringLiteral("empty path");
    if (n.startsWith(QLatin1Char('/')) || n.startsWith(QLatin1Char('\\')))
        return QStringLiteral("absolute path");
    if (n.contains(QLatin1Char('\0')))
        return QStringLiteral("NUL in path");
    if (n.contains(QLatin1Char(':')))
        return QStringLiteral("absolute path");

    const QStringList parts = n.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return QStringLiteral("invalid path component");
        if (dest == DestOs::Windows) {
            if (part.endsWith(QLatin1Char('.')) || part.endsWith(QLatin1Char(' ')))
                return QStringLiteral("invalid file name");
            const int dot = part.indexOf(QLatin1Char('.'));
            const QString stem = dot < 0 ? part : part.left(dot);
            if (isWindowsReservedStem(stem))
                return QStringLiteral("reserved device name");
        }
    }
    return {};
}

bool isSafeExtractDest(const QString &targetDir, const QString &destPath)
{
    const QString root = QDir::cleanPath(targetDir);
    const QString dest = QDir::cleanPath(destPath);
    if (root.isEmpty() || dest.isEmpty())
        return false;
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if (dest.compare(root, cs) != 0 && !dest.startsWith(root + QLatin1Char('/'), cs))
        return false;

    QString cur = dest;
    while (true) {
        if (QFileInfo(cur).isSymLink())
            return false;
        if (QDir::cleanPath(cur).compare(root, cs) == 0)
            break;
        const QString parent = QDir::cleanPath(QFileInfo(cur).path());
        if (parent == cur)
            break;
        cur = parent;
    }
    return true;
}

} // namespace ZipPath
