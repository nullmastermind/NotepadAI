/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlServer.h"

#include "ControlAuth.h"

#include <QHash>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

namespace control {

namespace {

constexpr int kMaxConnections = 64;
// Qt stops reading a socket at this much unread input, so a client the head
// check refuses has buffered at most this much of its body.
constexpr qint64 kReadBufferBytes = 64 * 1024;
// An /events subscriber this far behind is not reading: drop it rather than
// queue every frame for it.
constexpr qint64 kMaxEventBacklogBytes = 1024 * 1024;

QByteArray errorBody(const QByteArray &message)
{
    QByteArray escaped = message;
    escaped.replace('\\', "\\\\").replace('"', "\\\"");
    return "{\"ok\":false,\"error\":\"" + escaped + "\"}";
}

} // namespace

class ControlServerWorker : public QObject
{
public:
    explicit ControlServerWorker(ControlServer *front)
        : m_front(front)
    {
    }

    ~ControlServerWorker() override { closeAll(); }

    bool listen(const ControlServer::Config &config, quint16 *boundPort, QString *error)
    {
        m_server = new QTcpServer(this);
        m_server->setMaxPendingConnections(kMaxConnections);
        const int attempts = qMax(1, config.portAttempts);
        for (int i = 0; i < attempts; ++i) {
            const int candidate = int(config.port) + i;
            if (candidate > 65535)
                break;
            if (m_server->listen(config.bindAddress, quint16(candidate)))
                break;
        }
        if (!m_server->isListening()) {
            if (error)
                *error = m_server->errorString();
            delete m_server;
            m_server = nullptr;
            return false;
        }
        *boundPort = m_server->serverPort();
        m_token = config.token.toLatin1();
        m_policy = auth::HostPolicy::local(*boundPort);
        m_policy.addAddresses(config.hostAddresses);
        m_skill = config.skill;
        m_skillVersion = config.skillVersion;
        QObject::connect(m_server, &QTcpServer::newConnection, this, [this] { acceptPending(); });
        return true;
    }

    void setToken(const QByteArray &token) { m_token = token; }

    void reply(quint64 id, int status, const QByteArray &contentType, const QByteArray &body)
    {
        Conn *c = m_pending.take(id);
        if (!c)
            return;
        c->pendingId = 0;
        c->socket->write(http::response(status, contentType, body, c->keepAlive));
        if (!c->keepAlive) {
            close(c);
            return;
        }
        process(c);
    }

    void publish(const QByteArray &frame)
    {
        QList<QTcpSocket *> stalled;
        for (Conn *c : std::as_const(m_conns)) {
            if (!c->sse)
                continue;
            if (c->socket->bytesToWrite() > kMaxEventBacklogBytes)
                stalled.append(c->socket);
            else
                c->socket->write(frame);
        }
        // abort() emits disconnected (drop() edits m_conns): after the loop.
        for (QTcpSocket *s : std::as_const(stalled))
            s->abort();
    }

    void closeAll()
    {
        const QList<Conn *> conns = m_conns.values();
        m_conns.clear();
        m_pending.clear();
        for (Conn *c : conns) {
            c->socket->disconnect(this);
            c->socket->abort();
            c->socket->deleteLater();
            delete c;
        }
        if (m_server) {
            m_server->close();
            delete m_server;
            m_server = nullptr;
        }
        notifySubscribers();
    }

private:
    struct Conn
    {
        QTcpSocket *socket = nullptr;
        http::RequestParser parser;
        quint64 pendingId = 0;
        bool keepAlive = true;
        bool sse = false;
        bool closing = false;
        // The current request's head passed admit().
        bool admitted = false;
    };

    void acceptPending()
    {
        while (QTcpSocket *s = m_server->nextPendingConnection()) {
            if (m_conns.size() >= kMaxConnections) {
                s->abort();
                s->deleteLater();
                continue;
            }
            s->setReadBufferSize(kReadBufferBytes);
            auto *c = new Conn;
            c->socket = s;
            m_conns.insert(s, c);
            QObject::connect(s, &QTcpSocket::readyRead, this, [this, s] {
                if (Conn *conn = m_conns.value(s))
                    process(conn);
            });
            QObject::connect(s, &QTcpSocket::disconnected, this, [this, s] { drop(s); });
        }
    }

    void drop(QTcpSocket *s)
    {
        Conn *c = m_conns.take(s);
        if (!c)
            return;
        if (c->pendingId)
            m_pending.remove(c->pendingId);
        const bool wasSse = c->sse;
        delete c;
        s->deleteLater();
        if (wasSse)
            notifySubscribers();
    }

    void notifySubscribers()
    {
        int n = 0;
        for (Conn *c : std::as_const(m_conns))
            n += c->sse ? 1 : 0;
        if (n == m_subscribers)
            return;
        m_subscribers = n;
        QPointer<ControlServer> front = m_front;
        QMetaObject::invokeMethod(m_front, [front, n] {
            if (front)
                emit front->subscriberCountChanged(n);
        }, Qt::QueuedConnection);
    }

    void close(Conn *c)
    {
        c->closing = true;
        c->socket->disconnectFromHost();
    }

    void reject(Conn *c, int status, const QByteArray &reason)
    {
        c->socket->write(http::response(status, "application/json", errorBody(reason), false));
        close(c);
        QPointer<ControlServer> front = m_front;
        const QString text = QString::fromLatin1(reason);
        QMetaObject::invokeMethod(m_front, [front, status, text] {
            if (front)
                emit front->requestRejected(status, text);
        }, Qt::QueuedConnection);
    }

    // Parses what the socket holds and handles requests until one has to
    // wait for the GUI thread.
    void process(Conn *c)
    {
        while (c->pendingId == 0 && !c->sse && !c->closing) {
            http::RequestParser::State state = c->parser.parse();
            if (state == http::RequestParser::State::NeedMore) {
                const QByteArray bytes = c->socket->readAll();
                if (bytes.isEmpty())
                    return;
                state = c->parser.feed(bytes);
            }
            if (state == http::RequestParser::State::Error) {
                reject(c, c->parser.errorStatus(), c->parser.errorReason());
                return;
            }
            // Vet the head before reading the body: an unauthenticated client
            // never gets to make the server buffer it.
            if (!c->admitted && c->parser.hasHead()) {
                if (!admit(c, c->parser.head()))
                    return;
                c->admitted = true;
            }
            if (state == http::RequestParser::State::NeedMore)
                continue;
            c->admitted = false;
            handle(c, c->parser.take());
        }
    }

    bool admit(Conn *c, const http::Request &head)
    {
        if (!m_policy.hostAllowed(head.header("host"))) {
            reject(c, 403, "Host header does not name this server");
            return false;
        }
        if (!m_policy.originAllowed(head.header("origin"))) {
            reject(c, 403, "cross-origin requests are not allowed");
            return false;
        }
        if (!auth::bearerMatches(head.header("authorization"), m_token)) {
            reject(c, 401, "missing or wrong bearer token");
            return false;
        }
        return true;
    }

    void handle(Conn *c, http::Request &&request)
    {
        c->keepAlive = request.keepAlive;

        if (request.method == "GET" && request.path == "/skill") {
            c->socket->write(http::response(200, "text/markdown; charset=utf-8", m_skill, c->keepAlive));
            if (!c->keepAlive)
                close(c);
            return;
        }
        if (request.method == "GET" && request.path == "/skill/version") {
            c->socket->write(http::response(200, "application/json", m_skillVersion, c->keepAlive));
            if (!c->keepAlive)
                close(c);
            return;
        }
        if (request.method == "GET" && request.path == "/events") {
            c->sse = true;
            c->socket->write(http::sseHead());
            c->socket->write(": connected\n\n");
            notifySubscribers();
            return;
        }

        const quint64 id = ++m_nextId;
        c->pendingId = id;
        m_pending.insert(id, c);
        QPointer<ControlServer> front = m_front;
        QMetaObject::invokeMethod(m_front, [front, id, request = std::move(request)] {
            if (front)
                emit front->requestReceived(id, request);
        }, Qt::QueuedConnection);
    }

    ControlServer *m_front;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Conn *> m_conns;
    QHash<quint64, Conn *> m_pending;
    quint64 m_nextId = 0;
    QByteArray m_token;
    auth::HostPolicy m_policy;
    QByteArray m_skill;
    QByteArray m_skillVersion;
    int m_subscribers = 0;
};

ControlServer::ControlServer(QObject *parent)
    : QObject(parent)
{
}

ControlServer::~ControlServer()
{
    stop();
}

bool ControlServer::start(const Config &config, QString *error)
{
    stop();
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("ControlServer"));
    m_thread->start();
    m_worker = new ControlServerWorker(this);
    m_worker->moveToThread(m_thread);

    bool ok = false;
    quint16 bound = 0;
    QString err;
    QMetaObject::invokeMethod(m_worker, [&] { ok = m_worker->listen(config, &bound, &err); },
                              Qt::BlockingQueuedConnection);
    if (!ok) {
        if (error)
            *error = err;
        stop();
        return false;
    }
    m_port = bound;
    return true;
}

void ControlServer::stop()
{
    if (!m_thread)
        return;
    ControlServerWorker *worker = m_worker;
    m_worker = nullptr;
    // The worker owns sockets with affinity to its thread: delete it there.
    QMetaObject::invokeMethod(worker, [worker] { delete worker; }, Qt::BlockingQueuedConnection);
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    m_port = 0;
}

void ControlServer::setToken(const QString &token)
{
    if (!m_worker)
        return;
    ControlServerWorker *worker = m_worker;
    const QByteArray bytes = token.toLatin1();
    QMetaObject::invokeMethod(worker, [worker, bytes] { worker->setToken(bytes); }, Qt::QueuedConnection);
}

void ControlServer::reply(quint64 requestId, int status, const QByteArray &contentType, const QByteArray &body)
{
    if (!m_worker)
        return;
    ControlServerWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, requestId, status, contentType, body] {
        worker->reply(requestId, status, contentType, body);
    }, Qt::QueuedConnection);
}

void ControlServer::publishEvent(const QByteArray &frame)
{
    if (!m_worker)
        return;
    ControlServerWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, frame] { worker->publish(frame); }, Qt::QueuedConnection);
}

} // namespace control
