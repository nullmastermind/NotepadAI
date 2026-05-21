/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#include "TerminalCwdResolver.h"

#include <QDir>
#include <QFileInfo>

static bool directoryExists(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    QFileInfo info(path);
    return info.exists() && info.isDir();
}

bool TerminalCwdResolver::canOpenInWorkspace(const QString &workspaceRoot)
{
    return directoryExists(workspaceRoot);
}

bool TerminalCwdResolver::canOpenInFolder(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    if (activeBufferIsFile && !activeFilePath.isEmpty()) {
        const QString parent = QFileInfo(activeFilePath).absolutePath();
        if (directoryExists(parent)) {
            return true;
        }
    }
    return directoryExists(workspaceRoot);
}

QString TerminalCwdResolver::resolveWorkspace(const QString &workspaceRoot)
{
    if (directoryExists(workspaceRoot)) {
        return QDir::cleanPath(workspaceRoot);
    }
    return QString();
}

QString TerminalCwdResolver::resolveFolder(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot)
{
    if (activeBufferIsFile && !activeFilePath.isEmpty()) {
        const QString parent = QFileInfo(activeFilePath).absolutePath();
        if (directoryExists(parent)) {
            return QDir::cleanPath(parent);
        }
    }
    if (directoryExists(workspaceRoot)) {
        return QDir::cleanPath(workspaceRoot);
    }
    return QString();
}
