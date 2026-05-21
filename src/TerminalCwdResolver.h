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

#ifndef TERMINALCWDRESOLVER_H
#define TERMINALCWDRESOLVER_H

#include <QString>

class TerminalCwdResolver
{
public:
    static bool canOpenInWorkspace(const QString &workspaceRoot);
    static bool canOpenInFolder(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot);

    static QString resolveWorkspace(const QString &workspaceRoot);
    static QString resolveFolder(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot);
};

#endif
