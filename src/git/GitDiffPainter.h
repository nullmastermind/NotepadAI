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

#ifndef GIT_DIFF_PAINTER_H
#define GIT_DIFF_PAINTER_H

#include "GitDiffParser.h"
#include "GitDiffSyntaxMapper.h"

#include <cstdint>

class ScintillaNext;
struct GitDiffPalette;

// Owns the rendering of a parsed unified diff into a ScintillaNext editor.
// One-shot append + style flush — no incremental painting, no per-line style
// calls. Always issues SCI_SETREADONLY(true) at the end.
//
// Two layers of background:
//   1) Solid line bg (added / deleted) via the fixed StyleAdded / StyleDeleted
//      backgrounds — applied always.
//   2) Word-level intra-line highlight (added / deleted word spans) via
//      Scintilla indicators — applied only when an Overlay is supplied AND
//      contains word spans.
class GitDiffPainter
{
public:
    // Style IDs we own.
    enum StyleId : std::uint8_t {
        StyleDefault     = 0,
        StyleFileHeader  = 1,
        StyleHunkHeader  = 2,
        StyleContext     = 3,
        StyleAdded       = 4,
        StyleDeleted     = 5,
        StyleNoNewline   = 6,
        // Commit-view-only styles. Renderer prepends a commit header block
        // (meta + body) before the per-file diff; these styles colour it.
        StyleCommitMeta  = 7,
        StyleCommitBody  = 8
    };

    // Configure the editor's styling (lexer off, our color palette installed,
    // word-diff indicators allocated). Idempotent — safe to call multiple times.
    static void configureEditor(ScintillaNext *editor, const GitDiffPalette &pal);

    // Renders the parsed result. Clears the buffer first. Leaves the editor
    // in read-only mode. Overlay is optional; when null, only the fixed
    // background colors are applied. `pal` is used here as well so indicator
    // colors stay in sync if the theme changed between configureEditor and
    // render (cheap — just SCI_INDICSETFORE calls).
    static void render(ScintillaNext *editor,
                       const GitDiffParser::Result &parsed,
                       const GitDiffSyntaxMapper::Overlay *overlay,
                       const GitDiffPalette &pal);
};

#endif // GIT_DIFF_PAINTER_H
