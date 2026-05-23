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

#include "GitDiffPalette.h"

const GitDiffPalette &GitDiffPalette::light()
{
    static const GitDiffPalette p = []() {
        GitDiffPalette x;
        // Primer light tokens
        x.fgAdded       = QColor(0x1A, 0x7F, 0x37);   // success.fg
        x.fgModified    = QColor(0x09, 0x69, 0xDA);   // accent.fg
        x.fgDeleted     = QColor(0x65, 0x6D, 0x76);   // fg.muted (gray)
        x.fgRenamed     = QColor(0x82, 0x50, 0xDF);   // done.fg (purple)
        x.fgUntracked   = QColor(0x1A, 0x7F, 0x37);
        x.fgConflict    = QColor(0xCF, 0x22, 0x2E);   // danger.fg
        x.fgPlus        = QColor(0x1A, 0x7F, 0x37);
        x.fgMinus       = QColor(0xCF, 0x22, 0x2E);

        // diffBlob.additionLineBgColor / deletionLineBgColor (GitHub light)
        x.bgAddLine     = QColor(0xDA, 0xFB, 0xE1);   // #dafbe1
        x.bgDelLine     = QColor(0xFF, 0xEB, 0xE9);   // #ffebe9
        x.bgAddWord     = QColor(0xAC, 0xEE, 0xBB);   // #aceebb
        x.bgDelWord     = QColor(0xFF, 0xCE, 0xCB);   // #ffcecb
        x.fgHunkHeader  = QColor(0x65, 0x6D, 0x76);
        x.bgHunkHeader  = QColor(0xDD, 0xF4, 0xFF);   // accent.muted
        x.fgGutter      = QColor(0x8C, 0x95, 0x9F);

        // Commit header tokens (light): meta = fg.muted (#656d76),
        // body = fg.default near-black (#1f2328).
        x.commitMetaFg  = QColor(0x65, 0x6D, 0x76);
        x.commitBodyFg  = QColor(0x1F, 0x23, 0x28);
        return x;
    }();
    return p;
}

const GitDiffPalette &GitDiffPalette::dark()
{
    static const GitDiffPalette p = []() {
        GitDiffPalette x;
        // Primer dark tokens
        x.fgAdded       = QColor(0x3F, 0xB9, 0x50);   // success.fg
        x.fgModified    = QColor(0x58, 0xA6, 0xFF);   // accent.fg
        x.fgDeleted     = QColor(0x8B, 0x94, 0x9E);   // fg.muted (gray)
        x.fgRenamed     = QColor(0xA3, 0x71, 0xF7);   // done.fg
        x.fgUntracked   = QColor(0x3F, 0xB9, 0x50);
        x.fgConflict    = QColor(0xF8, 0x51, 0x49);   // danger.fg
        x.fgPlus        = QColor(0x3F, 0xB9, 0x50);
        x.fgMinus       = QColor(0xF8, 0x51, 0x49);

        // GitHub dark diff backgrounds (subtle, alpha-blended on #0d1117).
        // Line bg: rgba(46,160,67,0.15) / rgba(248,81,73,0.15) over canvas.
        // Word bg: rgba(46,160,67,0.40) / rgba(248,81,73,0.40) composed over
        //          the line bg (NOT over canvas) — that's how GitHub stacks
        //          the layers in the browser. Pre-blend here because our
        //          indicator paints opaque on top of the marker.
        x.bgAddLine     = QColor(0x12, 0x26, 0x1E);   // #12261e
        x.bgDelLine     = QColor(0x30, 0x1B, 0x1F);   // #301b1f
        x.bgAddWord     = QColor(0x1D, 0x57, 0x2D);   // #1d572d
        x.bgDelWord     = QColor(0x80, 0x31, 0x30);   // #803130
        x.fgHunkHeader  = QColor(0x8B, 0x94, 0x9E);
        x.bgHunkHeader  = QColor(0x38, 0x8B, 0xFD, 0x40); // accent.muted alpha
        x.fgGutter      = QColor(0x6E, 0x76, 0x81);

        // Commit header tokens (dark): meta = fg.muted (#8b949e),
        // body = fg.default near-white (#e6edf3).
        x.commitMetaFg  = QColor(0x8B, 0x94, 0x9E);
        x.commitBodyFg  = QColor(0xE6, 0xED, 0xF3);
        return x;
    }();
    return p;
}

const GitDiffPalette &GitDiffPalette::current(bool isDark)
{
    return isDark ? dark() : light();
}
