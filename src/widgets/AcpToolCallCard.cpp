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

#include "AcpToolCallCard.h"

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

AcpToolCallCard::AcpToolCallCard(const AcpProtocol::AcpToolCall &initial, QWidget *parent)
    : QFrame(parent)
    , m_id(initial.id)
    , m_title(initial.title)
    , m_status(initial.status)
    , m_groupId(initial.groupId)
    , m_content(initial.content)
{
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral("AcpToolCallCard { background: palette(base); border: 1px solid palette(mid); border-radius: 4px; }"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 4, 6, 4);
    outer->setSpacing(2);

    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);

    m_statusIcon = new QLabel(this);
    m_titleLabel = new QLabel(this);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_expandBtn = new QToolButton(this);
    m_expandBtn->setText(QStringLiteral("▾"));
    m_expandBtn->setCheckable(true);
    m_expandBtn->setChecked(true);
    m_expandBtn->setAutoRaise(true);

    header->addWidget(m_statusIcon);
    header->addWidget(m_titleLabel, 1);
    header->addWidget(m_expandBtn);

    outer->addLayout(header);

    m_body = new QTextBrowser(this);
    m_body->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; border: none; }"));
    m_body->setOpenExternalLinks(true);
    outer->addWidget(m_body);

    connect(m_expandBtn, &QToolButton::toggled, this, [this](bool checked) {
        setCollapsed(!checked);
    });

    refreshHeader();
    rerenderBody();
}

void AcpToolCallCard::apply(const AcpProtocol::AcpToolCallUpdate &update)
{
    if (update.status.has_value()) {
        m_status = *update.status;
    }
    if (update.content.has_value()) {
        m_content = *update.content;
    }
    refreshHeader();
    rerenderBody();
}

QString AcpToolCallCard::statusGlyph() const
{
    if (m_status == QLatin1String("completed")) return QStringLiteral("✓");
    if (m_status == QLatin1String("failed"))    return QStringLiteral("✗");
    if (m_status == QLatin1String("running"))   return QStringLiteral("⚙");
    return QStringLiteral("⏳");
}

void AcpToolCallCard::refreshHeader()
{
    m_statusIcon->setText(statusGlyph());
    m_titleLabel->setText(m_title.isEmpty() ? tr("Tool call") : m_title);
}

void AcpToolCallCard::rerenderBody()
{
    if (!m_body) return;
    QString text;
    bool decodedSomething = false;
    for (const auto &v : m_content) {
        if (!v.isObject()) continue;
        const QJsonObject obj = v.toObject();
        const QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("text")) {
            text += obj.value(QStringLiteral("text")).toString();
            text += QLatin1Char('\n');
            decodedSomething = true;
        } else if (type == QLatin1String("image")) {
            text += QStringLiteral("[image]\n");
            decodedSomething = true;
        }
    }
    if (!decodedSomething && !m_content.isEmpty()) {
        // Fall back to pretty JSON.
        const QJsonDocument doc(m_content);
        text = QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }
    m_body->document()->setPlainText(text);
    m_body->document()->setTextWidth(m_body->viewport()->width());
    const int h = static_cast<int>(m_body->document()->size().height()) + 8;
    m_body->setFixedHeight(qMax(20, qMin(h, 400)));
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
}
