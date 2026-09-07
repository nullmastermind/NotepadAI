/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX short: GPL-3.0-or-later
 */

#include <QtTest>
#include <QTextBrowser>

#include "AcpMessageWidget.h"

class TestAcpMessageWidgetStreaming : public QObject
{
    Q_OBJECT

private slots:
    void assistant_streaming_buffers_chunks();
    void thought_collapse_after_streaming_done();
    void thought_renders_markdown_as_raw_text();
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

void TestAcpMessageWidgetStreaming::thought_renders_markdown_as_raw_text()
{
    AcpMessageWidget w(QStringLiteral("thought"));
    const QString raw = QStringLiteral("**bold** and `code`");
    w.setText(raw);

    auto *browser = w.findChild<QTextBrowser *>();
    QVERIFY(browser);
    // Markdown would consume ** and backticks, leaving "bold and code".
    QCOMPARE(browser->toPlainText().trimmed(), raw);
}

QTEST_MAIN(TestAcpMessageWidgetStreaming)
#include "test_acp_message_widget_streaming.moc"
