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

#include <QEvent>
#include <QImage>
#include <QLabel>
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

namespace {

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
void normalizeBlockMargins(QTextDocument *doc)
{
    if (!doc) return;
    QTextBlockFormat zero;
    zero.setTopMargin(0);
    zero.setBottomMargin(0);
    QTextCursor cur(doc);
    cur.movePosition(QTextCursor::Start);
    do {
        QTextCursor blockCur = cur;
        blockCur.select(QTextCursor::BlockUnderCursor);
        blockCur.mergeBlockFormat(zero);
    } while (cur.movePosition(QTextCursor::NextBlock));
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
        ).arg(borderColor, headerBg));

        // QTextDocument renders markdown tables with default cellspacing,
        // producing visible double borders. Re-emit the HTML with
        // cellspacing="0" so border-collapse actually collapses.
        QTextDocument tmp;
        tmp.setMarkdown(m_text);
        QString html = tmp.toHtml();
        html.replace(QRegularExpression(QStringLiteral("<table([^>]*)>")),
                     QStringLiteral("<table\\1 cellspacing=\"0\" cellpadding=\"8\">"));
        m_browser->document()->setHtml(html);
        normalizeBlockMargins(m_browser->document());
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
        m_browser->document()->setMarkdown(text);
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
}

void AcpMessageWidget::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
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
