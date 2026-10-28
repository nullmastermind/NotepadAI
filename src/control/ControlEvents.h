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

// The event hub (one per ControlApi). While a listener exists it also watches
// windows opening and closing, docks showing and hiding, and focus moving;
// with none it has no event filter and no connections.
class ControlEvents : public QObject
{
    Q_OBJECT

public:
    // Returns true once satisfied: the waiter is then dropped.
    using Waiter = std::function<bool(const QString &name, const QJsonObject &data)>;

    explicit ControlEvents(QObject *parent = nullptr);
    ~ControlEvents() override;

    bool isActive() const { return m_active; }
    // Whether a widget is open even while hidden (a dock behind another tab).
    // dock.shown / dock.hidden carry it as `open`.
    void setPresence(std::function<bool(const QWidget *)> presence) { m_presence = std::move(presence); }
    void setSubscribers(int count);
    quint64 addWaiter(Waiter waiter);
    void removeWaiter(quint64 id);
    void clearWaiters();
    void publish(const QString &name, const QJsonObject &data);

signals:
    // An SSE frame for the server's subscribers.
    void frameReady(const QByteArray &frame);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateActive();
    void onFocusChanged(QWidget *old, QWidget *now);

    int m_subscribers = 0;
    bool m_active = false;
    quint64 m_seq = 0;
    quint64 m_nextWaiter = 1;
    QHash<quint64, Waiter> m_waiters;
    std::function<bool(const QWidget *)> m_presence;
    QMetaObject::Connection m_focusConnection;
};

} // namespace control

#endif // CONTROL_EVENTS_H
