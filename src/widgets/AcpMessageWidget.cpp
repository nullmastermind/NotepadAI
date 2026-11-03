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

#include "GoalAgent.h"
#include "MarkdownView.h"
#include "UsageStyle.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyle>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <cstdint>
#include <functional>
#include <utility>

namespace {

// King gold #D4AF37 — Goal identity (user inject, judge, Goal set, system).
constexpr const char *kFrameStyleUser =
    "AcpMessageWidget[role=\"user\"] { background: rgba(128, 128, 128, 38); border-radius: 14px; }";
constexpr const char *kFrameStyleUserGoal =
    "AcpMessageWidget[role=\"user\"][goalAgent=\"true\"] { background: rgba(212, 175, 55, 48); border: 1px solid rgba(212, 175, 55, 80); border-radius: 14px; }";
constexpr const char *kFrameStyleAssistant =
    "AcpMessageWidget[role=\"assistant\"] { background: palette(base); border-radius: 6px; }"
    "AcpMessageWidget[role=\"assistant\"][goalSet=\"true\"] { background: rgba(212, 175, 55, 48); border: 1px solid rgba(212, 175, 55, 80); border-radius: 6px; }"
    "AcpMessageWidget[role=\"assistant\"][goalAgent=\"true\"] { background: rgba(212, 175, 55, 48); border: 1px solid rgba(212, 175, 55, 80); border-radius: 6px; }";
constexpr const char *kFrameStyleThought =
    "AcpMessageWidget[role=\"thought\"] { background: palette(base); border-radius: 6px; }"
    "AcpMessageWidget[role=\"thought\"][goalAgent=\"true\"] { background: rgba(212, 175, 55, 48); border: 1px solid rgba(212, 175, 55, 80); border-radius: 6px; }";
// Extra left inset under "Thinking…". Not in the layout margins — those wrap
// the header too. Italic glyphs also overhang their advance by a few px, so
// wrap width must leave that slack inside the viewport or the last letters
// clip and QTextBrowser opens a horizontal scroll range.
constexpr int kThoughtBrowserPadLeft = 4;
constexpr int kUserBubblePadH = 14;
constexpr int kUserBubbleGap = 32; // space kept free to the left of a full-width user bubble
constexpr int kThoughtItalicRightSlack = 4;
// An expanded thought taller than this many lines stops growing and scrolls
// inside the bubble, so a long reasoning stream does not push the rest of the
// transcript away. Exception to ui-dna's "bubbles size to content" rule.
constexpr int kThoughtMaxLines = 12;
// Same "at the bottom" tolerance as the transcript (ui-dna).
constexpr int kThoughtFollowSlackPx = 8;
// Slim scrollbar for the capped thought body: no arrows, no track, a faint
// rounded handle that darkens under the mouse. Translucent grey reads on both
// light and dark themes. refitBrowserHeight wraps the text beside its width.
constexpr const char *kThoughtScrollBarStyle =
    "QScrollBar:vertical { background: transparent; width: 6px; margin: 0; border: none; }"
    "QScrollBar::handle:vertical { background: rgba(128, 128, 128, 80); border-radius: 3px; min-height: 24px; }"
    "QScrollBar::handle:vertical:hover, QScrollBar::handle:vertical:pressed { background: rgba(128, 128, 128, 160); }"
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; background: none; }"
    "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }";

// A word that cannot wrap (a path, a URL) must not set the bubble's minimum
// width. A bubble whose minimum is wider than the transcript gets laid out by
// QBoxLayout for its minimum width, not the width it has: the height comes out
// short and the last lines are clipped. The word is clipped instead.
void allowShrink(QWidget *w)
{
    w->setMinimumWidth(1);
}

// The transcript layout asks a bubble's layout (QWidgetItem::heightForWidth),
// not the bubble, for its height at the full transcript width; AlignRight then
// narrows the bubble to its hint. Answer for the width the bubble really gets,
// or lines that wrap only at that width are clipped.
class UserBubbleLayout : public QVBoxLayout
{
public:
    UserBubbleLayout(QWidget *parent, std::function<int()> width)
        : QVBoxLayout(parent)
        , m_width(std::move(width))
    {
    }

    int heightForWidth(int w) const override { return QVBoxLayout::heightForWidth(qMin(w, m_width())); }

    // Runs on every text, font or visibility change of a child.
    void invalidate() override
    {
        contentWidth = -1;
        QVBoxLayout::invalidate();
    }

    // AcpMessageWidget::measureUserContent() (text shaping), -1 = stale.
    mutable int contentWidth = -1;

private:
    std::function<int()> m_width;
};
// Final-report action row: square icon buttons, glyph per ui-dna toolbar chrome.
constexpr int kResponseActionsH = 22;
constexpr int kResponseActionIcon = 14;
constexpr int kCopiedTipMs = 1500;
constexpr const char *kResponseActionButtonStyle =
    "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 0; }"
    "QToolButton:hover { border-color: palette(mid); }"
    "QToolButton:pressed { background: rgba(128, 128, 128, 60); }";
constexpr const char *kFrameStyleSystem =
    "AcpMessageWidget[role=\"system\"] { background: rgba(212, 175, 55, 32); border: 1px solid rgba(212, 175, 55, 60); border-radius: 6px; }";

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

// QTextDocument lays out lazily: after setHtml()/setPlainText() the document
// reports a stale (often single-line) size until something forces the layout
// engine to run for the current text width. Measuring height before that pass
// clips multi-line thought/assistant bodies to ~one line. Touching the layout's
// documentSize() after pinning the text width forces the full pass, so the
// subsequent doc->size() read is authoritative. Same helper as AcpToolCallCard.
qreal layoutDocumentHeight(QTextDocument *doc, int textWidth)
{
    if (!doc) return 0.0;
    // setTextWidth relayouts the whole document even when the width is the
    // same (QTextDocument::setPageSize -> documentChanged(0, 0, length)). A
    // streaming bubble refits several times per tick: only re-wrap on a change.
    if (doc->textWidth() != textWidth)
        doc->setTextWidth(textWidth);
    QAbstractTextDocumentLayout *layout = doc->documentLayout();
    if (!layout) return doc->size().height();
    qreal h = layout->documentSize().height();
    const QTextBlock last = doc->lastBlock();
    if (last.isValid()) {
        const QRectF r = layout->blockBoundingRect(last);
        if (r.isValid())
            h = qMax(h, r.bottom());
    }
    return qMax(h, doc->size().height());
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

    if (m_role == QLatin1String("user")) {
        m_layout = new UserBubbleLayout(this, [this]() { return userBubbleWidth(); });
    } else {
        m_layout = new QVBoxLayout(this);
    }
    if (m_role == QLatin1String("thought")) {
        m_layout->setContentsMargins(4, 4, 4, 4);
    } else if (m_role == QLatin1String("user")) {
        m_layout->setContentsMargins(kUserBubblePadH, 8, kUserBubblePadH, 8);
        // The cap depends on the transcript width: re-query the hint when the
        // host is resized (the layout caches it).
        if (parent)
            parent->installEventFilter(this);
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
        m_browser->setStyleSheet(
            QStringLiteral("QTextBrowser { background: transparent; border: none; "
                           "font-style: italic; padding-left: %1px; }")
                .arg(kThoughtBrowserPadLeft));
        m_browser->setOpenExternalLinks(true);
        configureBubbleBrowser(m_browser);
        // Streaming appends through a cursor: keep them off the undo stack.
        m_browser->document()->setUndoRedoEnabled(false);
        // QTextDocument paragraphs carry an implicit ~12px bottom margin even
        // with documentMargin=0; zero it out so the bubble doesn't sprout
        // phantom whitespace below the last line.
        m_browser->document()->setDefaultStyleSheet(
            QStringLiteral("p, body { margin: 0; padding: 0; }"));
        m_layout->addWidget(m_browser);

        // A long body is capped at kThoughtMaxLines and scrolls inside the bubble
        // (refitBrowserHeight turns the scrollbar on). While the reader is at the
        // bottom, new text keeps the view at the bottom; scrolling up stops that
        // until they scroll back down.
        QScrollBar *vbar = m_browser->verticalScrollBar();
        vbar->setStyleSheet(QString::fromLatin1(kThoughtScrollBarStyle));
        connect(vbar, &QScrollBar::rangeChanged, this, [this, vbar](int, int max) {
            if (m_thoughtFollow)
                vbar->setValue(max);
        });
        connect(vbar, &QScrollBar::valueChanged, this, [this, vbar](int value) {
            m_thoughtFollow = value >= vbar->maximum() - kThoughtFollowSlackPx;
        });
        // The scrollbar appearing narrows the viewport and QTextEdit re-wraps the
        // document at the viewport width, without the italic slack: refit after.
        m_browser->viewport()->installEventFilter(this);

        connect(m_thoughtHeader, &QToolButton::toggled, this, [this](bool checked) {
            // Header checked = expanded. Keep m_collapsed in lockstep so height
            // fitting can gate on the logical flag, not QWidget::isVisible().
            applyCollapsed(!checked);
        });
    } else {
        // assistant + any other roles
        if (m_role == QLatin1String("assistant")) {
            // The shared markdown renderer (also drives the file Markdown preview):
            // math, allowlisted HTML, <details>, code surfaces, copy button, links.
            m_markdown = new MarkdownView(this);
            m_browser = m_markdown;
            // A <details> click re-renders immediately inside the view: re-fit the
            // bubble to the new document height.
            connect(m_markdown, &MarkdownView::rendered, this, [this]() {
                refitBrowserHeight();
                scheduleRefit();
            });
        } else {
            m_browser = new QTextBrowser(this);
            m_browser->setOpenExternalLinks(true);
            m_browser->document()->setUndoRedoEnabled(false); // see the thought role
        }
        m_browser->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; }"));
        configureBubbleBrowser(m_browser);
        m_layout->addWidget(m_browser);
    }

    // Debounce streamed re-renders so chunks don't drown the UI thread —
    // especially severe for assistant markdown tables, where every chunk
    // re-parses the whole payload.
    m_rerenderTimer = new QTimer(this);
    m_rerenderTimer->setSingleShot(true);
    m_rerenderTimer->setInterval(80);
    connect(m_rerenderTimer, &QTimer::timeout, this, &AcpMessageWidget::rerender);
}

void AcpMessageWidget::setGoalTint(bool on)
{
    if (m_fromGoalAgent == on)
        return;
    m_fromGoalAgent = on;
    setProperty("goalAgent", on);
    style()->unpolish(this);
    style()->polish(this);
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
            "text-transform: uppercase; color: rgb(212, 175, 55); "
            "background: palette(window); border: 1px solid rgba(212, 175, 55, 80); "
            "border-radius: 3px; padding: 0px 5px; }"));
        badge->setFixedHeight(badge->fontMetrics().height() + 4);
        m_goalBadge = badge;
        m_layout->insertWidget(0, badge);
    }
    style()->unpolish(this);
    style()->polish(this);
}

void AcpMessageWidget::applyGoalSetFrame()
{
    if (m_role != QLatin1String("assistant"))
        return;
    const bool goalSet = m_text.trimmed().startsWith(QLatin1String("Goal set:"));
    if (property("goalSet").isValid() && property("goalSet").toBool() == goalSet)
        return;
    setProperty("goalSet", goalSet);
    style()->unpolish(this);
    style()->polish(this);
    update();
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
    m_plainRebuild = true;
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
                    allowShrink(pendingTextLabel);
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
        updateGeometry();
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
    m_plainRebuild = true;
    rerender();
}

void AcpMessageWidget::setCollapsedSummary(const QString &summary, const QStringList &overview)
{
    if (m_role != QLatin1String("user") || !m_layout)
        return;
    if (!m_summaryHeader) {
        m_summaryHeader = new QToolButton(this);
        m_summaryHeader->setCheckable(true);
        m_summaryHeader->setAutoRaise(true);
        m_summaryHeader->setToolButtonStyle(Qt::ToolButtonTextOnly);
        allowShrink(m_summaryHeader);
        m_summaryHeader->setStyleSheet(QStringLiteral(
            "QToolButton { border: none; padding: 0; margin: 0; text-align: left; }"));
        m_summaryHeader->setCursor(Qt::PointingHandCursor);
        if (m_chatFontSet)
            m_summaryHeader->setFont(m_chatFont);
        m_layout->insertWidget(0, m_summaryHeader);
        m_summaryOverview = new QLabel(this);
        m_summaryOverview->setTextFormat(Qt::PlainText);
        m_summaryOverview->setWordWrap(true);
        allowShrink(m_summaryOverview);
        m_summaryOverview->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (m_chatFontSet)
            m_summaryOverview->setFont(m_chatFont);
        m_layout->insertWidget(1, m_summaryOverview);
        connect(m_summaryHeader, &QToolButton::toggled, this, [this](bool expanded) {
            for (QWidget *w : std::as_const(m_userBlocks))
                w->setVisible(expanded);
            // Expanded shows the full text; the overview would repeat it.
            m_summaryOverview->setVisible(!expanded && !m_summaryOverview->text().isEmpty());
            m_summaryHeader->setText(m_summary + (expanded ? QStringLiteral("  ▾") : QStringLiteral("  ▸")));
            updateGeometry();
        });
    }
    m_summary = QStringLiteral("↳ ") + summary;
    m_summaryOverview->setText(overview.join(QLatin1Char('\n')));
    m_summaryOverview->setVisible(!overview.isEmpty());
    m_summaryHeader->setChecked(false);
    m_summaryHeader->setText(m_summary + QStringLiteral("  ▸"));
    for (QWidget *w : std::as_const(m_userBlocks))
        w->setVisible(false);
    updateGeometry();
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
    applyGoalSetFrame();
    if (m_role == QLatin1String("user")) {
        // User content is rendered directly in setContent() — block-by-block.
        return;
    }
    if (!m_browser) return;

    if (m_role == QLatin1String("assistant")) {
        // One pipeline for chat and preview: see MarkdownView::rerender().
        m_markdown->setMarkdownSource(GoalAgent::nativeGoalDisplayText(m_text));
    } else {
        // thought + system + other: raw text (thought italic comes from the
        // QTextBrowser stylesheet). Do not setMarkdown — reasoning streams
        // contain ** / ` / ## that must stay visible as typed.
        // Streamed chunks often end with "\n", which QTextDocument turns into
        // an empty trailing block that adds a full line-height of phantom
        // whitespace below the visible text. Strip trailing whitespace so the
        // document size matches what the user actually reads.
        // Held-back whitespace is shown once text follows it, so "\r\n" never
        // splits across two renders.
        qsizetype end = m_text.size();
        while (end > 0) {
            const QChar c = m_text.at(end - 1);
            if (c != QLatin1Char('\n') && c != QLatin1Char('\r') && c != QLatin1Char(' ')
                && c != QLatin1Char('\t'))
                break;
            --end;
        }
        // Plain-text blocks carry no margins (only HTML gets paragraph margins),
        // so no normalizeBlockMargins pass: it rewrote every block per tick.
        QTextDocument *doc = m_browser->document();
        if (m_plainRebuild || end < m_plainShown) {
            doc->setPlainText(m_text.left(end));
        } else if (end > m_plainShown) {
            // m_text only grew since the last render: add the new tail. Same block
            // splitting as setPlainText (both go through QTextCursor::insertText).
            QTextCursor cur(doc);
            cur.movePosition(QTextCursor::End);
            cur.insertText(m_text.mid(m_plainShown, end - m_plainShown));
        }
        m_plainShown = end;
        m_plainRebuild = false;
    }
    refitBrowserHeight();
    scheduleRefit();
}

QSize AcpMessageWidget::sizeHint() const
{
    if (m_role != QLatin1String("user") || !m_layout)
        return QFrame::sizeHint();

    const int w = userBubbleWidth();
    const int h = m_layout->hasHeightForWidth() ? m_layout->totalHeightForWidth(w)
                                                : QFrame::sizeHint().height();
    return QSize(w, h);
}

int AcpMessageWidget::userBubbleWidth() const
{
    int marginL = 0, marginT = 0, marginR = 0, marginB = 0;
    m_layout->getContentsMargins(&marginL, &marginT, &marginR, &marginB);
    const auto *layout = static_cast<const UserBubbleLayout *>(m_layout);
    if (layout->contentWidth < 0)
        layout->contentWidth = measureUserContent();
    // +2 border, +2 so a line that exactly fits is not wrapped by rounding.
    int w = layout->contentWidth + marginL + marginR + 4;
    if (const QWidget *host = parentWidget())
        w = qMin(w, qMax(host->width() - kUserBubbleGap, 120));
    return w;
}

int AcpMessageWidget::measureUserContent() const
{
    int contentW = 0;
    if (m_summaryHeader)
        contentW = m_summaryHeader->sizeHint().width();
    if (m_summaryOverview && !m_summaryOverview->isHidden()) {
        const QFontMetrics fm = m_summaryOverview->fontMetrics();
        const QStringList lines = m_summaryOverview->text().split(QLatin1Char('\n'));
        for (const QString &line : lines)
            contentW = qMax(contentW, fm.horizontalAdvance(line));
    }
    for (const QWidget *w : m_userBlocks) {
        const auto *lbl = qobject_cast<const QLabel *>(w);
        if (!lbl || lbl->isHidden())
            continue;
        const QFontMetrics fm = lbl->fontMetrics();
        const QStringList lines = lbl->text().split(QLatin1Char('\n'));
        for (const QString &line : lines)
            contentW = qMax(contentW, fm.horizontalAdvance(line));
    }
    for (const auto &ui : m_userImages) {
        if (!ui.original.isNull() && !ui.label->isHidden())
            contentW = qMax(contentW, ui.original.width());
    }
    return contentW;
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

    // Pin the bubble's own height too. setFixedHeight on the inner browser
    // only clamps the browser — QFrame's sizeHint cascades through QBoxLayout
    // and QAbstractScrollArea's font-derived default still inflates the
    // bubble. Computing the bubble height here directly is authoritative.
    int bubbleH = marginT + marginB;
    if (m_goalBadge) {
        // Stylesheet font/border make the badge taller than fontMetrics+4.
        // Using the short value eats the 6px bottom margin so the last line
        // sits on the frame. Prefer live size, then sizeHint, then metrics.
        const int badgeH = qMax(m_goalBadge->height(),
                                qMax(m_goalBadge->sizeHint().height(),
                                     m_goalBadge->fontMetrics().height() + 4));
        bubbleH += badgeH;
        bubbleH += m_layout->spacing();
    }
    if (m_thoughtHeader) {
        // Use font metrics for the header height; QToolButton::sizeHint()
        // adds style-derived button margins even with stylesheet padding:0,
        // which adds phantom vertical space inside the bubble.
        bubbleH += m_thoughtHeader->fontMetrics().height();
        // Gate on the LOGICAL expand state, not m_browser->isVisible(): a
        // thought on a hidden QStackedWidget page (inactive session tab) —
        // or a just-inserted widget not yet painted — reads isVisible()==false
        // even though the body WILL paint once the tab is shown. That under-
        // pins the frame to header-only height while the body keeps its
        // measured height, clipping the thinking text. m_collapsed is
        // independent of show timing.
        if (m_collapsed) {
            setFixedHeight(bubbleH);
            return;
        }
        bubbleH += m_layout->spacing();
    }

    if (m_responseActionsEnabled && m_responseActions)
        bubbleH += m_layout->spacing() + kResponseActionsH;

    const int w = width() - marginL - marginR;
    if (w <= 0) {
        // Width not settled (never-shown stack page). Keep whatever height we
        // already have unless we just collapsed to header-only above.
        return;
    }
    int textW = w;
    if (m_thoughtHeader) {
        textW = qMax(0, w - kThoughtBrowserPadLeft - kThoughtItalicRightSlack);
    }
    QTextDocument *doc = m_browser->document();
    // Force a full layout pass for the current width before measuring —
    // QTextDocument under-reports height for freshly-set multi-line text
    // until the layout engine has run, which clips an expanded thought down
    // to roughly its first line.
    auto measure = [doc](int width) {
        return qMax(0, static_cast<int>(std::ceil(layoutDocumentHeight(doc, qMax(0, width)))));
    };
    int browserH = 0;
    if (m_thoughtHeader) {
        // Capped: the body is kThoughtMaxLines tall and wraps beside the
        // scrollbar. Measure at the width the current state uses, so a streaming
        // tick never re-wraps the document (each width change is a full
        // relayout). Narrower text is never shorter, so neither switch can flip
        // straight back.
        const int cap = static_cast<int>(
            std::ceil(QFontMetricsF(doc->defaultFont()).lineSpacing() * kThoughtMaxLines));
        const int barW = m_browser->verticalScrollBar()->sizeHint().width();
        bool capped = m_thoughtCapped;
        browserH = measure(capped ? textW - barW : textW);
        if (!capped && browserH > cap) {
            capped = true;
            measure(textW - barW);
        } else if (capped && browserH <= cap) {
            capped = false; // the text was replaced by a shorter one
            browserH = measure(textW);
        }
        if (capped)
            browserH = cap;
        if (capped != m_thoughtCapped) {
            m_thoughtCapped = capped;
            m_browser->setVerticalScrollBarPolicy(capped ? Qt::ScrollBarAsNeeded
                                                         : Qt::ScrollBarAlwaysOff);
        }
    } else {
        browserH = measure(textW);
    }
    m_browser->setFixedHeight(browserH);
    bubbleH += browserH;
    // Stylesheet border is inside the widget and shrinks the layout box.
    // Without this, marginB is consumed and the last line sits on the frame.
    if (height() > contentsRect().height())
        bubbleH += height() - contentsRect().height();
    setFixedHeight(bubbleH);
}

void AcpMessageWidget::scheduleRefit()
{
    if (m_refitScheduled) return;
    m_refitScheduled = true;
    QPointer<AcpMessageWidget> guard(this);
    QTimer::singleShot(0, this, [guard]() {
        if (!guard) return;
        guard->m_refitScheduled = false;
        guard->refitBrowserHeight();
    });
}

void AcpMessageWidget::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    refitBrowserHeight();
    rescaleUserImages();
}

void AcpMessageWidget::showEvent(QShowEvent *event)
{
    QFrame::showEvent(event);
    // Session-tab switch shows this page without a size change, so resizeEvent
    // may not run. Re-measure now that we are actually visible and the stacked
    // layout has given us a real width.
    refitBrowserHeight();
    scheduleRefit();
}

void AcpMessageWidget::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        refitBrowserHeight();
        scheduleRefit();
    }
    // Palette changes: the MarkdownView re-skins itself (copy glyph, code surface,
    // math tint) and emits rendered(), which re-fits the bubble.
    if (event->type() == QEvent::PaletteChange)
        retintResponseActionIcons();
}

bool AcpMessageWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize && watched == parentWidget()) {
        updateGeometry();
        return false;
    }
    if (event->type() == QEvent::Resize && m_thoughtHeader && m_browser
        && watched == m_browser->viewport()) {
        scheduleRefit(); // queued: runs after QTextEdit's own re-wrap
        return false;
    }
    return QFrame::eventFilter(watched, event);
}

void AcpMessageWidget::setResponseActions(bool enabled)
{
    if (m_role != QLatin1String("assistant") || enabled == m_responseActionsEnabled)
        return;
    m_responseActionsEnabled = enabled;
    if (enabled)
        ensureResponseActions();
    if (m_responseActions)
        m_responseActions->setVisible(enabled);
    refitBrowserHeight();
    scheduleRefit();
}

void AcpMessageWidget::ensureResponseActions()
{
    if (m_responseActions || !m_layout)
        return;
    m_responseActions = new QWidget(this);
    m_responseActions->setFixedHeight(kResponseActionsH);
    auto *row = new QHBoxLayout(m_responseActions);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    auto makeButton = [this, row](const QString &tip) {
        auto *b = new QToolButton(m_responseActions);
        b->setAutoRaise(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        b->setIconSize(QSize(kResponseActionIcon, kResponseActionIcon));
        b->setFixedSize(kResponseActionsH, kResponseActionsH);
        b->setStyleSheet(QString::fromLatin1(kResponseActionButtonStyle));
        row->addWidget(b);
        return b;
    };
    m_copyResponseBtn = makeButton(tr("Copy response"));
    m_copyConversationBtn = makeButton(tr("Copy conversation"));
    row->addStretch(1);
    m_layout->addWidget(m_responseActions);
    retintResponseActionIcons();

    // QClipboard::setText has no failure result, so the tip always confirms.
    auto confirm = [](QToolButton *b, const QString &text) {
        QToolTip::showText(b->mapToGlobal(QPoint(0, b->height())), text, b, QRect(), kCopiedTipMs);
    };
    connect(m_copyResponseBtn, &QToolButton::clicked, this, [this, confirm]() {
        // The markdown source of what the bubble shows.
        QGuiApplication::clipboard()->setText(GoalAgent::nativeGoalDisplayText(m_text).trimmed());
        confirm(m_copyResponseBtn, tr("Copied"));
    });
    connect(m_copyConversationBtn, &QToolButton::clicked, this, [this, confirm]() {
        emit copyConversationRequested();
        confirm(m_copyConversationBtn, tr("Conversation copied"));
    });
}

void AcpMessageWidget::retintResponseActionIcons()
{
    if (!m_copyResponseBtn)
        return;
    const QColor color = palette().color(QPalette::ButtonText);
    const qreal dpr = devicePixelRatioF();
    m_copyResponseBtn->setIcon(QIcon(usagestyle::tintedPixmap(
        QStringLiteral(":/icons/copy.svg"), color, kResponseActionIcon, dpr)));
    m_copyConversationBtn->setIcon(QIcon(usagestyle::tintedPixmap(
        QStringLiteral(":/icons/messages-square.svg"), color, kResponseActionIcon, dpr)));
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
    if (m_markdown) {
        // rerender=false: this widget re-renders itself below, once.
        m_markdown->setChatFont(font, false);
    } else if (m_browser) {
        m_browser->document()->setDefaultFont(font);
    }
    if (m_thoughtHeader) {
        QFont hdr = font;
        hdr.setItalic(true); // matches the header stylesheet
        m_thoughtHeader->setFont(hdr);
        m_thoughtHeader->setFixedHeight(m_thoughtHeader->fontMetrics().height());
    }
    if (m_summaryHeader)
        m_summaryHeader->setFont(font);
    if (m_summaryOverview)
        m_summaryOverview->setFont(font);
    // User-role bodies are QLabels built lazily in setContent(); stamp the ones
    // that exist now (newly-created ones are stamped in setContent()).
    for (QWidget *w : m_userBlocks) {
        if (auto *lbl = qobject_cast<QLabel *>(w)) {
            lbl->setFont(font);
        }
    }
    updateGeometry();

    // Assistant bodies bake the font into HTML, so re-serialize; other roles
    // already follow document()->defaultFont(). Skip an empty assistant bubble
    // (nothing baked yet — the next rerender picks up m_chatFont). All paths
    // refit height under the new metrics.
    if (m_role == QLatin1String("assistant") && !m_text.isEmpty()) {
        rerender();
    } else {
        refitBrowserHeight();
        scheduleRefit();
    }
}

void AcpMessageWidget::markStreamingDone()
{
    if (m_role != QLatin1String("thought")) return;
    m_streamingDone = true;
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
    // A finished thought opened later reads from its start.
    if (!collapsed && m_streamingDone && m_thoughtHeader && m_browser) {
        m_thoughtFollow = false;
        m_browser->verticalScrollBar()->setValue(0);
    }
    // Body may have been laid out at a stale width while hidden; re-measure
    // once the expanded geometry settles so the thought doesn't clip on
    // first expand / tab show.
    if (!collapsed) scheduleRefit();
}
