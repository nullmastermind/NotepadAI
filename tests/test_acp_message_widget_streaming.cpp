/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
 *
 * SPDX short: GPL-3.0-or-later
 */

#include <QtTest>

#include "AcpMessageWidget.h"

class TestAcpMessageWidgetStreaming : public QObject
{
    Q_OBJECT

private slots:
    void assistant_streaming_buffers_chunks();
    void thought_collapse_after_streaming_done();
};

void TestAcpMessageWidgetStreaming::assistant_streaming_buffers_chunks()
{
    AcpMessageWidget w(QStringLiteral("assistant"));
    w.appendChunk(QStringLiteral("Hello "));
    w.appendChunk(QStringLiteral("world"));
    QVERIFY(w.plainText().contains(QStringLiteral("Hello world")));
}

void TestAcpMessageWidgetStreaming::thought_collapse_after_streaming_done()
{
    AcpMessageWidget w(QStringLiteral("thought"));
    w.appendChunk(QStringLiteral("considering options..."));
    QVERIFY(!w.isCollapsed());
    w.markStreamingDone();
    QVERIFY(w.isCollapsed());
}

QTEST_MAIN(TestAcpMessageWidgetStreaming)
#include "test_acp_message_widget_streaming.moc"
