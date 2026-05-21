/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {

constexpr const char *kFrameStyleUser =
    "AcpMessageWidget[role=\"user\"] { background: palette(alternate-base); border-radius: 6px; }";
constexpr const char *kFrameStyleAssistant =
    "AcpMessageWidget[role=\"assistant\"] { background: palette(base); border-radius: 6px; }";
constexpr const char *kFrameStyleThought =
    "AcpMessageWidget[role=\"thought\"] { background: palette(window); border: 1px dashed palette(mid); border-radius: 6px; }";

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

} // namespace

AcpMessageWidget::AcpMessageWidget(QString role, QWidget *parent)
    : QFrame(parent)
    , m_role(std::move(role))
{
    setProperty("role", m_role);
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QString::fromLatin1(kFrameStyleUser) +
                  QString::fromLatin1(kFrameStyleAssistant) +
                  QString::fromLatin1(kFrameStyleThought));

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 6, 8, 6);
    m_layout->setSpacing(2);

    if (m_role == QLatin1String("user")) {
        // User-role children are built lazily in setContent() so we can render
        // a sequence of text blocks + image thumbnails in their original order.
    } else if (m_role == QLatin1String("thought")) {
        m_thoughtHeader = new QToolButton(this);
        m_thoughtHeader->setText(tr("Thinking…"));
        m_thoughtHeader->setCheckable(true);
        m_thoughtHeader->setChecked(true); // start expanded while streaming
        m_thoughtHeader->setStyleSheet(QStringLiteral("QToolButton { border: none; font-style: italic; color: palette(placeholder-text); }"));
        m_thoughtHeader->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_layout->addWidget(m_thoughtHeader);

        m_browser = new QTextBrowser(this);
        m_browser->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; font-style: italic; }"));
        m_browser->setOpenExternalLinks(true);
        configureBubbleBrowser(m_browser);
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
}

void AcpMessageWidget::appendChunk(const QString &chunk)
{
    m_text += chunk;
    rerender();
}

void AcpMessageWidget::setText(const QString &fullText)
{
    m_text = fullText;
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
        m_browser->document()->setMarkdown(m_text);
    } else {
        m_browser->document()->setPlainText(m_text);
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
        bubbleH += m_thoughtHeader->sizeHint().height();
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
    // Re-pin the bubble so it shrinks to header-only when collapsed (and
    // grows back when re-expanded).
    refitBrowserHeight();
}
