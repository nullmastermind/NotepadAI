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

#ifndef GIT_DIFF_PALETTE_H
#define GIT_DIFF_PALETTE_H

#include <QColor>

// Primer-derived color tokens (GitHub design system) for git status / diff UI.
// Tree foreground colors mirror GitHub Desktop / VS Code Source Control conventions;
// diff backgrounds mirror github.com unified diff view.
struct GitDiffPalette {
    // Tree row foregrounds (status section). Pure-deleted rows render gray;
    // modified rows blue; added/new rows green; conflicts red.
    QColor fgAdded;
    QColor fgModified;
    QColor fgDeleted;
    QColor fgRenamed;
    QColor fgUntracked;
    QColor fgConflict;
    QColor fgPlus;     // numstat "+N" inline color
    QColor fgMinus;    // numstat "-N" inline color

    // Diff editor backgrounds (full-line) + intra-line word fills.
    QColor bgAddLine;
    QColor bgDelLine;
    QColor bgAddWord;
    QColor bgDelWord;
    QColor fgHunkHeader;
    QColor bgHunkHeader;
    QColor fgGutter;
    QColor bgGutter;

    // Canvas (editor background + default foreground for context/default lines).
    QColor canvasBg;
    QColor canvasFg;

    // Commit-view-only tokens (header block of a commit detail tab).
    // commitMetaFg = author / SHA / date muted label color.
    // commitBodyFg = commit message body color (slightly stronger than meta).
    QColor commitMetaFg;
    QColor commitBodyFg;

    static const GitDiffPalette &light();
    static const GitDiffPalette &dark();
    static const GitDiffPalette &current(bool isDark);
};

#endif // GIT_DIFF_PALETTE_H
