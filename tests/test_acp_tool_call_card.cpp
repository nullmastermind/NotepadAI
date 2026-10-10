/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX short: GPL-3.0-or-later
 */

#include <QtTest>

#include <QLabel>
#include <QTextBrowser>
#include <QStringList>


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
    void expanded_diff_taller_than_collapsed_while_hidden();
    void omp_pending_status_uses_running_glyph();
    void omp_in_progress_diff_stays_collapsed();
    void omp_write_title_from_kind_edit_content();
    void omp_edit_title_from_prefixed_path();
    void compact_numbered_diff_renders_without_old_new();
    void compact_unified_diff_renders_hunk();
    void edit_full_file_old_new_shows_only_hunk();
    void numbered_full_file_dump_collapses_to_hunk();
    void omp_hashline_text_not_shown_when_diff_present();
    void goalJudge_setsGoalAgentPropertyAndBadge();


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

QString glyphOf(AcpToolCallCard &card)
{
    const auto labels = card.findChildren<QLabel *>();
    for (QLabel *label : labels) {
        if (label->text().size() == 1) {
            return label->text();
        }
    }
    return {};
}

QString titleOf(AcpToolCallCard &card)
{
    const auto labels = card.findChildren<QLabel *>();
    for (QLabel *label : labels) {
        if (label->text().size() > 1) {
            return label->text();
        }
    }
    return {};
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

void TestAcpToolCallCard::expanded_diff_taller_than_collapsed_while_hidden()
{
    // Inactive session tabs live on a hidden QStackedWidget page. An
    // auto-expanded diff must still include the body in its pinned height
    // even though isVisible() is false — otherwise switching back clips
    // the diff to the header.
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.status = QStringLiteral("completed");
    tc.content.append(diffBlock());

    AcpToolCallCard expanded(tc);
    expanded.resize(480, 120);
    QVERIFY(!expanded.isVisible());
    QVERIFY(!expanded.isCollapsed());
    QTRY_VERIFY(bodyFor(expanded)->toPlainText().contains(QStringLiteral("a.cpp")));
    const int expandedH = expanded.height();

    AcpProtocol::AcpToolCall running = baseCall();
    AcpToolCallCard collapsed(running);
    collapsed.resize(480, 120);
    QVERIFY(collapsed.isCollapsed());
    const int collapsedH = collapsed.height();

    QVERIFY2(expandedH > collapsedH,
             qPrintable(QStringLiteral("expanded=%1 collapsed=%2")
                            .arg(expandedH)
                            .arg(collapsedH)));
}

void TestAcpToolCallCard::omp_pending_status_uses_running_glyph()
{
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.status = QStringLiteral("pending");
    AcpToolCallCard pending(tc);
    pending.resize(480, 120);
    QCOMPARE(glyphOf(pending), QStringLiteral("⚙"));

    AcpProtocol::AcpToolCallUpdate up;
    up.id = tc.id;
    up.status = QStringLiteral("in_progress");
    pending.apply(up);
    QCOMPARE(glyphOf(pending), QStringLiteral("⚙"));
}

void TestAcpToolCallCard::omp_in_progress_diff_stays_collapsed()
{
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.status = QStringLiteral("in_progress");
    tc.content.append(diffBlock());
    AcpToolCallCard card(tc);
    card.resize(480, 120);
    QVERIFY(card.isCollapsed());
    QTest::qWait(120);
    QVERIFY(bodyFor(card)->toPlainText().isEmpty());
}

void TestAcpToolCallCard::omp_write_title_from_kind_edit_content()
{
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.title = QStringLiteral("edit: D:/tmp/test-git/probe.txt");
    tc.kind = QStringLiteral("edit");
    tc.rawInput.insert(QStringLiteral("path"), QStringLiteral("D:/tmp/test-git/probe.txt"));
    tc.rawInput.insert(QStringLiteral("content"), QStringLiteral("hello\n"));
    AcpToolCallCard card(tc);
    card.resize(800, 80);
    const QString title = titleOf(card);
    QVERIFY2(title.contains(QLatin1String("Write:")), qPrintable(title));
    QVERIFY2(!title.contains(QLatin1String("Edit:")), qPrintable(title));

}

void TestAcpToolCallCard::omp_edit_title_from_prefixed_path()
{
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.title = QStringLiteral("edit: conflict.txt");
    tc.kind = QStringLiteral("edit");
    tc.rawInput.insert(QStringLiteral("path"), QStringLiteral("conflict.txt"));
    tc.rawInput.insert(QStringLiteral("oldString"), QStringLiteral("a"));
    tc.rawInput.insert(QStringLiteral("newString"), QStringLiteral("b"));
    AcpToolCallCard card(tc);
    card.resize(800, 80);
    QVERIFY(titleOf(card).contains(QStringLiteral("Edit: conflict.txt")));
}

void TestAcpToolCallCard::compact_numbered_diff_renders_without_old_new()
{
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.status = QStringLiteral("completed");
    QJsonObject block;
    block.insert(QStringLiteral("type"), QStringLiteral("diff"));
    block.insert(QStringLiteral("path"), QStringLiteral("numbered.cpp"));
    block.insert(QStringLiteral("diff"),
                 QStringLiteral("  10|keep\n-11|old line\n+11|new line\n"));
    tc.content.append(block);
    AcpToolCallCard card(tc);
    card.resize(480, 200);
    QVERIFY(!card.isCollapsed());
    QTRY_VERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("numbered.cpp")));
    QVERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("keep")));
    QVERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("old line")));
    QVERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("new line")));
}

void TestAcpToolCallCard::compact_unified_diff_renders_hunk()
{
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.status = QStringLiteral("completed");
    QJsonObject block;
    block.insert(QStringLiteral("type"), QStringLiteral("diff"));
    block.insert(QStringLiteral("path"), QStringLiteral("unified.cpp"));
    block.insert(QStringLiteral("diff"),
                 QStringLiteral("@@ -1,2 +1,2 @@\n keep\n-old\n+new\n"));
    tc.content.append(block);
    AcpToolCallCard card(tc);
    card.resize(480, 200);
    QVERIFY(!card.isCollapsed());
    QTRY_VERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("unified.cpp")));
    QVERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("keep")));
    QVERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("old")));
    QVERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("new")));
}

void TestAcpToolCallCard::edit_full_file_old_new_shows_only_hunk()
{
    QStringList oldLines;
    QStringList newLines;
    oldLines.reserve(40);
    newLines.reserve(40);
    for (int i = 1; i <= 40; ++i) {
        const QString line = QStringLiteral("line-%1").arg(i, 2, 10, QLatin1Char('0'));
        oldLines.append(line);
        newLines.append(line);
    }
    newLines[19] = QStringLiteral("changed-20");

    AcpProtocol::AcpToolCall tc = baseCall();
    tc.status = QStringLiteral("completed");
    QJsonObject block;
    block.insert(QStringLiteral("type"), QStringLiteral("diff"));
    block.insert(QStringLiteral("path"), QStringLiteral("big.cpp"));
    block.insert(QStringLiteral("oldText"), oldLines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
    block.insert(QStringLiteral("newText"), newLines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
    tc.content.append(block);
    AcpToolCallCard card(tc);
    card.resize(480, 400);
    QVERIFY(!card.isCollapsed());
    QTRY_VERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("changed-20")));
    const QString body = bodyFor(card)->toPlainText();
    QVERIFY(body.contains(QStringLiteral("line-20")));
    QVERIFY(body.contains(QStringLiteral("...")));
    QVERIFY(!body.contains(QStringLiteral("line-05")));
    QVERIFY(!body.contains(QStringLiteral("line-35")));
}

void TestAcpToolCallCard::numbered_full_file_dump_collapses_to_hunk()
{
    QString dump;
    dump.reserve(40 * 24);
    for (int i = 1; i <= 40; ++i) {
        if (i == 20) {
            dump += QStringLiteral("-20|line-20\n+20|changed-20\n");
        } else {
            dump += QStringLiteral("  %1|line-%2\n")
                        .arg(i, 2, 10, QLatin1Char('0'))
                        .arg(i, 2, 10, QLatin1Char('0'));
        }
    }

    AcpProtocol::AcpToolCall tc = baseCall();
    tc.status = QStringLiteral("completed");
    QJsonObject block;
    block.insert(QStringLiteral("type"), QStringLiteral("diff"));
    block.insert(QStringLiteral("path"), QStringLiteral("numbered.cpp"));
    block.insert(QStringLiteral("diff"), dump);
    tc.content.append(block);
    AcpToolCallCard card(tc);
    card.resize(480, 400);
    QVERIFY(!card.isCollapsed());
    QTRY_VERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("changed-20")));
    const QString body = bodyFor(card)->toPlainText();
    QVERIFY(body.contains(QStringLiteral("...")));
    QVERIFY(!body.contains(QStringLiteral("line-05")));
    QVERIFY(!body.contains(QStringLiteral("line-35")));
}

void TestAcpToolCallCard::omp_hashline_text_not_shown_when_diff_present()
{
    AcpProtocol::AcpToolCall tc = baseCall();
    tc.status = QStringLiteral("completed");
    QJsonObject dump;
    dump.insert(QStringLiteral("type"), QStringLiteral("text"));
    dump.insert(QStringLiteral("text"),
                QStringLiteral("[test.txt#86C8]\n1:test\n2:# edited\n3:2\n4:3\n"));
    QJsonObject diff;
    diff.insert(QStringLiteral("type"), QStringLiteral("diff"));
    diff.insert(QStringLiteral("path"), QStringLiteral("test.txt"));
    diff.insert(QStringLiteral("oldText"), QStringLiteral("test\n2\n3\n"));
    diff.insert(QStringLiteral("newText"), QStringLiteral("test\n# edited\n2\n3\n"));
    tc.content.append(dump);
    tc.content.append(diff);
    AcpToolCallCard card(tc);
    card.resize(480, 400);
    QVERIFY(!card.isCollapsed());
    QTRY_VERIFY(bodyFor(card)->toPlainText().contains(QStringLiteral("# edited")));
    const QString body = bodyFor(card)->toPlainText();
    QVERIFY(!body.contains(QStringLiteral("[test.txt#")));
    QVERIFY(!body.contains(QStringLiteral("86C8")));
}

void TestAcpToolCallCard::goalJudge_setsGoalAgentPropertyAndBadge()
{
    AcpToolCallCard card(baseCall());
    QVERIFY(!card.property("goalAgent").toBool());
    QVERIFY(card.isCollapsed());
    card.setFromGoalAgent(true);
    QCOMPARE(card.property("goalAgent").toBool(), true);
    QVERIFY(card.isFromGoalAgent());
    QVERIFY(card.isCollapsed());
    bool found = false;
    for (QLabel *label : card.findChildren<QLabel *>()) {
        if (label->text() == QStringLiteral("Goal")) {
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

QTEST_MAIN(TestAcpToolCallCard)
#include "test_acp_tool_call_card.moc"
