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

#include "control/ControlHttp.h"

using control::http::Request;
using control::http::RequestParser;

class TestControlHttp : public QObject
{
    Q_OBJECT

private slots:
    void getWithQuery();
    void postBodyArrivingInPieces();
    void pipelinedRequestsComeOutInOrder();
    void keepAliveDefaults();
    void malformedRequestLine();
    void oversizedHeadAndBody();
    void chunkedUploadIsRefused();
    void responseCarriesLengthAndConnection();
    void sseEventSplitsLines();
};

void TestControlHttp::getWithQuery()
{
    RequestParser p;
    QCOMPARE(p.feed("GET /ui/read?id=MainWindow%2Fx&depth=2 HTTP/1.1\r\nHost: 127.0.0.1:47821\r\n"
                    "Authorization: Bearer abc\r\n\r\n"),
             RequestParser::State::Complete);
    const Request r = p.take();
    QCOMPARE(r.method, QByteArray("GET"));
    QCOMPARE(r.path, QByteArray("/ui/read"));
    QCOMPARE(r.queryValue("id"), QStringLiteral("MainWindow/x"));
    QCOMPARE(r.queryValue("depth"), QStringLiteral("2"));
    QCOMPARE(r.header("host"), QByteArray("127.0.0.1:47821"));
    QCOMPARE(r.header("AUTHORIZATION"), QByteArray("Bearer abc"));
    QVERIFY(r.body.isEmpty());
}

void TestControlHttp::postBodyArrivingInPieces()
{
    RequestParser p;
    const QByteArray wire = "POST /actions/trigger HTTP/1.1\r\nHost: x\r\nContent-Length: 18\r\n\r\n{\"id\":\"actionNew\"}";
    // One byte at a time: the head terminator and the body straddle every boundary.
    for (int i = 0; i < wire.size() - 1; ++i)
        QCOMPARE(p.feed(wire.mid(i, 1)), RequestParser::State::NeedMore);
    QCOMPARE(p.feed(wire.right(1)), RequestParser::State::Complete);
    const Request r = p.take();
    QCOMPARE(r.method, QByteArray("POST"));
    QCOMPARE(r.body, QByteArray("{\"id\":\"actionNew\"}"));
}

void TestControlHttp::pipelinedRequestsComeOutInOrder()
{
    RequestParser p;
    QCOMPARE(p.feed("GET /a HTTP/1.1\r\nHost: x\r\n\r\nPOST /b HTTP/1.1\r\nHost: x\r\nContent-Length: 2\r\n\r\nhiGET /c"),
             RequestParser::State::Complete);
    QCOMPARE(p.take().path, QByteArray("/a"));
    QCOMPARE(p.parse(), RequestParser::State::Complete);
    const Request b = p.take();
    QCOMPARE(b.path, QByteArray("/b"));
    QCOMPARE(b.body, QByteArray("hi"));
    QCOMPARE(p.parse(), RequestParser::State::NeedMore);
    QCOMPARE(p.feed(" HTTP/1.1\r\nHost: x\r\n\r\n"), RequestParser::State::Complete);
    QCOMPARE(p.take().path, QByteArray("/c"));
}

void TestControlHttp::keepAliveDefaults()
{
    RequestParser p;
    p.feed("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    QVERIFY(p.take().keepAlive);
    p.feed("GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    QVERIFY(!p.take().keepAlive);
    p.feed("GET / HTTP/1.0\r\n\r\n");
    QVERIFY(!p.take().keepAlive);
    p.feed("GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n");
    QVERIFY(p.take().keepAlive);
}

void TestControlHttp::malformedRequestLine()
{
    RequestParser p;
    QCOMPARE(p.feed("HELLO\r\n\r\n"), RequestParser::State::Error);
    QCOMPARE(p.errorStatus(), 400);

    RequestParser q;
    QCOMPARE(q.feed("GET / HTTP/1.1\r\nContent-Length: -4\r\n\r\n"), RequestParser::State::Error);
    QCOMPARE(q.errorStatus(), 400);

    RequestParser r;
    QCOMPARE(r.feed("GET / HTTP/1.1\r\nno colon here\r\n\r\n"), RequestParser::State::Error);
    QCOMPARE(r.errorStatus(), 400);
}

void TestControlHttp::oversizedHeadAndBody()
{
    RequestParser p;
    // A head that never ends is refused once it passes the limit, without waiting for more.
    QCOMPARE(p.feed("GET / HTTP/1.1\r\nX: " + QByteArray(RequestParser::kMaxHeadBytes, 'a')),
             RequestParser::State::Error);
    QCOMPARE(p.errorStatus(), 431);

    RequestParser q;
    QCOMPARE(q.feed("POST / HTTP/1.1\r\nContent-Length: " + QByteArray::number(RequestParser::kMaxBodyBytes + 1)
                    + "\r\n\r\n"),
             RequestParser::State::Error);
    QCOMPARE(q.errorStatus(), 413);
}

void TestControlHttp::chunkedUploadIsRefused()
{
    RequestParser p;
    QCOMPARE(p.feed("POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n"), RequestParser::State::Error);
    QCOMPARE(p.errorStatus(), 411);
}

void TestControlHttp::responseCarriesLengthAndConnection()
{
    const QByteArray r = control::http::response(404, "application/json", "{\"error\":\"x\"}", false);
    QVERIFY(r.startsWith("HTTP/1.1 404 Not Found\r\n"));
    QVERIFY(r.contains("\r\nContent-Length: 13\r\n"));
    QVERIFY(r.contains("\r\nContent-Type: application/json\r\n"));
    QVERIFY(r.contains("\r\nConnection: close\r\n"));
    QVERIFY(r.contains("\r\nCache-Control: no-store\r\n"));
    QVERIFY(r.endsWith("\r\n\r\n{\"error\":\"x\"}"));

    QVERIFY(control::http::response(200, "text/plain", "ok", true).contains("\r\nConnection: keep-alive\r\n"));
}

void TestControlHttp::sseEventSplitsLines()
{
    QCOMPARE(control::http::sseEvent("dialog.opened", "{\"a\":1}\nsecond"),
             QByteArray("event: dialog.opened\ndata: {\"a\":1}\ndata: second\n\n"));
    QVERIFY(control::http::sseHead().contains("Content-Type: text/event-stream"));
}

QTEST_APPLESS_MAIN(TestControlHttp)
#include "test_control_http.moc"
