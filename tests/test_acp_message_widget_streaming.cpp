/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX short: GPL-3.0-or-later
 */

#include <QtTest>
#include <QTextBrowser>
#include <QToolButton>

#include "AcpMessageWidget.h"

class TestAcpMessageWidgetStreaming : public QObject
{
    Q_OBJECT

private slots:
    void assistant_streaming_buffers_chunks();
    void assistant_goalSetPrefix_usesKingGoldFrame();
    void assistant_goalSet_hidesWorktreeInjection();
    void thought_collapse_after_streaming_done();
    void thought_renders_markdown_as_raw_text();
    void thought_keeps_body_height_while_hidden();
    void thought_reexpand_while_hidden_restores_body_height();
};

void TestAcpMessageWidgetStreaming::assistant_streaming_buffers_chunks()
{
    AcpMessageWidget w(QStringLiteral("assistant"));
    w.appendChunk(QStringLiteral("Hello "));
    w.appendChunk(QStringLiteral("world"));
    QVERIFY(w.plainText().contains(QStringLiteral("Hello world")));
}

void TestAcpMessageWidgetStreaming::assistant_goalSetPrefix_usesKingGoldFrame()
{
    AcpMessageWidget goalSet(QStringLiteral("assistant"));
    goalSet.setText(QStringLiteral("Goal set: say hi in chinese"));
    QCOMPARE(goalSet.property("goalSet").toBool(), true);

    AcpMessageWidget reply(QStringLiteral("assistant"));
    reply.setText(QStringLiteral("こんにちは。"));
    QCOMPARE(reply.property("goalSet").toBool(), false);
}

void TestAcpMessageWidgetStreaming::assistant_goalSet_hidesWorktreeInjection()
{
    const QString instruction = QStringLiteral(
        "Create a new git worktree for this task. When finished, merge the result "
        "into the current branch and remove the worktree to free disk space.");

    AcpMessageWidget echoed(QStringLiteral("assistant"));
    echoed.setText(QStringLiteral("Goal set: say hi in japanese\n") + instruction);
    auto *browser = echoed.findChild<QTextBrowser *>();
    QVERIFY(browser);
    const QString shown = browser->toPlainText();
    QVERIFY(shown.contains(QStringLiteral("Goal set: say hi in japanese")));
    QVERIFY(!shown.contains(QStringLiteral("Create a new git worktree")));
    QVERIFY(echoed.plainText().contains(instruction));

    AcpMessageWidget blankLine(QStringLiteral("assistant"));
    blankLine.setText(QStringLiteral("Goal set: say hi in japanese\n\n") + instruction);
    auto *blankBrowser = blankLine.findChild<QTextBrowser *>();
    QVERIFY(blankBrowser);
    QVERIFY(!blankBrowser->toPlainText().contains(QStringLiteral("Create a new git worktree")));

    AcpMessageWidget partial(QStringLiteral("assistant"));
    partial.setText(QStringLiteral("Goal set: say hi in japanese\nCreate a new git worktree for this"));
    auto *partialBrowser = partial.findChild<QTextBrowser *>();
    QVERIFY(partialBrowser);
    QVERIFY(partialBrowser->toPlainText().contains(QStringLiteral("say hi in japanese")));
    QVERIFY(!partialBrowser->toPlainText().contains(QStringLiteral("Create a new git worktree")));

    AcpMessageWidget other(QStringLiteral("assistant"));
    other.setText(QStringLiteral("Note:\n") + instruction);
    auto *otherBrowser = other.findChild<QTextBrowser *>();
    QVERIFY(otherBrowser);
    QVERIFY(otherBrowser->toPlainText().contains(QStringLiteral("Create a new git worktree")));
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

void TestAcpMessageWidgetStreaming::thought_keeps_body_height_while_hidden()
{
    // Inactive session tabs live on a hidden QStackedWidget page, so
    // QWidget::isVisible() is false while thought chunks still stream.
    // Height must follow the logical collapse flag, not isVisible() —
    // otherwise the bubble pins to header height and clips the body.
    AcpMessageWidget w(QStringLiteral("thought"));
    w.resize(420, 40);
    QVERIFY(!w.isVisible());

    w.setText(QStringLiteral(
        "line one of the thought\n"
        "line two of the thought\n"
        "line three of the thought\n"
        "line four of the thought"));
    const int expandedH = w.height();

    w.markStreamingDone();
    QVERIFY(w.isCollapsed());
    const int collapsedH = w.height();

    QVERIFY2(expandedH > collapsedH,
             qPrintable(QStringLiteral("expanded=%1 collapsed=%2")
                            .arg(expandedH)
                            .arg(collapsedH)));
}

void TestAcpMessageWidgetStreaming::thought_reexpand_while_hidden_restores_body_height()
{
    AcpMessageWidget w(QStringLiteral("thought"));
    w.resize(420, 40);
    w.setText(QStringLiteral("a\nb\nc\nd"));
    w.markStreamingDone();
    QVERIFY(w.isCollapsed());
    const int collapsedH = w.height();

    auto *header = w.findChild<QToolButton *>();
    QVERIFY(header);
    header->setChecked(true);
    QVERIFY(!w.isCollapsed());
    QVERIFY2(w.height() > collapsedH,
             qPrintable(QStringLiteral("expanded=%1 collapsed=%2")
                            .arg(w.height())
                            .arg(collapsedH)));
}

QTEST_MAIN(TestAcpMessageWidgetStreaming)
#include "test_acp_message_widget_streaming.moc"
