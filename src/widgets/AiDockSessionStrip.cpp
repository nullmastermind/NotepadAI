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

#include "AiDockSessionStrip.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPalette>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTabBar>
#include <QToolButton>
#include <QToolTip>

AiDockSessionStrip::AiDockSessionStrip(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(kSpacingPx);
    // QTabBar::tabSizeHint copies rightWidget->size(), not sizeHint
    // (qtabbar.cpp initBasicStyleOption). Maximum lets a hidden header
    // layout lock a 0-width geometry that the tab then treats as the
    // reserved button width. Fixed = the button row is the only size.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void AiDockSessionStrip::setSlots(int count, int current, const QVector<SlotSnapshot> &snapshots)
{
    m_count = qMax(0, count);
    m_current = (m_count == 0) ? 0 : qBound(0, current, m_count - 1);
    m_snapshots = snapshots;
    if (m_snapshots.size() > m_count)
        m_snapshots.resize(m_count);
    while (m_snapshots.size() < m_count)
        m_snapshots.append(SlotSnapshot{});
    rebuildButtons();
}

QSize AiDockSessionStrip::sizeHint() const
{
    int w = 0;
    int h = 0;
    for (const QToolButton *btn : m_buttons) {
        const QSize sh = btn->sizeHint();
        w += sh.width();
        h = qMax(h, sh.height());
    }
    if (m_buttons.size() > 1)
        w += kSpacingPx * (m_buttons.size() - 1);
    return QSize(w, h);
}

QSize AiDockSessionStrip::minimumSizeHint() const
{
    return sizeHint();
}

void AiDockSessionStrip::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event && event->type() == QEvent::PaletteChange)
        restyleButtons();
}

bool AiDockSessionStrip::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    // setTabButton reparents then layoutTabs; size() must already be
    // intrinsic or the tab will not grow and the digits clip.
    if (event && event->type() == QEvent::ParentChange)
        syncIntrinsicSize();
    return handled;
}

bool AiDockSessionStrip::eventFilter(QObject *watched, QEvent *event)
{
    const int index = indexOfButton(watched);
    if (index < 0)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        QToolTip::showText(help->globalPos(), tooltipFor(index), m_buttons.at(index));
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::MiddleButton) {
            if (event->type() == QEvent::MouseButtonRelease)
                emit closeRequested(index);
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void AiDockSessionStrip::rebuildButtons()
{
    while (m_buttons.size() > m_count) {
        QToolButton *btn = m_buttons.takeLast();
        m_layout->removeWidget(btn);
        delete btn;
    }
    while (m_buttons.size() < m_count) {
        auto *btn = new QToolButton(this);
        btn->setAutoRaise(true);
        btn->setFocusPolicy(Qt::TabFocus);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        btn->installEventFilter(this);
        m_layout->addWidget(btn);
        m_buttons.append(btn);
    }
    for (int i = 0; i < m_buttons.size(); ++i) {
        QToolButton *btn = m_buttons.at(i);
        disconnect(btn, &QToolButton::clicked, nullptr, nullptr);
        connect(btn, &QToolButton::clicked, this, [this, i]() {
            emit activated(i);
        });
        btn->setText(QString::number(i + 1));
    }
    restyleButtons();
}

void AiDockSessionStrip::restyleButtons()
{
    const QColor currentColor = palette().color(QPalette::ButtonText);
    const QColor muted = palette().color(QPalette::PlaceholderText);

    for (int i = 0; i < m_buttons.size(); ++i) {
        QToolButton *btn = m_buttons.at(i);
        const bool current = (i == m_current);
        const bool busyNonCurrent = !current && i < m_snapshots.size() && m_snapshots.at(i).busy;

        QFont font = btn->font();
        font.setBold(current);
        font.setItalic(busyNonCurrent);
        btn->setFont(font);

        QPalette pal = btn->palette();
        pal.setColor(QPalette::ButtonText, current ? currentColor : muted);
        pal.setColor(QPalette::WindowText, current ? currentColor : muted);
        btn->setPalette(pal);
        // Toolbar chrome (ui-dna): auto-raise + 1px mid border on hover.
        // Transparent rest border keeps size stable. Windows QStyle ignores
        // QToolButton palette colors, so paint via palette() roles.
        const QString colorRole = current ? QStringLiteral("palette(button-text)")
                                          : QStringLiteral("palette(placeholder-text)");
        const QString restBorder = current ? QStringLiteral("1px solid palette(mid)")
                                           : QStringLiteral("1px solid transparent");
        btn->setStyleSheet(
            QStringLiteral("QToolButton { color: %1; padding: 0px %2px; "
                           "border: %3; background: transparent; }"
                           "QToolButton:hover { background: palette(midlight); "
                           "border: 1px solid palette(mid); }")
                .arg(colorRole, QString::number(kSpacingPx), restBorder));
        btn->setMinimumSize(btn->sizeHint());
    }
    syncIntrinsicSize();
    notifyHostTabBar();
}

void AiDockSessionStrip::syncIntrinsicSize()
{
    if (m_layout)
        m_layout->activate();
    const QSize intrinsic = sizeHint();
    setMinimumSize(intrinsic);
    if (size() != intrinsic)
        resize(intrinsic);
}

void AiDockSessionStrip::notifyHostTabBar()
{
    auto *bar = qobject_cast<QTabBar *>(parentWidget());
    if (!bar)
        return;
    // layoutTabs is not invoked on child resize. A same-size ResizeEvent
    // is the public path into QTabBar::resizeEvent -> layoutTabs.
    const QSize barSize = bar->size();
    QResizeEvent ev(barSize, barSize);
    QCoreApplication::sendEvent(bar, &ev);
}

QString AiDockSessionStrip::tooltipFor(int index) const
{
    if (index < 0 || index >= m_snapshots.size())
        return {};
    return m_snapshots.at(index).lastUserPreview;
}

int AiDockSessionStrip::indexOfButton(const QObject *obj) const
{
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons.at(i) == obj)
            return i;
    }
    return -1;
}
