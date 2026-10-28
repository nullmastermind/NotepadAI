/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_SERVER_H
#define CONTROL_SERVER_H

#include "ControlHttp.h"

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QStringList>

class QThread;

namespace control {

class ControlServerWorker;

// The Control API's sockets. Accepting, parsing, authentication, the static
// skill and server-sent events all run on a worker thread; only authenticated
// requests reach the GUI thread (requestReceived), and their replies go back
// by id. One request in flight per connection.
class ControlServer : public QObject
{
    Q_OBJECT

public:
    struct Config
    {
        QHostAddress bindAddress = QHostAddress::LocalHost;
        quint16 port = 47821;
        // Ports tried after `port` when it is taken (another instance).
        int portAttempts = 20;
        QString token;
        // Extra addresses a client may name in Host (LAN mode).
        QStringList hostAddresses;
        QByteArray skill;        // GET /skill (text/markdown)
        QByteArray skillVersion; // GET /skill/version (application/json)
    };

    explicit ControlServer(QObject *parent = nullptr);
    ~ControlServer() override;

    // Binds on the worker (blocks the caller for the bind only). False and
    // `error` when no port in the range could be bound.
    bool start(const Config &config, QString *error = nullptr);
    void stop();
    bool isListening() const { return m_port != 0; }
    quint16 port() const { return m_port; }

    void setToken(const QString &token);

    // Thread-safe. Unknown or closed ids are dropped.
    void reply(quint64 requestId, int status, const QByteArray &contentType, const QByteArray &body);
    // One pre-serialised SSE frame to every /events subscriber.
    void publishEvent(const QByteArray &frame);

signals:
    void requestReceived(quint64 requestId, const control::http::Request &request);
    void subscriberCountChanged(int count);
    void requestRejected(int status, const QString &reason);

private:
    friend class ControlServerWorker;

    QThread *m_thread = nullptr;
    ControlServerWorker *m_worker = nullptr;
    quint16 m_port = 0;
};

} // namespace control

#endif // CONTROL_SERVER_H
