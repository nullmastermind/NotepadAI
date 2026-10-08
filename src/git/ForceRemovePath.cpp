/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ForceRemovePath.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#ifdef Q_OS_WIN
#    include <windows.h>
#endif

namespace ForceRemovePath {

#ifdef Q_OS_WIN

static QString extendedWinPath(const QString &absPath)
{
    QString native = QDir::toNativeSeparators(absPath);
    while (native.size() > 3 && (native.endsWith(QLatin1Char('\\')) || native.endsWith(QLatin1Char('/'))))
        native.chop(1);
    if (native.startsWith(QLatin1String("\\\\?\\")) || native.startsWith(QLatin1String("\\\\.\\")))
        return native;
    if (native.startsWith(QLatin1String("\\\\")))
        return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    return QStringLiteral("\\\\?\\") + native;
}

static const wchar_t *wide(const QString &s)
{
    return reinterpret_cast<const wchar_t *>(s.utf16());
}

static bool existsExtended(const QString &absPath)
{
    const QString ext = extendedWinPath(absPath);
    return GetFileAttributesW(wide(ext)) != INVALID_FILE_ATTRIBUTES;
}

static bool deleteExtendedOnce(const QString &absPath)
{
    const QString ext = extendedWinPath(absPath);
    const DWORD attr = GetFileAttributesW(wide(ext));
    if (attr == INVALID_FILE_ATTRIBUTES)
        return true;
    if (attr & FILE_ATTRIBUTE_DIRECTORY)
        return RemoveDirectoryW(wide(ext)) != 0;
    return DeleteFileW(wide(ext)) != 0;
}

static bool clearAttributesAndDelete(const QString &absPath)
{
    const QString ext = extendedWinPath(absPath);
    if (GetFileAttributesW(wide(ext)) == INVALID_FILE_ATTRIBUTES)
        return true;
    SetFileAttributesW(wide(ext), FILE_ATTRIBUTE_NORMAL);
    return deleteExtendedOnce(absPath);
}

static bool renameThenDelete(const QString &absPath)
{
    const QFileInfo info(absPath);
    const QString dest = info.dir().filePath(
        QStringLiteral(".nn-del-") + QUuid::createUuid().toString(QUuid::Id128));
    const QString srcExt = extendedWinPath(absPath);
    const QString dstExt = extendedWinPath(dest);
    if (!MoveFileW(wide(srcExt), wide(dstExt)))
        return false;
    SetFileAttributesW(wide(dstExt), FILE_ATTRIBUTE_NORMAL);
    if (!DeleteFileW(wide(dstExt)))
        RemoveDirectoryW(wide(dstExt));
    return !existsExtended(absPath) && !existsExtended(dest);
}

#endif

bool forceRemove(const QString &absPath)
{
    if (absPath.isEmpty())
        return false;
#ifdef Q_OS_WIN
    if (!existsExtended(absPath))
        return true;
    if (deleteExtendedOnce(absPath) && !existsExtended(absPath))
        return true;
    if (clearAttributesAndDelete(absPath) && !existsExtended(absPath))
        return true;
    if (renameThenDelete(absPath) && !existsExtended(absPath))
        return true;
    return !existsExtended(absPath);
#else
    const QFileInfo info(absPath);
    if (!info.exists())
        return true;
    if (info.isDir())
        return QDir(absPath).removeRecursively();
    return QFile::remove(absPath);
#endif
}

bool recoverFailedUntrackedDeletes(const QString &repoRoot, const QStringList &relPaths)
{
    if (relPaths.isEmpty())
        return false;
    const QString root = QDir::cleanPath(repoRoot);
    if (root.isEmpty())
        return false;
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    for (const QString &rel : relPaths) {
        if (rel.isEmpty() || rel.contains(QLatin1String("..")))
            return false;
        const QString abs = QDir::cleanPath(QDir(root).filePath(rel));
        if (abs.compare(root, cs) != 0 && !abs.startsWith(root + QLatin1Char('/'), cs))
            return false;
        if (!forceRemove(abs))
            return false;
    }
    return true;
}

} // namespace ForceRemovePath
