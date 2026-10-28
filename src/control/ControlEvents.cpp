/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlEvents.h"

#include "ControlEffects.h"
#include "ControlHttp.h"
#include "ControlIds.h"

#include <QApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QWidget>

namespace control {

namespace {

ControlEvents *g_hub = nullptr;

QJsonObject windowData(const QWidget *w)
{
    QJsonObject o{{QStringLiteral("id"), ids::nodeId(w).id}, {QStringLiteral("kind"), rootKind(w)}};
    const QString title = rootTitle(w);
    if (!title.isEmpty())
        o.insert(QStringLiteral("title"), title);
    if (w->isModal())
        o.insert(QStringLiteral("modal"), true);
    return o;
}

} // namespace

bool events::wanted()
{
    return g_hub && g_hub->isActive();
}

void events::publish(const QString &name, const QJsonObject &data)
{
    if (wanted())
        g_hub->publish(name, data);
}

ControlEvents::ControlEvents(QObject *parent)
    : QObject(parent)
{
    g_hub = this;
}

ControlEvents::~ControlEvents()
{
    if (m_active)
        qApp->removeEventFilter(this);
    if (g_hub == this)
        g_hub = nullptr;
}

void ControlEvents::setSubscribers(int count)
{
    m_subscribers = qMax(0, count);
    updateActive();
}

quint64 ControlEvents::addWaiter(Waiter waiter)
{
    const quint64 id = m_nextWaiter++;
    m_waiters.emplace(id, std::move(waiter));
    updateActive();
    return id;
}

void ControlEvents::removeWaiter(quint64 id)
{
    if (m_waiters.remove(id))
        updateActive();
}

void ControlEvents::clearWaiters()
{
    m_waiters.clear();
    updateActive();
}

void ControlEvents::updateActive()
{
    const bool active = m_subscribers > 0 || !m_waiters.isEmpty();
    if (active == m_active)
        return;
    m_active = active;
    if (active) {
        qApp->installEventFilter(this);
        m_focusConnection = connect(qApp, &QApplication::focusChanged, this, &ControlEvents::onFocusChanged);
    } else {
        qApp->removeEventFilter(this);
        disconnect(m_focusConnection);
    }
}

void ControlEvents::publish(const QString &name, const QJsonObject &data)
{
    if (!m_active)
        return;
    if (m_subscribers > 0) {
        QJsonObject payload = data;
        payload.insert(QStringLiteral("seq"), qint64(++m_seq));
        payload.insert(QStringLiteral("time"), QDateTime::currentMSecsSinceEpoch());
        emit frameReady(http::sseEvent(name.toUtf8(), QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    }
    if (m_waiters.isEmpty())
        return;
    // A waiter may finish (and remove itself) while we walk: walk a copy of the ids.
    const QList<quint64> ids = m_waiters.keys();
    for (quint64 id : ids) {
        const auto it = m_waiters.constFind(id);
        if (it != m_waiters.constEnd() && (*it)(name, data))
            removeWaiter(id);
    }
}

bool ControlEvents::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if ((type == QEvent::Show || type == QEvent::Hide) && watched->isWidgetType()) {
        auto *w = static_cast<QWidget *>(watched);
        if (w->isWindow()) {
            const Qt::WindowType wt = w->windowType();
            if (wt != Qt::ToolTip && wt != Qt::SplashScreen && !ids::isIgnored(w))
                publish(type == QEvent::Show ? QStringLiteral("window.opened") : QStringLiteral("window.closed"),
                        windowData(w));
        } else if (w->inherits("ads::CDockWidget")) {
            QJsonObject data = windowData(w);
            if (type == QEvent::Show) {
                // It is its area's current tab.
                data.insert(QStringLiteral("open"), true);
                data.insert(QStringLiteral("current"), true);
                publish(QStringLiteral("dock.shown"), data);
            } else {
                // Closed, or another tab of its area became current. ADS hides
                // a closing tab before marking it closed (and deletes it
                // later), so ask once the close has run.
                data.insert(QStringLiteral("current"), false);
                QMetaObject::invokeMethod(this, [this, dock = QPointer<QWidget>(w), data]() mutable {
                    data.insert(QStringLiteral("open"), dock && (m_presence ? m_presence(dock) : !dock->isHidden()));
                    publish(QStringLiteral("dock.hidden"), data);
                }, Qt::QueuedConnection);
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void ControlEvents::onFocusChanged(QWidget *, QWidget *now)
{
    if (now && !ids::isInsideIgnored(now))
        publish(QStringLiteral("focus.changed"), {{QStringLiteral("id"), ids::nodeId(now).id}});
}

} // namespace control
