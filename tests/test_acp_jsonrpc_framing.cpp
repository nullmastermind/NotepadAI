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
#include <QByteArray>

#include "AcpProtocol.h"

class TestAcpJsonRpcFraming : public QObject
{
    Q_OBJECT

private slots:
    void singleFrame();
    void twoFramesInOneChunk();
    void partialTrailing();
    void emptyLinesBetween();
    void crlfEndings();
    void streamingByteByByte();
};

void TestAcpJsonRpcFraming::singleFrame()
{
    QByteArray buf = "{\"a\":1}\n";
    QStringList frames = AcpProtocol::acpExtractFrames(buf);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0), QStringLiteral("{\"a\":1}"));
    QVERIFY(buf.isEmpty());
}

void TestAcpJsonRpcFraming::twoFramesInOneChunk()
{
    QByteArray buf = "{\"a\":1}\n{\"b\":2}\n";
    QStringList frames = AcpProtocol::acpExtractFrames(buf);
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0), QStringLiteral("{\"a\":1}"));
    QCOMPARE(frames.at(1), QStringLiteral("{\"b\":2}"));
    QVERIFY(buf.isEmpty());
}

void TestAcpJsonRpcFraming::partialTrailing()
{
    QByteArray buf = "{\"a\":1}\n{\"b\":";
    QStringList frames = AcpProtocol::acpExtractFrames(buf);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0), QStringLiteral("{\"a\":1}"));
    QCOMPARE(buf, QByteArray("{\"b\":"));
}

void TestAcpJsonRpcFraming::emptyLinesBetween()
{
    QByteArray buf = "{\"a\":1}\n\n{\"b\":2}\n";
    QStringList frames = AcpProtocol::acpExtractFrames(buf);
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0), QStringLiteral("{\"a\":1}"));
    QCOMPARE(frames.at(1), QStringLiteral("{\"b\":2}"));
}

void TestAcpJsonRpcFraming::crlfEndings()
{
    QByteArray buf = "{\"a\":1}\r\n";
    QStringList frames = AcpProtocol::acpExtractFrames(buf);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.at(0), QStringLiteral("{\"a\":1}"));
}

void TestAcpJsonRpcFraming::streamingByteByByte()
{
    const QByteArray input = "{\"a\":1}\n";
    QByteArray buf;
    QStringList allFrames;
    for (char c : input) {
        buf.append(c);
        QStringList got = AcpProtocol::acpExtractFrames(buf);
        allFrames.append(got);
    }
    QCOMPARE(allFrames.size(), 1);
    QCOMPARE(allFrames.at(0), QStringLiteral("{\"a\":1}"));
    QVERIFY(buf.isEmpty());
}

QTEST_GUILESS_MAIN(TestAcpJsonRpcFraming)
#include "test_acp_jsonrpc_framing.moc"
