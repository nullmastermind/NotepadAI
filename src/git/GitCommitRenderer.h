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

#ifndef GIT_COMMIT_RENDERER_H
#define GIT_COMMIT_RENDERER_H

#include "GitCommitDetail.h"

#include <QByteArray>
#include <QString>
#include <QVector>

class ScintillaNext;
struct GitDiffPalette;

// Renders a GitCommitDetail (header + body + per-file unified diff) into a
// ScintillaNext editor that has been pre-configured via
// GitDiffPainter::configureEditor(). The renderer:
//
//   1. Clears the buffer.
//   2. Emits a header block (commit meta + body) styled with StyleCommitMeta
//      / StyleCommitBody.
//   3. Parses the unified diff via GitDiffParser and renders it using the
//      same StyleAdded / StyleDeleted / StyleContext / Style*Header palette
//      as the existing GitDiffViewController preview tab.
//   4. Marks each "+++ b/<path>" / "diff --git" line as a clickable file
//      link via a Scintilla indicator; stores the corresponding (path,
//      buffer position range) so the host controller can dispatch clicks.
//
// Leaves the editor read-only on return. Idempotent — safe to call again to
// re-render after a theme switch (the editor was already configured by
// GitDiffPainter::configureEditor which is itself idempotent).
class GitCommitRenderer
{
public:
    // Buffer-position range for one clickable file path inside the rendered
    // diff. Used by the host to translate Scintilla indicator clicks into
    // a "open file at sha" request.
    struct FileLink {
        QByteArray path;        // relative path inside repo
        qint32     start;       // byte offset in buffer
        qint32     length;
    };

    struct RenderResult {
        QVector<FileLink> fileLinks;
    };

    // Indicator ID used for clickable file paths. Allocated lazily on first
    // call via ScintillaNext::allocateIndicator().
    static int fileLinkIndicatorId(ScintillaNext *editor);

    // Render the detail into the editor.
    static RenderResult render(ScintillaNext *editor,
                                const GitCommitDetail &detail,
                                const GitDiffPalette &pal);
};

#endif // GIT_COMMIT_RENDERER_H
