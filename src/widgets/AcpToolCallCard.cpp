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

#include <QAbstractTextDocumentLayout>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QStringList>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

// Diff-row palette. Greens/reds match the accent colors already in use by
// AcpUsageIndicator. Backgrounds are semi-transparent so the contrast holds in
// both light and dark Qt palettes without hard-coding a chrome surface.
constexpr const char *kDiffAddBg   = "rgba(76, 175, 80, 0.18)";
constexpr const char *kDiffDelBg   = "rgba(217, 83, 79, 0.18)";
constexpr const char *kDiffAddMark = "#4caf50";
constexpr const char *kDiffDelMark = "#d9534f";

// Cap LCS to bound the O(m*n) memory/time. Real edit payloads are small; this
// is just a guard so a pathological full-file paste doesn't lock the UI.
constexpr int kMaxLcsLines = 2000;
constexpr int kMaxRawOutputChars = 60000;

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
    QVector<QVector<int>> dp(m + 1, QVector<int>(n + 1, 0));
    for (int i = 1; i <= m; ++i) {
        for (int j = 1; j <= n; ++j) {
            if (oldLines[i - 1] == newLines[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else {
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
            }
        }
    }
    QVector<DiffLine> out;
    int i = m, j = n;
    while (i > 0 && j > 0) {
        if (oldLines[i - 1] == newLines[j - 1]) {
            out.prepend({DiffKind::Context, i, j, oldLines[i - 1]});
            --i;
            --j;
        } else if (dp[i - 1][j] > dp[i][j - 1]) {
            out.prepend({DiffKind::Remove, i, 0, oldLines[i - 1]});
            --i;
        } else {
            out.prepend({DiffKind::Add, 0, j, newLines[j - 1]});
            --j;
        }
    }
    while (i > 0) {
        out.prepend({DiffKind::Remove, i, 0, oldLines[i - 1]});
        --i;
    }
    while (j > 0) {
        out.prepend({DiffKind::Add, 0, j, newLines[j - 1]});
        --j;
    }
    return out;
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
    const bool oldIsNull = oldVal.isNull() || oldVal.isUndefined();
    const QString oldText = oldIsNull ? QString() : oldVal.toString();
    const QString newText = newVal.toString();

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
    const QStringList oldLines = splitLines(oldText);
    const QStringList newLines = splitLines(newText);

    QVector<DiffLine> rows;
    if (oldIsNull || oldText.isEmpty()) {
        rows.reserve(newLines.size());
        for (int k = 0; k < newLines.size(); ++k) {
            rows.append({DiffKind::Add, 0, k + 1, newLines[k]});
        }
    } else if (oldLines.size() > kMaxLcsLines || newLines.size() > kMaxLcsLines) {
        rows.reserve(oldLines.size() + newLines.size());
        for (int k = 0; k < oldLines.size(); ++k) {
            rows.append({DiffKind::Remove, k + 1, 0, oldLines[k]});
        }
        for (int k = 0; k < newLines.size(); ++k) {
            rows.append({DiffKind::Add, 0, k + 1, newLines[k]});
        }
    } else {
        rows = lcsLineDiff(oldLines, newLines);
    }

    const int numWidth = qMax(1,
        qMax(QString::number(oldLines.size()).size(),
             QString::number(newLines.size()).size()));

    QString html;
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

QString firstLine(QString text)
{
    text = text.trimmed();
    const int nl = text.indexOf(QLatin1Char('\n'));
    if (nl >= 0) {
        text.truncate(nl);
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
    , m_kind(initial.kind)
    , m_status(initial.status)
    , m_groupId(initial.groupId)
    , m_content(initial.content)
    , m_rawInput(initial.rawInput)
    , m_rawOutput(initial.rawOutput)
{
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QStringLiteral("AcpToolCallCard { background: palette(base); border-radius: 4px; }"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 4, 6, 4);
    outer->setSpacing(2);
    m_outer = outer;

    auto *header = new QHBoxLayout();
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
    header->addWidget(m_titleLabel, 1);
    header->addWidget(m_expandBtn);

    outer->addLayout(header);

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
    outer->addWidget(m_body);

    connect(m_expandBtn, &QToolButton::toggled, this, [this](bool checked) {
        m_userToggled = true;
        setCollapsed(!checked);
    });

    refreshHeader();
    rerenderBody();
    // Cards land collapsed by default — the title alone is enough to scan
    // a turn; click the chevron to read the body. Keeps long transcripts
    // skimmable without forcing the user to collapse each card by hand.
    // Diffs are an exception: their value is in the body, so auto-expand.
    setCollapsed(true);
    maybeAutoExpandForDiff();
}

void AcpToolCallCard::apply(const AcpProtocol::AcpToolCallUpdate &update)
{
    if (update.title.has_value()) {
        m_title = *update.title;
    }
    if (update.kind.has_value()) {
        m_kind = *update.kind;
    }
    if (update.status.has_value()) {
        m_status = *update.status;
    }
    if (update.content.has_value()) {
        m_content = *update.content;
    }
    if (update.rawInput.has_value()) {
        m_rawInput = *update.rawInput;
    }
    if (update.rawOutput.has_value()) {
        m_rawOutput = *update.rawOutput;
    }
    refreshHeader();
    rerenderBody();
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

void AcpToolCallCard::maybeAutoExpandForDiff()
{
    if (m_autoExpandedForDiff || m_userToggled) return;
    if (!hasDiffContent()) return;
    m_autoExpandedForDiff = true;
    setCollapsed(false);
}

QString AcpToolCallCard::statusGlyph() const
{
    if (m_status == QLatin1String("completed")) return QStringLiteral("✓");
    if (m_status == QLatin1String("failed"))    return QStringLiteral("✗");
    if (m_status == QLatin1String("running"))   return QStringLiteral("⚙");
    return QStringLiteral("⏳");
}

QString AcpToolCallCard::computeEnrichedTitle() const
{
    if (m_title == QLatin1String("TaskOutput")) {
        return m_status == QLatin1String("running")
            ? tr("Waiting for background jobs...")
            : tr("Background jobs complete");
    }

    QString t = m_title.isEmpty() ? m_id : m_title;
    const QString tLower = t.toLower();
    const QJsonObject args = m_rawInput.value(QStringLiteral("arguments")).toObject();

    if (t.contains(QLatin1String("codebase-retrieval"))) {
        QString req = m_rawInput.value(QStringLiteral("information_request")).toString();
        if (req.isEmpty()) {
            req = args.value(QStringLiteral("information_request")).toString();
        }
        if (!req.isEmpty())
            return QStringLiteral("Context Engine: \"%1\"").arg(req);
        return QStringLiteral("Context Engine");
    }

    if (t.startsWith(QLatin1String("Tool: "))) {
        const QString server = m_rawInput.value(QStringLiteral("server")).toString();
        const QString tool = m_rawInput.value(QStringLiteral("tool")).toString();
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
        const QString skill = m_rawInput.value(QStringLiteral("skill")).toString();
        if (!skill.isEmpty())
            return QStringLiteral("Skill: %1").arg(skill);
        return QStringLiteral("Skill");
    }

    if (tLower == QLatin1String("agent") || tLower == QLatin1String("task")) {
        const QString type = m_rawInput.value(QStringLiteral("subagent_type")).toString();
        const QString desc = m_rawInput.value(QStringLiteral("description")).toString();
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

    static const QStringList readTitles = {
        QStringLiteral("read"), QStringLiteral("read file")
    };
    if (readTitles.contains(tLower)) {
        QString path = m_rawInput.value(QStringLiteral("file_path")).toString();
        if (path.isEmpty())
            path = m_rawInput.value(QStringLiteral("path")).toString();
        if (!path.isEmpty())
            return QStringLiteral("Read: %1").arg(path);
        return t;
    }

    static const QStringList writeTitles = {
        QStringLiteral("write"), QStringLiteral("write file")
    };
    if (writeTitles.contains(tLower)) {
        const QString path = m_rawInput.value(QStringLiteral("file_path")).toString();
        if (!path.isEmpty())
            return QStringLiteral("Write: %1").arg(path);
        return t;
    }

    static const QStringList editTitles = {
        QStringLiteral("edit"), QStringLiteral("edit file")
    };
    if (editTitles.contains(tLower)) {
        const QString path = m_rawInput.value(QStringLiteral("file_path")).toString();
        if (!path.isEmpty())
            return QStringLiteral("Edit: %1").arg(path);
        return t;
    }

    static const QStringList bashTitles = {
        QStringLiteral("bash"), QStringLiteral("terminal")
    };
    const QString rawCommand = commandFromRaw(m_rawInput);
    if (bashTitles.contains(tLower)
        || m_kind == QLatin1String("execute")
        || (!rawCommand.isEmpty() && m_rawInput.contains(QStringLiteral("cwd")))) {
        QString cmd = rawCommand;
        if (!cmd.isEmpty()) {
            return QStringLiteral("Command: %1").arg(firstLine(cmd));
        }
        const QString desc = m_rawInput.value(QStringLiteral("description")).toString();
        if (!desc.isEmpty())
            return QStringLiteral("Command: %1").arg(desc);
        return t;
    }

    static const QStringList grepTitles = {
        QStringLiteral("grep"), QStringLiteral("search")
    };
    if (grepTitles.contains(tLower)) {
        const QString pattern = m_rawInput.value(QStringLiteral("pattern")).toString();
        if (!pattern.isEmpty())
            return QStringLiteral("Grep: %1").arg(pattern);
        return t;
    }

    static const QStringList globTitles = {
        QStringLiteral("glob"), QStringLiteral("find")
    };
    if (globTitles.contains(tLower)) {
        const QString pattern = m_rawInput.value(QStringLiteral("pattern")).toString();
        if (!pattern.isEmpty())
            return QStringLiteral("Glob: %1").arg(pattern);
        return t;
    }

    static const QStringList webSearchTitles = {
        QStringLiteral("websearch"), QStringLiteral("web search"), QStringLiteral("searching the web")
    };
    if (webSearchTitles.contains(tLower)) {
        QString query = m_rawInput.value(QStringLiteral("query")).toString();
        if (query.isEmpty()) {
            query = m_rawInput.value(QStringLiteral("action")).toObject()
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
        QString url = m_rawInput.value(QStringLiteral("url")).toString();
        if (url.isEmpty()) {
            const QJsonObject action = m_rawInput.value(QStringLiteral("action")).toObject();
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

void AcpToolCallCard::refreshHeader()
{
    m_statusIcon->setText(statusGlyph());
    const QString enriched = computeEnrichedTitle();
    const int availableWidth = m_titleLabel->width();
    if (availableWidth > 0) {
        const QFontMetrics fm(m_titleLabel->font());
        m_titleLabel->setText(fm.elidedText(enriched, Qt::ElideRight, availableWidth));
    } else {
        m_titleLabel->setText(enriched);
    }
}

void AcpToolCallCard::rerenderBody()
{
    if (!m_body) return;

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
            } else if (type == QLatin1String("content")) {
                // Wrapped content block — agent nests {type:text|image, ...}
                // under a `content` field. Surface the inner payload only,
                // stripping the tool_use_error sentinel / fence noise.
                const QJsonObject inner = obj.value(QStringLiteral("content")).toObject();
                const QString innerType = inner.value(QStringLiteral("type")).toString();
                if (innerType == QLatin1String("text")) {
                    const QString clean = sanitizeToolText(
                        inner.value(QStringLiteral("text")).toString());
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
    m_body->document()->setPlainText(text);
    refitBodyHeight();
    scheduleRefit();
}

void AcpToolCallCard::refitBodyHeight()
{
    if (!m_body) return;
    // Width from our own already-set geometry, not the child viewport — the
    // child has not been laid out yet inside our resizeEvent.
    int marginL = 0, marginT = 0, marginR = 0, marginB = 0;
    if (m_outer) {
        m_outer->getContentsMargins(&marginL, &marginT, &marginR, &marginB);
    }
    const int w = width() - marginL - marginR;
    if (w <= 0) return;
    QTextDocument *doc = m_body->document();
    // Force a full layout pass for the current width before measuring —
    // QTextDocument under-reports height for freshly-set multi-line HTML
    // (the diff body) until the layout engine has run, which clips an
    // expanded card down to roughly its first line.
    const int bodyH = qMax(0, static_cast<int>(std::ceil(layoutDocumentHeight(doc, w))));
    m_body->setFixedHeight(bodyH);

    // Pin the card's own height. setFixedHeight on the inner browser only
    // clamps the browser; the card's QFrame sizeHint still cascades through
    // QAbstractScrollArea's font-derived default and inflates the card.
    int headerH = 0;
    if (m_outer && m_outer->count() > 0) {
        if (auto *headerItem = m_outer->itemAt(0)) {
            headerH = headerItem->sizeHint().height();
        }
    }
    int cardH = marginT + marginB + headerH;
    // Gate on the LOGICAL expand state, not m_body->isVisible(): a freshly
    // inserted/auto-expanded card can still be unshown (parent not yet painted)
    // when the synchronous + deferred refit runs, so isVisible() reads false
    // even though the body WILL paint once the card is shown. That under-pins
    // the frame to header-only height while the body keeps its measured fixed
    // height — the body then overflows the too-short frame and overlaps the next
    // card in the transcript (the diff cards' overlap bug). m_collapsed is
    // authoritative and independent of show timing, so the height stays correct
    // whether or not the card is on screen at measure time.
    if (!m_collapsed) {
        cardH += (m_outer ? m_outer->spacing() : 0) + bodyH;
    }
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
        guard->refitBodyHeight();
    });
}

void AcpToolCallCard::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    refreshHeader();
    refitBodyHeight();
}

void AcpToolCallCard::setCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    if (m_body) m_body->setVisible(!collapsed);
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
    refitBodyHeight();
    if (!collapsed) scheduleRefit();
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
    if (m_statusIcon) m_statusIcon->setFont(font);
    if (m_titleLabel) m_titleLabel->setFont(font);
    if (m_body) m_body->document()->setDefaultFont(font);

    // Re-render so the HTML branch re-bakes the font, then refit under the new
    // metrics. rerenderBody() already calls refitBodyHeight()/scheduleRefit().
    refreshHeader(); // re-elide title under the new font metrics
    rerenderBody();
}
