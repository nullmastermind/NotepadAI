/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "control/ControlServer.h"

using control::ControlServer;

namespace {

const QString kToken = QStringLiteral("tok-123");

struct Reply
{
    int status = 0;
    QByteArray head;
    QByteArray body;
};

// Reads one response (Content-Length framed) from the socket.
Reply readReply(QTcpSocket &s, QByteArray &buffer)
{
    Reply r;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 5000) {
        const qsizetype headEnd = buffer.indexOf("\r\n\r\n");
        if (headEnd >= 0) {
            r.head = buffer.left(headEnd);
            r.status = r.head.mid(9, 3).toInt();
            qsizetype len = 0;
            const qsizetype cl = r.head.indexOf("Content-Length: ");
            if (cl >= 0)
                len = r.head.mid(cl + 16, r.head.indexOf("\r\n", cl) - cl - 16).toLongLong();
            if (buffer.size() >= headEnd + 4 + len) {
                r.body = buffer.mid(headEnd + 4, len);
                buffer.remove(0, headEnd + 4 + len);
                return r;
            }
        }
        if (s.state() != QAbstractSocket::ConnectedState && s.bytesAvailable() == 0)
            break;
        s.waitForReadyRead(100);
        buffer += s.readAll();
    }
    return r;
}

QByteArray get(quint16 port, const QByteArray &path, const QByteArray &extra = {})
{
    return "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\n" + extra + "\r\n";
}

QByteArray bearer() { return "Authorization: Bearer " + kToken.toLatin1() + "\r\n"; }

} // namespace

class TestControlServer : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void rejectsMissingTokenAndForeignHosts();
    void servesSkillWithoutTheGuiThread();
    void roundTripsThroughTheGuiThread();
    void takesTheNextPortWhenBusy();
    void regeneratedTokenRevokesTheOldOne();
    void stopClosesConnections();
    void eventsReachSubscribers();

private:
    bool startServer(quint16 port = 0);

    ControlServer *m_server = nullptr;
    QList<QByteArray> m_paths;
};

bool TestControlServer::startServer(quint16 port)
{
    ControlServer::Config config;
    config.port = port;
    config.token = kToken;
    config.skill = "---\nname: notepadai-control\n---\n";
    config.skillVersion = "{\"apiVersion\":1}";
    return m_server->start(config);
}

void TestControlServer::init()
{
    m_server = new ControlServer;
    m_paths.clear();
    connect(m_server, &ControlServer::requestReceived, this,
            [this](quint64 id, const control::http::Request &r) {
                // Proves the handler runs on the thread that owns the server.
                QCOMPARE(QThread::currentThread(), thread());
                m_paths.append(r.path);
                m_server->reply(id, 200, "application/json", "{\"path\":\"" + r.path + "\",\"body\":" +
                                (r.body.isEmpty() ? QByteArray("null") : r.body) + "}");
            });
}

void TestControlServer::cleanup()
{
    delete m_server;
    m_server = nullptr;
}

void TestControlServer::rejectsMissingTokenAndForeignHosts()
{
    QVERIFY(startServer());
    const quint16 port = m_server->port();
    QVERIFY(port != 0);

    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(s.waitForConnected(2000));
        QByteArray buf;
        s.write(get(port, "/instance"));
        QCOMPARE(readReply(s, buf).status, 401);
    }
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(s.waitForConnected(2000));
        QByteArray buf;
        s.write("GET /instance HTTP/1.1\r\nHost: evil.example:" + QByteArray::number(port) + "\r\n" + bearer() + "\r\n");
        QCOMPARE(readReply(s, buf).status, 403);
    }
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port);
        QVERIFY(s.waitForConnected(2000));
        QByteArray buf;
        s.write(get(port, "/instance", bearer() + "Origin: https://example.com\r\n"));
        QCOMPARE(readReply(s, buf).status, 403);
    }
    QVERIFY(m_paths.isEmpty()); // nothing reached the GUI thread
}

void TestControlServer::servesSkillWithoutTheGuiThread()
{
    QVERIFY(startServer());
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, m_server->port());
    QVERIFY(s.waitForConnected(2000));
    QByteArray buf;
    s.write(get(m_server->port(), "/skill", bearer()));
    // Read without spinning this thread's event loop: the worker answers alone.
    const Reply r = readReply(s, buf);
    QCOMPARE(r.status, 200);
    QVERIFY(r.head.contains("text/markdown"));
    QVERIFY(r.body.startsWith("---\nname: notepadai-control"));
    QVERIFY(m_paths.isEmpty());
}

void TestControlServer::roundTripsThroughTheGuiThread()
{
    QVERIFY(startServer());
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, m_server->port());
    QVERIFY(s.waitForConnected(2000));
    // Two pipelined requests on one keep-alive connection come back in order.
    s.write("POST /actions/trigger HTTP/1.1\r\nHost: localhost:" + QByteArray::number(m_server->port()) + "\r\n"
            + bearer() + "Content-Length: 11\r\n\r\n{\"id\":\"a\"}\n"
            + get(m_server->port(), "/instance", bearer()));
    QByteArray buf;
    // The second request reaches the GUI thread only after the first is answered.
    QTRY_COMPARE(m_paths.size(), 2);
    QCOMPARE(m_paths, (QList<QByteArray>{"/actions/trigger", "/instance"}));
    QTRY_VERIFY((buf += s.readAll(), buf.contains("/instance")));
    const Reply first = readReply(s, buf);
    QCOMPARE(first.status, 200);
    QCOMPARE(first.body, QByteArray("{\"path\":\"/actions/trigger\",\"body\":{\"id\":\"a\"}\n}"));
    QCOMPARE(readReply(s, buf).body, QByteArray("{\"path\":\"/instance\",\"body\":null}"));
}

void TestControlServer::takesTheNextPortWhenBusy()
{
    QTcpServer squatter;
    QVERIFY(squatter.listen(QHostAddress::LocalHost, 0));
    const quint16 busy = squatter.serverPort();
    if (busy == 65535)
        QSKIP("ephemeral port at the top of the range");
    QVERIFY(startServer(busy));
    QVERIFY(m_server->port() > busy);
    QVERIFY(m_server->port() <= busy + 20);
}

void TestControlServer::regeneratedTokenRevokesTheOldOne()
{
    QVERIFY(startServer());
    m_server->setToken(QStringLiteral("new-token"));
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, m_server->port());
    QVERIFY(s.waitForConnected(2000));
    QByteArray buf;
    s.write(get(m_server->port(), "/skill", bearer()));
    QCOMPARE(readReply(s, buf).status, 401);

    QTcpSocket t;
    t.connectToHost(QHostAddress::LocalHost, m_server->port());
    QVERIFY(t.waitForConnected(2000));
    QByteArray buf2;
    t.write(get(m_server->port(), "/skill", "Authorization: Bearer new-token\r\n"));
    QCOMPARE(readReply(t, buf2).status, 200);
}

void TestControlServer::stopClosesConnections()
{
    QVERIFY(startServer());
    const quint16 port = m_server->port();
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, port);
    QVERIFY(s.waitForConnected(2000));
    m_server->stop();
    QVERIFY(!m_server->isListening());
    QTRY_COMPARE(s.state(), QAbstractSocket::UnconnectedState);

    QTcpServer probe; // the port is free again
    QVERIFY(probe.listen(QHostAddress::LocalHost, port));
}

void TestControlServer::eventsReachSubscribers()
{
    QVERIFY(startServer());
    QSignalSpy count(m_server, &ControlServer::subscriberCountChanged);
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, m_server->port());
    QVERIFY(s.waitForConnected(2000));
    s.write(get(m_server->port(), "/events", bearer()));
    QTRY_COMPARE(count.size(), 1);
    QCOMPARE(count.at(0).at(0).toInt(), 1);

    m_server->publishEvent("event: focus.changed\ndata: {}\n\n");
    QByteArray buf;
    QTRY_VERIFY((buf += s.readAll(), buf.contains("event: focus.changed\ndata: {}\n\n")));
    QVERIFY(buf.startsWith("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream"));

    s.disconnectFromHost();
    QTRY_COMPARE(count.size(), 2);
    QCOMPARE(count.at(1).at(0).toInt(), 0);
}

QTEST_GUILESS_MAIN(TestControlServer)
#include "test_control_server.moc"
