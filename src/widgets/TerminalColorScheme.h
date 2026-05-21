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

#ifndef TERMINALCOLORSCHEME_H
#define TERMINALCOLORSCHEME_H

#include <QColor>

struct TerminalColorScheme
{
    QColor ansi[16];
    QColor defaultBg;
    QColor defaultFg;
    QColor cursor;
    QColor selection;

    static TerminalColorScheme lightScheme();
    static TerminalColorScheme darkScheme();
};

#endif
