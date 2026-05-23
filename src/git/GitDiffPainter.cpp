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

#include "GitDiffPainter.h"

#include "GitDiffPalette.h"
#include "ProfileScope.h"
#include "ScintillaNext.h"

#include "Scintilla.h"

#include <QByteArray>
#include <QVariant>
#include <QtGlobal>
#include <cstring>

namespace {

// Indicator IDs are allocated lazily on first configureEditor via
// ScintillaNext::allocateIndicator(name) and cached as QObject dynamic
// properties so subsequent calls reuse them.
constexpr const char *kPropIndicAddWord = "gitdiff_indic_add_word";
constexpr const char *kPropIndicDelWord = "gitdiff_indic_del_word";

constexpr int kMarkerAddLine = 18;
constexpr int kMarkerDelLine = 19;
constexpr int kMarkerHunkHeader = 20;

inline sptr_t rgb(const QColor &c)
{
    // Scintilla uses 0x00BBGGRR.
    return sptr_t(c.red()) | (sptr_t(c.green()) << 8) | (sptr_t(c.blue()) << 16);
}

inline void setStyleFB(ScintillaNext *e, int id, const QColor &fg, const QColor &bg)
{
    e->send(SCI_STYLESETFORE, id, rgb(fg));
    e->send(SCI_STYLESETBACK, id, rgb(bg));
}

inline int countDigits(qint32 x)
{
    return (x < 10 ? 1 :
           (x < 100 ? 2 :
           (x < 1000 ? 3 :
           (x < 10000 ? 4 :
           (x < 100000 ? 5 :
           (x < 1000000 ? 6 :
           (x < 10000000 ? 7 :
           (x < 100000000 ? 8 :
           (x < 1000000000 ? 9 :
           10)))))))));
}

int ensureWordIndicator(ScintillaNext *editor, const char *propName, const char *allocName)
{
    const QVariant v = editor->QObject::property(propName);
    if (v.isValid()) return v.toInt();
    const int id = editor->allocateIndicator(allocName);
    editor->QObject::setProperty(propName, id);
    return id;
}

void configureLineMarkers(ScintillaNext *editor, const GitDiffPalette &pal)
{
    // GitHub uses rgba(70,149,74,0.15) and rgba(229,83,75,0.10) which blend
    // against the editor background. SC_MARK_BACKGROUND only paints full rows
    // on SC_LAYER_BASE, and BASE ignores alpha — so we pass the palette's
    // pre-blended hex (#DAFBE1/#FFEBE9 light, #12261E/#301B1F dark) which
    // already represents the GitHub RGBA result against the theme background.
    editor->send(SCI_MARKERDEFINE, kMarkerAddLine, SC_MARK_BACKGROUND);
    editor->send(SCI_MARKERSETBACK, kMarkerAddLine, rgb(pal.bgAddLine));
    editor->send(SCI_MARKERSETLAYER, kMarkerAddLine, SC_LAYER_BASE);

    editor->send(SCI_MARKERDEFINE, kMarkerDelLine, SC_MARK_BACKGROUND);
    editor->send(SCI_MARKERSETBACK, kMarkerDelLine, rgb(pal.bgDelLine));
    editor->send(SCI_MARKERSETLAYER, kMarkerDelLine, SC_LAYER_BASE);

    editor->send(SCI_MARKERDEFINE, kMarkerHunkHeader, SC_MARK_BACKGROUND);
    editor->send(SCI_MARKERSETBACK, kMarkerHunkHeader, rgb(pal.bgHunkHeader));
    editor->send(SCI_MARKERSETLAYER, kMarkerHunkHeader, SC_LAYER_BASE);
}

void configureWordIndicators(ScintillaNext *editor, const GitDiffPalette &pal)
{
    const int addId = ensureWordIndicator(editor, kPropIndicAddWord, "git_diff_word_added");
    const int delId = ensureWordIndicator(editor, kPropIndicDelWord, "git_diff_word_deleted");

    // INDIC_STRAIGHTBOX: filled rectangle drawn under text. Alpha is opaque
    // on the fill (255 / 255), outline transparent — matches GitHub's
    // "darker inline patch" look on top of the solid line bg.
    editor->send(SCI_INDICSETSTYLE, addId, INDIC_STRAIGHTBOX);
    editor->send(SCI_INDICSETFORE, addId, rgb(pal.bgAddWord));
    editor->send(SCI_INDICSETALPHA, addId, 255);
    editor->send(SCI_INDICSETOUTLINEALPHA, addId, 0);
    editor->send(SCI_INDICSETUNDER, addId, 1);

    editor->send(SCI_INDICSETSTYLE, delId, INDIC_STRAIGHTBOX);
    editor->send(SCI_INDICSETFORE, delId, rgb(pal.bgDelWord));
    editor->send(SCI_INDICSETALPHA, delId, 255);
    editor->send(SCI_INDICSETOUTLINEALPHA, delId, 0);
    editor->send(SCI_INDICSETUNDER, delId, 1);
}

} // namespace

void GitDiffPainter::configureEditor(ScintillaNext *editor, const GitDiffPalette &pal)
{
    if (!editor) return;

    // No lexer — we apply styles ourselves.
    editor->send(SCI_SETREADONLY, 0, 0);
    editor->send(SCI_CLEARALL, 0, 0);
    editor->send(SCI_STYLECLEARALL, 0, 0);

    // Default style uses the editor's existing default background; ensure
    // foreground readable.
    const QColor defFg = pal.fgHunkHeader; // close to muted text, just placeholder
    setStyleFB(editor, StyleDefault,    QColor(Qt::black),  QColor(Qt::white));
    setStyleFB(editor, StyleFileHeader, pal.fgHunkHeader,   QColor(Qt::transparent));
    setStyleFB(editor, StyleHunkHeader, pal.fgHunkHeader,   QColor(Qt::transparent));
    setStyleFB(editor, StyleContext,    QColor(Qt::black),  QColor(Qt::white));
    setStyleFB(editor, StyleAdded,      QColor(Qt::black),  QColor(Qt::transparent));
    setStyleFB(editor, StyleDeleted,    QColor(Qt::black),  QColor(Qt::transparent));
    setStyleFB(editor, StyleNoNewline,  pal.fgHunkHeader,   QColor(Qt::transparent));
    setStyleFB(editor, StyleCommitMeta, pal.commitMetaFg,   QColor(Qt::transparent));
    setStyleFB(editor, StyleCommitBody, pal.commitBodyFg,   QColor(Qt::transparent));

    configureLineMarkers(editor, pal);

    configureWordIndicators(editor, pal);

    // Margin 0 is repurposed as a right-aligned TEXT margin: GitDiffPainter::render
    // populates it per-row with the diff's own old/new file line numbers (blank for
    // headers / hunk headers / no-newline markers / blank separators). The width is
    // set in render() once the maximum displayed number is known.
    editor->send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_RTEXT);
    editor->send(SCI_SETMARGINWIDTHN, 0, 0);
    // Hide selection / line markers / fold markers to look like a static view.
    editor->send(SCI_SETMARGINWIDTHN, 1, 0);
    editor->send(SCI_SETMARGINWIDTHN, 2, 0);
    (void)defFg;
}

void GitDiffPainter::render(ScintillaNext *editor,
                            const GitDiffParser::Result &parsed,
                            const GitDiffSyntaxMapper::Overlay *overlay,
                            const GitDiffPalette &pal)
{
    PROFILE_SCOPE("GitDiffPainter::render");
    if (!editor) return;

    // Refresh indicator / marker colors in case the theme changed between
    // configureEditor and render.
    configureLineMarkers(editor, pal);
    configureWordIndicators(editor, pal);

    editor->send(SCI_SETREADONLY, 0, 0);
    editor->send(SCI_CLEARALL, 0, 0);
    editor->send(SCI_MARKERDELETEALL, kMarkerAddLine, 0);
    editor->send(SCI_MARKERDELETEALL, kMarkerDelLine, 0);
    editor->send(SCI_MARKERDELETEALL, kMarkerHunkHeader, 0);

    const int rows = parsed.kinds.size();
    if (rows == 0) {
        editor->send(SCI_SETREADONLY, 1, 0);
        return;
    }

    // Estimate total size: sum of line lengths + 2 bytes/line (prefix + LF).
    qsizetype total = 0;
    for (int r = 0; r < rows; ++r) total += parsed.texts.at(r).size() + 2;

    QByteArray text;
    QByteArray styles;
    text.reserve(total);
    styles.reserve(total);

    // Per-row start byte offset in `text`, used for indicator fill ranges
    // and word-span position translation.
    QVector<qsizetype> rowStartInText;
    rowStartInText.resize(rows);
    // Per-row content start offset (excludes the '+'/'-' prefix byte).
    QVector<qsizetype> rowContentStartInText;
    rowContentStartInText.resize(rows);

    for (int r = 0; r < rows; ++r) {
        const QByteArray &line = parsed.texts.at(r);
        const auto kind = parsed.kinds.at(r);

        char prefix = '\0';
        char styleId = StyleDefault;
        switch (kind) {
            case GitDiffParser::LineKind::FileHeader: styleId = StyleFileHeader; break;
            case GitDiffParser::LineKind::HunkHeader: styleId = StyleHunkHeader; break;
            case GitDiffParser::LineKind::Context:    styleId = StyleContext;   prefix = ' '; break;
            case GitDiffParser::LineKind::Added:      styleId = StyleAdded;     prefix = '+'; break;
            case GitDiffParser::LineKind::Deleted:    styleId = StyleDeleted;   prefix = '-'; break;
            case GitDiffParser::LineKind::NoNewline:  styleId = StyleNoNewline; break;
            case GitDiffParser::LineKind::Empty:      styleId = StyleDefault;   break;
        }

        const qsizetype lineStartInText = text.size();
        rowStartInText[r] = lineStartInText;
        if (prefix != '\0') text.append(prefix);
        rowContentStartInText[r] = text.size();
        text.append(line);
        text.append('\n');
        const qsizetype lineEndInText = text.size();
        const qsizetype lineBytes = lineEndInText - lineStartInText;

        // Append styleId for each byte of this line.
        const qsizetype oldStylesSize = styles.size();
        styles.resize(oldStylesSize + lineBytes);
        std::memset(styles.data() + oldStylesSize, styleId, static_cast<size_t>(lineBytes));
    }

    // One bulk append + one bulk style flush.
    editor->send(SCI_APPENDTEXT, text.size(), reinterpret_cast<sptr_t>(text.constData()));
    editor->send(SCI_STARTSTYLING, 0, 0);
    editor->send(SCI_SETSTYLINGEX, styles.size(), reinterpret_cast<sptr_t>(styles.constData()));

    for (int r = 0; r < rows; ++r) {
        switch (parsed.kinds.at(r)) {
            case GitDiffParser::LineKind::HunkHeader:
                editor->send(SCI_MARKERADD, r, kMarkerHunkHeader);
                break;
            case GitDiffParser::LineKind::Added:
                editor->send(SCI_MARKERADD, r, kMarkerAddLine);
                break;
            case GitDiffParser::LineKind::Deleted:
                editor->send(SCI_MARKERADD, r, kMarkerDelLine);
                break;
            default:
                break;
        }
    }

    // Word-level indicator fills. Only applied when the overlay is present
    // AND has at least one word span. The indicator IDs were allocated /
    // configured in configureEditor (re-applied at the top of this function).
    if (overlay && (!overlay->addWordSpans.isEmpty() || !overlay->delWordSpans.isEmpty())) {
        const int addId = editor->QObject::property(kPropIndicAddWord).toInt();
        const int delId = editor->QObject::property(kPropIndicDelWord).toInt();

        // Clear any previous indicator state on this editor (cheap: single
        // pair of SCI calls per indicator over the entire document).
        editor->send(SCI_SETINDICATORCURRENT, addId, 0);
        editor->send(SCI_INDICATORCLEARRANGE, 0, text.size());
        editor->send(SCI_SETINDICATORCURRENT, delId, 0);
        editor->send(SCI_INDICATORCLEARRANGE, 0, text.size());

        auto fillSpans = [&](int indicId, const QVector<GitDiffSyntaxMapper::WordSpan> &spans) {
            if (spans.isEmpty()) return;
            editor->send(SCI_SETINDICATORCURRENT, indicId, 0);
            // Single chokepoint for the mapper -> painter positional contract.
            // colStart/colEnd are byte offsets within parsed.texts[row]
            // (the line content WITHOUT the '+'/'-' prefix the painter adds).
            // Anything outside that window is a mapper bug — Q_ASSERT in debug
            // so it shows up loudly; drop silently in release so the user sees
            // a missing highlight rather than a misplaced one.
            auto spanIsValid = [&](const GitDiffSyntaxMapper::WordSpan &s) -> bool {
                if (s.row < 0 || s.row >= rows) return false;
                const qint32 lineBytes = static_cast<qint32>(parsed.texts.at(s.row).size());
                if (s.colStart < 0 || s.colEnd <= s.colStart) return false;
                if (s.colEnd > lineBytes) return false;
                return true;
            };
            for (const auto &s : spans) {
                Q_ASSERT(spanIsValid(s));
                if (!spanIsValid(s)) continue;
                const qsizetype contentStart = rowContentStartInText[s.row];
                const qsizetype absStart = contentStart + s.colStart;
                const qsizetype absEnd = contentStart + s.colEnd;
                if (absEnd <= absStart) continue;
                editor->send(SCI_INDICATORFILLRANGE,
                             static_cast<sptr_t>(absStart),
                             static_cast<sptr_t>(absEnd - absStart));
            }
        };
        fillSpans(addId, overlay->addWordSpans);
        fillSpans(delId, overlay->delWordSpans);
    }

    // Margin 0 text: per-row diff line numbers. Added/Context show newLn, Deleted
    // shows oldLn, everything else (FileHeader/HunkHeader/NoNewline/Empty) is blank.
    // First pass finds the widest number so the margin can be sized exactly once;
    // second pass posts SCI_MARGINSETTEXT per row. Per-row sends are unavoidable —
    // Scintilla has no batch margin-text API — but each goes through SciFnDirect,
    // i.e. a single virtual dispatch into Editor::WndProc with no allocations.
    editor->send(SCI_MARGINTEXTCLEARALL, 0, 0);

    qint32 maxLn = 0;
    for (int r = 0; r < rows; ++r) {
        const auto kind = parsed.kinds.at(r);
        qint32 n = -1;
        switch (kind) {
            case GitDiffParser::LineKind::Added:
            case GitDiffParser::LineKind::Context:
                n = parsed.newLn.at(r);
                break;
            case GitDiffParser::LineKind::Deleted:
                n = parsed.oldLn.at(r);
                break;
            default:
                break;
        }
        if (n > maxLn) maxLn = n;
    }

    if (maxLn > 0) {
        const sptr_t charW = editor->send(SCI_TEXTWIDTH, STYLE_LINENUMBER,
                                          reinterpret_cast<sptr_t>("8"));
        const int digits = countDigits(maxLn);
        const sptr_t marginWidth = sptr_t(8) + (sptr_t(digits) + 1) * charW;
        editor->send(SCI_SETMARGINWIDTHN, 0, marginWidth);

        char buf[16];
        for (int r = 0; r < rows; ++r) {
            const auto kind = parsed.kinds.at(r);
            qint32 n = -1;
            switch (kind) {
                case GitDiffParser::LineKind::Added:
                case GitDiffParser::LineKind::Context:
                    n = parsed.newLn.at(r);
                    break;
                case GitDiffParser::LineKind::Deleted:
                    n = parsed.oldLn.at(r);
                    break;
                default:
                    break;
            }
            if (n <= 0) continue;

            // Render number into a fixed stack buffer to avoid per-row QByteArray
            // allocations on large diffs.
            int len = 0;
            char tmp[16];
            qint32 v = n;
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
        // No diff body lines (e.g. binary banner only) — keep the margin hidden.
        editor->send(SCI_SETMARGINWIDTHN, 0, 0);
    }

    editor->send(SCI_SETREADONLY, 1, 0);
}
