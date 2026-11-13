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

#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTabBar>
#include <QToolButton>
#include <QToolTip>

namespace {

constexpr int kBusyFillAlpha = 110; // Highlight at ~43%: the digit stays readable

// A session number that can wear a working fill and a waiting dot. Painting
// only, and static: the size never changes (a busy session never relayouts the
// tab bar) and nothing repaints on a timer.
class SessionNumberButton : public QToolButton
{
public:
    using QToolButton::QToolButton;

    void setBusy(bool busy)
    {
        if (m_busy == busy)
            return;
        m_busy = busy;
        update();
    }
    void setWaiting(bool waiting)
    {
        if (m_waiting == waiting)
            return;
        m_waiting = waiting;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        // One painter at a time on a widget: the base paint opens its own.
        if (m_busy) {
            // Under the stylesheet's transparent rest background, so the
            // digit, the current number's border and the hover fill draw on top.
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            QColor fill = palette().color(QPalette::Highlight);
            fill.setAlpha(kBusyFillAlpha);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(rect()), kFillRadius, kFillRadius);
        }
        QToolButton::paintEvent(event);
        if (m_waiting) {
            QPainter p(this);
            p.setRenderHint(QPainter::Antialiasing);
            // A window-coloured rim keeps the dot apart from the digit.
            const QPointF center(width() - kDotRadius - 1, kDotRadius + 1);
            p.setPen(QPen(palette().color(QPalette::Window), 1));
            p.setBrush(AiDockSessionStrip::attentionColor());
            p.drawEllipse(center, kDotRadius, kDotRadius);
        }
    }

private:
    static constexpr qreal kDotRadius = 3.0; // ~6px dot, top-right corner
    static constexpr qreal kFillRadius = 4.0; // ui-dna status-surface radius
    bool m_busy = false;
    bool m_waiting = false;
};

} // namespace

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
    const int newCount = qMax(0, count);
    const int newCurrent = (newCount == 0) ? 0 : qBound(0, current, newCount - 1);
    QVector<SlotSnapshot> snaps = snapshots;
    if (snaps.size() > newCount)
        snaps.resize(newCount);
    while (snaps.size() < newCount)
        snaps.append(SlotSnapshot{});
    // The dock pushes on every state flicker; nothing to restyle (a stylesheet
    // reset per button, and none mid-drag) when nothing changed.
    if (newCount == m_count && newCurrent == m_current && snaps == m_snapshots
        && m_buttons.size() == newCount) {
        return;
    }
    m_count = newCount;
    m_current = newCurrent;
    m_snapshots = std::move(snaps);
    rebuildButtons();
}

bool AiDockSessionStrip::busyAt(int index) const
{
    return index >= 0 && index < m_snapshots.size() && m_snapshots.at(index).busy;
}

QColor AiDockSessionStrip::attentionColor()
{
    return QColor(0xe5, 0x48, 0x4d);
}

bool AiDockSessionStrip::waitingAt(int index) const
{
    return index >= 0 && index < m_snapshots.size() && m_snapshots.at(index).waiting;
}

void AiDockSessionStrip::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    endDrag();
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
    // QTabBar::removeTab() deleteLater()s its tab buttons, but the owner may
    // have moved the strip onto another bar before the event lands. That bar
    // keeps a raw pointer and calls hide() on it in its own removeTab.
    if (event && event->type() == QEvent::DeferredDelete)
        detachFromHostTabBar();
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

    if (event->type() == QEvent::ContextMenu) {
        // The owner builds the menu: it maps the index to a session id before
        // any nested loop can reorder or close sessions.
        endDrag();
        emit contextMenuRequested(index, static_cast<QContextMenuEvent *>(event)->globalPos());
        return true;
    }

    if (handleDrag(index, event))
        return true;

    return QWidget::eventFilter(watched, event);
}

bool AiDockSessionStrip::handleDrag(int index, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove
        && type != QEvent::MouseButtonRelease) {
        return false;
    }
    auto *mouse = static_cast<QMouseEvent *>(event);

    if (type == QEvent::MouseButtonPress) {
        if (mouse->button() == Qt::LeftButton) {
            m_pressIndex = index;
            m_pressGlobalPos = mouse->globalPosition().toPoint();
        }
        return false; // the button still goes down: a plain click activates
    }
    if (m_pressIndex != index)
        return false;

    const QPoint globalPos = mouse->globalPosition().toPoint();
    if (type == QEvent::MouseMove) {
        if (!(mouse->buttons() & Qt::LeftButton)) {
            // The release never reached us (a popup or modal took it): drop the
            // half-made drag instead of leaving the numbers scrambled.
            endDrag();
            return false;
        }
        if (!m_dragging) {
            if ((globalPos - m_pressGlobalPos).manhattanLength() < QApplication::startDragDistance())
                return false;
            m_dragging = true;
            m_dragPosition = index;
            // Up without being down: the release below cannot count as a click.
            m_buttons.at(index)->setDown(false);
        }
        moveDraggedButton(dropPositionAt(mapFromGlobal(globalPos).x()));
        return true;
    }

    // Release
    if (mouse->button() != Qt::LeftButton)
        return false;
    if (!m_dragging) {
        m_pressIndex = -1;
        return false;
    }
    const int from = m_pressIndex;
    const int to = m_dragPosition;
    endDrag();
    if (to != from)
        emit moveRequested(from, to);
    return true;
}

int AiDockSessionStrip::dropPositionAt(int stripX) const
{
    const int last = m_layout->count() - 1;
    for (int k = 0; k <= last; ++k) {
        if (stripX < m_layout->itemAt(k)->geometry().right() + kSpacingPx / 2)
            return k;
    }
    return qMax(0, last);
}

void AiDockSessionStrip::moveDraggedButton(int position)
{
    if (position == m_dragPosition)
        return;
    QToolButton *btn = m_buttons.at(m_pressIndex);
    m_layout->removeWidget(btn);
    m_layout->insertWidget(position, btn);
    m_layout->activate();
    m_dragPosition = position;
}

// Puts the layout back to creation order; digits are positions, so the owner
// re-pushes the snapshots after a move.
void AiDockSessionStrip::endDrag()
{
    if (m_dragging && m_pressIndex >= 0 && m_pressIndex < m_buttons.size()) {
        QToolButton *btn = m_buttons.at(m_pressIndex);
        m_layout->removeWidget(btn);
        m_layout->insertWidget(m_pressIndex, btn);
        m_layout->activate();
    }
    m_dragging = false;
    m_pressIndex = -1;
    m_dragPosition = -1;
}

void AiDockSessionStrip::rebuildButtons()
{
    // A session opened or closed mid-drag: the dragged index no longer holds.
    if (m_count != m_buttons.size())
        endDrag();
    while (m_buttons.size() > m_count) {
        QToolButton *btn = m_buttons.takeLast();
        m_layout->removeWidget(btn);
        delete btn;
    }
    while (m_buttons.size() < m_count) {
        auto *btn = new SessionNumberButton(this);
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
        const bool busy = busyAt(i);
        auto *numberBtn = static_cast<SessionNumberButton *>(btn);
        numberBtn->setBusy(busy);
        numberBtn->setWaiting(waitingAt(i));

        QFont font = btn->font();
        font.setBold(current);
        btn->setFont(font);

        // A muted digit on the working fill is too faint: busy numbers take the
        // full text colour (the current one still has the bold and the border).
        const bool strong = current || busy;
        QPalette pal = btn->palette();
        pal.setColor(QPalette::ButtonText, strong ? currentColor : muted);
        pal.setColor(QPalette::WindowText, strong ? currentColor : muted);
        btn->setPalette(pal);
        // Toolbar chrome (ui-dna): auto-raise + 1px mid border on hover.
        // Transparent rest border keeps size stable. Windows QStyle ignores
        // QToolButton palette colors, so paint via palette() roles.
        const QString colorRole = strong ? QStringLiteral("palette(button-text)")
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

void AiDockSessionStrip::detachFromHostTabBar()
{
    auto *bar = qobject_cast<QTabBar *>(parentWidget());
    if (!bar)
        return;
    for (int i = 0; i < bar->count(); ++i) {
        if (bar->tabButton(i, QTabBar::RightSide) == this)
            bar->setTabButton(i, QTabBar::RightSide, nullptr);
        if (bar->tabButton(i, QTabBar::LeftSide) == this)
            bar->setTabButton(i, QTabBar::LeftSide, nullptr);
    }
}

QString AiDockSessionStrip::tooltipFor(int index) const
{
    if (index < 0 || index >= m_snapshots.size())
        return {};
    const SlotSnapshot &snap = m_snapshots.at(index);
    QStringList lines;
    lines.reserve(3);
    if (!snap.name.isEmpty())
        lines.append(snap.name);
    // Colour is never the only carrier of meaning (ui-dna).
    if (snap.waiting)
        lines.append(tr("Waiting for your answer"));
    if (!snap.lastUserPreview.isEmpty())
        lines.append(snap.lastUserPreview);
    return lines.join(QLatin1Char('\n'));
}

int AiDockSessionStrip::indexOfButton(const QObject *obj) const
{
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons.at(i) == obj)
            return i;
    }
    return -1;
}
