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

#include "GitTabSegmentedBar.h"

#include <QButtonGroup>
#include <QPainter>
#include <QPalette>
#include <QStyle>
#include <QToolButton>

namespace {
constexpr int kBarHeight = 28;
constexpr int kUnderlineHeight = 2;
} // namespace

GitTabSegmentedBar::GitTabSegmentedBar(QWidget *parent)
    : QWidget(parent), m_group(new QButtonGroup(this))
{
    m_group->setExclusive(true);
    setFixedHeight(kBarHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_StyledBackground, false);
}

void GitTabSegmentedBar::setSegments(const QStringList &labels)
{
    m_labels = labels;
    rebuild();
    if (!m_buttons.isEmpty()) setCurrentIndex(0);
}

void GitTabSegmentedBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_buttons.size()) return;
    if (m_currentIndex == index) return;
    m_currentIndex = index;
    for (int i = 0; i < m_buttons.size(); ++i) {
        m_buttons[i]->setChecked(i == index);
    }
    update();
    emit currentChanged(index);
}

void GitTabSegmentedBar::rebuild()
{
    // Remove previous buttons.
    qDeleteAll(m_buttons);
    m_buttons.clear();
    // QButtonGroup auto-removes destroyed buttons.

    for (int i = 0; i < m_labels.size(); ++i) {
        auto *b = new QToolButton(this);
        b->setText(m_labels.at(i));
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::TabFocus);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setCursor(Qt::PointingHandCursor);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // No painted border — underline is the only active-state indicator.
        b->setStyleSheet(QStringLiteral(
            "QToolButton { border: none; background: transparent; padding: 4px 8px; }"
            "QToolButton:hover { background: palette(midlight); }"
            "QToolButton:checked { background: transparent; font-weight: 600; }"
        ));
        m_group->addButton(b, i);
        m_buttons.append(b);
        connect(b, &QToolButton::toggled, this, [this, i](bool on) {
            if (on) setCurrentIndex(i);
        });
    }
    m_currentIndex = -1;
    layoutSegments();
}

void GitTabSegmentedBar::layoutSegments()
{
    if (m_buttons.isEmpty()) return;
    const int n = m_buttons.size();
    const int totalW = width();
    const int segW = qMax(40, totalW / n);
    const int barH = kBarHeight - kUnderlineHeight;   // leave room for underline
    int x = 0;
    for (int i = 0; i < n; ++i) {
        const int w = (i == n - 1) ? totalW - x : segW;
        m_buttons[i]->setGeometry(x, 0, w, barH);
        x += w;
    }
}

void GitTabSegmentedBar::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    layoutSegments();
}

void GitTabSegmentedBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    const int yLine = height() - kUnderlineHeight;

    // Inactive baseline (subtle separator under all segments).
    p.fillRect(0, yLine, width(), 1, palette().color(QPalette::Mid));

    if (m_currentIndex < 0 || m_currentIndex >= m_buttons.size()) return;

    // Active segment underline (kUnderlineHeight px, highlight color).
    QWidget *active = m_buttons[m_currentIndex];
    const QRect r = active->geometry();
    p.fillRect(r.x(), yLine, r.width(), kUnderlineHeight,
               palette().color(QPalette::Highlight));
}
