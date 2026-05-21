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

#include "TerminalColorScheme.h"

TerminalColorScheme TerminalColorScheme::lightScheme()
{
    TerminalColorScheme s;
    s.ansi[0]  = QColor(0x00, 0x00, 0x00);
    s.ansi[1]  = QColor(0xCD, 0x00, 0x00);
    s.ansi[2]  = QColor(0x00, 0x80, 0x00);
    s.ansi[3]  = QColor(0xB5, 0x83, 0x00);
    s.ansi[4]  = QColor(0x00, 0x00, 0xEE);
    s.ansi[5]  = QColor(0xCD, 0x00, 0xCD);
    s.ansi[6]  = QColor(0x00, 0xCD, 0xCD);
    s.ansi[7]  = QColor(0xE5, 0xE5, 0xE5);
    s.ansi[8]  = QColor(0x7F, 0x7F, 0x7F);
    s.ansi[9]  = QColor(0xFF, 0x00, 0x00);
    s.ansi[10] = QColor(0x00, 0xB2, 0x00);
    s.ansi[11] = QColor(0xCD, 0x9F, 0x00);
    s.ansi[12] = QColor(0x52, 0x6F, 0xFF);
    s.ansi[13] = QColor(0xFF, 0x00, 0xFF);
    s.ansi[14] = QColor(0x00, 0xFF, 0xFF);
    s.ansi[15] = QColor(0xFF, 0xFF, 0xFF);
    s.defaultBg = QColor(0xFF, 0xFF, 0xFF);
    s.defaultFg = QColor(0x1E, 0x1E, 0x1E);
    s.cursor    = QColor(0x1E, 0x1E, 0x1E);
    s.selection = QColor(0xAD, 0xD6, 0xFF);
    return s;
}

TerminalColorScheme TerminalColorScheme::darkScheme()
{
    TerminalColorScheme s;
    s.ansi[0]  = QColor(0x1E, 0x1E, 0x1E);
    s.ansi[1]  = QColor(0xCD, 0x31, 0x31);
    s.ansi[2]  = QColor(0x0D, 0xBC, 0x79);
    s.ansi[3]  = QColor(0xE5, 0xE5, 0x10);
    s.ansi[4]  = QColor(0x24, 0x72, 0xC8);
    s.ansi[5]  = QColor(0xBC, 0x3F, 0xBC);
    s.ansi[6]  = QColor(0x11, 0xA8, 0xCD);
    s.ansi[7]  = QColor(0xE5, 0xE5, 0xE5);
    s.ansi[8]  = QColor(0x66, 0x66, 0x66);
    s.ansi[9]  = QColor(0xF1, 0x4C, 0x4C);
    s.ansi[10] = QColor(0x23, 0xD1, 0x8B);
    s.ansi[11] = QColor(0xF5, 0xF5, 0x43);
    s.ansi[12] = QColor(0x3B, 0x8E, 0xEA);
    s.ansi[13] = QColor(0xD6, 0x70, 0xD6);
    s.ansi[14] = QColor(0x29, 0xB8, 0xDB);
    s.ansi[15] = QColor(0xE5, 0xE5, 0xE5);
    s.defaultBg = QColor(0x1E, 0x1E, 0x1E);
    s.defaultFg = QColor(0xCC, 0xCC, 0xCC);
    s.cursor    = QColor(0xCC, 0xCC, 0xCC);
    s.selection = QColor(0x26, 0x4F, 0x78);
    return s;
}
