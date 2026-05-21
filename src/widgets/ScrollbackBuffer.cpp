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

#include "ScrollbackBuffer.h"

#include <algorithm>
#include <cstring>

ScrollbackBuffer::ScrollbackBuffer(int maxLines)
    : m_maxLines(maxLines > 0 ? maxLines : 0)
{
}

int ScrollbackBuffer::setMaxLines(int maxLines)
{
    m_maxLines = maxLines > 0 ? maxLines : 0;
    return enforceCap();
}

void ScrollbackBuffer::clear()
{
    m_lines.clear();
}

void ScrollbackBuffer::blankCell(VTermScreenCell *cell)
{
    std::memset(cell, 0, sizeof(*cell));
    cell->width = 1;
}

int ScrollbackBuffer::enforceCap()
{
    int evicted = 0;
    if (m_maxLines <= 0) {
        evicted = m_lines.size();
        m_lines.clear();
        return evicted;
    }
    while (m_lines.size() > m_maxLines) {
        m_lines.removeFirst();
        ++evicted;
    }
    return evicted;
}

int ScrollbackBuffer::pushLine(int cols, const VTermScreenCell *cells)
{
    if (m_maxLines <= 0 || cols <= 0 || cells == nullptr) {
        return 0;
    }
    QVector<VTermScreenCell> row(cols);
    std::memcpy(row.data(), cells, static_cast<size_t>(cols) * sizeof(VTermScreenCell));
    m_lines.append(std::move(row));
    return enforceCap();
}

bool ScrollbackBuffer::popLine(int cols, VTermScreenCell *out)
{
    if (m_lines.isEmpty() || cols <= 0 || out == nullptr) {
        return false;
    }
    const QVector<VTermScreenCell> row = m_lines.takeLast();
    const int copy = std::min(cols, static_cast<int>(row.size()));
    if (copy > 0) {
        std::memcpy(out, row.constData(), static_cast<size_t>(copy) * sizeof(VTermScreenCell));
    }
    for (int i = copy; i < cols; ++i) {
        blankCell(&out[i]);
    }
    return true;
}

bool ScrollbackBuffer::lineAt(int index, int cols, VTermScreenCell *out) const
{
    if (index < 0 || index >= m_lines.size() || cols <= 0 || out == nullptr) {
        return false;
    }
    const QVector<VTermScreenCell> &row = m_lines.at(index);
    const int copy = std::min(cols, static_cast<int>(row.size()));
    if (copy > 0) {
        std::memcpy(out, row.constData(), static_cast<size_t>(copy) * sizeof(VTermScreenCell));
    }
    for (int i = copy; i < cols; ++i) {
        blankCell(&out[i]);
    }
    return true;
}
