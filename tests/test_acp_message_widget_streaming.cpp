/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX short: GPL-3.0-or-later
 */

#include <QtTest>
#include <QTextBrowser>
#include <QToolButton>
#include <QAbstractTextDocumentLayout>
#include <QLabel>
#include <QTextBlock>

#include "AcpMessageWidget.h"

class TestAcpMessageWidgetStreaming : public QObject
{
    Q_OBJECT

private slots:
    void assistant_streaming_buffers_chunks();
    void assistant_goalSetPrefix_usesKingGoldFrame();
    void assistant_fromGoalJudge_setsGoalAgentPropertyAndBadge();
    void assistant_goalTint_hasGoldWithoutBadge();
    void assistant_plain_isNotGoalAgent();
    void assistant_goalSet_hidesWorktreeInjection();
    void thought_collapse_after_streaming_done();
    void thought_renders_markdown_as_raw_text();
    void thought_keeps_body_height_while_hidden();
    void thought_body_does_not_overflow_viewport();
    void thought_reexpand_while_hidden_restores_body_height();
    void system_goalAchieved_heightIncludesBadge();
    void system_goalAchieved_keepsBottomMargin();
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

void TestAcpMessageWidgetStreaming::assistant_fromGoalJudge_setsGoalAgentPropertyAndBadge()
{
    AcpMessageWidget w(QStringLiteral("assistant"));
    QVERIFY(!w.property("goalAgent").toBool());
    w.setFromGoalAgent(true);
    QCOMPARE(w.property("goalAgent").toBool(), true);
    QVERIFY(w.isFromGoalAgent());
    auto *badge = w.findChild<QLabel *>();
    QVERIFY(badge);
    QCOMPARE(badge->text(), QStringLiteral("Goal"));
}

void TestAcpMessageWidgetStreaming::assistant_goalTint_hasGoldWithoutBadge()
{
    AcpMessageWidget w(QStringLiteral("assistant"));
    w.setGoalTint(true);
    QCOMPARE(w.property("goalAgent").toBool(), true);
    QVERIFY(w.findChild<QLabel *>() == nullptr);
}

void TestAcpMessageWidgetStreaming::assistant_plain_isNotGoalAgent()
{
    AcpMessageWidget w(QStringLiteral("assistant"));
    w.setText(QStringLiteral("hello"));
    QVERIFY(!w.property("goalAgent").toBool());
    QVERIFY(w.findChild<QLabel *>() == nullptr);
}

void TestAcpMessageWidgetStreaming::assistant_goalSet_hidesWorktreeInjection()
{
    const QString instruction = QStringLiteral(
        "Create a new git worktree for this task inside the repository's .claude/worktrees "
        "directory. Do not choose any other location. When finished, merge the result "
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

void TestAcpMessageWidgetStreaming::thought_body_does_not_overflow_viewport()
{
    // Thought body has stylesheet padding-left and italic glyphs that overhang
    // their advance. Layout at the frame content width makes the document a
    // few px wider than the viewport: horizontal scroll + clipped last letters.
    AcpMessageWidget w(QStringLiteral("thought"));
    w.resize(420, 40);
    w.setText(QStringLiteral(
        "The compile timed out again at 300s while compiling. The crate is huge. "
        "I need a longer timeout. 0 disable. the deadline? The bash tool says "
        "timeout 0 disables the command deadline. I should set timeout 0."));

    auto *browser = w.findChild<QTextBrowser *>();
    QVERIFY(browser);
    QTextDocument *doc = browser->document();
    QVERIFY(doc);

    const qreal contentW = w.width() - 8; // thought layout margins 4+4
    QVERIFY(doc->textWidth() > 0);
    // Must leave pad-left (4) plus italic overhang slack (>=2) inside the
    // allocated width. Equality with contentW is the bug this test exists for.
    QVERIFY2(doc->textWidth() <= contentW - 4 - 2,
             qPrintable(QStringLiteral("textWidth=%1 contentW=%2")
                            .arg(doc->textWidth())
                            .arg(contentW)));
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

void TestAcpMessageWidgetStreaming::system_goalAchieved_heightIncludesBadge()
{
    // Goal achieved is a system row with a Goal badge. refit must add the
    // badge to the frame height or the wrapped body is clipped to one line.
    const QString text = QStringLiteral(
        "✓ Goal achieved: The agent greeted in Bahasa Indonesia with \"Hai\", "
        "which is the appropriate informal greeting.");

    AcpMessageWidget plain(QStringLiteral("system"));
    plain.resize(360, 40);
    plain.setText(text);

    AcpMessageWidget goal(QStringLiteral("system"));
    goal.setFromGoalAgent(true);
    goal.resize(360, 40);
    goal.setText(text);

    auto *badge = goal.findChild<QLabel *>();
    QVERIFY(badge);
    QCOMPARE(badge->text(), QStringLiteral("Goal"));
    auto *browser = goal.findChild<QTextBrowser *>();
    QVERIFY(browser);
    QVERIFY(browser->toPlainText().contains(QStringLiteral("informal greeting")));

    QVERIFY2(goal.height() >= plain.height() + badge->height(),
             qPrintable(QStringLiteral("goal=%1 plain=%2 badge=%3")
                            .arg(goal.height())
                            .arg(plain.height())
                            .arg(badge->height())));
}

void TestAcpMessageWidgetStreaming::system_goalAchieved_keepsBottomMargin()
{
    // Frame border is inside the widget. A height that only sums layout
    // children eats the 6px bottom margin, so the last line sits on the border.
    AcpMessageWidget w(QStringLiteral("system"));
    w.setFromGoalAgent(true);
    w.resize(360, 40);
    w.setText(QStringLiteral(
        "✓ Goal achieved: The assistant greeted the developer in Bahasa Indonesia "
        "with \"Hai\" which fulfills the criterion of saying hi in Bahasa Indonesia."));
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));

    auto *browser = w.findChild<QTextBrowser *>();
    QVERIFY(browser);
    QTextDocument *doc = browser->document();
    QVERIFY(doc);
    const QTextBlock last = doc->lastBlock();
    const QRectF block = doc->documentLayout()->blockBoundingRect(last);
    const int textBottom = browser->viewport()->mapTo(&w, block.bottomLeft().toPoint()).y();
    const int gap = w.rect().bottom() - textBottom;
    QVERIFY2(gap >= 6,
             qPrintable(QStringLiteral("gap=%1 textBottom=%2 widgetH=%3")
                            .arg(gap)
                            .arg(textBottom)
                            .arg(w.height())));
}

QTEST_MAIN(TestAcpMessageWidgetStreaming)
#include "test_acp_message_widget_streaming.moc"
