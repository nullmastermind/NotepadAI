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

#include "AcpToolCallCard.h"

#include "AcpDiffStat.h"
#include "GrillMeForm.h"
#include "MarkdownView.h"

#include <QAbstractTextDocumentLayout>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStringList>
#include <QStringView>
#include <QStyle>
#include <QTextBlock>
#include <QTextFrame>
#include <QVector>

#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

// The card's outer column holds an optional Goal badge, then the header row:
// its first child layout.
QLayout *headerRow(const QVBoxLayout *outer)
{
    for (int i = 0; outer && i < outer->count(); ++i) {
        if (QLayout *row = outer->itemAt(i)->layout())
            return row;
    }
    return nullptr;
}

// Diff-row palette. Greens/reds match the accent colors already in use by
// AcpUsageIndicator. Backgrounds are semi-transparent so the contrast holds in
// both light and dark Qt palettes without hard-coding a chrome surface.
constexpr const char *kDiffAddBg   = "rgba(76, 175, 80, 0.18)";
constexpr const char *kDiffDelBg   = "rgba(217, 83, 79, 0.18)";
constexpr const char *kDiffAddMark = AcpDiffStat::kAddedColor;
constexpr const char *kDiffDelMark = AcpDiffStat::kRemovedColor;

// Cap LCS to bound the O(m*n) cell count. Compact hunks skip this path.
// 256² ints ≈ 260 KiB, one allocation, only on an expanded completed card
// (collapsed cards skip body render; 80 ms debounce coalesces updates).
constexpr int kMaxLcsLines = 256;
constexpr int kMaxOmpDiffLines = 2000;
constexpr int kMaxCompactDiffChars = 128 * 1024;
constexpr int kMaxDiffSourceChars = 512 * 1024;
constexpr int kKeepContext = 3;
constexpr int kMaxRawOutputChars = 60000;
constexpr int kBodyRenderDebounceMs = 80;
// Instructions longer than this many lines (plus one) show only that many
// until "Show more": the subagent's calls stay within reach.
constexpr int kPromptPreviewLines = 6;
constexpr int kSectionGapPx = 4; // air above "Instructions" / "Result"

enum class DiffKind : std::uint8_t { Context, Add, Remove };
struct DiffLine
{
    DiffKind kind;
    int oldLine; // 0 when N/A
    int newLine; // 0 when N/A
    QString text;
};

// QTextDocument's HTML/CSS subset does not understand the `palette(...)`
// function. Resolve the palette to hex strings on the widget side and inline
// them so dark themes don't fall back to black ink.
struct DiffPalette
{
    QString text;
    QString dim;
    QString border;
    QString headerBg;
    // CSS font declaration ("font-family:...; font-size:...;") for code/diff
    // bodies, derived from the chat (Default Font). Always ends with a
    // monospace fallback. Empty falls back to the hardcoded Consolas default.
    QString fontCss;
};

// Build a CSS font declaration from a QFont for the tool-call body HTML. Always
// appends a monospace fallback so code/diff stays fixed-width even if the chat
// font has no monospace glyphs. Family is single-quoted to survive spaces.
QString fontCssDecl(const QFont &font)
{
    QString decl = QStringLiteral("font-family: '%1', monospace;")
                       .arg(font.family());
    if (font.pointSizeF() > 0.0) {
        decl += QStringLiteral(" font-size: %1pt;").arg(font.pointSizeF());
    } else if (font.pixelSize() > 0) {
        decl += QStringLiteral(" font-size: %1px;").arg(font.pixelSize());
    }
    return decl;
}

QVector<DiffLine> lcsLineDiff(const QStringList &oldLines, const QStringList &newLines)
{
    const int m = oldLines.size();
    const int n = newLines.size();
    const int cols = n + 1;
    QVector<int> dp((m + 1) * cols, 0);
    auto at = [&](int i, int j) -> int & { return dp[i * cols + j]; };
    for (int i = 1; i <= m; ++i) {
        const QString &oldLine = oldLines.at(i - 1);
        for (int j = 1; j <= n; ++j) {
            if (oldLine == newLines.at(j - 1)) {
                at(i, j) = at(i - 1, j - 1) + 1;
            } else {
                at(i, j) = std::max(at(i - 1, j), at(i, j - 1));
            }
        }
    }
    QVector<DiffLine> out;
    out.reserve(m + n);
    int i = m;
    int j = n;
    while (i > 0 && j > 0) {
        if (oldLines.at(i - 1) == newLines.at(j - 1)) {
            out.append({DiffKind::Context, i, j, oldLines.at(i - 1)});
            --i;
            --j;
        } else if (at(i - 1, j) >= at(i, j - 1)) {
            out.append({DiffKind::Remove, i, 0, oldLines.at(i - 1)});
            --i;
        } else {
            out.append({DiffKind::Add, 0, j, newLines.at(j - 1)});
            --j;
        }
    }
    while (i > 0) {
        out.append({DiffKind::Remove, i, 0, oldLines.at(i - 1)});
        --i;
    }
    while (j > 0) {
        out.append({DiffKind::Add, 0, j, newLines.at(j - 1)});
        --j;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// O(m+n) fallback when either side exceeds kMaxLcsLines: common prefix/suffix
// plus one middle replace. collapseContext then keeps 3 lines around the change.
QVector<DiffLine> prefixSuffixDiff(const QStringList &oldLines, const QStringList &newLines)
{
    const int m = oldLines.size();
    const int n = newLines.size();
    int pre = 0;
    while (pre < m && pre < n && oldLines.at(pre) == newLines.at(pre)) {
        ++pre;
    }
    int oldEnd = m;
    int newEnd = n;
    while (oldEnd > pre && newEnd > pre
           && oldLines.at(oldEnd - 1) == newLines.at(newEnd - 1)) {
        --oldEnd;
        --newEnd;
    }
    QVector<DiffLine> rows;
    rows.reserve(qMin(m + n, kMaxOmpDiffLines));
    for (int k = 0; k < pre && rows.size() < kMaxOmpDiffLines; ++k) {
        rows.append({DiffKind::Context, k + 1, k + 1, oldLines.at(k)});
    }
    for (int k = pre; k < oldEnd && rows.size() < kMaxOmpDiffLines; ++k) {
        rows.append({DiffKind::Remove, k + 1, 0, oldLines.at(k)});
    }
    for (int k = pre; k < newEnd && rows.size() < kMaxOmpDiffLines; ++k) {
        rows.append({DiffKind::Add, 0, k + 1, newLines.at(k)});
    }
    for (int k = oldEnd; k < m && rows.size() < kMaxOmpDiffLines; ++k) {
        rows.append({DiffKind::Context, k + 1, newEnd + (k - oldEnd) + 1, oldLines.at(k)});
    }
    return rows;
}

const QChar kBoxGutter(0x2502);

int parseLeadingInt(QStringView s, int *consumed)
{
    int i = 0;
    int n = 0;
    const int len = s.size();
    while (i < len) {
        const ushort u = s.at(i).unicode();
        if (u < '0' || u > '9') {
            break;
        }
        n = n * 10 + static_cast<int>(u - '0');
        ++i;
        if (n > 10'000'000) {
            break;
        }
    }
    *consumed = i;
    return n;
}

bool parseOmpNumberedLine(QStringView line, DiffLine *out)
{
    int i = 0;
    const int n = line.size();
    while (i < n && line.at(i) == QLatin1Char(' ')) {
        ++i;
    }
    if (i >= n) {
        return false;
    }
    QChar mark = QLatin1Char(' ');
    const QChar first = line.at(i);
    if (first == QLatin1Char('+') || first == QLatin1Char('-')) {
        mark = first;
        ++i;
    }
    int consumed = 0;
    const int lineNo = parseLeadingInt(line.mid(i), &consumed);
    if (consumed == 0 || lineNo <= 0) {
        return false;
    }
    i += consumed;
    while (i < n && line.at(i) == QLatin1Char(' ')) {
        ++i;
    }
    if (i >= n) {
        return false;
    }
    const QChar sep = line.at(i);
    if (sep != QLatin1Char('|') && sep != kBoxGutter) {
        return false;
    }
    ++i;
    const QString text = line.mid(i).toString();
    if (mark == QLatin1Char('+')) {
        *out = {DiffKind::Add, 0, lineNo, text};
    } else if (mark == QLatin1Char('-')) {
        *out = {DiffKind::Remove, lineNo, 0, text};
    } else {
        *out = {DiffKind::Context, lineNo, lineNo, text};
    }
    return true;
}

QVector<DiffLine> parseOmpNumberedDiff(const QString &text)
{
    QVector<DiffLine> rows;
    const QStringList lines = text.split(QLatin1Char('\n'));
    bool started = false;
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (line.isEmpty()) {
            continue;
        }
        DiffLine row{};
        if (!parseOmpNumberedLine(QStringView{line}, &row)) {
            if (!started) {
                return {};
            }
            continue;
        }
        started = true;
        rows.append(row);
        if (rows.size() >= kMaxOmpDiffLines) {
            break;
        }
    }
    return rows;
}

bool parseUnifiedHunkHeader(QStringView line, int *oldStart, int *newStart)
{
    const int minus = line.indexOf(QLatin1Char('-'));
    const int plus = line.indexOf(QLatin1Char('+'));
    if (minus < 0 || plus <= minus) {
        return false;
    }
    int consumed = 0;
    const int oldN = parseLeadingInt(line.mid(minus + 1), &consumed);
    if (consumed == 0) {
        return false;
    }
    consumed = 0;
    const int newN = parseLeadingInt(line.mid(plus + 1), &consumed);
    if (consumed == 0) {
        return false;
    }
    *oldStart = oldN > 0 ? oldN : 1;
    *newStart = newN > 0 ? newN : 1;
    return true;
}

QVector<DiffLine> parseUnifiedDiff(const QString &text)
{
    QVector<DiffLine> rows;
    const QStringList lines = text.split(QLatin1Char('\n'));
    int oldL = 0;
    int newL = 0;
    bool seenHunk = false;
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (line.startsWith(QLatin1String("@@"))) {
            if (!parseUnifiedHunkHeader(QStringView{line}, &oldL, &newL)) {
                if (!seenHunk) {
                    return {};
                }
                continue;
            }
            seenHunk = true;
            continue;
        }
        if (line.startsWith(QLatin1String("---"))
            || line.startsWith(QLatin1String("+++"))
            || line.startsWith(QLatin1String("diff "))
            || line.startsWith(QLatin1String("index "))
            || line.startsWith(QLatin1Char('\\'))) {
            continue;
        }
        if (line.isEmpty()) {
            if (seenHunk && oldL > 0) {
                rows.append({DiffKind::Context, oldL, newL, QString()});
                ++oldL;
                ++newL;
            }
            continue;
        }
        const QChar mark = line.at(0);
        const QString body = line.mid(1);
        if (mark == QLatin1Char('+')) {
            if (newL <= 0) {
                newL = 1;
            }
            seenHunk = true;
            rows.append({DiffKind::Add, 0, newL, body});
            ++newL;
        } else if (mark == QLatin1Char('-')) {
            if (oldL <= 0) {
                oldL = 1;
            }
            seenHunk = true;
            rows.append({DiffKind::Remove, oldL, 0, body});
            ++oldL;
        } else if (mark == QLatin1Char(' ') || mark == QLatin1Char('\t')) {
            if (oldL <= 0) {
                oldL = 1;
            }
            if (newL <= 0) {
                newL = 1;
            }
            seenHunk = true;
            rows.append({DiffKind::Context, oldL, newL, body});
            ++oldL;
            ++newL;
        } else if (!seenHunk) {
            continue;
        }
        if (rows.size() >= kMaxOmpDiffLines) {
            break;
        }
    }
    return rows;
}

QVector<DiffLine> collapseContext(const QVector<DiffLine> &rows)
{
    if (rows.isEmpty()) {
        return rows;
    }
    bool anyChange = false;
    bool anyContext = false;
    for (const auto &row : rows) {
        if (row.kind == DiffKind::Context) {
            anyContext = true;
        } else {
            anyChange = true;
        }
    }
    if (!anyChange || !anyContext) {
        return rows;
    }

    QVector<DiffLine> out;
    out.reserve(qMin(rows.size(), kMaxOmpDiffLines));
    const int n = rows.size();
    int i = 0;
    while (i < n && out.size() < kMaxOmpDiffLines) {
        if (rows.at(i).kind != DiffKind::Context) {
            out.append(rows.at(i));
            ++i;
            continue;
        }
        int j = i;
        while (j < n && rows.at(j).kind == DiffKind::Context) {
            ++j;
        }
        const int run = j - i;
        const bool leading = (i == 0);
        const bool trailing = (j == n);
        if (run <= kKeepContext) {
            for (int k = i; k < j && out.size() < kMaxOmpDiffLines; ++k) {
                out.append(rows.at(k));
            }
        } else if (leading) {
            out.append({DiffKind::Context, 0, 0, QStringLiteral("...")});
            for (int k = j - kKeepContext; k < j && out.size() < kMaxOmpDiffLines; ++k) {
                out.append(rows.at(k));
            }
        } else if (trailing) {
            for (int k = i; k < i + kKeepContext && out.size() < kMaxOmpDiffLines; ++k) {
                out.append(rows.at(k));
            }
            if (out.size() < kMaxOmpDiffLines) {
                out.append({DiffKind::Context, 0, 0, QStringLiteral("...")});
            }
        } else if (run <= 2 * kKeepContext) {
            for (int k = i; k < j && out.size() < kMaxOmpDiffLines; ++k) {
                out.append(rows.at(k));
            }
        } else {
            for (int k = i; k < i + kKeepContext && out.size() < kMaxOmpDiffLines; ++k) {
                out.append(rows.at(k));
            }
            if (out.size() < kMaxOmpDiffLines) {
                out.append({DiffKind::Context, 0, 0, QStringLiteral("...")});
            }
            for (int k = j - kKeepContext; k < j && out.size() < kMaxOmpDiffLines; ++k) {
                out.append(rows.at(k));
            }
        }
        i = j;
    }
    return out;
}

QVector<DiffLine> rowsFromCompactDiff(const QString &compact)
{
    if (compact.isEmpty()) {
        return {};
    }
    QString src = compact;
    if (src.size() > kMaxCompactDiffChars) {
        src.truncate(kMaxCompactDiffChars);
    }
    QVector<DiffLine> rows = parseOmpNumberedDiff(src);
    if (rows.isEmpty()) {
        rows = parseUnifiedDiff(src);
    }
    return rows;
}


QString padNumber(int n, int width)
{
    const QString s = n > 0 ? QString::number(n) : QString();
    // Use NBSP so QTextDocument doesn't collapse the leading spaces; we're
    // inside white-space:pre but it's the more robust choice for HTML.
    return QString(width - s.size(), QChar(0x00A0)) + s.toHtmlEscaped();
}

QString renderDiffRow(const DiffLine &line, const DiffPalette &pal, int numWidth)
{
    QString bg;
    QString mark;
    QString markColor;
    int displayNum = 0;
    switch (line.kind) {
        case DiffKind::Add:
            bg = QString::fromLatin1(kDiffAddBg);
            mark = QStringLiteral("+");
            markColor = QString::fromLatin1(kDiffAddMark);
            displayNum = line.newLine;
            break;
        case DiffKind::Remove:
            bg = QString::fromLatin1(kDiffDelBg);
            mark = QStringLiteral("-");
            markColor = QString::fromLatin1(kDiffDelMark);
            displayNum = line.oldLine;
            break;
        case DiffKind::Context:
            bg = QStringLiteral("transparent");
            mark = QStringLiteral(" ");
            markColor = pal.dim;
            displayNum = line.newLine > 0 ? line.newLine : line.oldLine;
            break;
    }
    return QStringLiteral(
               "<div style=\"background:%1; color:%2; white-space:pre-wrap; "
               "%8 padding: 0 4px;\">"
               "<span style=\"color:%3;\">%4</span> "
               "<span style=\"color:%5;\">%6</span> %7</div>")
        .arg(bg, pal.text, pal.dim, padNumber(displayNum, numWidth),
             markColor, mark, line.text.toHtmlEscaped(),
             pal.fontCss.isEmpty()
                 ? QStringLiteral("font-family: Consolas, monospace;")
                 : pal.fontCss);
}

QString renderDiffBlock(const QJsonObject &block, const DiffPalette &pal)
{
    const QString path = block.value(QStringLiteral("path")).toString();
    const QJsonValue oldVal = block.value(QStringLiteral("oldText"));
    const QJsonValue newVal = block.value(QStringLiteral("newText"));
    const QJsonValue compactVal = block.value(QStringLiteral("diff"));
    const bool oldIsNull = oldVal.isNull() || oldVal.isUndefined();
    const QString oldText = oldVal.isString() ? oldVal.toString() : QString();
    const QString newText = newVal.isString() ? newVal.toString() : QString();
    const QString compact = compactVal.isString() ? compactVal.toString() : QString();

    auto splitLines = [](const QString &s) {
        QStringList lines = s.split(QLatin1Char('\n'));
        for (QString &line : lines) {
            if (line.endsWith(QLatin1Char('\r'))) {
                line.chop(1);
            }
        }
        if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
        lines.removeAll(QStringLiteral("No newline at end of file"));
        return lines;
    };

    QVector<DiffLine> rows = rowsFromCompactDiff(compact);
    if (rows.isEmpty()) {
        if (oldText.size() > kMaxDiffSourceChars || newText.size() > kMaxDiffSourceChars) {
            rows.append({DiffKind::Context, 0, 0,
                         QStringLiteral("… (diff too large to render)")});
        } else {
            const QStringList oldLines = splitLines(oldText);
            const QStringList newLines = splitLines(newText);
            if (oldIsNull || oldText.isEmpty()) {
                const int limit = qMin(newLines.size(), kMaxOmpDiffLines);
                rows.reserve(limit);
                for (int k = 0; k < limit; ++k) {
                    rows.append({DiffKind::Add, 0, k + 1, newLines.at(k)});
                }
            } else if (oldLines.size() > kMaxLcsLines || newLines.size() > kMaxLcsLines) {
                rows = prefixSuffixDiff(oldLines, newLines);
            } else {
                rows = lcsLineDiff(oldLines, newLines);
            }
        }
    }

    bool onlyAdds = !rows.isEmpty();
    for (const auto &row : rows) {
        if (row.kind != DiffKind::Add) {
            onlyAdds = false;
            break;
        }
    }
    if (!onlyAdds) {
        rows = collapseContext(rows);
    }

    int maxNum = 1;
    for (const auto &row : rows) {
        maxNum = qMax(maxNum, qMax(row.oldLine, row.newLine));
    }
    const int numWidth = qMax(1, QString::number(maxNum).size());

    QString html;
    html.reserve(path.size() + rows.size() * 96 + 256);
    html += QStringLiteral(
                "<div style=\"background: %1; color: %2; padding: 4px 6px; "
                "border: 1px solid %3; border-radius: 4px; "
                "%5 margin-bottom: 4px;\">%4</div>")
                .arg(pal.headerBg, pal.text, pal.border, path.toHtmlEscaped(),
                     pal.fontCss.isEmpty()
                         ? QStringLiteral("font-family: Consolas, monospace;")
                         : pal.fontCss);
    html += QStringLiteral("<div style=\"border: 1px solid %1; border-radius: 4px;\">")
                .arg(pal.border);
    for (const auto &row : rows) {
        if (row.kind == DiffKind::Context && row.text.isEmpty()) {
            continue;
        }
        html += renderDiffRow(row, pal, numWidth);
    }
    html += QStringLiteral("</div>");
    return html;
}

// Strip the markdown code fence and the `<tool_use_error>` sentinel that the
// Claude Code agent wraps tool errors in. The user wants to see the human
// message, not the formatting envelope.
QString sanitizeToolText(const QString &raw)
{
    QString s = raw;
    static const QRegularExpression kFenceStart(
        QStringLiteral("\\A```[A-Za-z0-9_+\\-]*\\n"));
    static const QRegularExpression kFenceEnd(
        QStringLiteral("\\n?```\\s*\\z"));
    static const QRegularExpression kToolErrTag(
        QStringLiteral("</?tool_use_error>"));
    s.remove(kFenceStart);
    s.remove(kFenceEnd);
    s.remove(kToolErrTag);
    return s.trimmed();
}

// Returns true if `text` is a JSON representation of empty content, e.g.
// `{"content": []}` or just `[]`. Agents sometimes echo back the raw output
// structure when there is nothing meaningful to report.
bool isEmptyContentJson(const QString &text)
{
    const QByteArray utf8 = text.trimmed().toUtf8();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(utf8, &err);
    if (err.error != QJsonParseError::NoError)
        return false;
    if (doc.isArray())
        return doc.array().isEmpty();
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        const QJsonValue cv = obj.value(QStringLiteral("content"));
        if (cv.isArray() && cv.toArray().isEmpty())
            return true;
    }
    return false;
}

// omp acp often ships the hashline edit payload as a sibling type:text
// (`[file#tag]\\n1:old\\n2:new`) next to type:diff. The colored hunk is enough.
bool looksLikeOmpHashlinePayload(const QString &text)
{
    int i = 0;
    const int n = text.size();
    while (i < n && text.at(i).isSpace())
        ++i;
    if (i >= n || text.at(i) != QLatin1Char('['))
        return false;
    ++i;
    bool sawHash = false;
    while (i < n) {
        const QChar c = text.at(i);
        if (c == QLatin1Char(']'))
            return sawHash;
        if (c == QLatin1Char('#'))
            sawHash = true;
        else if (c == QLatin1Char('\n') || c == QLatin1Char('\r'))
            return false;
        ++i;
    }
    return false;
}


// Muted section label inside an expanded Agent card ("Instructions", "Result").
QLabel *makeSectionCaption(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setStyleSheet(QStringLiteral("QLabel { color: palette(placeholder-text); }"));
    label->setContentsMargins(0, kSectionGapPx, 0, 0);
    return label;
}

// Every status glyph the header can show gets the same slot, so titles (and the
// rows a subagent card lines up under them) keep one column whatever the state.
int statusSlotWidth(const QFont &font)
{
    static QHash<QString, int> cache;
    const QString key = font.key();
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return *it;
    const QFontMetrics fm(font);
    int w = 0;
    for (const char16_t *glyph : {u"✓", u"✗", u"⚙", u"⏳", u"📡"})
        w = qMax(w, fm.horizontalAdvance(QString::fromUtf16(glyph)));
    return *cache.insert(key, w);
}

// Indent a markdown body's text without a wrapper widget: QTextEdit has no
// public viewport inset, and the root frame's format is rebuilt by every
// setHtml, so owners re-apply this after each render (MarkdownView::rendered).
void setDocumentIndent(QTextDocument *doc, int left)
{
    QTextFrameFormat ff = doc->rootFrame()->frameFormat();
    if (qFuzzyCompare(ff.leftMargin() + 1.0, left + 1.0)) return;
    ff.setLeftMargin(left);
    doc->rootFrame()->setFrameFormat(ff);
}

// Content-sized markdown, as in the chat bubbles: no scrollbars, no frame, no
// document margin, transparent over the card.
void configureMarkdownBody(MarkdownView *view)
{
    view->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; }"));
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setFrameShape(QFrame::NoFrame);
    view->document()->setDocumentMargin(0);
}

QString firstLine(QString text)
{
    text = text.trimmed();
    const int nl = text.indexOf(QLatin1Char('\n'));
    if (nl >= 0) {
        text.truncate(nl);
    }
    return text;
}

// First `maxLines` lines of `text`. Trailing newlines do not count as extra
// lines, so a one-line command that happens to end with `\n` stays one line.
QString firstLines(QString text, int maxLines)
{
    text.replace(QLatin1String("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    while (text.endsWith(QLatin1Char('\n'))) {
        text.chop(1);
    }
    if (maxLines <= 0 || text.isEmpty()) {
        return text;
    }
    int seen = 0;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\n')) {
            ++seen;
            if (seen == maxLines) {
                return text.left(i);
            }
        }
    }
    return text;
}

QString truncateForUi(QString text)
{
    if (text.size() <= kMaxRawOutputChars) {
        return text;
    }
    text.truncate(kMaxRawOutputChars);
    text += QStringLiteral("\n\n[output truncated]");
    return text;
}

QString commandFromRaw(const QJsonObject &raw)
{
    const QJsonArray parsed = raw.value(QStringLiteral("parsed_cmd")).toArray();
    if (!parsed.isEmpty()) {
        const QString cmd = parsed.first().toObject().value(QStringLiteral("cmd")).toString();
        if (!cmd.isEmpty()) {
            return cmd;
        }
    }

    const QJsonValue command = raw.value(QStringLiteral("command"));
    if (command.isString()) {
        return command.toString();
    }
    if (command.isArray()) {
        const QJsonArray arr = command.toArray();
        for (int i = 0; i + 1 < arr.size(); ++i) {
            if (arr.at(i).toString().compare(QStringLiteral("-Command"), Qt::CaseInsensitive) == 0
                || arr.at(i).toString().compare(QStringLiteral("/C"), Qt::CaseInsensitive) == 0) {
                const QString script = arr.at(i + 1).toString();
                if (!script.isEmpty()) {
                    return script;
                }
            }
        }

        QStringList parts;
        parts.reserve(arr.size());
        for (const auto &v : arr) {
            const QString s = v.toString();
            if (!s.isEmpty()) {
                parts.append(s);
            }
        }
        if (!parts.isEmpty()) {
            return parts.join(QLatin1Char(' '));
        }
    }

    return {};
}

QString jsonValueText(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isArray() || value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.isArray()
                                   ? QJsonDocument(value.toArray())
                                   : QJsonDocument(value.toObject()))
                                     .toJson(QJsonDocument::Indented));
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    return {};
}

QString rawOutputText(const QJsonObject &raw)
{
    QString text;
    const QString formatted = raw.value(QStringLiteral("formatted_output")).toString();
    const QString aggregated = raw.value(QStringLiteral("aggregated_output")).toString();
    const QString stdoutText = raw.value(QStringLiteral("stdout")).toString();
    const QString stderrText = raw.value(QStringLiteral("stderr")).toString();
    const QString outputText = jsonValueText(raw.value(QStringLiteral("output")));
    const QString contentText = jsonValueText(raw.value(QStringLiteral("content")));
    const QString resultText = jsonValueText(raw.value(QStringLiteral("result")));
    const QString resultsText = jsonValueText(raw.value(QStringLiteral("results")));

    if (!formatted.isEmpty()) {
        text = formatted;
    } else if (!aggregated.isEmpty()) {
        text = aggregated;
    } else if (!outputText.isEmpty()) {
        text = outputText;
    } else if (!contentText.isEmpty()) {
        text = contentText;
    } else if (!resultText.isEmpty()) {
        text = resultText;
    } else if (!resultsText.isEmpty()) {
        text = resultsText;
    } else {
        if (!stdoutText.isEmpty()) {
            text += stdoutText;
            if (!text.endsWith(QLatin1Char('\n'))) {
                text += QLatin1Char('\n');
            }
        }
        if (!stderrText.isEmpty()) {
            if (!text.isEmpty()) {
                text += QStringLiteral("\nstderr:\n");
            }
            text += stderrText;
        }
    }

    const QJsonValue exitCode = raw.value(QStringLiteral("exit_code"));
    if (exitCode.isDouble()) {
        if (!text.isEmpty() && !text.endsWith(QLatin1Char('\n'))) {
            text += QLatin1Char('\n');
        }
        text += QStringLiteral("\nexit code: %1").arg(exitCode.toInt());
    }

    if (text.isEmpty()) {
        text = QString::fromUtf8(QJsonDocument(raw).toJson(QJsonDocument::Indented));
    }
    return truncateForUi(text.trimmed());
}

QString webInputText(const QJsonObject &raw)
{
    const QJsonObject action = raw.value(QStringLiteral("action")).toObject();
    const QString actionType = action.value(QStringLiteral("type")).toString();
    QString query = raw.value(QStringLiteral("query")).toString();
    if (query.isEmpty()) {
        query = action.value(QStringLiteral("query")).toString();
    }
    QString url = raw.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        url = action.value(QStringLiteral("url")).toString();
    }

    if (query.isEmpty() && url.isEmpty()) {
        return {};
    }

    QString text;
    if (!actionType.isEmpty()) {
        text += QStringLiteral("Action: %1\n").arg(actionType);
    }
    if (!query.isEmpty()) {
        text += QStringLiteral("Query: %1\n").arg(query);
    }
    if (!url.isEmpty()) {
        text += QStringLiteral("URL: %1\n").arg(url);
    }
    return text.trimmed();
}

// QTextDocument lays out lazily: after setHtml()/setPlainText() the document
// reports a stale (often single-line) size until something forces the layout
// engine to run for the current text width. Measuring height before that pass
// clips multi-line diff bodies to ~one line. Touching the layout's
// documentSize() after pinning the text width forces the full pass, so the
// subsequent doc->size() read is authoritative.
qreal layoutDocumentHeight(QTextDocument *doc, int textWidth)
{
    if (!doc) return 0.0;
    doc->setTextWidth(textWidth);
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    if (!layout) return doc->size().height();
    // documentSize() drives the layout to completion for the pinned width.
    qreal h = layout->documentSize().height();
    // Fall back to the block-walked extent if the layout still under-reports
    // (can happen on the very first paint before the widget has a real width).
    const QTextBlock last = doc->lastBlock();
    if (last.isValid()) {
        const QRectF r = layout->blockBoundingRect(last);
        if (r.isValid())
            h = std::max(h, r.bottom());
    }
    return std::max(h, doc->size().height());
}

} // namespace

AcpToolCallCard::AcpToolCallCard(const AcpProtocol::AcpToolCall &initial, QWidget *parent)
    : QFrame(parent)
    , m_id(initial.id)
    , m_title(initial.title)
    , m_name(initial.name)
    , m_kind(initial.kind)
    , m_status(initial.status)
    , m_backgroundRunning(initial.backgroundRunning)
    , m_groupId(initial.groupId)
    , m_durationMs(initial.durationMs)
    , m_content(initial.content)
    , m_rawInput(initial.rawInput)
    , m_rawOutput(initial.rawOutput)
{
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral(
        "AcpToolCallCard { background: palette(base); border-radius: 4px; }"
        "AcpToolCallCard[goalAgent=\"true\"] { background: rgba(212, 175, 55, 48); "
        "border: 1px solid rgba(212, 175, 55, 80); border-radius: 4px; }"
        // King gold (#FFD700): the advisor consult stands out from ordinary tools.
        "AcpToolCallCard[advisor=\"true\"] { background: rgba(255, 215, 0, 40); "
        "border: 1px solid rgba(255, 215, 0, 160); border-radius: 4px; }"));
    if (AcpProtocol::isAdvisorToolName(initial.name)) {
        setProperty("advisor", true);
    }
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 4, 6, 4);
    outer->setSpacing(2);
    m_outer = outer;

    auto *header = new QHBoxLayout();
    m_headerLayout = header;
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);

    m_statusIcon = new QLabel(this);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_expandBtn = new QToolButton(this);
    m_expandBtn->setText(QStringLiteral("▸"));
    m_expandBtn->setCheckable(true);
    m_expandBtn->setChecked(false);
    m_expandBtn->setAutoRaise(true);

    header->addWidget(m_statusIcon);
    // A call that runs subagents is not an ordinary tool: a bot icon before its
    // title says so (refreshSubagentMark).
    m_agentIcon = new QLabel(this);
    m_agentIcon->setObjectName(QStringLiteral("acpSubagentIcon"));
    m_agentIcon->setToolTip(tr("Runs subagents"));
    m_agentIcon->hide();
    header->addWidget(m_agentIcon);
    m_statusIcon->setAlignment(Qt::AlignCenter);
    m_statusIcon->setFixedWidth(statusSlotWidth(m_statusIcon->font()));
    m_durationLabel = new QLabel(this);
    m_durationLabel->setEnabled(false); // dimmed, like secondary text
    m_durationLabel->hide();

    header->addWidget(m_titleLabel, 1);
    header->addWidget(m_durationLabel);
    header->addWidget(m_expandBtn);

    outer->addLayout(header);

    // The body (QTextBrowser: scroll area, viewport, two scrollbars, document)
    // and its render timer are built on first expand — ensureBody(). Most cards
    // stay collapsed, and an expanded "Worked for" turn instantiates hundreds.

    connect(m_expandBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_userToggled = true;
        setCollapsed(!checked);
    });

    refreshSubagentMark();
    refreshHeader();
    // Cards land collapsed by default — the title alone is enough to scan a
    // turn. Body rendering is intentionally lazy: tool output can be huge, and
    // collapsed QTextDocument layout must not sit in the update hot path.
    setCollapsed(true);
    // A card restored from history (or constructed already-finished) may arrive
    // at terminal status with diff content — auto-expand it here too, not just
    // on a later apply().
    maybeAutoExpandForDiff();
}

void AcpToolCallCard::ensureBody()
{
    if (m_body) return;
    if (isSubagentCard()) {
        // The subagent's report is markdown, like the chat it would have been.
        m_resultCaption = makeSectionCaption(tr("Result"), this);
        if (m_chatFontSet) m_resultCaption->setFont(m_chatFont);
        m_resultCaption->setContentsMargins(m_contentIndent, kSectionGapPx, 0, 0);
        m_resultCaption->hide();
        m_outer->addWidget(m_resultCaption);
        m_markdownBody = new MarkdownView(this);
        configureMarkdownBody(m_markdownBody);
        if (m_chatFontSet) m_markdownBody->setChatFont(m_chatFont, false);
        // A <details> click or a theme change re-renders inside the view.
        connect(m_markdownBody, &MarkdownView::rendered, this, [this]() {
            setDocumentIndent(m_markdownBody->document(), m_contentIndent);
            refitBodyHeight();
            scheduleRefit();
        });
        m_body = m_markdownBody;
    } else {
        m_body = new QTextBrowser(this);
        m_body->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; }"));
        m_body->setOpenExternalLinks(true);
        // Cards size to content. Internal scroll on a card produces a nested
        // scrollbar inside the transcript's own scroll area — bad UX. The expand
        // button collapses verbose output instead.
        m_body->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_body->setFrameShape(QFrame::NoFrame);
        m_body->document()->setDocumentMargin(0);
        if (m_chatFontSet) m_body->document()->setDefaultFont(m_chatFont);
    }
    m_outer->addWidget(m_body);

    m_renderTimer = new QTimer(this);
    m_renderTimer->setSingleShot(true);
    m_renderTimer->setInterval(kBodyRenderDebounceMs);
    connect(m_renderTimer, &QTimer::timeout, this, &AcpToolCallCard::flushBodyRender);
}

void AcpToolCallCard::setGoalTint(bool on)
{
    if (m_fromGoalAgent == on)
        return;
    m_fromGoalAgent = on;
    style()->unpolish(this);
    style()->polish(this);
}

void AcpToolCallCard::setFromGoalAgent(bool goal)
{
    if (m_fromGoalAgent == goal)
        return;
    m_fromGoalAgent = goal;
    if (goal && m_outer) {
        auto *badge = new QLabel(tr("Goal"), this);
        badge->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 9px; font-weight: 600; letter-spacing: 0.04em; "
            "text-transform: uppercase; color: rgb(212, 175, 55); "
            "background: palette(window); border: 1px solid rgba(212, 175, 55, 80); "
            "border-radius: 3px; padding: 0px 5px; }"));
        badge->setFixedHeight(badge->fontMetrics().height() + 4);
        m_outer->insertWidget(0, badge);
    }
    style()->unpolish(this);
    style()->polish(this);
}

void AcpToolCallCard::apply(const AcpProtocol::AcpToolCallUpdate &update)
{
    bool headerChanged = false;
    bool bodyChanged = false;
    if (update.title.has_value()) {
        if (m_title != *update.title) {
            m_title = *update.title;
            headerChanged = true;
        }
    }
    if (update.name.has_value() && !update.name->isEmpty() && m_name != *update.name) {
        m_name = *update.name;
        headerChanged = true;
    }
    if (update.kind.has_value()) {
        if (m_kind != *update.kind) {
            m_kind = *update.kind;
            headerChanged = true;
        }
    }
    if (update.status.has_value()) {
        if (m_status != *update.status) {
            m_status = *update.status;
            headerChanged = true;
        }
    }
    if (update.durationMs.has_value() && m_durationMs != *update.durationMs) {
        m_durationMs = *update.durationMs;
        headerChanged = true;
    }
    if (update.backgroundRunning.has_value() && m_backgroundRunning != *update.backgroundRunning) {
        m_backgroundRunning = *update.backgroundRunning;
        headerChanged = true;
    }
    if (update.content.has_value()) {
        m_content = *update.content;
        bodyChanged = true;
    }
    if (update.rawInput.has_value()) {
        m_rawInput = *update.rawInput;
        headerChanged = true;
        bodyChanged = true;
    }
    if (update.rawOutput.has_value()) {
        m_rawOutput = *update.rawOutput;
        bodyChanged = true;
    }
    if (headerChanged) {
        refreshHeader();
        if (m_nowLabel)
            refreshSubagentRows(); // the now line goes once the subagent ends
    }
    if (update.rawInput.has_value()) {
        refreshSubagentMark();
        refreshPrompt();
    }
    if (bodyChanged) {
        m_bodyDirty = true;
    }
    if (m_bodyDirty) {
        if (!m_collapsed && isTerminalStatus()) {
            flushBodyRender();
        } else {
            scheduleBodyRender();
        }
    }
    maybeAutoExpandForDiff();
}

bool AcpToolCallCard::hasDiffContent() const
{
    for (const auto &v : m_content) {
        if (v.isObject()
            && v.toObject().value(QStringLiteral("type")).toString() == QLatin1String("diff")) {
            return true;
        }
    }
    return false;
}

bool AcpToolCallCard::isTerminalStatus() const
{
    return m_status == QLatin1String("completed")
           || m_status == QLatin1String("failed")
           || m_status == QLatin1String("cancelled")
           || m_status == QLatin1String("canceled");
}

void AcpToolCallCard::maybeAutoExpandForDiff()
{
    // Diff cards (Edit/Write/any diff content) carry their value in the body,
    // so auto-expand them — but only once the tool has FINISHED. Expanding
    // mid-stream would drag the expensive QTextDocument diff render into the
    // update hot path; gating on terminal status means setCollapsed(false)
    // fires exactly one render, which still rides the debounce path below.
    if (m_autoExpandedForDiff || m_userToggled) return;
    if (!isTerminalStatus() || !hasDiffContent()) return;
    m_autoExpandedForDiff = true;  // keep shouldPreserveExpanded() true so
                                   // onTurnEnded() won't re-collapse it.
    setCollapsed(false);
}

QString AcpToolCallCard::statusGlyph() const
{
    // Claude reports a backgrounded command as `completed` the moment it is
    // handed off; the work is still running, so show that instead of a check.
    if (m_backgroundRunning)
        return QStringLiteral("📡");
    const QString ui = AcpProtocol::toolCallStatusForUi(m_status);
    if (ui == QLatin1String("completed")) return QStringLiteral("✓");
    if (ui == QLatin1String("failed"))    return QStringLiteral("✗");
    if (ui == QLatin1String("running"))   return QStringLiteral("⚙");
    return QStringLiteral("⏳");
}

QString AcpToolCallCard::enrichedTitle(const AcpProtocol::AcpToolCall &tc)
{
    // grill_me's ask: name it by what it did, not mcp__grill_me__ask.
    if (GrillMeForm::isGrillMeToolCall(tc.name, tc.title, tc.rawInput)) {
        const QJsonObject askArgs = GrillMeForm::toolArguments(tc.rawInput, tc.rawOutput);
        const QString formTitle = askArgs.value(QStringLiteral("title")).toString().trimmed();
        if (!formTitle.isEmpty())
            return tr("Asked you: %1").arg(formTitle);
        QJsonValue questions = askArgs.value(QStringLiteral("questions"));
        if (questions.isString())
            questions = QJsonDocument::fromJson(questions.toString().toUtf8()).array();
        const qsizetype count = questions.toArray().size();
        if (count == 0)
            return tr("Asking you");
        return count == 1 ? tr("Asked you a question") : tr("Asked you %1 questions").arg(count);
    }

    // omp's task: one call starts every subagent it lists.
    if (tc.rawInput.value(QLatin1String("tasks")).isArray()
        && AcpProtocol::isSubagentToolInput(tc.rawInput)) {
        QString label = tr("Agent");
        const QString type = tc.rawInput.value(QLatin1String("agent")).toString();
        if (!type.isEmpty() && type != QLatin1String("task"))
            label += QLatin1Char(' ') + type;
        const int count = AcpProtocol::subagentToolCount(tc.rawInput);
        if (count > 1)
            label += QStringLiteral(" ×%1").arg(count);
        QString what = tc.title;
        if (what.isEmpty() || what.compare(QLatin1String("task"), Qt::CaseInsensitive) == 0)
            what = tc.rawInput.value(QLatin1String("i")).toString();
        if (what.isEmpty()) {
            what = tc.rawInput.value(QLatin1String("tasks")).toArray().first().toObject()
                       .value(QLatin1String("name")).toString();
        }
        what = firstLine(what);
        return what.isEmpty() ? label : QStringLiteral("%1: %2").arg(label, what);
    }

    if (tc.title == QLatin1String("TaskOutput")) {
        return AcpProtocol::toolCallStatusForUi(tc.status) == QLatin1String("running")
            ? tr("Waiting for background jobs...")
            : tr("Background jobs complete");
    }

    // Antigravity names calls "Run <tool>" / "File <path>" / the command line:
    // build the title from what the call did, like an untitled call.
    const bool agyPlaceholder = AcpProtocol::isAntigravityPlaceholderTitle(tc.name, tc.title, tc.kind);

    // Agents that report the wire tool name plus a refined title (claude-agent-acp
    // sends a placeholder like "Terminal"/"Find" first, then "echo hi", "Read a.txt")
    // render as `Name: title` rather than being rebuilt from raw params.
    if (!agyPlaceholder && !tc.name.isEmpty() && !tc.title.isEmpty()
        && !tc.title.contains(QLatin1String("codebase-retrieval"))
        && !tc.title.startsWith(QLatin1String("Tool: "))) {
        static const QStringList placeholders = {
            QStringLiteral("terminal"), QStringLiteral("find"), QStringLiteral("read file"),
            QStringLiteral("preparing file…"), QStringLiteral("preparing file..."),
            QStringLiteral("edit"), QStringLiteral("fetch"), QStringLiteral("task"),
            QStringLiteral("grep"), QStringLiteral("search"), QStringLiteral("write"),
            QStringLiteral("read"), QStringLiteral("think")
        };
        const QString lower = tc.title.toLower();
        if (lower != tc.name.toLower() && !placeholders.contains(lower)) {
            // "Read a.txt" -> "Read: a.txt": don't repeat the name the title already leads with.
            if (tc.title.startsWith(tc.name, Qt::CaseInsensitive)
                && tc.title.size() > tc.name.size() && tc.title.at(tc.name.size()).isSpace()) {
                return QStringLiteral("%1: %2").arg(tc.name, tc.title.mid(tc.name.size()).trimmed());
            }
            return QStringLiteral("%1: %2").arg(tc.name, tc.title);
        }
    }

    QString t = tc.title.isEmpty() ? tc.id : tc.title;
    // agy's invoke_subagent ("Run invoke_subagent"), input normalized to an
    // Agent call's: titled like one.
    if (agyPlaceholder && AcpProtocol::isSubagentToolInput(tc.rawInput))
        t = QStringLiteral("Agent");
    const QString tLower = t.toLower();
    const QJsonObject args = tc.rawInput.value(QStringLiteral("arguments")).toObject();

    if (t.contains(QLatin1String("codebase-retrieval"))) {
        QString req = tc.rawInput.value(QStringLiteral("information_request")).toString();
        if (req.isEmpty()) {
            req = args.value(QStringLiteral("information_request")).toString();
        }
        if (!req.isEmpty())
            return QStringLiteral("Context Engine: \"%1\"").arg(req);
        return QStringLiteral("Context Engine");
    }

    if (t.startsWith(QLatin1String("Tool: "))) {
        const QString server = tc.rawInput.value(QStringLiteral("server")).toString();
        const QString tool = tc.rawInput.value(QStringLiteral("tool")).toString();
        if (!server.isEmpty() && !tool.isEmpty()) {
            QString detail = args.value(QStringLiteral("information_request")).toString();
            if (detail.isEmpty()) {
                detail = args.value(QStringLiteral("file_path")).toString();
            }
            if (detail.isEmpty()) {
                detail = args.value(QStringLiteral("query")).toString();
            }
            if (!detail.isEmpty()) {
                return QStringLiteral("Tool %1/%2: %3").arg(server, tool, firstLine(detail));
            }
            return QStringLiteral("Tool %1/%2").arg(server, tool);
        }
    }

    if (tLower == QLatin1String("skill")) {
        const QString skill = tc.rawInput.value(QStringLiteral("skill")).toString();
        if (!skill.isEmpty())
            return QStringLiteral("Skill: %1").arg(skill);
        return QStringLiteral("Skill");
    }

    if (tLower == QLatin1String("agent") || tLower == QLatin1String("task")) {
        const QString type = tc.rawInput.value(QStringLiteral("subagent_type")).toString();
        const QString desc = tc.rawInput.value(QStringLiteral("description")).toString();
        if (!type.isEmpty() && !desc.isEmpty())
            return QStringLiteral("Agent %1: %2").arg(type, desc);
        if (!type.isEmpty())
            return QStringLiteral("Agent %1").arg(type);
        if (!desc.isEmpty())
            return QStringLiteral("Agent: %1").arg(desc);
        return t;
    }
    if (t.startsWith(QLatin1String("sub-agent-"))) {
        const int colonIdx = t.indexOf(QLatin1Char(':'));
        if (colonIdx >= 0) {
            const QString type = t.mid(10, colonIdx - 10);
            const QString desc = t.mid(colonIdx + 1).trimmed();
            return QStringLiteral("Agent %1: %2").arg(type, desc);
        }
        return t.mid(10);
    }

    const QString mutatedPath = AcpProtocol::toolCallMutatedPath(
        tc.rawInput, tc.content, tc.rawOutput);
    const bool writeLike =
        (tc.kind == QLatin1String("edit")
         && tc.rawInput.value(QStringLiteral("content")).isString()
         && !tc.rawInput.contains(QStringLiteral("input"))
         && !tc.rawInput.contains(QStringLiteral("edits")))
        || (agyPlaceholder && tc.name == QLatin1String("write_file"));

    static const QStringList readTitles = {
        QStringLiteral("read"), QStringLiteral("read file")
    };
    if (readTitles.contains(tLower)
        || tLower.startsWith(QLatin1String("read:"))
        || tLower.startsWith(QLatin1String("read "))
        || tc.kind == QLatin1String("read")) {
        if (!mutatedPath.isEmpty())
            return QStringLiteral("Read: %1").arg(mutatedPath);
        return t;
    }

    static const QStringList writeTitles = {
        QStringLiteral("write"), QStringLiteral("write file")
    };
    if (writeTitles.contains(tLower)
        || tLower.startsWith(QLatin1String("write:"))
        || tLower.startsWith(QLatin1String("write "))
        || writeLike) {
        if (!mutatedPath.isEmpty())
            return QStringLiteral("Write: %1").arg(mutatedPath);
        return t;
    }

    static const QStringList editTitles = {
        QStringLiteral("edit"), QStringLiteral("edit file")
    };
    if (editTitles.contains(tLower)
        || tLower.startsWith(QLatin1String("edit:"))
        || tLower.startsWith(QLatin1String("edit "))
        || tc.kind == QLatin1String("edit")) {
        if (!mutatedPath.isEmpty())
            return QStringLiteral("Edit: %1").arg(mutatedPath);
        return t;
    }


    static const QStringList bashTitles = {
        QStringLiteral("bash"), QStringLiteral("terminal")
    };
    const QString rawCommand = commandFromRaw(tc.rawInput);
    if (bashTitles.contains(tLower)
        || tc.kind == QLatin1String("execute")
        || (!rawCommand.isEmpty() && tc.rawInput.contains(QStringLiteral("cwd")))) {
        if (!rawCommand.isEmpty()) {
            return QStringLiteral("Command: %1").arg(firstLines(rawCommand.trimmed(), 2));
        }
        const QString desc = tc.rawInput.value(QStringLiteral("description")).toString();
        if (!desc.isEmpty())
            return QStringLiteral("Command: %1").arg(desc);
        return t;
    }

    static const QStringList grepTitles = {
        QStringLiteral("grep"), QStringLiteral("search")
    };
    if (grepTitles.contains(tLower)) {
        const QString pattern = tc.rawInput.value(QStringLiteral("pattern")).toString();
        if (!pattern.isEmpty())
            return QStringLiteral("Grep: %1").arg(pattern);
        return t;
    }

    static const QStringList globTitles = {
        QStringLiteral("glob"), QStringLiteral("find")
    };
    if (globTitles.contains(tLower)) {
        const QString pattern = tc.rawInput.value(QStringLiteral("pattern")).toString();
        if (!pattern.isEmpty())
            return QStringLiteral("Glob: %1").arg(pattern);
        return t;
    }

    static const QStringList webSearchTitles = {
        QStringLiteral("websearch"), QStringLiteral("web search"), QStringLiteral("searching the web")
    };
    if (webSearchTitles.contains(tLower)) {
        QString query = tc.rawInput.value(QStringLiteral("query")).toString();
        if (query.isEmpty()) {
            query = tc.rawInput.value(QStringLiteral("action")).toObject()
                        .value(QStringLiteral("query")).toString();
        }
        if (!query.isEmpty())
            return QStringLiteral("Search: \"%1\"").arg(query);
        if (tLower != QLatin1String("searching the web"))
            return t;
    }

    static const QStringList webFetchTitles = {
        QStringLiteral("webfetch"), QStringLiteral("web fetch"), QStringLiteral("searching the web")
    };
    if (webFetchTitles.contains(tLower)) {
        QString url = tc.rawInput.value(QStringLiteral("url")).toString();
        if (url.isEmpty()) {
            const QJsonObject action = tc.rawInput.value(QStringLiteral("action")).toObject();
            url = action.value(QStringLiteral("url")).toString();
            if (url.isEmpty()) {
                url = action.value(QStringLiteral("query")).toString();
            }
        }
        if (!url.isEmpty())
            return QStringLiteral("Fetch: %1").arg(url);
        return t;
    }

    return t;
}

QString AcpToolCallCard::computeEnrichedTitle() const
{
    // Implicitly shared members: these copies are reference-count bumps.
    AcpProtocol::AcpToolCall tc;
    tc.id = m_id;
    tc.title = m_title;
    tc.name = m_name;
    tc.kind = m_kind;
    tc.status = m_status;
    tc.content = m_content;
    tc.rawInput = m_rawInput;
    tc.rawOutput = m_rawOutput;
    return enrichedTitle(tc);
}

void AcpToolCallCard::refreshDuration()
{
    // Only finished, foreground calls: a backgrounded command is `completed`
    // at hand-off, so its measured time would be misleading.
    const bool show = m_durationMs >= 0 && isTerminalStatus() && !m_backgroundRunning;
    QString text;
    if (m_subagentToolCount > 0) {
        text = m_subagentToolCount == 1 ? tr("1 tool")
                                        : tr("%1 tools").arg(m_subagentToolCount);
    }
    if (show) {
        if (!text.isEmpty())
            text += QStringLiteral(" · ");
        text += AcpProtocol::formatToolDuration(m_durationMs);
    }
    m_durationLabel->setText(text);
    m_durationLabel->setVisible(!text.isEmpty());
}

void AcpToolCallCard::setSubagentActivity(int toolCount, const QString &nowLine)
{
    if (toolCount != m_subagentToolCount) {
        m_subagentToolCount = toolCount;
        refreshSubagentMark();
        refreshDuration();
    }
    if (nowLine != m_subagentNowLine) {
        m_subagentNowLine = nowLine;
        if (!m_nowLabel) {
            m_nowLabel = new QLabel(this);
            m_nowLabel->setFont(m_titleLabel->font());
            m_nowLabel->setStyleSheet(QStringLiteral("QLabel { color: palette(placeholder-text); }"));
            m_nowLabel->setContentsMargins(m_contentIndent, 0, 0, 0);
            m_nowLabel->setTextFormat(Qt::PlainText);
            // Elided to the card's width; its full text must not widen the card.
            m_nowLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            m_outer->insertWidget(headerRowIndex() + 1, m_nowLabel);
        }
        refreshNowLine();
    }
    refreshSubagentRows();
}

int AcpToolCallCard::headerRowIndex() const
{
    for (int i = 0; i < m_outer->count(); ++i) {
        if (m_outer->itemAt(i)->layout() == m_headerLayout)
            return i;
    }
    return 0;
}

void AcpToolCallCard::refreshNowLine()
{
    if (!m_nowLabel) return;
    // From the card's width: a label that was hidden has a stale one.
    int marginL = 0, marginT = 0, marginR = 0, marginB = 0;
    m_outer->getContentsMargins(&marginL, &marginT, &marginR, &marginB);
    const int w = contentsRect().width() - marginL - marginR - m_contentIndent;
    const QString line = QStringLiteral("↳ ") + firstLine(m_subagentNowLine);
    m_nowLabel->setText(w > 0 ? m_nowLabel->fontMetrics().elidedText(line, Qt::ElideRight, w)
                              : line);
}

// The "now" line (the running call, else the last one) while the subagent's
// calls are folded away; the nested cards only while the card is open.
void AcpToolCallCard::refreshSubagentRows()
{
    if (m_nowLabel)
        m_nowLabel->setHidden(!m_collapsed || m_subagentNowLine.isEmpty());
    if (m_promptBox)
        m_promptBox->setHidden(m_collapsed || m_promptView->markdownSource().isEmpty());
    if (m_childHost)
        m_childHost->setHidden(m_collapsed || m_childCards.isEmpty());
    if (m_resultCaption)
        m_resultCaption->setHidden(m_collapsed || !m_hasResultText);
    refitBodyHeight();
}

// Rows below the header: the now line, then the instructions, the nested
// calls, and the result.
int AcpToolCallCard::rowAfterHeader() const
{
    return headerRowIndex() + (m_nowLabel ? 2 : 1);
}

bool AcpToolCallCard::isSubagentCard() const
{
    return m_subagentToolCount > 0 || AcpProtocol::isSubagentToolInput(m_rawInput);
}

// Rows inside an Agent card are a list, not a stack of cards: tight vertical
// metrics and a chevron no taller than the text.
void AcpToolCallCard::setNested(bool nested)
{
    if (m_nested == nested) return;
    m_nested = nested;
    applyNestedMetrics();
}

void AcpToolCallCard::refreshSubagentMark()
{
    const bool on = isSubagentCard();
    if (on) {
        // Text-high, tinted with the palette (the SVG's currentColor renders
        // black). Rendered once per size, colour and DPR across all cards.
        const int px = qMax(12, m_titleLabel->fontMetrics().height() - 2);
        const qreal dpr = devicePixelRatioF();
        const QColor color = palette().color(QPalette::ButtonText);
        static QHash<QString, QPixmap> cache;
        const QString key = QStringLiteral("%1/%2/%3").arg(px).arg(dpr).arg(color.rgba());
        auto it = cache.constFind(key);
        if (it == cache.constEnd()) {
            QPixmap pm = QIcon(QStringLiteral(":/icons/bot.svg")).pixmap(QSize(px, px), dpr);
            if (!pm.isNull()) {
                QPainter p(&pm);
                p.setCompositionMode(QPainter::CompositionMode_SourceIn);
                p.fillRect(pm.rect(), color);
            }
            it = cache.insert(key, pm);
        }
        if (m_agentIcon->pixmap().cacheKey() != it->cacheKey())
            m_agentIcon->setPixmap(*it);
        m_agentIcon->setFixedWidth(px);
    }
    if (m_agentIcon->isHidden() == on)
        m_agentIcon->setHidden(!on);
    setProperty("subagent", on);
    applyContentIndent();
}

// A subagent card's rows below the header (the "now" line, the instructions,
// the nested calls, the result) start where its title text starts: one left
// edge instead of one per row kind.
void AcpToolCallCard::applyContentIndent()
{
    const int spacing = m_headerLayout->spacing();
    int indent = 0;
    if (!m_agentIcon->isHidden())
        indent = m_statusIcon->width() + spacing + m_agentIcon->width() + spacing;
    if (indent == m_contentIndent) return;
    m_contentIndent = indent;
    if (m_nowLabel) {
        m_nowLabel->setContentsMargins(indent, 0, 0, 0);
        refreshNowLine();
    }
    if (m_promptBox)
        m_promptBox->layout()->setContentsMargins(indent, 0, 0, 0);
    if (m_childHost)
        m_childHost->layout()->setContentsMargins(indent, 0, 0, 0);
    if (m_resultCaption)
        m_resultCaption->setContentsMargins(indent, kSectionGapPx, 0, 0);
    if (m_markdownBody)
        setDocumentIndent(m_markdownBody->document(), indent);
    scheduleRefit();
}

void AcpToolCallCard::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && m_agentIcon && !m_agentIcon->isHidden())
        refreshSubagentMark(); // re-tint for the new theme
}

void AcpToolCallCard::applyNestedMetrics()
{
    if (!m_nested) return;
    m_outer->setContentsMargins(0, 2, 2, 2); // the parent's host indents the row
    const int h = m_titleLabel->fontMetrics().height();
    m_expandBtn->setFixedSize(h + 4, h);
    if (QLayout *row = headerRow(m_outer))
        row->invalidate();
    refitBodyHeight();
}

// The instructions are rendered on expand only; a collapsed card holds none.
void AcpToolCallCard::refreshPrompt()
{
    if (m_collapsed) return;
    const QString prompt = AcpProtocol::subagentInstructions(m_rawInput);
    if (prompt.isEmpty() && !m_promptBox) return;
    if (!m_promptBox) {
        m_promptBox = new QWidget(this);
        m_promptBox->setObjectName(QStringLiteral("acpSubagentPrompt"));
        auto *layout = new QVBoxLayout(m_promptBox);
        layout->setContentsMargins(m_contentIndent, 0, 0, 0);
        layout->setSpacing(2);
        QLabel *caption = makeSectionCaption(tr("Instructions"), m_promptBox);
        if (m_chatFontSet) caption->setFont(m_chatFont);
        layout->addWidget(caption);
        m_promptView = new MarkdownView(m_promptBox);
        configureMarkdownBody(m_promptView);
        if (m_chatFontSet) m_promptView->setChatFont(m_chatFont, false);
        connect(m_promptView, &MarkdownView::rendered, this, [this]() { refitBodyHeight(); });
        layout->addWidget(m_promptView);
        m_promptMoreBtn = new QToolButton(m_promptBox);
        m_promptMoreBtn->setObjectName(QStringLiteral("acpSubagentPromptMore"));
        m_promptMoreBtn->setAutoRaise(true);
        m_promptMoreBtn->setCursor(Qt::PointingHandCursor);
        m_promptMoreBtn->setStyleSheet(QStringLiteral(
            "QToolButton { border: none; padding: 0; color: palette(link); }"));
        m_promptMoreBtn->setText(tr("Show more"));
        if (m_chatFontSet) m_promptMoreBtn->setFont(m_chatFont);
        m_promptMoreBtn->hide();
        connect(m_promptMoreBtn, &QToolButton::clicked, this, [this]() {
            m_promptExpanded = !m_promptExpanded;
            m_promptMoreBtn->setText(m_promptExpanded ? tr("Show less") : tr("Show more"));
            refitBodyHeight();
        });
        layout->addWidget(m_promptMoreBtn, 0, Qt::AlignLeft);
        m_outer->insertWidget(rowAfterHeader(), m_promptBox);
    }
    if (prompt != m_promptView->markdownSource())
        m_promptView->setMarkdownSource(prompt); // refits through rendered()
    refreshSubagentRows();
}

// Fits the instructions to `width`, clamped to kPromptPreviewLines unless the
// user asked for all of them; returns the box's height.
int AcpToolCallCard::fitPromptBox(int width)
{
    QTextDocument *doc = m_promptView->document();
    const int docH = qMax(0, static_cast<int>(std::ceil(layoutDocumentHeight(doc, width))));
    const int lineH = QFontMetrics(doc->defaultFont()).lineSpacing();
    const int clampH = lineH * kPromptPreviewLines;
    const bool overflow = docH > clampH + lineH;
    m_promptMoreBtn->setHidden(!overflow);
    m_promptView->setFixedHeight(overflow && !m_promptExpanded ? clampH : docH);
    m_promptBox->layout()->invalidate();
    return m_promptBox->sizeHint().height();
}

void AcpToolCallCard::addChildCard(AcpToolCallCard *child)
{
    if (!child) return;
    if (!m_childHost) {
        m_childHost = new QFrame(this);
        m_childHost->setObjectName(QStringLiteral("acpSubagentChildren"));
        auto *layout = new QVBoxLayout(m_childHost);
        layout->setContentsMargins(m_contentIndent, 0, 0, 0);
        layout->setSpacing(0);
        // Below the instructions, above the result.
        m_outer->insertWidget(rowAfterHeader() + (m_promptBox ? 1 : 0), m_childHost);
        // A nested card pins its own height on expand/collapse; its new size
        // reaches this card only through the host's layout request.
        m_childHost->installEventFilter(this);
    }
    child->setParent(m_childHost);
    child->setNested(true);
    m_childHost->layout()->addWidget(child);
    m_childCards.insert(child->id(), child);
    // One coalesced refit per batch: measuring the host per card is O(n^2).
    m_childHost->setHidden(m_collapsed);
    scheduleRefit();
}

bool AcpToolCallCard::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_childHost && event->type() == QEvent::LayoutRequest)
        scheduleRefit();
    return QFrame::eventFilter(watched, event);
}

void AcpToolCallCard::refreshHeader()
{
    // What can change the header row's height besides the title: the status
    // glyph (emoji may come from a taller fallback font) and the duration.
    const QString glyph = statusGlyph();
    bool headerRowChanged = m_statusIcon->text() != glyph;
    const bool hadDuration = !m_durationLabel->isHidden();
    refreshDuration();
    headerRowChanged = headerRowChanged || hadDuration == m_durationLabel->isHidden();
    m_statusIcon->setText(glyph);
    m_statusIcon->setToolTip(m_backgroundRunning ? tr("Running in the background") : QString());
    const QString shown = firstLines(computeEnrichedTitle(), 2);

    const int availableWidth = m_titleLabel->width();
    const QFontMetrics fm(m_titleLabel->font());
    if (availableWidth > 0) {
        // Elide each line on its own. QFontMetrics::elidedText is single-line:
        // running it over a `\n`-joined string keeps the newline and the card
        // then clips the second line inside a one-line frame.
        const QStringList rawLines = shown.split(QLatin1Char('\n'));
        QStringList elided;
        elided.reserve(rawLines.size());
        for (const QString &line : rawLines) {
            elided.append(fm.elidedText(line, Qt::ElideRight, availableWidth));
        }
        m_titleLabel->setText(elided.join(QLatin1Char('\n')));
    } else {
        m_titleLabel->setText(shown);
    }

    // QLabel + TextSelectableByMouse under-reports sizeHint until a full
    // layout pass (the expand→collapse path). Pin the min height from font
    // metrics so a collapsed card autosizes to 1 or 2 lines on first paint.
    const int minTitleH = shown.contains(QLatin1Char('\n')) ? fm.size(0, shown).height() : 0;
    if (m_titleLabel->minimumHeight() != minTitleH) {
        m_titleLabel->setMinimumHeight(minTitleH);
        headerRowChanged = true;
    }
    // The header row caches its size hint, which refitBodyHeight reads: drop
    // it only when something in the row changed size (setChatFont drops it
    // for a new font).
    if (headerRowChanged) {
        if (QLayout *row = headerRow(m_outer))
            row->invalidate();
    }
    refitBodyHeight();
}

void AcpToolCallCard::scheduleBodyRender()
{
    if (!m_bodyDirty) return;
    if (m_collapsed) {
        if (m_renderTimer && m_renderTimer->isActive()) {
            m_renderTimer->stop();
        }
        return;
    }
    if (m_renderTimer) {
        if (!m_renderTimer->isActive()) {
            m_renderTimer->start();
        }
    } else {
        flushBodyRender();
    }
}

void AcpToolCallCard::flushBodyRender()
{
    if (!m_bodyDirty) return;
    if (m_renderTimer && m_renderTimer->isActive()) {
        m_renderTimer->stop();
    }
    rerenderBody();
}

void AcpToolCallCard::rerenderBody()
{
    if (!m_body) return;
    m_bodyDirty = false;

    if (hasDiffContent()) {
        // Resolve palette colors to hex strings — QTextDocument's CSS subset
        // does not honour palette(...) so leaving them symbolic would render
        // black ink on dark themes.
        const QPalette pal = palette();
        const DiffPalette dpal{
            pal.color(QPalette::Text).name(),
            pal.color(QPalette::PlaceholderText).name(),
            pal.color(QPalette::Mid).name(),
            pal.color(QPalette::AlternateBase).name(),
            m_chatFontSet ? fontCssDecl(m_chatFont) : QString(),
        };
        // HTML mode: mix text + image + diff blocks into one document.
        QString html;
        bool decodedSomething = false;
        // Font declaration for <pre> code blocks: chat font if set, else the
        // hardcoded Consolas fallback (keeps fixed-width when no pref present).
        const QString codeCss = dpal.fontCss.isEmpty()
            ? QStringLiteral("font-family: Consolas, monospace;")
            : dpal.fontCss;
        for (const auto &v : m_content) {
            if (!v.isObject()) continue;
            const QJsonObject obj = v.toObject();
            const QString type = obj.value(QStringLiteral("type")).toString();
            if (type == QLatin1String("text")) {
                const QString t = obj.value(QStringLiteral("text")).toString();
                if (looksLikeOmpHashlinePayload(t))
                    continue;
                if (!t.isEmpty() && !isEmptyContentJson(t)) {
                    html += QStringLiteral("<pre style=\"%1 "
                                           "color: %2; white-space: pre-wrap; "
                                           "margin: 0 0 4px 0;\">%3</pre>")
                                .arg(codeCss, dpal.text, t.toHtmlEscaped());
                    decodedSomething = true;
                }
            } else if (type == QLatin1String("image")) {
                html += QStringLiteral("<p style=\"color: %1; margin: 0 0 4px 0;\">[image]</p>")
                            .arg(dpal.dim);
                decodedSomething = true;
            } else if (type == QLatin1String("diff")) {
                html += renderDiffBlock(obj, dpal);
                decodedSomething = true;
            } else if (type == QLatin1String("terminal")) {
                // Zed/PI execute-tool PTY placeholder. Output is folded into a
                // text block by AcpSessionModel; don't JSON-dump the stub.
                decodedSomething = true;
            } else if (type == QLatin1String("content")) {
                // Wrapped content block — agent nests {type:text|image, ...}
                // under a `content` field. Surface the inner payload only,
                // stripping the tool_use_error sentinel / fence noise.
                const QJsonObject inner = obj.value(QStringLiteral("content")).toObject();
                const QString innerType = inner.value(QStringLiteral("type")).toString();
                if (innerType == QLatin1String("text")) {
                    const QString clean = sanitizeToolText(
                        inner.value(QStringLiteral("text")).toString());
                    if (looksLikeOmpHashlinePayload(clean))
                        continue;
                    if (!clean.isEmpty() && !isEmptyContentJson(clean)) {
                        html += QStringLiteral("<pre style=\"%1 "
                                               "color: %2; white-space: pre-wrap; "
                                               "margin: 0 0 4px 0;\">%3</pre>")
                                    .arg(codeCss, dpal.text, clean.toHtmlEscaped());
                        decodedSomething = true;
                    }
                } else if (innerType == QLatin1String("image")) {
                    html += QStringLiteral("<p style=\"color: %1; margin: 0 0 4px 0;\">[image]</p>")
                                .arg(dpal.dim);
                    decodedSomething = true;
                }
            }
        }
        if (!decodedSomething && !m_rawOutput.isEmpty()) {
            html = QStringLiteral("<pre style=\"%1 "
                                  "color: %2; white-space: pre-wrap; "
                                  "margin: 0;\">%3</pre>")
                       .arg(codeCss, dpal.text,
                            rawOutputText(m_rawOutput).toHtmlEscaped());
        } else if (!decodedSomething) {
            html = QStringLiteral("<pre style=\"color: %1;\">%2</pre>")
                       .arg(dpal.text,
                            QString::fromUtf8(QJsonDocument(m_content)
                                                  .toJson(QJsonDocument::Indented))
                                .toHtmlEscaped());
        }
        m_body->setHtml(html);
        refitBodyHeight();
        scheduleRefit();
        return;
    }

    // Plain-text mode (no diff in this card).
    QString text;
    bool decodedSomething = false;
    for (const auto &v : m_content) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        const QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            const QString t = obj.value(QStringLiteral("text")).toString();
            if (!t.isEmpty() && !isEmptyContentJson(t)) {
                text += t;
                text += QLatin1Char('\n');
                decodedSomething = true;
            }
        } else if (type == QLatin1String("image")) {
            text += QStringLiteral("[image]\n");
            decodedSomething = true;
        } else if (type == QLatin1String("terminal")) {
            decodedSomething = true;
        } else if (type == QLatin1String("content")) {
            const QJsonObject inner = obj.value(QStringLiteral("content")).toObject();
            const QString innerType = inner.value(QStringLiteral("type")).toString();
            if (innerType == QLatin1String("text")) {
                const QString clean = sanitizeToolText(inner.value(QStringLiteral("text")).toString());
                if (!clean.isEmpty() && !isEmptyContentJson(clean)) {
                    text += clean;
                    text += QLatin1Char('\n');
                    decodedSomething = true;
                }
            } else if (innerType == QLatin1String("image")) {
                text += QStringLiteral("[image]\n");
                decodedSomething = true;
            }
        }
    }
    if (!decodedSomething && !m_rawOutput.isEmpty()) {
        text = rawOutputText(m_rawOutput);
        decodedSomething = true;
    }
    if (!decodedSomething) {
        text = webInputText(m_rawInput);
        decodedSomething = !text.isEmpty();
    }
    if (!decodedSomething && !m_content.isEmpty()) {
        // Fall back to pretty JSON.
        const QJsonDocument doc(m_content);
        text = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }
    if (m_markdownBody) {
        const bool hadResult = m_hasResultText;
        m_hasResultText = !text.trimmed().isEmpty();
        if (m_resultCaption && hadResult != m_hasResultText)
            m_resultCaption->setHidden(m_collapsed || !m_hasResultText);
        m_markdownBody->setMarkdownSource(text); // refits through rendered()
        return;
    }
    m_body->document()->setPlainText(text);
    refitBodyHeight();
    scheduleRefit();
}

void AcpToolCallCard::refitBodyHeight()
{
    // Width from our own already-set geometry, not the child viewport — the
    // child has not been laid out yet inside our resizeEvent.
    int marginL = 0, marginT = 0, marginR = 0, marginB = 0;
    if (m_outer) {
        m_outer->getContentsMargins(&marginL, &marginT, &marginR, &marginB);
    }
    // Pin the card's own height. setFixedHeight on the inner browser only
    // clamps the browser; the card's QFrame sizeHint still cascades through
    // QAbstractScrollArea's font-derived default and inflates the card.
    // Rows up to the header layout: a Goal badge may sit above it.
    int headerH = 0;
    for (int i = 0; m_outer && i < m_outer->count(); ++i) {
        QLayoutItem *item = m_outer->itemAt(i);
        headerH += item->sizeHint().height() + (i > 0 ? m_outer->spacing() : 0);
        if (item->layout())
            break;
    }
    int cardH = marginT + marginB + headerH;
    const int rowGap = m_outer ? m_outer->spacing() : 0;
    // Subagent rows; isHidden() is the logical flag, set before any show. The
    // instructions, nested calls and result caption are hidden while collapsed.
    if (m_nowLabel && !m_nowLabel->isHidden())
        cardH += rowGap + m_nowLabel->sizeHint().height();
    if (m_collapsed || !m_body) {
        setFixedHeight(cardH);
        return;
    }

    // contentsRect: a Goal / advisor card has a stylesheet border.
    const int w = contentsRect().width() - marginL - marginR;
    if (w <= 0) {
        // Width not settled (never-shown stack page). Keep whatever height we
        // already have — pinning to header-only here clips an expanded body
        // that streamed on a hidden tab.
        return;
    }
    if (m_promptBox && !m_promptBox->isHidden())
        cardH += rowGap + fitPromptBox(w - m_contentIndent);
    if (m_childHost && !m_childHost->isHidden())
        cardH += rowGap + m_childHost->sizeHint().height();
    if (m_resultCaption && !m_resultCaption->isHidden())
        cardH += rowGap + m_resultCaption->sizeHint().height();
    QTextDocument *doc = m_body->document();
    // Force a full layout pass for the current width before measuring —
    // QTextDocument under-reports height for freshly-set multi-line HTML
    // (the diff body) until the layout engine has run, which clips an
    // expanded card down to roughly its first line.
    const int bodyH = qMax(0, static_cast<int>(std::ceil(layoutDocumentHeight(doc, w))));
    m_body->setFixedHeight(bodyH);

    // Gate on the LOGICAL expand state, not m_body->isVisible(): a freshly
    // inserted/auto-expanded card can still be unshown (parent not yet painted)
    // when the synchronous + deferred refit runs, so isVisible() reads false
    // even though the body WILL paint once the card is shown. That under-pins
    // the frame to header-only height while the body keeps its measured fixed
    // height — the body then overflows the too-short frame and overlaps the next
    // card in the transcript (the diff cards' overlap bug). m_collapsed is
    // authoritative and independent of show timing, so the height stays correct
    // whether or not the card is on screen at measure time.
    cardH += rowGap + bodyH;
    setFixedHeight(cardH);
}

// Coalesced one-shot refit. When content lands during construction/hydration
// or a streamed update, the card's final width isn't settled yet, so the
// synchronous refit measures against a stale width. Re-run once the event loop
// has applied the pending layout. Guarded so a burst of updates queues at most
// one deferred pass.
void AcpToolCallCard::scheduleRefit()
{
    if (m_refitScheduled) return;
    m_refitScheduled = true;
    QPointer<AcpToolCallCard> guard(this);
    QTimer::singleShot(0, this, [guard]() {
        if (!guard) return;
        guard->m_refitScheduled = false;
        if (!guard->m_collapsed && guard->m_bodyDirty) {
            guard->flushBodyRender();
        } else {
            guard->refitBodyHeight();
        }
    });
}

void AcpToolCallCard::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    refreshHeader();
    refreshNowLine();
    if (!m_collapsed && m_bodyDirty) {
        flushBodyRender();
    } else {
        refitBodyHeight();
    }
}

void AcpToolCallCard::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    // Session-tab switch shows this page without a size change, so resizeEvent
    // may not run. Re-measure now that we are actually visible and the stacked
    // layout has given us a real width.
    refreshHeader();
    if (!m_collapsed && m_bodyDirty) {
        flushBodyRender();
    } else {
        refitBodyHeight();
    }
    scheduleRefit();
}

void AcpToolCallCard::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    if (!collapsed) ensureBody();
    if (m_body) m_body->setVisible(!collapsed);
    if (collapsed && m_renderTimer && m_renderTimer->isActive()) {
        m_renderTimer->stop();
    }
    if (m_expandBtn) {
        m_expandBtn->blockSignals(true);
        m_expandBtn->setChecked(!collapsed);
        m_expandBtn->setText(collapsed ? QStringLiteral("▸") : QStringLiteral("▾"));
        m_expandBtn->blockSignals(false);
    }
    // Re-pin the card so it shrinks to header-only when collapsed (and
    // grows back when re-expanded). The body may have been laid out at a
    // stale width while hidden, so follow up with a deferred refit once the
    // expanded geometry settles — otherwise the diff clips on first expand.
    if (!collapsed) {
        refreshPrompt();
        flushBodyRender();
    }
    refreshSubagentRows(); // refits
    if (!collapsed) scheduleRefit();
    if (!collapsed && m_subagentToolCount > m_childCards.size())
        emit childCardsRequested();
}

void AcpToolCallCard::setChatFont(const QFont &font)
{
    m_chatFont = font;
    m_chatFontSet = true;

    // Styled widgets don't inherit a parent's setFont(), so push it down:
    // - title/status QLabels: direct setFont (they live in the stylesheet'd frame)
    // - body QTextBrowser: document defaultFont drives plain-text mode and the
    //   font-family-less fallback <pre>; HTML mode re-renders below threading
    //   the font's family/size into the inline CSS (see fontCssDecl).
    if (m_statusIcon) {
        m_statusIcon->setFont(font);
        m_statusIcon->setFixedWidth(statusSlotWidth(font));
    }
    if (m_titleLabel) m_titleLabel->setFont(font);
    if (m_durationLabel) m_durationLabel->setFont(font);
    if (m_nowLabel) m_nowLabel->setFont(font);
    if (m_resultCaption) m_resultCaption->setFont(font);
    if (m_markdownBody) {
        m_markdownBody->setChatFont(font, false); // re-rendered below
    } else if (m_body) {
        m_body->document()->setDefaultFont(font);
    }
    if (m_promptBox) {
        for (QWidget *w : m_promptBox->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
            if (w != m_promptView) w->setFont(font);
        }
        m_promptView->setChatFont(font); // re-renders and refits
    }
    for (AcpToolCallCard *child : std::as_const(m_childCards))
        child->setChatFont(font);
    refreshNowLine();
    applyNestedMetrics(); // the chevron follows the text height
    refreshSubagentMark(); // and so does the bot icon

    // New metrics change the header row's height: drop its cached hint.
    if (QLayout *row = headerRow(m_outer))
        row->invalidate();
    refreshHeader(); // re-elide title under the new font metrics
    m_bodyDirty = true;
    if (m_collapsed) {
        refitBodyHeight();
    } else {
        flushBodyRender();
    }
}
