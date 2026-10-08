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

#ifndef GIT_ERROR_H
#define GIT_ERROR_H

#include <QMetaType>
#include <QString>
#include <QStringList>

#include <cstdint>

struct GitError
{
    enum Kind : std::uint8_t {
        None,
        NotInstalled,
        NotARepo,
        EmptyRepo,
        DetachedHead,
        NoRemote,
        NoUpstream,
        DirtyTree,
        NonFastForward,
        PullDiverged,
        MergeConflict,
        AuthFailed,
        NetworkFailed,
        LockHeld,
        Cancelled,
        Timeout,
        Unknown,
        ReservedName
    };

    Kind kind = None;
    QString humanMessage;
    QString details;
    QString hint;
    QStringList suggestedActions;

    bool isError() const { return kind != None; }
};

Q_DECLARE_METATYPE(GitError)

#endif // GIT_ERROR_H
