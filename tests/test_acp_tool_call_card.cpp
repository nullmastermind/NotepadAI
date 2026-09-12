/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX short: GPL-3.0-or-later
 */

#include <QtTest>

#include <QTextBrowser>

#include "AcpToolCallCard.h"

class TestAcpToolCallCard : public QObject
{
    Q_OBJECT

private slots:
    void collapsed_card_defers_body_render_until_expanded();
    void expanded_card_coalesces_running_updates();
    void diff_card_auto_expands_at_terminal_status();
    void running_diff_card_stays_collapsed();
    void collapsed_multiline_title_autosizes_to_two_lines();
};

namespace {

QTextBrowser *bodyFor(AcpToolCallCard &card)
{
    return card.findChild<QTextBrowser *>();
}

AcpProtocol::AcpToolCall baseCall()
{
    AcpProtocol::AcpToolCall tc;
    tc.id = QStringLiteral("tool-1");
    tc.title = QStringLiteral("Command");
    tc.status = QStringLiteral("running");
    return tc;
}

QJsonObject rawStdout(const QString &text)
{
    QJsonObject raw;
    raw.insert(QStringLiteral("stdout"), text);
    return raw;
}

QJsonObject diffBlock()
{
    QJsonObject diff;
    diff.insert(QStringLiteral("type"), QStringLiteral("diff"));
    diff.insert(QStringLiteral("path"), QStringLiteral("a.cpp"));
    diff.insert(QStringLiteral("oldText"), QStringLiteral("old\n"));
    diff.insert(QStringLiteral("newText"), QStringLiteral("new\n"));
    return diff;
}

} // namespace

void TestAcpToolCallCard::collapsed_card_defers_body_render_until_expanded()
{
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.rawOutput = rawStdout(QStringLiteral("initial output"));

    AcpToolCallCard card(tc);
    card.resize(480, 120);
    QTextBrowser *body = bodyFor(card);
    QVERIFY(body);
    QVERIFY(card.isCollapsed());
    QVERIFY(body->toPlainText().isEmpty());

    AcpProtocol::AcpToolCallUpdate update;
    update.id = tc.id;
    update.status = QStringLiteral("completed");
    update.rawOutput = rawStdout(QStringLiteral("final output"));
    card.apply(update);

    QTest::qWait(120);
    QVERIFY(body->toPlainText().isEmpty());

    card.setCollapsed(false);
    QVERIFY(body->toPlainText().contains(QStringLiteral("final output")));
}

void TestAcpToolCallCard::expanded_card_coalesces_running_updates()
{
    AcpProtocol::AcpToolCall tc = baseCall();
    AcpToolCallCard card(tc);
    card.resize(480, 120);
    QTextBrowser *body = bodyFor(card);
    QVERIFY(body);

    card.setCollapsed(false);
    QVERIFY(body->toPlainText().isEmpty());

    AcpProtocol::AcpToolCallUpdate first;
    first.id = tc.id;
    first.status = QStringLiteral("running");
    first.rawOutput = rawStdout(QStringLiteral("first output"));
    card.apply(first);
    QVERIFY(!body->toPlainText().contains(QStringLiteral("first output")));

    AcpProtocol::AcpToolCallUpdate second;
    second.id = tc.id;
    second.status = QStringLiteral("running");
    second.rawOutput = rawStdout(QStringLiteral("second output"));
    card.apply(second);
    QVERIFY(!body->toPlainText().contains(QStringLiteral("second output")));

    QTRY_VERIFY(body->toPlainText().contains(QStringLiteral("second output")));
    QVERIFY(!body->toPlainText().contains(QStringLiteral("first output")));
}

void TestAcpToolCallCard::diff_card_auto_expands_at_terminal_status()
{
    // A diff card arriving already-completed must auto-expand so the user sees
    // the change without clicking. The render still rides the debounce path.
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.status = QStringLiteral("completed");
    tc.content.append(diffBlock());

    AcpToolCallCard card(tc);
    card.resize(480, 120);
    QTextBrowser *body = bodyFor(card);
    QVERIFY(body);

    QVERIFY(!card.isCollapsed());
    QVERIFY(card.shouldPreserveExpanded());  // survives onTurnEnded() re-collapse
    QTRY_VERIFY(body->toPlainText().contains(QStringLiteral("a.cpp")));
}

void TestAcpToolCallCard::running_diff_card_stays_collapsed()
{
    // Diff content present but tool still running: must NOT expand or render —
    // expanding mid-stream drags the diff render into the update hot path.
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.status = QStringLiteral("running");
    tc.content.append(diffBlock());

    AcpToolCallCard card(tc);
    card.resize(480, 120);
    QTextBrowser *body = bodyFor(card);
    QVERIFY(body);

    QVERIFY(card.isCollapsed());
    QTest::qWait(120);
    QVERIFY(body->toPlainText().isEmpty());

    // Once it finishes, it auto-expands and renders.
    AcpProtocol::AcpToolCallUpdate update;
    update.id = tc.id;
    update.status = QStringLiteral("completed");
    card.apply(update);

    QVERIFY(!card.isCollapsed());
    QTRY_VERIFY(body->toPlainText().contains(QStringLiteral("a.cpp")));
}

void TestAcpToolCallCard::collapsed_multiline_title_autosizes_to_two_lines()
{
    // A one-line command keeps the original collapsed height. Two (or more)
    // lines must grow the header so the second line is not clipped — including
    // on first paint, not only after expand→collapse.
    AcpProtocol::AcpToolCall oneLine = baseCall();
    oneLine.title = QStringLiteral("python3 script.py");
    AcpToolCallCard one(oneLine);
    one.resize(480, 40);
    QVERIFY(one.isCollapsed());
    const int h1 = one.height();

    AcpProtocol::AcpToolCall twoLine = baseCall();
    twoLine.title = QStringLiteral("python3 -c \"\nimport hashlib");
    AcpToolCallCard two(twoLine);
    two.resize(480, 40);
    QVERIFY(two.isCollapsed());
    const int h2 = two.height();
    QVERIFY(h2 > h1);

    AcpProtocol::AcpToolCall threeLine = baseCall();
    threeLine.title = QStringLiteral("python3 -c \"\nimport hashlib\nprint(1)");
    AcpToolCallCard three(threeLine);
    three.resize(480, 40);
    QCOMPARE(three.height(), h2);

    // Title arriving via apply() must grow a card that started as one line.
    AcpProtocol::AcpToolCallUpdate update;
    update.id = oneLine.id;
    update.title = twoLine.title;
    one.apply(update);
    QVERIFY(one.height() > h1);

    // Expand→collapse must not change the two-line height (the previously-
    // working path).
    const int beforeToggle = two.height();
    two.setCollapsed(false);
    two.setCollapsed(true);
    QCOMPARE(two.height(), beforeToggle);
}

QTEST_MAIN(TestAcpToolCallCard)
#include "test_acp_tool_call_card.moc"
