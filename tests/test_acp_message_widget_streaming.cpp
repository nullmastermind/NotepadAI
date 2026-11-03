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
#include <QScrollBar>
#include <QTextBlock>
#include <QTimer>

#include <climits>
#include <limits>

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
    void thought_streaming_tick_appends_only_new_text();
    void thought_streaming_matches_full_render();
    void thought_setText_rewrite_replaces_body();
    void thought_long_body_caps_and_scrolls();
    void thought_short_body_has_no_scrollbar();
    void thought_follows_bottom_until_reader_scrolls_up();
    void thought_reopened_after_streaming_starts_at_top();
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

namespace {

// Long reasoning like a real thinking stream: paragraphs, blank lines between.
QString longThought(int chars)
{
    const QString sentence = QStringLiteral(
        "The original was already patched with NOP padding; patch.py expected the old "
        "bytes ldc_w, invokestatic, dup, astore_2, so the backup must hold the original. ");
    QString s;
    int para = 0;
    while (s.size() < chars) {
        s += sentence;
        if (++para % 4 == 0)
            s += QStringLiteral("\n\n");
    }
    return s;
}

// Run the 80 ms streaming debounce now instead of waiting for it.
void fireRerender(AcpMessageWidget &w)
{
    auto *timer = w.findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(timer);
    if (timer->isActive()) {
        timer->stop();
        QVERIFY(QMetaObject::invokeMethod(timer, "timeout"));
    }
    QCoreApplication::processEvents();
}

} // namespace

void TestAcpMessageWidgetStreaming::thought_streaming_tick_appends_only_new_text()
{
    // A long thought streams for minutes. Rebuilding the whole document on every
    // debounce tick made each tick O(length): ~150 ms at 100K chars every 80 ms,
    // which starved the UI thread and lagged the whole editor. A tick must only
    // add the new text at the end of the document.
    AcpMessageWidget w(QStringLiteral("thought"));
    w.resize(420, 40);
    w.appendChunk(longThought(20'000));
    fireRerender(w);

    auto *browser = w.findChild<QTextBrowser *>();
    QVERIFY(browser);
    QTextDocument *doc = browser->document();
    const int shownBefore = doc->characterCount();

    int removed = 0;
    int added = 0;
    int firstPos = INT_MAX;
    connect(doc, &QTextDocument::contentsChange, this, [&](int pos, int rem, int add) {
        removed += rem;
        added += add;
        firstPos = qMin(firstPos, pos);
    });
    // A full relayout (e.g. setTextWidth at an unchanged width re-wraps every
    // block) repaints from the top; an append repaints from the changed block.
    qreal firstRepaintY = std::numeric_limits<qreal>::max();
    connect(doc->documentLayout(), &QAbstractTextDocumentLayout::update, this,
            [&](const QRectF &r) { firstRepaintY = qMin(firstRepaintY, r.top()); });

    const QString chunk = QStringLiteral("more reasoning\n\nnext paragraph starts");
    w.appendChunk(chunk);
    fireRerender(w);

    QCOMPARE(removed, 0);
    QVERIFY2(added <= chunk.size() + 2,
             qPrintable(QStringLiteral("added=%1 chunk=%2").arg(added).arg(chunk.size())));
    QVERIFY2(firstPos >= shownBefore - 2,
             qPrintable(QStringLiteral("firstPos=%1 shownBefore=%2").arg(firstPos).arg(shownBefore)));
    const qreal docH = doc->documentLayout()->documentSize().height();
    QVERIFY2(firstRepaintY > docH / 2,
             qPrintable(QStringLiteral("relaid out from y=%1 of %2").arg(firstRepaintY).arg(docH)));
}

void TestAcpMessageWidgetStreaming::thought_streaming_matches_full_render()
{
    // Chunks split anywhere: inside words, between "\r" and "\n", on trailing
    // whitespace (which is held back until text follows it). The streamed body
    // must read and measure exactly like the same text rendered at once.
    const QStringList chunks = {
        QStringLiteral("first line "),     QStringLiteral(" \n"),
        QStringLiteral("\nsecond para\r"), QStringLiteral("\nthird **raw** `md`"),
        QStringLiteral("\t\n\n"),          QStringLiteral("tail words"),
        QStringLiteral(" "),               longThought(3'000),
        QStringLiteral("\n"),
    };
    AcpMessageWidget streamed(QStringLiteral("thought"));
    streamed.resize(420, 40);
    QString full;
    for (const QString &c : chunks) {
        full += c;
        streamed.appendChunk(c);
        fireRerender(streamed);
    }

    auto *a = streamed.findChild<QTextBrowser *>();
    QVERIFY(a);
    QCOMPARE(streamed.plainText(), full);
    const QString shownStreamed = a->toPlainText();
    const int blocksStreamed = a->document()->blockCount();
    const int heightStreamed = streamed.height();

    // The same widget rendering the whole text at once (setText rebuilds).
    streamed.setText(full);
    QCoreApplication::processEvents();
    QCOMPARE(shownStreamed, a->toPlainText());
    QCOMPARE(blocksStreamed, a->document()->blockCount());
    QCOMPARE(heightStreamed, streamed.height());
    for (QTextBlock blk = a->document()->begin(); blk.isValid(); blk = blk.next()) {
        QCOMPARE(blk.blockFormat().topMargin(), 0.0);
        QCOMPARE(blk.blockFormat().bottomMargin(), 0.0);
    }
}

void TestAcpMessageWidgetStreaming::thought_setText_rewrite_replaces_body()
{
    AcpMessageWidget w(QStringLiteral("thought"));
    w.resize(420, 40);
    w.appendChunk(QStringLiteral("draft reasoning that gets rewritten"));
    fireRerender(w);
    w.setText(QStringLiteral("compacted"));
    auto *browser = w.findChild<QTextBrowser *>();
    QVERIFY(browser);
    QCOMPARE(browser->toPlainText(), QStringLiteral("compacted"));
    w.appendChunk(QStringLiteral(" and more"));
    fireRerender(w);
    QCOMPARE(browser->toPlainText(), QStringLiteral("compacted and more"));
}

namespace {

constexpr int kMaxLines = 12; // AcpMessageWidget.cpp kThoughtMaxLines

int lineSpacing(QTextBrowser *b)
{
    return qCeil(QFontMetricsF(b->document()->defaultFont()).lineSpacing());
}

// Streams a long thought into a shown bubble and settles queued refits.
QTextBrowser *streamLong(AcpMessageWidget &w, int chars)
{
    w.resize(420, 40);
    w.show();
    if (!QTest::qWaitForWindowExposed(&w))
        return nullptr;
    w.appendChunk(longThought(chars));
    fireRerender(w);
    QCoreApplication::processEvents();
    return w.findChild<QTextBrowser *>();
}

} // namespace

void TestAcpMessageWidgetStreaming::thought_long_body_caps_and_scrolls()
{
    AcpMessageWidget w(QStringLiteral("thought"));
    QTextBrowser *b = streamLong(w, 20'000);
    QVERIFY(b);
    const int cap = lineSpacing(b) * kMaxLines;
    QVERIFY2(qAbs(b->height() - cap) <= 1,
             qPrintable(QStringLiteral("body=%1 cap=%2").arg(b->height()).arg(cap)));
    QVERIFY(w.height() < cap + 3 * lineSpacing(b));
    QCOMPARE(b->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QTRY_VERIFY(b->verticalScrollBar()->maximum() > 0);
    // A slim bar, not the style's default (arrows, ~16 px).
    QTRY_VERIFY(b->verticalScrollBar()->isVisible());
    QVERIFY2(b->verticalScrollBar()->width() <= 8,
             qPrintable(QStringLiteral("bar width=%1").arg(b->verticalScrollBar()->width())));
    // Text wraps beside the scrollbar, keeping the italic slack.
    QTRY_VERIFY2(b->document()->textWidth() <= b->viewport()->width() - 2,
                 qPrintable(QStringLiteral("textWidth=%1 viewport=%2")
                                .arg(b->document()->textWidth())
                                .arg(b->viewport()->width())));
}

void TestAcpMessageWidgetStreaming::thought_short_body_has_no_scrollbar()
{
    AcpMessageWidget w(QStringLiteral("thought"));
    QTextBrowser *b = streamLong(w, 200);
    QVERIFY(b);
    QCOMPARE(b->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QVERIFY(b->height() < lineSpacing(b) * kMaxLines);

    // A rewrite to a short text drops the cap again.
    AcpMessageWidget longer(QStringLiteral("thought"));
    QTextBrowser *lb = streamLong(longer, 20'000);
    QVERIFY(lb);
    QCOMPARE(lb->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    longer.setText(QStringLiteral("short again"));
    QCoreApplication::processEvents();
    QCOMPARE(lb->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
    QVERIFY(lb->height() < lineSpacing(lb) * 3);
}

void TestAcpMessageWidgetStreaming::thought_follows_bottom_until_reader_scrolls_up()
{
    AcpMessageWidget w(QStringLiteral("thought"));
    QTextBrowser *b = streamLong(w, 20'000);
    QVERIFY(b);
    QScrollBar *bar = b->verticalScrollBar();
    QTRY_VERIFY(bar->maximum() > 0);
    QTRY_COMPARE(bar->value(), bar->maximum());

    const QString more = QStringLiteral("\n\n") + longThought(1'000);
    int maxBefore = bar->maximum();
    w.appendChunk(more);
    fireRerender(w);
    QTRY_VERIFY(bar->maximum() > maxBefore);
    QCOMPARE(bar->value(), bar->maximum());

    // The reader scrolls up: new text no longer moves the view.
    bar->setValue(0);
    maxBefore = bar->maximum();
    w.appendChunk(more);
    fireRerender(w);
    QTRY_VERIFY(bar->maximum() > maxBefore);
    QCOMPARE(bar->value(), 0);

    // Back at the bottom: following again.
    bar->setValue(bar->maximum());
    maxBefore = bar->maximum();
    w.appendChunk(more);
    fireRerender(w);
    QTRY_VERIFY(bar->maximum() > maxBefore);
    QCOMPARE(bar->value(), bar->maximum());
}

void TestAcpMessageWidgetStreaming::thought_reopened_after_streaming_starts_at_top()
{
    AcpMessageWidget w(QStringLiteral("thought"));
    QTextBrowser *b = streamLong(w, 20'000);
    QVERIFY(b);
    QTRY_VERIFY(b->verticalScrollBar()->value() > 0);
    w.markStreamingDone();
    QVERIFY(w.isCollapsed());
    auto *header = w.findChild<QToolButton *>();
    QVERIFY(header);
    header->setChecked(true);
    QCoreApplication::processEvents();
    QVERIFY(!w.isCollapsed());
    QTRY_VERIFY(b->verticalScrollBar()->maximum() > 0);
    QCOMPARE(b->verticalScrollBar()->value(), 0);
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
