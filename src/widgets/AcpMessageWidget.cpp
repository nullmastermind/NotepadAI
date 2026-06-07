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

#include "AcpMessageWidget.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollBar>
#include <QStyle>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <cstdint>

namespace {

// SVG icons that declare stroke="currentColor" resolve to opaque black under
// Qt's svg icon engine, so they vanish on dark backgrounds. Re-render at the
// sizes Qt asks for and tint each pixmap via SourceIn so the alpha (strokes) is
// kept while the rgb becomes the palette color. Mirrors AcpSessionView's local
// tintedIcon (deliberately duplicated per-file, like makeTintedIcon elsewhere).
QIcon tintedCodeIcon(const QString &svgPath, const QColor &color)
{
    QIcon source(svgPath);
    if (source.isNull()) return source;
    QIcon dst;
    for (int sz : {16, 20, 22, 24}) {
        QPixmap pm = source.pixmap(sz, sz);
        if (pm.isNull()) continue;
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
        p.end();
        dst.addPixmap(pm);
    }
    return dst;
}

constexpr const char *kFrameStyleUser =
    "AcpMessageWidget[role=\"user\"] { background: rgba(128, 128, 128, 38); border-radius: 6px; margin-left: 12px; }";
constexpr const char *kFrameStyleUserGoal =
    "AcpMessageWidget[role=\"user\"][goalAgent=\"true\"] { background: rgba(180, 140, 50, 48); border: 1px solid rgba(180, 140, 50, 80); border-radius: 6px; margin-left: 12px; }";
constexpr const char *kFrameStyleAssistant =
    "AcpMessageWidget[role=\"assistant\"] { background: palette(base); border-radius: 6px; }";
constexpr const char *kFrameStyleThought =
    "AcpMessageWidget[role=\"thought\"] { background: palette(base); border-radius: 6px; }";
constexpr const char *kFrameStyleSystem =
    "AcpMessageWidget[role=\"system\"] { background: rgba(180, 140, 50, 32); border: 1px solid rgba(180, 140, 50, 60); border-radius: 6px; }";

// Inline message bubbles size to content — they must never show a scrollbar
// (it would reserve viewport width and create a feedback loop where the height
// fitter keeps reading a width that's smaller than what the parent actually
// gives us).
void configureBubbleBrowser(QTextBrowser *b)
{
    b->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    b->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    b->setFrameShape(QFrame::NoFrame);
    b->document()->setDocumentMargin(0);
}

// QTextBlock carries an implicit top/bottom margin (paragraph leading) that
// `documentMargin(0)` does NOT zero. The result is that `doc->size().height()`
// over-reports the rendered text height by ~one line's worth of leading,
// inflating bubbles with empty space below the last visible line. Walk every
// block and merge a zero-margin block format so the document's size matches
// what the user actually sees.
//
// Exception: fenced code blocks carry a block background (our <pre> fill) and
// deliberately keep vertical margins for breathing room around the code
// surface — zeroing them would glue the block to adjacent prose. Leave any
// background-bearing block's margins untouched.
void normalizeBlockMargins(QTextDocument *doc)
{
    if (!doc) return;
    QTextBlockFormat zero;
    zero.setTopMargin(0);
    zero.setBottomMargin(0);
    QTextCursor cur(doc);
    cur.movePosition(QTextCursor::Start);
    do {
        if (cur.blockFormat().background().style() != Qt::NoBrush) {
            continue; // code surface — preserve its top/bottom margins
        }
        QTextCursor blockCur = cur;
        blockCur.select(QTextCursor::BlockUnderCursor);
        blockCur.mergeBlockFormat(zero);
    } while (cur.movePosition(QTextCursor::NextBlock));
}

// Agent output uses bare \n for visual line breaks, but CommonMark treats a
// single newline as a soft break (rendered as a space). Convert lone \n into
// hard breaks (two trailing spaces before the newline) so they render visually.
// Preserves code fences, blank-line paragraph separators, and lines that
// already end with trailing spaces or a backslash hard break.
QString ensureHardBreaks(const QString &md)
{
    const QStringList lines = md.split(QLatin1Char('\n'));
    QString out;
    out.reserve(md.size() + lines.size() * 2);
    bool inFence = false;

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];

        if (line.startsWith(QLatin1String("```")) || line.startsWith(QLatin1String("~~~"))) {
            inFence = !inFence;
        }

        out += line;

        if (i < lines.size() - 1) {
            const bool nextIsBlank = (i + 1 < lines.size()) && lines[i + 1].trimmed().isEmpty();
            const bool alreadyHard = line.endsWith(QLatin1String("  "))
                                     || line.endsWith(QLatin1Char('\\'));
            if (!inFence && !nextIsBlank && !line.trimmed().isEmpty() && !alreadyHard) {
                out += QLatin1String("  ");
            }
            out += QLatin1Char('\n');
        }
    }
    return out;
}

} // namespace

AcpMessageWidget::AcpMessageWidget(QString role, QWidget *parent)
    : QFrame(parent)
    , m_role(std::move(role))
{
    setProperty("role", m_role);
    setFrameShape(QFrame::NoFrame);
    setStyleSheet(QString::fromLatin1(kFrameStyleUser) +
                  QString::fromLatin1(kFrameStyleUserGoal) +
                  QString::fromLatin1(kFrameStyleAssistant) +
                  QString::fromLatin1(kFrameStyleThought) +
                  QString::fromLatin1(kFrameStyleSystem));

    m_layout = new QVBoxLayout(this);
    if (m_role == QLatin1String("thought")) {
        m_layout->setContentsMargins(4, 4, 4, 4);
    } else {
        m_layout->setContentsMargins(8, 6, 8, 6);
    }
    m_layout->setSpacing(2);

    if (m_role == QLatin1String("user")) {
        // User-role children are built lazily in setContent() so we can render
        // a sequence of text blocks + image thumbnails in their original order.
    } else if (m_role == QLatin1String("thought")) {
        m_thoughtHeader = new QToolButton(this);
        m_thoughtHeader->setText(tr("Thinking…"));
        m_thoughtHeader->setCheckable(true);
        m_thoughtHeader->setChecked(true); // start expanded while streaming
        m_thoughtHeader->setStyleSheet(QStringLiteral("QToolButton { border: none; padding: 0; margin: 0; font-style: italic; color: palette(placeholder-text); text-align: left; }"));
        m_thoughtHeader->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_thoughtHeader->setFixedHeight(m_thoughtHeader->fontMetrics().height());
        m_layout->addWidget(m_thoughtHeader);

        m_browser = new QTextBrowser(this);
        m_browser->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; font-style: italic; padding-left: 4px; }"));
        m_browser->setOpenExternalLinks(true);
        configureBubbleBrowser(m_browser);
        // QTextDocument paragraphs carry an implicit ~12px bottom margin even
        // with documentMargin=0; zero it out so the bubble doesn't sprout
        // phantom whitespace below the last line.
        m_browser->document()->setDefaultStyleSheet(
            QStringLiteral("p, body { margin: 0; padding: 0; }"));
        m_layout->addWidget(m_browser);

        connect(m_thoughtHeader, &QToolButton::toggled, this, [this](bool checked) {
            if (m_browser) m_browser->setVisible(checked);
            refitBrowserHeight();
        });
    } else {
        // assistant + any other roles
        m_browser = new QTextBrowser(this);
        m_browser->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; }"));
        m_browser->setOpenExternalLinks(true);
        configureBubbleBrowser(m_browser);
        m_layout->addWidget(m_browser);
        // Only assistant bubbles render fenced code worth copying. Create the
        // hover copy button + viewport event filter up front so the very first
        // mouse move is observed (the filter can't install itself from inside
        // its own callback).
        if (m_role == QLatin1String("assistant")) {
            ensureCopyButton();
        }
    }

    // Debounce assistant/thought re-renders so streamed chunks don't drown the
    // UI thread in markdown parsing — especially severe for table-heavy
    // replies where every chunk re-parses the whole payload.
    m_rerenderTimer = new QTimer(this);
    m_rerenderTimer->setSingleShot(true);
    m_rerenderTimer->setInterval(80);
    connect(m_rerenderTimer, &QTimer::timeout, this, &AcpMessageWidget::rerender);
}

void AcpMessageWidget::setFromGoalAgent(bool goal)
{
    if (m_fromGoalAgent == goal) return;
    m_fromGoalAgent = goal;
    setProperty("goalAgent", goal);
    if (goal && m_layout) {
        auto *badge = new QLabel(tr("Goal"), this);
        badge->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 9px; font-weight: 600; letter-spacing: 0.04em; "
            "text-transform: uppercase; color: rgb(180, 140, 50); "
            "background: palette(window); border: 1px solid rgba(180, 140, 50, 80); "
            "border-radius: 3px; padding: 0px 5px; }"));
        badge->setFixedHeight(badge->fontMetrics().height() + 4);
        m_layout->insertWidget(0, badge);
    }
    style()->unpolish(this);
    style()->polish(this);
}

void AcpMessageWidget::scheduleRerender()
{
    if (m_rerenderTimer) {
        if (!m_rerenderTimer->isActive()) {
            m_rerenderTimer->start();
        }
    } else {
        rerender();
    }
}

void AcpMessageWidget::flushRerender()
{
    if (m_rerenderTimer && m_rerenderTimer->isActive()) {
        m_rerenderTimer->stop();
    }
}

void AcpMessageWidget::appendChunk(const QString &chunk)
{
    m_text += chunk;
    scheduleRerender();
}

void AcpMessageWidget::setText(const QString &fullText)
{
    m_text = fullText;
    // Wholesale replacements are usually terminal states (compaction done,
    // model rewrote in place) — render immediately so the user sees the
    // settled state rather than waiting for the debounce.
    flushRerender();
    rerender();
}

void AcpMessageWidget::setContent(const QVector<AcpProtocol::AcpContentBlock> &content)
{
    if (m_role == QLatin1String("user")) {
        clearUserBlocks();
        QString plainJoined;
        QLabel *pendingTextLabel = nullptr;
        for (const auto &block : content) {
            if (block.kind == AcpProtocol::AcpContentBlock::Kind::Text) {
                if (!pendingTextLabel) {
                    pendingTextLabel = new QLabel(this);
                    pendingTextLabel->setWordWrap(true);
                    pendingTextLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
                    // Stamp the chat font now: this QLabel is created after the
                    // view's initial setChatFont() pass, and being inside a
                    // stylesheet'd frame it won't inherit the font otherwise.
                    if (m_chatFontSet) pendingTextLabel->setFont(m_chatFont);
                    m_layout->addWidget(pendingTextLabel);
                    m_userBlocks.append(pendingTextLabel);
                }
                const QString existing = pendingTextLabel->text();
                pendingTextLabel->setText(existing + block.text);
                plainJoined += block.text;
            } else {
                pendingTextLabel = nullptr; // images break a text run
                QPixmap pix;
                if (!block.imageData.isEmpty() && pix.loadFromData(block.imageData)) {
                    auto *imgLabel = new QLabel(this);
                    imgLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                    imgLabel->setTextInteractionFlags(Qt::NoTextInteraction);
                    m_layout->addWidget(imgLabel);
                    m_userBlocks.append(imgLabel);
                    m_userImages.append({imgLabel, pix});
                    plainJoined += QStringLiteral("[image]");
                } else {
                    // Decode failed — fall back to a muted placeholder so the
                    // block isn't silently dropped.
                    auto *fb = new QLabel(tr("[image]"), this);
                    fb->setStyleSheet(QStringLiteral("QLabel { color: palette(placeholder-text); font-style: italic; }"));
                    m_layout->addWidget(fb);
                    m_userBlocks.append(fb);
                    plainJoined += QStringLiteral("[image]");
                }
            }
        }
        m_text = plainJoined;
        rescaleUserImages();
        return;
    }

    QString joined;
    for (const auto &block : content) {
        if (block.kind == AcpProtocol::AcpContentBlock::Kind::Text) {
            joined += block.text;
        } else {
            joined += QStringLiteral("[image]");
        }
    }
    m_text = joined;
    rerender();
}

void AcpMessageWidget::clearUserBlocks()
{
    for (QWidget *w : m_userBlocks) {
        if (w) {
            m_layout->removeWidget(w);
            w->deleteLater();
        }
    }
    m_userBlocks.clear();
    m_userImages.clear();
}

void AcpMessageWidget::rescaleUserImages()
{
    if (m_userImages.isEmpty()) return;
    int marginL = 0, marginT = 0, marginR = 0, marginB = 0;
    if (m_layout) {
        m_layout->getContentsMargins(&marginL, &marginT, &marginR, &marginB);
    }
    const int avail = width() - marginL - marginR;
    if (avail <= 0) return;
    constexpr int kMaxThumbHeight = 240;
    for (const auto &ui : m_userImages) {
        if (!ui.label || ui.original.isNull()) continue;
        const QSize natural = ui.original.size();
        int targetW = qMin(natural.width(), avail);
        QPixmap scaled = ui.original.scaled(targetW, kMaxThumbHeight,
                                            Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui.label->setPixmap(scaled);
    }
}

void AcpMessageWidget::rerender()
{
    if (m_role == QLatin1String("user")) {
        // User content is rendered directly in setContent() — block-by-block.
        return;
    }
    if (!m_browser) return;

    if (m_role == QLatin1String("assistant")) {
        const QString borderColor = palette().color(QPalette::Mid).name();
        const QString headerBg = palette().color(QPalette::AlternateBase).name();
        m_browser->document()->setDefaultStyleSheet(QStringLiteral(
            "table { border-collapse: collapse; }"
            "th, td { border: 1px solid %1; padding: 8px; }"
            "th { background-color: %2; }"
        ).arg(borderColor, headerBg) + codeStyleSheet());

        // QTextDocument renders markdown tables with default cellspacing,
        // producing visible double borders. Re-emit the HTML with
        // cellspacing="0" so border-collapse actually collapses.
        QTextDocument tmp;
        // toHtml() bakes the document's default font (family + size) into the
        // serialized <body> as absolute styles, which then win over anything the
        // target document's defaultFont would supply. A default-constructed
        // QTextDocument uses QApplication::font() (~Segoe UI 9pt on Windows), so
        // without seeding it the assistant body renders in the app font instead
        // of the chat (Default Font) typeface. Seed with the chat font so the
        // round-trip carries it for prose; code keeps Qt's monospace family here
        // and is rewritten to the chat family in styleCodeInDocument() below.
        if (m_chatFontSet) tmp.setDefaultFont(m_chatFont);
        // MarkdownNoHTML: agent output is markdown, never trusted HTML. Qt's
        // default MarkdownDialectGitHub leaves md4c's HTML parsing on, so a bare
        // angle-bracket construct in model text (Surreal<Db>, vector<int>,
        // "a < b") is consumed as an inline HTML tag — it swallows everything
        // after it and only inline-code fragments survive, garbling the bubble.
        tmp.setMarkdown(ensureHardBreaks(m_text),
                        QTextDocument::MarkdownFeatures(
                            QTextDocument::MarkdownDialectGitHub
                            | QTextDocument::MarkdownNoHTML));
        QString html = tmp.toHtml();
        html.replace(QRegularExpression(QStringLiteral("<table([^>]*)>")),
                     QStringLiteral("<table\\1 cellspacing=\"0\" cellpadding=\"8\">"));
        m_browser->document()->setHtml(html);
        // Skin code on the document model (reliable, unlike QTextDocument's CSS
        // subset and unlike fragile HTML string surgery): detect code blocks /
        // inline code by their monospace fragments, then apply the block
        // background, rewrite the baked monospace family to the chat family, and
        // add vertical breathing. Must run BEFORE normalizeBlockMargins so the
        // code-region margins it sets survive (that pass skips background blocks).
        styleCodeInDocument();
        normalizeBlockMargins(m_browser->document());
        // Map fenced-code regions so the hover copy button can target them.
        scanCodeRegions();
    } else if (m_role == QLatin1String("thought")) {
        // Thoughts are model reasoning streams that contain markdown
        // (headings, lists, code spans). setPlainText leaves "##", "###",
        // "- " as raw characters; render through setMarkdown so the bubble
        // reads as formatted text. The italic stylesheet on the QTextBrowser
        // still cascades to all rendered blocks.
        QString text = m_text;
        while (!text.isEmpty() && (text.endsWith(QLatin1Char('\n'))
                                   || text.endsWith(QLatin1Char('\r'))
                                   || text.endsWith(QLatin1Char(' '))
                                   || text.endsWith(QLatin1Char('\t')))) {
            text.chop(1);
        }
        // MarkdownNoHTML: see the assistant branch above — a bare "<tag>" in
        // reasoning text (e.g. "one Surreal<Db> per repo") would otherwise be
        // eaten as inline HTML, blanking the rest of the thought.
        m_browser->document()->setMarkdown(ensureHardBreaks(text),
                                           QTextDocument::MarkdownFeatures(
                                               QTextDocument::MarkdownDialectGitHub
                                               | QTextDocument::MarkdownNoHTML));
        normalizeBlockMargins(m_browser->document());
    } else {
        // Streamed chunks often end with "\n", which QTextDocument turns into
        // an empty trailing block that adds a full line-height of phantom
        // whitespace below the visible text. Strip trailing whitespace so the
        // document size matches what the user actually reads.
        QString text = m_text;
        while (!text.isEmpty() && (text.endsWith(QLatin1Char('\n'))
                                   || text.endsWith(QLatin1Char('\r'))
                                   || text.endsWith(QLatin1Char(' '))
                                   || text.endsWith(QLatin1Char('\t')))) {
            text.chop(1);
        }
        m_browser->document()->setPlainText(text);
        normalizeBlockMargins(m_browser->document());
    }
    refitBrowserHeight();
}

void AcpMessageWidget::refitBrowserHeight()
{
    if (!m_browser) return;
    // Read available width from our own already-set geometry, not from the
    // child viewport: inside resizeEvent the child has not been laid out yet,
    // so m_browser->viewport()->width() is stale.
    int marginL = 0, marginT = 0, marginR = 0, marginB = 0;
    if (m_layout) {
        m_layout->getContentsMargins(&marginL, &marginT, &marginR, &marginB);
    }
    const int w = width() - marginL - marginR;
    if (w <= 0) {
        return;
    }
    QTextDocument *doc = m_browser->document();
    doc->setTextWidth(w);
    const int browserH = qMax(0, static_cast<int>(std::ceil(doc->size().height())));
    m_browser->setFixedHeight(browserH);

    // Pin the bubble's own height too. setFixedHeight on the inner browser
    // only clamps the browser — QFrame's sizeHint cascades through QBoxLayout
    // and QAbstractScrollArea's font-derived default still inflates the
    // bubble. Computing the bubble height here directly is authoritative.
    int bubbleH = marginT + marginB;
    if (m_thoughtHeader) {
        // Use font metrics for the header height; QToolButton::sizeHint()
        // adds style-derived button margins even with stylesheet padding:0,
        // which adds phantom vertical space inside the bubble.
        bubbleH += m_thoughtHeader->fontMetrics().height();
        if (m_browser->isVisible()) {
            bubbleH += m_layout->spacing() + browserH;
        }
    } else {
        bubbleH += browserH;
    }
    setFixedHeight(bubbleH);
}

void AcpMessageWidget::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    refitBrowserHeight();
    rescaleUserImages();
    // The hovered code region's geometry moved with the reflow; re-place (or
    // hide) the copy button against the new layout.
    if (m_copyCodeBtn && m_copyCodeBtn->isVisible() && m_browser) {
        updateCopyButtonForPos(m_browser->viewport()->mapFromGlobal(QCursor::pos()));
    }
}

void AcpMessageWidget::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        refitBrowserHeight();
    } else if (event->type() == QEvent::PaletteChange
               || event->type() == QEvent::ApplicationPaletteChange) {
        // Re-tint the copy glyph and re-skin code surfaces for the new theme.
        rebuildCopyIcon();
        if (m_role == QLatin1String("assistant") && !m_text.isEmpty()) {
            rerender();
        }
    }
}

QString AcpMessageWidget::codeStyleSheet() const
{
    // Inline <code> only. Its background is a CHAR-format fill: QTextHtmlParser
    // promotes a CSS background to the block format only for <hr> — for every
    // other element (including <pre>) the CSS background lands on the char
    // format, painting behind glyphs at text width. Inline code has no
    // block-level fill path, so this rule is the only thing that tints it.
    //
    // Deliberately NO `pre` rule here. A <pre> CSS background would also be a
    // char-level fill (text width, and absent on blank lines because they have
    // no fragment) — it would NOT set the block's background brush. The
    // full-width block fill, border-equivalent, and the blank-line handling all
    // live in styleCodeInDocument() on the document model, which is the single
    // source of truth for fenced-block chrome. Adding a `pre` background here
    // would layer a second, partial fill over the model fill and decouple the
    // visual from the brush that scanCodeRegions()/normalizeBlockMargins() read.
    //
    // No padding on `code` by toolkit constraint: QTextDocument applies CSS
    // `padding` only to table cells (qtextdocumentfragment: isTableCell guard) —
    // inline runs have no box model, so inline padding is silently dropped and
    // would be dead code. Inline code therefore keeps a tight tint; faking inset
    // with thin spaces would pollute selection/copy. Fenced-block breathing
    // (line height + region margins) is handled on the model in styleCodeInDocument().
    const QString fill = palette().color(QPalette::AlternateBase).name();
    return QStringLiteral("code { background-color: %1; }").arg(fill);
}

void AcpMessageWidget::styleCodeInDocument()
{
    if (!m_browser) return;
    QTextDocument *doc = m_browser->document();
    if (!doc) return;

    // Family to apply to code so it matches the chat text. setMarkdown tags
    // code with QFontDatabase's system fixed font; toHtml() bakes that literal
    // family inline (e.g. 'Consolas'/'Courier New') whenever it differs from the
    // seeded default, so without this rewrite code renders in the system fixed
    // font instead of the chat font. Done on the model (not regex on the HTML):
    // robust to Qt's serialization and idempotent — rerender() rebuilds from raw
    // markdown each time. Fall back to the document default family when no chat
    // font has been pushed yet.
    const QString codeFamily = m_chatFontSet ? m_chatFont.family()
                                             : doc->defaultFont().family();

    // Vertical breathing for code regions (issue: lines tightly packed, no gap
    // from prose). QTextDocument honours neither vertical padding nor block
    // margins from CSS, so apply them on the model: a proportional line height
    // loosens the packed lines, and top/bottom block margins separate the
    // region from adjacent paragraphs.
    constexpr int kCodeMarginPx = 8;       // gap above/below the whole region
    constexpr int kCodeLineHeightPct = 125; // intra-block line spacing
    const QBrush codeFill(palette().color(QPalette::AlternateBase));

    // Classification, verified empirically against THIS Qt build by running the
    // real setMarkdown -> toHtml -> setHtml pipeline and inspecting the result:
    //
    //  - nonBreakableLines() does NOT survive the round-trip. toHtml() emits each
    //    code line as `<pre style="...margin...">` with NO `whitespace` decl, so
    //    re-import never calls setNonBreakableLines — every block reads nbl=0.
    //    BlockCodeFence and fontFixedPitch are likewise dropped. Keying off any of
    //    them finds zero code (kills BOTH the fill/margins and the copy button).
    //
    //  - What DOES survive: each code fragment's font family is the literal string
    //    "monospace" (the exporter writes `<span style="font-family:'monospace'">`
    //    for inline AND fenced code; re-import keeps it). So detect code by a
    //    monospace family on the fragment. This covers inline `<code>` and every
    //    non-blank fenced line.
    //
    //  - A blank line inside a fence is an EMPTY block (no fragment, so no family
    //    signal). Promote an empty block to code when code flanks it on both
    //    sides, else the fill gaps and the copy region splits.
    auto fragIsCode = [](const QTextCharFormat &cf) {
        const QStringList fams = cf.fontFamilies().toStringList();
        for (const QString &f : fams) {
            if (f.contains(QLatin1String("mono"), Qt::CaseInsensitive))
                return true;
        }
        return cf.fontFixedPitch(); // belt-and-suspenders for other Qt paths
    };

    // First pass: per-block kind (Prose / Code / Empty) + collect code fragment
    // ranges for the family rewrite. A block is Code if it has a code fragment
    // and no prose; Empty if it has no fragments at all (blank fence line).
    enum class Kind : std::uint8_t { Prose, Code, Empty };
    QVector<Kind> kinds;
    QVector<int> positions;
    QVector<QPair<int, int>> codeFragRanges; // char ranges to re-family
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        bool hasCode = false, hasProse = false, hasAny = false;
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            if (!frag.isValid()) continue;
            hasAny = true;
            if (fragIsCode(frag.charFormat())) {
                hasCode = true;
                codeFragRanges.append({frag.position(), frag.position() + frag.length()});
            } else if (!frag.text().trimmed().isEmpty()) {
                hasProse = true;
            }
        }
        kinds.append(hasCode && !hasProse ? Kind::Code
                     : !hasAny           ? Kind::Empty
                                         : Kind::Prose);
        positions.append(b.position());
    }

    // Second pass: promote an Empty block enclosed by code (scanning over runs of
    // Empties) so an internal blank line keeps its fill and the region stays one
    // contiguous run with one copy button.
    auto codeOnSide = [&](int from, int step) {
        for (int i = from; i >= 0 && i < kinds.size(); i += step) {
            if (kinds[i] == Kind::Empty) continue;
            return kinds[i] == Kind::Code;
        }
        return false;
    };
    QVector<int> codeBlockPositions; // first position of each code block
    for (int i = 0; i < kinds.size(); ++i) {
        if (kinds[i] == Kind::Empty && codeOnSide(i - 1, -1) && codeOnSide(i + 1, 1))
            kinds[i] = Kind::Code;
        if (kinds[i] == Kind::Code)
            codeBlockPositions.append(positions[i]);
    }

    QTextCursor cur(doc);
    cur.beginEditBlock();

    // Block chrome: background + line height on each fenced code block; region
    // top/bottom margins on the first/last block of each contiguous run so the
    // region separates from adjacent prose without doubling the gap internally.
    auto isCodeBlockAt = [&](int pos) {
        return pos >= 0 && codeBlockPositions.contains(pos);
    };
    for (int pos : codeBlockPositions) {
        const QTextBlock b = doc->findBlock(pos);
        if (!b.isValid()) continue;
        const QTextBlock prev = b.previous();
        const QTextBlock next = b.next();
        const bool prevIsCode = prev.isValid() && isCodeBlockAt(prev.position());
        const bool nextIsCode = next.isValid() && isCodeBlockAt(next.position());

        QTextBlockFormat bf = b.blockFormat();
        bf.setBackground(codeFill);
        bf.setLineHeight(kCodeLineHeightPct, QTextBlockFormat::ProportionalHeight);
        bf.setTopMargin(prevIsCode ? 0 : kCodeMarginPx);
        bf.setBottomMargin(nextIsCode ? 0 : kCodeMarginPx);
        QTextCursor bc(b);
        bc.setBlockFormat(bf);
    }

    // Family rewrite for every code fragment (fenced + inline), so code follows
    // the chat font instead of the baked monospace family.
    QTextCharFormat repl;
    repl.setFontFamilies({codeFamily});
    repl.setFontFixedPitch(false); // family is authoritative now
    for (const auto &range : codeFragRanges) {
        QTextCursor fc(doc);
        fc.setPosition(range.first);
        fc.setPosition(range.second, QTextCursor::KeepAnchor);
        fc.mergeCharFormat(repl);
    }
    cur.endEditBlock();
}

void AcpMessageWidget::ensureCopyButton()
{
    if (m_copyCodeBtn || !m_browser) return;
    // Parent to the viewport so the button floats over the scrolled content and
    // is clipped to the visible body, not the frame chrome.
    m_copyCodeBtn = new QToolButton(m_browser->viewport());
    m_copyCodeBtn->setCursor(Qt::PointingHandCursor);
    m_copyCodeBtn->setFocusPolicy(Qt::NoFocus);
    m_copyCodeBtn->setToolTip(tr("Copy code"));
    m_copyCodeBtn->setAutoRaise(true);
    m_copyCodeBtn->setIconSize(QSize(16, 16));
    // Semi-transparent pill so the glyph reads over any code-surface tint,
    // following the media-overlay chrome convention.
    m_copyCodeBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: rgba(128,128,128,0.22); border: none; "
        "border-radius: 4px; padding: 2px; }"
        "QToolButton:hover { background: rgba(128,128,128,0.40); }"));
    m_copyCodeBtn->hide();
    rebuildCopyIcon();
    connect(m_copyCodeBtn, &QToolButton::clicked, this, [this]() {
        if (m_hoverCodeIndex < 0 || m_hoverCodeIndex >= m_codeRegions.size())
            return;
        QApplication::clipboard()->setText(m_codeRegions[m_hoverCodeIndex].text);
    });
    // Watch the viewport for hover/move/leave to drive button visibility.
    m_browser->viewport()->setMouseTracking(true);
    m_browser->viewport()->installEventFilter(this);
}

void AcpMessageWidget::rebuildCopyIcon()
{
    if (!m_copyCodeBtn) return;
    m_copyCodeBtn->setIcon(tintedCodeIcon(QStringLiteral(":/icons/copy.svg"),
                                          palette().color(QPalette::WindowText)));
}

void AcpMessageWidget::scanCodeRegions()
{
    m_codeRegions.clear();
    m_hoverCodeIndex = -1;
    if (m_copyCodeBtn) m_copyCodeBtn->hide();
    if (m_role != QLatin1String("assistant") || !m_browser) return;

    QTextDocument *doc = m_browser->document();
    // Fenced code blocks carry the AlternateBase background our pre rule sets;
    // prose blocks have no block background. Group consecutive code blocks (a
    // multi-line fence is several blocks) into one region. Table cells can also
    // carry a background, so exclude any block that lives inside a table.
    bool inRegion = false;
    CodeRegion cur{};
    QStringList curLines;
    auto flush = [&]() {
        if (!inRegion) return;
        cur.text = curLines.join(QLatin1Char('\n'));
        m_codeRegions.append(cur);
        inRegion = false;
        curLines.clear();
    };
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        QTextCursor probe(b);
        const bool inTable = probe.currentTable() != nullptr;
        const bool isCode = !inTable
                            && b.blockFormat().background().style() != Qt::NoBrush;
        if (isCode) {
            if (!inRegion) {
                inRegion = true;
                cur = CodeRegion{};
                cur.start = b.position();
                curLines.clear();
            }
            cur.end = b.position() + b.length() - 1;
            curLines.append(b.text());
        } else {
            flush();
        }
    }
    flush();
}

void AcpMessageWidget::updateCopyButtonForPos(const QPoint &viewportPos)
{
    if (!m_copyCodeBtn || !m_browser) return;
    if (m_codeRegions.isEmpty()) {
        m_hoverCodeIndex = -1;
        m_copyCodeBtn->hide();
        return;
    }
    QTextDocument *doc = m_browser->document();
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    if (!layout) return;
    // Map the viewport point into document coordinates (account for scroll).
    const QPoint docPos = viewportPos
        + QPoint(m_browser->horizontalScrollBar()->value(),
                 m_browser->verticalScrollBar()->value());
    const int hit = layout->hitTest(docPos, Qt::FuzzyHit);

    int idx = -1;
    for (int i = 0; i < m_codeRegions.size(); ++i) {
        if (hit >= m_codeRegions[i].start && hit <= m_codeRegions[i].end) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        m_hoverCodeIndex = -1;
        m_copyCodeBtn->hide();
        return;
    }

    m_hoverCodeIndex = idx;
    // Anchor to the top-right of the region's first block. Use the viewport's
    // right edge (not the block's text width) so a short code line still gets
    // the button in the corner where users expect it.
    QTextBlock first = doc->findBlock(m_codeRegions[idx].start);
    const QRectF blockRect = layout->blockBoundingRect(first);
    const int vpScrollY = m_browser->verticalScrollBar()->value();
    const int vpW = m_browser->viewport()->width();
    const int btnW = m_copyCodeBtn->sizeHint().width();
    const int btnH = m_copyCodeBtn->sizeHint().height();
    const int x = qMax(4, vpW - btnW - 6);
    const int y = static_cast<int>(blockRect.top()) - vpScrollY + 4;
    m_copyCodeBtn->resize(btnW, btnH);
    m_copyCodeBtn->move(x, y);
    m_copyCodeBtn->raise();
    m_copyCodeBtn->show();
}

bool AcpMessageWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (m_browser && watched == m_browser->viewport()) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            ensureCopyButton();
            updateCopyButtonForPos(me->position().toPoint());
            break;
        }
        case QEvent::Leave:
            // Don't hide while the pointer is over the button itself (the button
            // is a child of the viewport, so moving onto it fires Leave here).
            if (m_copyCodeBtn && !m_copyCodeBtn->underMouse()) {
                m_hoverCodeIndex = -1;
                m_copyCodeBtn->hide();
            }
            break;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void AcpMessageWidget::setChatFont(const QFont &font)
{
    m_chatFont = font;
    m_chatFontSet = true;

    // Styled widgets (this frame + the inner browser both carry a stylesheet)
    // do not inherit a parent's setFont(), so push the font down explicitly.
    // QTextDocument::setDefaultFont is the authoritative knob for QTextBrowser
    // body text — and the assistant role additionally re-bakes the font into
    // HTML on rerender (which reads m_chatFont).
    if (m_browser) {
        m_browser->document()->setDefaultFont(font);
    }
    if (m_thoughtHeader) {
        QFont hdr = font;
        hdr.setItalic(true); // matches the header stylesheet
        m_thoughtHeader->setFont(hdr);
        m_thoughtHeader->setFixedHeight(m_thoughtHeader->fontMetrics().height());
    }
    // User-role bodies are QLabels built lazily in setContent(); stamp the ones
    // that exist now (newly-created ones are stamped in setContent()).
    for (QWidget *w : m_userBlocks) {
        if (auto *lbl = qobject_cast<QLabel *>(w)) {
            lbl->setFont(font);
        }
    }

    // Assistant bodies bake the font into HTML, so re-serialize; other roles
    // already follow document()->defaultFont(). Skip an empty assistant bubble
    // (nothing baked yet — the next rerender picks up m_chatFont). All paths
    // refit height under the new metrics.
    if (m_role == QLatin1String("assistant") && !m_text.isEmpty()) {
        rerender();
    } else {
        refitBrowserHeight();
    }
}

void AcpMessageWidget::markStreamingDone()
{
    if (m_role != QLatin1String("thought")) return;
    if (m_thoughtHeader) {
        m_thoughtHeader->setChecked(false);
    }
    applyCollapsed(true);
}

void AcpMessageWidget::applyCollapsed(bool collapsed)
{
    m_collapsed = collapsed;
    if (m_browser) {
        m_browser->setVisible(!collapsed);
    }
    refitBrowserHeight();
}
