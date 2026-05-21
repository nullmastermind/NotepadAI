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

#include <QLabel>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr const char *kFrameStyleUser =
    "AcpMessageWidget[role=\"user\"] { background: palette(alternate-base); border-radius: 6px; }";
constexpr const char *kFrameStyleAssistant =
    "AcpMessageWidget[role=\"assistant\"] { background: palette(base); border-radius: 6px; }";
constexpr const char *kFrameStyleThought =
    "AcpMessageWidget[role=\"thought\"] { background: palette(window); border: 1px dashed palette(mid); border-radius: 6px; }";

void resizeBrowserToContents(QTextBrowser *browser)
{
    if (!browser) return;
    QTextDocument *doc = browser->document();
    doc->setTextWidth(browser->viewport()->width());
    const int margin = 4;
    const int h = static_cast<int>(doc->size().height()) + 2 * margin;
    browser->setFixedHeight(qMax(20, h));
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
        m_userLabel = new QLabel(this);
        m_userLabel->setWordWrap(true);
        m_userLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_layout->addWidget(m_userLabel);
    } else if (m_role == QLatin1String("thought")) {
        m_thoughtHeader = new QToolButton(this);
        m_thoughtHeader->setText(tr("Thinking…"));
        m_thoughtHeader->setCheckable(true);
        m_thoughtHeader->setChecked(true); // start expanded while streaming
        m_thoughtHeader->setStyleSheet(QStringLiteral("QToolButton { border: none; font-style: italic; color: palette(mid); }"));
        m_thoughtHeader->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_layout->addWidget(m_thoughtHeader);

        m_browser = new QTextBrowser(this);
        m_browser->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; font-style: italic; }"));
        m_browser->setOpenExternalLinks(true);
        m_layout->addWidget(m_browser);

        connect(m_thoughtHeader, &QToolButton::toggled, this, [this](bool checked) {
            if (m_browser) m_browser->setVisible(checked);
        });
    } else {
        // assistant + any other roles
        m_browser = new QTextBrowser(this);
        m_browser->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; }"));
        m_browser->setOpenExternalLinks(true);
        m_layout->addWidget(m_browser);
    }
}

void AcpMessageWidget::appendChunk(const QString &chunk)
{
    m_text += chunk;
    rerender();
}

void AcpMessageWidget::setContent(const QVector<AcpProtocol::AcpContentBlock> &content)
{
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

void AcpMessageWidget::rerender()
{
    if (m_userLabel) {
        m_userLabel->setText(m_text);
        return;
    }
    if (!m_browser) return;

    if (m_role == QLatin1String("assistant")) {
        m_browser->document()->setMarkdown(m_text);
    } else {
        m_browser->document()->setPlainText(m_text);
    }
    resizeBrowserToContents(m_browser);
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
}
