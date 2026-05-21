/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtTest>
#include <QJsonObject>

#include "AcpProtocol.h"

using AcpProtocol::AcpContentBlock;
using AcpProtocol::AcpPermissionOption;
using AcpProtocol::AcpPermissionRequest;

class TestAcpProtocolSerialization : public QObject
{
    Q_OBJECT

private slots:
    void textContentBlockRoundtrip();
    void imageContentBlockRoundtrip();
    void permissionOptionRoundtrip();
    void permissionRequestRoundtrip();
};

void TestAcpProtocolSerialization::textContentBlockRoundtrip()
{
    AcpContentBlock b;
    b.kind = AcpContentBlock::Kind::Text;
    b.text = QStringLiteral("hello world");
    const QJsonObject json = AcpProtocol::contentBlockToJson(b);
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    AcpContentBlock decoded = AcpProtocol::contentBlockFromJson(json);
    QCOMPARE(int(decoded.kind), int(AcpContentBlock::Kind::Text));
    QCOMPARE(decoded.text, QStringLiteral("hello world"));
}

void TestAcpProtocolSerialization::imageContentBlockRoundtrip()
{
    AcpContentBlock b;
    b.kind = AcpContentBlock::Kind::Image;
    b.imageData = QByteArray("\x89PNG\r\n\x1a\n", 8);
    b.mimeType = QStringLiteral("image/png");
    const QJsonObject json = AcpProtocol::contentBlockToJson(b);
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("image"));
    QCOMPARE(json.value(QStringLiteral("mimeType")).toString(), QStringLiteral("image/png"));
    // base64 of the PNG signature.
    const QString b64 = QString::fromLatin1(b.imageData.toBase64());
    QCOMPARE(json.value(QStringLiteral("data")).toString(), b64);
    AcpContentBlock decoded = AcpProtocol::contentBlockFromJson(json);
    QCOMPARE(int(decoded.kind), int(AcpContentBlock::Kind::Image));
    QCOMPARE(decoded.imageData, b.imageData);
    QCOMPARE(decoded.mimeType, b.mimeType);
}

void TestAcpProtocolSerialization::permissionOptionRoundtrip()
{
    AcpPermissionOption o;
    o.id = QStringLiteral("opt-1");
    o.label = QStringLiteral("Allow");
    o.kind = QStringLiteral("allow_once");
    const QJsonObject json = AcpProtocol::permissionOptionToJson(o);
    AcpPermissionOption decoded = AcpProtocol::permissionOptionFromJson(json);
    QCOMPARE(decoded.id, o.id);
    QCOMPARE(decoded.label, o.label);
    QCOMPARE(decoded.kind, o.kind);
}

void TestAcpProtocolSerialization::permissionRequestRoundtrip()
{
    AcpPermissionRequest r;
    r.requestId = QStringLiteral("42");
    r.title = QStringLiteral("Run command?");
    r.description = QStringLiteral("rm -rf /");
    AcpPermissionOption a;
    a.id = QStringLiteral("a");
    a.label = QStringLiteral("Allow once");
    a.kind = QStringLiteral("allow_once");
    AcpPermissionOption d;
    d.id = QStringLiteral("d");
    d.label = QStringLiteral("Deny");
    d.kind = QStringLiteral("deny");
    r.options = {a, d};
    const QJsonObject json = AcpProtocol::permissionRequestToJson(r);
    AcpPermissionRequest decoded = AcpProtocol::permissionRequestFromJson(json);
    QCOMPARE(decoded.requestId, r.requestId);
    QCOMPARE(decoded.title, r.title);
    QCOMPARE(decoded.description, r.description);
    QCOMPARE(decoded.options.size(), 2);
    QCOMPARE(decoded.options.at(0).id, QStringLiteral("a"));
    QCOMPARE(decoded.options.at(0).kind, QStringLiteral("allow_once"));
    QCOMPARE(decoded.options.at(1).id, QStringLiteral("d"));
}

QTEST_GUILESS_MAIN(TestAcpProtocolSerialization)
#include "test_acp_protocol_serialization.moc"
