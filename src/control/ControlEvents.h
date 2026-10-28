/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_EVENTS_H
#define CONTROL_EVENTS_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

namespace control {

namespace events {
// Whether anyone listens: an /events stream or a /wait for an event. Callers
// check it before building an event, so a listener-less app pays one load.
bool wanted();
// Sends `name` with `data` to every listener; no-op when nobody listens.
void publish(const QString &name, const QJsonObject &data = {});
} // namespace events

// The event hub (one per ControlApi). Domain code publishes where things
// happen (a tab opened, a chat turn ended, a task exited); with no listener
// publishing costs one load.
class ControlEvents : public QObject
{
    Q_OBJECT

public:
    // Returns true once satisfied: the waiter is then dropped.
    using Waiter = std::function<bool(const QString &name, const QJsonObject &data)>;

    explicit ControlEvents(QObject *parent = nullptr);
    ~ControlEvents() override;

    bool isActive() const { return m_active; }
    void setSubscribers(int count);
    quint64 addWaiter(Waiter waiter);
    void removeWaiter(quint64 id);
    void clearWaiters();
    void publish(const QString &name, const QJsonObject &data);

signals:
    // An SSE frame for the server's subscribers.
    void frameReady(const QByteArray &frame);

private:
    void updateActive();

    int m_subscribers = 0;
    bool m_active = false;
    quint64 m_seq = 0;
    quint64 m_nextWaiter = 1;
    QHash<quint64, Waiter> m_waiters;
};

} // namespace control

#endif // CONTROL_EVENTS_H
