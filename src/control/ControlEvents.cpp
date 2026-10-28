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

#include "ControlHttp.h"

#include <QDateTime>
#include <QJsonDocument>

namespace control {

namespace {

ControlEvents *g_hub = nullptr;

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
    m_active = m_subscribers > 0 || !m_waiters.isEmpty();
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

} // namespace control
