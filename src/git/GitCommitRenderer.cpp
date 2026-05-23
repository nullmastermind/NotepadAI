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

#include "GitCommitRenderer.h"

#include "GitDiffPainter.h"
#include "GitDiffPalette.h"
#include "GitDiffParser.h"
#include "GitDiffSyntaxMapper.h"
#include "ProfileScope.h"
#include "ScintillaNext.h"

#include "Scintilla.h"

#include <QByteArray>
#include <QDateTime>
#include <QVariant>

#include <cstring>

namespace {

constexpr const char *kPropFileLinkIndic = "gitcommit_indic_file_link";
// Mirror GitDiffPainter's dynamic-property names — the add/del word
// indicators are allocated by GitDiffPainter::configureEditor() (called from
// GitCommitView before this renderer runs), so we just fetch the IDs.
constexpr const char *kPropIndicAddWord = "gitdiff_indic_add_word";
constexpr const char *kPropIndicDelWord = "gitdiff_indic_del_word";
constexpr int kMarkerAddLine = 18;      // mirror GitDiffPainter constants
constexpr int kMarkerDelLine = 19;
constexpr int kMarkerHunkHeader = 20;

inline sptr_t rgb(const QColor &c)
{
    return sptr_t(c.red()) | (sptr_t(c.green()) << 8) | (sptr_t(c.blue()) << 16);
}

inline void appendStyled(QByteArray &text, QByteArray &styles,
                          QByteArrayView s, char styleId)
{
    const qsizetype before = text.size();
    text.append(s);
    const qsizetype after = text.size();
    const qsizetype oldStylesSize = styles.size();
    styles.resize(oldStylesSize + (after - before));
    std::memset(styles.data() + oldStylesSize, styleId,
                static_cast<size_t>(after - before));
}

// Format a UTC epoch as "YYYY-MM-DD HH:MM:SS UTC" — ISO 8601 short form,
// stable across locales. We avoid QLocale here because the commit header is
// developer-facing, not end-user-facing.
QByteArray formatTimestamp(qint64 epoch)
{
    if (epoch <= 0) return {};
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(epoch, Qt::UTC);
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")).toUtf8()
        + QByteArrayLiteral(" UTC");
}

// Extract the "b/<path>" path from a "+++ b/<path>" line. Returns empty if
// the line doesn't match. The +++ form is the destination path under
// `git diff` convention; for renames Scintilla shows both the --- and +++
// forms but we only link the +++ to the new path (the file at this commit).
QByteArray extractPlusPath(const QByteArray &line)
{
    // "+++ b/<path>" or "+++ /dev/null" for delete.
    if (!line.startsWith(QByteArrayLiteral("+++ "))) return {};
    QByteArray rest = line.mid(4);
    if (rest == QByteArrayLiteral("/dev/null")) return {};
    if (rest.startsWith(QByteArrayLiteral("b/"))) rest = rest.mid(2);
    // Strip any trailing tab-separated metadata (rare for `git show -p`).
    const int tab = rest.indexOf('\t');
    if (tab >= 0) rest.truncate(tab);
    return rest;
}

} // namespace

int GitCommitRenderer::fileLinkIndicatorId(ScintillaNext *editor)
{
    if (!editor) return -1;
    const QVariant v = editor->QObject::property(kPropFileLinkIndic);
    if (v.isValid()) return v.toInt();
    const int id = editor->allocateIndicator(QStringLiteral("git_commit_file_link"));
    editor->QObject::setProperty(kPropFileLinkIndic, id);
    // Style: hidden underline so the cursor changes to a link cursor on hover
    // without distracting the diff body. INDIC_PLAIN is a 1px underline.
    editor->send(SCI_INDICSETSTYLE, id, INDIC_PLAIN);
    editor->send(SCI_INDICSETHOVERSTYLE, id, INDIC_PLAIN);
    editor->send(SCI_INDICSETUNDER, id, 0);
    return id;
}

GitCommitRenderer::RenderResult
GitCommitRenderer::render(ScintillaNext *editor,
                          const GitCommitDetail &detail,
                          const GitDiffPalette &pal)
{
    PROFILE_SCOPE("GitCommitRenderer::render");
    RenderResult result;
    if (!editor) return result;

    editor->send(SCI_SETREADONLY, 0, 0);
    editor->send(SCI_CLEARALL, 0, 0);
    editor->send(SCI_MARKERDELETEALL, -1, 0);

    if (detail.isEmpty()) {
        editor->send(SCI_SETREADONLY, 1, 0);
        return result;
    }

    // -------- Header block --------
    QByteArray text;
    QByteArray styles;
    text.reserve(detail.body.size() + detail.diffBytes.size() + 4096);
    styles.reserve(text.capacity());

    using PStyle = GitDiffPainter::StyleId;

    // "commit <sha>"
    appendStyled(text, styles, QByteArrayLiteral("commit "),
                 PStyle::StyleCommitMeta);
    appendStyled(text, styles, detail.sha, PStyle::StyleCommitMeta);
    appendStyled(text, styles, QByteArrayLiteral("\n"),
                 PStyle::StyleCommitMeta);

    if (detail.isMerge()) {
        appendStyled(text, styles, QByteArrayLiteral("Merge: "),
                     PStyle::StyleCommitMeta);
        // Show short SHAs for parents (8 chars per Linux convention).
        QByteArray parentsShort;
        const QList<QByteArray> ps = detail.parents.split(' ');
        for (const auto &p : ps) {
            if (!parentsShort.isEmpty()) parentsShort.append(' ');
            parentsShort.append(p.left(7));
        }
        appendStyled(text, styles, parentsShort, PStyle::StyleCommitMeta);
        appendStyled(text, styles, QByteArrayLiteral("\n"),
                     PStyle::StyleCommitMeta);
    }

    appendStyled(text, styles, QByteArrayLiteral("Author: "),
                 PStyle::StyleCommitMeta);
    appendStyled(text, styles, detail.authorName, PStyle::StyleCommitMeta);
    if (!detail.authorEmail.isEmpty()) {
        appendStyled(text, styles, QByteArrayLiteral(" <"),
                     PStyle::StyleCommitMeta);
        appendStyled(text, styles, detail.authorEmail, PStyle::StyleCommitMeta);
        appendStyled(text, styles, QByteArrayLiteral(">"),
                     PStyle::StyleCommitMeta);
    }
    appendStyled(text, styles, QByteArrayLiteral("\n"),
                 PStyle::StyleCommitMeta);

    appendStyled(text, styles, QByteArrayLiteral("Date:   "),
                 PStyle::StyleCommitMeta);
    appendStyled(text, styles, formatTimestamp(detail.authorTime),
                 PStyle::StyleCommitMeta);
    appendStyled(text, styles, QByteArrayLiteral("\n\n"),
                 PStyle::StyleCommitMeta);

    // Body (commit message). Indented 4 spaces like `git log` does.
    if (!detail.body.isEmpty()) {
        // Split body into lines, prepend 4 spaces, style each line.
        qsizetype start = 0;
        const char *bd = detail.body.constData();
        const qsizetype bn = detail.body.size();
        for (qsizetype i = 0; i <= bn; ++i) {
            const bool atNl = (i < bn && bd[i] == '\n');
            const bool atEnd = (i == bn);
            if (atNl || atEnd) {
                appendStyled(text, styles, QByteArrayLiteral("    "),
                             PStyle::StyleCommitBody);
                appendStyled(text, styles,
                             QByteArrayView(bd + start, i - start),
                             PStyle::StyleCommitBody);
                appendStyled(text, styles, QByteArrayLiteral("\n"),
                             PStyle::StyleCommitBody);
                start = i + 1;
            }
        }
        appendStyled(text, styles, QByteArrayLiteral("\n"),
                     PStyle::StyleCommitBody);
    }

    // -------- Diff block --------
    // We collect per-row info as in GitDiffPainter::render so we can apply
    // markers + line-numbers after the bulk SCI_APPENDTEXT. Header bytes were
    // already appended; their row count = number of '\n' so far.
    int diffStartRow = 0;
    for (char c : text) if (c == '\n') ++diffStartRow;

    QVector<int>       rowMarker;     // row → marker id (-1 for none)
    QVector<qint32>    rowLineNum;    // for margin display (0 = blank)
    QVector<QByteArray> filePlusLine; // per row: the "+++ b/X" line if seen
    // Per parsed-diff-row, byte offset in `text` where the line content
    // starts (i.e. AFTER any '+'/'-' prefix). Used to translate
    // GitDiffSyntaxMapper word-spans (which are colStart/colEnd within
    // `parsed.texts[row]`) into absolute buffer positions for the
    // SCI_INDICATORFILLRANGE word-diff highlight.
    QVector<qsizetype> diffRowContentStartInBuf;
    rowMarker.reserve(diffStartRow + 64);
    rowLineNum.reserve(diffStartRow + 64);
    filePlusLine.reserve(diffStartRow + 64);
    for (int i = 0; i < diffStartRow; ++i) {
        rowMarker.append(-1);
        rowLineNum.append(0);
        filePlusLine.append(QByteArray());
    }

    qsizetype diffStartByte = text.size();

    // Word-level diff overlay (intra-line character highlight). Declared
    // outside the diff-bytes block so we can apply its INDIC fills AFTER the
    // bulk SCI_APPENDTEXT below — Scintilla refuses indicator ranges past
    // the document end, so the buffer must be populated first.
    GitDiffSyntaxMapper::Overlay diffOverlay;

    if (!detail.diffBytes.isEmpty()) {
        GitDiffParser::Result parsed = GitDiffParser::parse(detail.diffBytes);
        const int rows = parsed.kinds.size();
        diffRowContentStartInBuf.reserve(rows);
        for (int r = 0; r < rows; ++r) {
            const QByteArray &line = parsed.texts.at(r);
            const auto kind = parsed.kinds.at(r);

            char prefix = '\0';
            char styleId = PStyle::StyleDefault;
            int marker = -1;
            qint32 displayLn = 0;
            switch (kind) {
                case GitDiffParser::LineKind::FileHeader:
                    styleId = PStyle::StyleFileHeader; break;
                case GitDiffParser::LineKind::HunkHeader:
                    styleId = PStyle::StyleHunkHeader;
                    marker = kMarkerHunkHeader; break;
                case GitDiffParser::LineKind::Context:
                    styleId = PStyle::StyleContext; prefix = ' ';
                    displayLn = parsed.newLn.at(r); break;
                case GitDiffParser::LineKind::Added:
                    styleId = PStyle::StyleAdded; prefix = '+';
                    marker = kMarkerAddLine;
                    displayLn = parsed.newLn.at(r); break;
                case GitDiffParser::LineKind::Deleted:
                    styleId = PStyle::StyleDeleted; prefix = '-';
                    marker = kMarkerDelLine;
                    displayLn = parsed.oldLn.at(r); break;
                case GitDiffParser::LineKind::NoNewline:
                    styleId = PStyle::StyleNoNewline; break;
                case GitDiffParser::LineKind::Empty:
                    styleId = PStyle::StyleDefault; break;
            }

            const qsizetype rowStart = text.size();
            if (prefix != '\0') {
                text.append(prefix);
                styles.resize(styles.size() + 1);
                styles[styles.size() - 1] = styleId;
            }
            const qsizetype contentStart = text.size();
            diffRowContentStartInBuf.append(contentStart);
            appendStyled(text, styles, line, styleId);
            text.append('\n');
            styles.resize(styles.size() + 1);
            styles[styles.size() - 1] = styleId;

            rowMarker.append(marker);
            rowLineNum.append(displayLn);

            // FileHeader line: capture if it's "+++ b/X" for file linking.
            QByteArray plusPath;
            if (kind == GitDiffParser::LineKind::FileHeader) {
                plusPath = extractPlusPath(line);
            }
            filePlusLine.append(plusPath);
            // FileLink emit — indicator covers the file path text (not the
            // "+++ b/" prefix).
            if (!plusPath.isEmpty()) {
                // Find "b/" inside the original line, then the path follows.
                const int bSlash = line.indexOf(QByteArrayLiteral("b/"));
                qint32 pathStartInLine = (bSlash >= 0) ? bSlash + 2 : 0;
                qint32 pathStartInBuf =
                    static_cast<qint32>(contentStart + pathStartInLine);
                FileLink fl;
                fl.path = plusPath;
                fl.start = pathStartInBuf;
                fl.length = static_cast<qint32>(plusPath.size());
                result.fileLinks.append(fl);
            }
            (void)rowStart;
        }
        // Optional "Diff truncated" footer.
        if (detail.truncated) {
            const QByteArray foot =
                QByteArrayLiteral("\n... diff truncated (commit too large) ...\n");
            appendStyled(text, styles, foot, PStyle::StyleCommitMeta);
            // Add blank entries for these rows so the parallel arrays stay
            // in sync (they're only consulted for diff rows but defensive).
            for (qsizetype i = 0; i < foot.count('\n'); ++i) {
                rowMarker.append(-1);
                rowLineNum.append(0);
                filePlusLine.append(QByteArray());
            }
        }

        // Compute word-level diff overlay (intra-line "character diff"
        // highlight). Pass empty lexer names → GitDiffSyntaxMapper skips
        // syntax lexing but still produces add/del WordSpans via token-LCS
        // on paired Del+Add blocks (independent of lexer). Per-file lexing
        // is out of scope here because a commit's combined diff covers
        // mixed file types; doing it correctly would require splitting the
        // parsed result by file boundary, which we defer.
        if (!parsed.isBinary && !parsed.kinds.isEmpty()) {
            GitDiffSyntaxMapper::Input in;
            in.parsed = &parsed;
            // Empty lexer names — see comment above.
            GitDiffSyntaxMapper::State mapperState;
            diffOverlay = GitDiffSyntaxMapper::map(in, mapperState);
        }
    }

    // Bulk-append text + styles in one shot.
    editor->send(SCI_APPENDTEXT, text.size(),
                 reinterpret_cast<sptr_t>(text.constData()));
    editor->send(SCI_STARTSTYLING, 0, 0);
    editor->send(SCI_SETSTYLINGEX, styles.size(),
                 reinterpret_cast<sptr_t>(styles.constData()));

    // Apply per-row markers.
    for (int r = 0; r < rowMarker.size(); ++r) {
        if (rowMarker[r] >= 0) {
            editor->send(SCI_MARKERADD, r, rowMarker[r]);
        }
    }

    // Margin 0 = right-aligned text (configured by GitDiffPainter::configureEditor).
    // For diff rows show the source/dest line number; header rows blank.
    qint32 maxLn = 0;
    for (int r = diffStartRow; r < rowLineNum.size(); ++r) {
        if (rowLineNum[r] > maxLn) maxLn = rowLineNum[r];
    }
    editor->send(SCI_MARGINTEXTCLEARALL, 0, 0);
    if (maxLn > 0) {
        const sptr_t charW = editor->send(SCI_TEXTWIDTH, STYLE_LINENUMBER,
                                           reinterpret_cast<sptr_t>("8"));
        int digits = 1;
        for (qint32 v = maxLn; v >= 10; v /= 10) ++digits;
        const sptr_t marginWidth = sptr_t(8) + (sptr_t(digits) + 1) * charW;
        editor->send(SCI_SETMARGINWIDTHN, 0, marginWidth);
        char buf[16];
        for (int r = diffStartRow; r < rowLineNum.size(); ++r) {
            if (rowLineNum[r] <= 0) continue;
            int len = 0;
            char tmp[16];
            qint32 v = rowLineNum[r];
            do {
                tmp[len++] = char('0' + (v % 10));
                v /= 10;
            } while (v > 0);
            for (int i = 0; i < len; ++i) buf[i] = tmp[len - 1 - i];
            buf[len] = '\0';
            editor->send(SCI_MARGINSETTEXT, r, reinterpret_cast<sptr_t>(buf));
            editor->send(SCI_MARGINSETSTYLE, r, STYLE_LINENUMBER);
        }
    } else {
        editor->send(SCI_SETMARGINWIDTHN, 0, 0);
    }

    // Word-level diff highlight (intra-line "character diff"). Translate
    // mapper output (row indices into parsed.texts + colStart/colEnd byte
    // offsets within the line content) into absolute buffer ranges via
    // diffRowContentStartInBuf, then fire two ranges of indicator fills.
    if (diffOverlay.ok &&
        (!diffOverlay.addWordSpans.isEmpty() || !diffOverlay.delWordSpans.isEmpty())) {
        const int addIndicId = editor->QObject::property(kPropIndicAddWord).toInt();
        const int delIndicId = editor->QObject::property(kPropIndicDelWord).toInt();

        editor->send(SCI_SETINDICATORCURRENT, addIndicId, 0);
        editor->send(SCI_INDICATORCLEARRANGE, 0, static_cast<sptr_t>(text.size()));
        editor->send(SCI_SETINDICATORCURRENT, delIndicId, 0);
        editor->send(SCI_INDICATORCLEARRANGE, 0, static_cast<sptr_t>(text.size()));

        const qsizetype rowCount = diffRowContentStartInBuf.size();
        auto fillSpans = [&](int indicId,
                              const QVector<GitDiffSyntaxMapper::WordSpan> &spans) {
            if (spans.isEmpty()) return;
            editor->send(SCI_SETINDICATORCURRENT, indicId, 0);
            for (const auto &s : spans) {
                if (s.row < 0 || s.row >= rowCount) continue;
                if (s.colStart < 0 || s.colEnd <= s.colStart) continue;
                const qsizetype contentStart = diffRowContentStartInBuf[s.row];
                const qsizetype absStart = contentStart + s.colStart;
                const qsizetype absEnd   = contentStart + s.colEnd;
                if (absEnd <= absStart) continue;
                editor->send(SCI_INDICATORFILLRANGE,
                             static_cast<sptr_t>(absStart),
                             static_cast<sptr_t>(absEnd - absStart));
            }
        };
        fillSpans(addIndicId, diffOverlay.addWordSpans);
        fillSpans(delIndicId, diffOverlay.delWordSpans);
    }

    // File-link indicators on the path runs we collected.
    if (!result.fileLinks.isEmpty()) {
        const int indicId = fileLinkIndicatorId(editor);
        if (indicId >= 0) {
            editor->send(SCI_INDICSETFORE, indicId,
                         rgb(pal.fgModified));   // blue underline
            editor->send(SCI_SETINDICATORCURRENT, indicId, 0);
            editor->send(SCI_INDICATORCLEARRANGE, 0,
                         static_cast<sptr_t>(text.size()));
            for (const auto &fl : result.fileLinks) {
                editor->send(SCI_INDICATORFILLRANGE,
                             static_cast<sptr_t>(fl.start),
                             static_cast<sptr_t>(fl.length));
            }
        }
    }

    (void)diffStartByte;
    editor->send(SCI_SETREADONLY, 1, 0);
    return result;
}
