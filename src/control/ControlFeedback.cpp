/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlFeedback.h"

#include "ControlIds.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolBar>
#include <QVector>

namespace control {

namespace {

constexpr int kPad = 3;

// One per window; hidden (zero cost) whenever no flash is fading.
class FlashOverlay : public QWidget
{
public:
    explicit FlashOverlay(QWidget *window)
        : QWidget(window)
    {
        setObjectName(QStringLiteral("qt_controlFlashOverlay"));
        ids::setIgnored(this);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);
        m_timer.setInterval(16);
        connect(&m_timer, &QTimer::timeout, this, [this] { tick(); });
        hide();
    }

    static FlashOverlay *forWindow(QWidget *window)
    {
        for (QObject *child : window->children()) {
            if (auto *overlay = dynamic_cast<FlashOverlay *>(child))
                return overlay;
        }
        return new FlashOverlay(window);
    }

    void add(const QRect &rectInWindow)
    {
        const QRect r = rectInWindow.adjusted(-kPad, -kPad, kPad, kPad);
        m_flashes.append({r, 0});
        if (!m_clock.isValid())
            m_clock.start();
        m_flashes.last().start = m_clock.elapsed();
        fitToFlashes();
        show();
        raise();
        update();
        if (!m_timer.isActive())
            m_timer.start();
    }

private:
    // The overlay covers only the flashes, so showing it never repaints the
    // whole window.
    void fitToFlashes()
    {
        QRect bounds;
        for (const Flash &f : std::as_const(m_flashes))
            bounds |= f.rect;
        setGeometry(bounds.adjusted(-2, -2, 2, 2) & parentWidget()->rect());
    }

protected:

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor accent = palette().color(QPalette::Highlight);
        const qint64 now = m_clock.elapsed();
        p.translate(-pos());
        for (const Flash &f : std::as_const(m_flashes)) {
            const qreal t = qBound<qreal>(0.0, qreal(now - f.start) / ControlFeedback::kFlashMs, 1.0);
            const qreal alpha = 1.0 - t * t;
            QColor stroke = accent;
            stroke.setAlphaF(0.95 * alpha);
            QColor fill = accent;
            fill.setAlphaF(0.18 * alpha);
            p.setPen(QPen(stroke, 2));
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(f.rect).adjusted(1, 1, -1, -1), 5, 5);
        }
    }

private:
    struct Flash
    {
        QRect rect;
        qint64 start;
    };

    void tick()
    {
        const qint64 now = m_clock.elapsed();
        const qsizetype before = m_flashes.size();
        for (qsizetype i = m_flashes.size() - 1; i >= 0; --i) {
            if (now - m_flashes.at(i).start >= ControlFeedback::kFlashMs)
                m_flashes.removeAt(i);
        }
        if (m_flashes.isEmpty()) {
            m_timer.stop();
            hide();
            return;
        }
        if (m_flashes.size() != before)
            fitToFlashes();
        update();
    }

    QVector<Flash> m_flashes;
    QElapsedTimer m_clock;
    QTimer m_timer;
};

} // namespace

ControlFeedback::ControlFeedback(QObject *parent)
    : QObject(parent)
{
}

void ControlFeedback::flash(QWidget *target, const QRect &rect)
{
    if (!target || !target->isVisible())
        return;
    QWidget *window = target->window();
    // Menus are short-lived popups: highlight is pointless there.
    if (!window || qobject_cast<QMenu *>(window))
        return;
    const QRect local = rect.isEmpty() ? target->rect() : rect;
    const QRect inWindow(target->mapTo(window, local.topLeft()), local.size());
    FlashOverlay::forWindow(window)->add(inWindow);
}

void ControlFeedback::flashAction(QAction *action)
{
    if (!action)
        return;
    // A toolbar button showing it?
    const QList<QObject *> owners = action->associatedObjects();
    for (QObject *o : owners) {
        if (auto *bar = qobject_cast<QToolBar *>(o)) {
            if (QWidget *button = bar->widgetForAction(action); button && button->isVisible()) {
                flash(button);
                return;
            }
        }
    }
    // Else the menu bar title of its top menu.
    for (QObject *o : owners) {
        auto *menu = qobject_cast<QMenu *>(o);
        if (!menu)
            continue;
        while (QMenu *parent = ids::parentMenuOf(menu))
            menu = parent;
        const QList<QObject *> menuOwners = menu->menuAction()->associatedObjects();
        for (QObject *mo : menuOwners) {
            if (auto *bar = qobject_cast<QMenuBar *>(mo)) {
                const QRect r = bar->actionGeometry(menu->menuAction());
                if (!r.isEmpty()) {
                    flash(bar, r);
                    return;
                }
            }
        }
    }
}

} // namespace control
