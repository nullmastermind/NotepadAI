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

#ifndef SCROLLBACKBUFFER_H
#define SCROLLBACKBUFFER_H

#include <vterm.h>

#include <QList>
#include <QVector>

// Bounded ring of historical terminal lines; sb_pushline writes here, sb_popline reads.
class ScrollbackBuffer
{
public:
    explicit ScrollbackBuffer(int maxLines = 5000);

    int  maxLines() const { return m_maxLines; }
    // Returns the number of lines evicted by the resize (0 if none).
    int  setMaxLines(int maxLines);

    int  size() const { return m_lines.size(); }
    bool isEmpty() const { return m_lines.isEmpty(); }

    void clear();

    // Push a line onto the bottom (most recent) of the scrollback. Returns the
    // number of lines evicted by this call (0 in steady state, 1 at cap).
    int pushLine(int cols, const VTermScreenCell *cells);

    // Pop the most recent line off the bottom. Writes up to `cols` cells into
    // `out`, padding with blank cells if the stored line was narrower.
    // Returns true if a line was popped, false if the buffer was empty.
    bool popLine(int cols, VTermScreenCell *out);

    // Fill `out` with the line at history index `index` (0 = oldest line still
    // in the buffer, size()-1 = most recent line). Padded/truncated to `cols`.
    // Returns true on success, false if index is out of range.
    bool lineAt(int index, int cols, VTermScreenCell *out) const;

private:
    static void blankCell(VTermScreenCell *cell);
    int enforceCap();

    int m_maxLines;
    QList<QVector<VTermScreenCell>> m_lines;
};

#endif
