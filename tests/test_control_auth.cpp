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

#include "control/ControlAuth.h"

using namespace control::auth;

class TestControlAuth : public QObject
{
    Q_OBJECT

private slots:
    void tokensAre256BitBase64Url();
    void bearerMatch();
    void localHostPolicy();
    void lanHostPolicy();
    void originPolicy();
};

void TestControlAuth::tokensAre256BitBase64Url()
{
    const QString a = generateToken();
    const QString b = generateToken();
    QCOMPARE(a.size(), 43); // 32 bytes, base64url without padding
    QVERIFY(a != b);
    static const QRegularExpression alphabet(QStringLiteral("^[A-Za-z0-9_-]+$"));
    QVERIFY(alphabet.match(a).hasMatch());
}

void TestControlAuth::bearerMatch()
{
    QVERIFY(bearerMatches("Bearer abc123", "abc123"));
    QVERIFY(bearerMatches("bearer   abc123  ", "abc123"));
    QVERIFY(!bearerMatches("Bearer abc124", "abc123"));
    QVERIFY(!bearerMatches("Bearer abc12", "abc123"));
    QVERIFY(!bearerMatches("Basic abc123", "abc123"));
    QVERIFY(!bearerMatches("", "abc123"));
    QVERIFY(!bearerMatches("Bearer ", "")); // an empty token never authorises
}

void TestControlAuth::localHostPolicy()
{
    const HostPolicy p = HostPolicy::local(47821);
    QVERIFY(p.hostAllowed("127.0.0.1:47821"));
    QVERIFY(p.hostAllowed("localhost:47821"));
    QVERIFY(p.hostAllowed("LOCALHOST:47821"));
    QVERIFY(p.hostAllowed("[::1]:47821"));
    QVERIFY(!p.hostAllowed("127.0.0.1:47822"));
    QVERIFY(!p.hostAllowed("127.0.0.1"));
    QVERIFY(!p.hostAllowed("evil.example:47821")); // DNS rebinding
    QVERIFY(!p.hostAllowed(""));
    QVERIFY(!p.hostAllowed("192.168.1.5:47821"));
}

void TestControlAuth::lanHostPolicy()
{
    HostPolicy p = HostPolicy::local(47821);
    p.addAddresses({QStringLiteral("192.168.1.5"), QStringLiteral("fe80::1")});
    QVERIFY(p.hostAllowed("192.168.1.5:47821"));
    QVERIFY(p.hostAllowed("[fe80::1]:47821"));
    QVERIFY(p.hostAllowed("127.0.0.1:47821"));
    QVERIFY(!p.hostAllowed("192.168.1.6:47821"));
}

void TestControlAuth::originPolicy()
{
    const HostPolicy p = HostPolicy::local(47821);
    QVERIFY(p.originAllowed(""));
    QVERIFY(p.originAllowed("http://127.0.0.1:47821"));
    QVERIFY(p.originAllowed("http://localhost:47821"));
    QVERIFY(!p.originAllowed("https://example.com"));
    QVERIFY(!p.originAllowed("null"));
    QVERIFY(!p.originAllowed("http://127.0.0.1:3000"));
}

QTEST_APPLESS_MAIN(TestControlAuth)
#include "test_control_auth.moc"
