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
#include <QString>
#include <QVector>

#include <cstdint>

#include "AcpProtocol.h"
#include "AcpSessionModel.h"
#include "AcpTranscriptTruncation.h"

using AcpTranscriptTruncation::Plan;

class TestAcpTranscriptTruncation : public QObject
{
    Q_OBJECT

private slots:
    void belowCap_allVisible();
    void exactlyCap_allVisible();
    void exampleShape_keepsSummaryAndLiveTail();
    void keepsCapVisibleNotCollapseAllCompletedTurns();
    void completedTurnPinsMayExceedCap();
    void liveTurnKeepsNewestToFitBudget();
    void neverHidesUserOrGoalOrSystem();
    void systemDoesNotStealCompletedSummary();
    void goalMessageSplitsTurn();
    void lastAgentMayBeToolCall();
    void emptyTimeline();
    void gapsSitBetweenAnchorAndSummary_notAtHead();
};

namespace {

AcpMessage makeMsg(const char *role, bool fromGoal = false)
{
    AcpMessage m;
    m.role = QLatin1String(role);
    m.fromGoalAgent = fromGoal;
    return m;
}

AcpTimelineEntry msgEntry(int idx)
{
    AcpTimelineEntry e;
    e.kind = AcpTimelineEntry::Kind::Message;
    e.messageIndex = idx;
    return e;
}

AcpTimelineEntry toolEntry(const QString &id)
{
    AcpTimelineEntry e;
    e.kind = AcpTimelineEntry::Kind::ToolCall;
    e.toolCallId = id;
    return e;
}

int countVisible(const Plan &p)
{
    int n = 0;
    for (std::uint8_t v : p.visible)
        n += v != 0 ? 1 : 0;
    return n;
}

} // namespace

void TestAcpTranscriptTruncation::belowCap_allVisible()
{
    QVector<AcpMessage> msgs{makeMsg("user"), makeMsg("assistant")};
    QVector<AcpTimelineEntry> tl{msgEntry(0), toolEntry(QStringLiteral("t1")), msgEntry(1)};
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 256);
    QCOMPARE(p.visibleCount, 3);
    QCOMPARE(countVisible(p), 3);
    QVERIFY(p.gaps.isEmpty());
}

void TestAcpTranscriptTruncation::exactlyCap_allVisible()
{
    QVector<AcpMessage> msgs{makeMsg("user")};
    QVector<AcpTimelineEntry> tl{msgEntry(0)};
    for (int i = 0; i < 255; ++i)
        tl.append(toolEntry(QStringLiteral("t%1").arg(i)));
    QCOMPARE(tl.size(), 256);
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 256);
    QCOMPARE(p.visibleCount, 256);
    QVERIFY(p.gaps.isEmpty());
}

void TestAcpTranscriptTruncation::exampleShape_keepsSummaryAndLiveTail()
{
    // user / t0 t1 t2 / lastAsst / goal / t3 t4 / lastAsst / user / e0 e1 e2
    QVector<AcpMessage> msgs{
        makeMsg("user"),
        makeMsg("assistant"),
        makeMsg("user", true),
        makeMsg("assistant"),
        makeMsg("user"),
    };
    QVector<AcpTimelineEntry> tl{
        msgEntry(0),
        toolEntry(QStringLiteral("t0")),
        toolEntry(QStringLiteral("t1")),
        toolEntry(QStringLiteral("t2")),
        msgEntry(1),
        msgEntry(2),
        toolEntry(QStringLiteral("t3")),
        toolEntry(QStringLiteral("t4")),
        msgEntry(3),
        msgEntry(4),
        toolEntry(QStringLiteral("e0")),
        toolEntry(QStringLiteral("e1")),
        toolEntry(QStringLiteral("e2")),
    };
    // 13 events. Cap 8: collapse two completed turns (hide t0-t2 and t3-t4),
    // live turn (user + 3 events) still fits in the remainder.
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 8);
    QCOMPARE(p.visibleCount, 8);
    QVERIFY(p.visible.at(0));  // user
    QVERIFY(!p.visible.at(1));
    QVERIFY(!p.visible.at(2));
    QVERIFY(!p.visible.at(3));
    QVERIFY(p.visible.at(4));  // last assistant of turn 1
    QVERIFY(p.visible.at(5));  // goal
    QVERIFY(!p.visible.at(6));
    QVERIFY(!p.visible.at(7));
    QVERIFY(p.visible.at(8));  // last assistant of turn 2
    QVERIFY(p.visible.at(9));  // live user
    QVERIFY(p.visible.at(10)); // live e0
    QVERIFY(p.visible.at(11)); // live e1
    QVERIFY(p.visible.at(12)); // live e2

    QCOMPARE(p.gaps.size(), 2);
    QCOMPARE(p.gaps.at(0).hiddenCount, 3);
    QCOMPARE(p.gaps.at(0).beforeIndex, 4); // before last assistant of turn 1
    QCOMPARE(p.gaps.at(1).hiddenCount, 2);
    QCOMPARE(p.gaps.at(1).beforeIndex, 8); // before last assistant of turn 2
}

void TestAcpTranscriptTruncation::keepsCapVisibleNotCollapseAllCompletedTurns()
{
    // 3 turns × (user + 30 tools + assistant) = 96 events. Cap 50.
    // Collapsing every completed turn would hide 60 tools and leave 36 —
    // the screenshot bug. Correct: hide only 46 oldest hideable, keep 50.
    QVector<AcpMessage> msgs;
    QVector<AcpTimelineEntry> tl;
    for (int t = 0; t < 3; ++t) {
        msgs.append(makeMsg("user"));
        msgs.append(makeMsg("assistant"));
        tl.append(msgEntry(t * 2));
        for (int i = 0; i < 30; ++i)
            tl.append(toolEntry(QStringLiteral("t%1_%2").arg(t).arg(i)));
        tl.append(msgEntry(t * 2 + 1));
    }
    QCOMPARE(tl.size(), 96);
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 50);
    QCOMPARE(p.visibleCount, 50);
    QCOMPARE(countVisible(p), 50);

    // Turn 0: all 30 tools hidden, user + last assistant kept.
    QVERIFY(p.visible.at(0));
    for (int i = 1; i <= 30; ++i)
        QVERIFY(!p.visible.at(i));
    QVERIFY(p.visible.at(31));

    // Live turn (index 64..95): still fully visible.
    for (int i = 64; i < 96; ++i)
        QVERIFY(p.visible.at(i));
}

void TestAcpTranscriptTruncation::completedTurnPinsMayExceedCap()
{
    // 6 completed turns of (user + last assistant) = 12 pins, cap 5.
    // Nothing hideable remains after collapse, so visibleCount stays 12.
    QVector<AcpMessage> msgs;
    QVector<AcpTimelineEntry> tl;
    for (int i = 0; i < 6; ++i) {
        msgs.append(makeMsg("user"));
        msgs.append(makeMsg("assistant"));
        tl.append(msgEntry(i * 2));
        tl.append(toolEntry(QStringLiteral("old%1").arg(i)));
        tl.append(msgEntry(i * 2 + 1));
    }
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 5);
    QCOMPARE(p.visibleCount, 12);
    for (int i = 0; i < 6; ++i) {
        QVERIFY(p.visible.at(i * 3));     // user
        QVERIFY(!p.visible.at(i * 3 + 1)); // tool
        QVERIFY(p.visible.at(i * 3 + 2)); // last assistant
    }
}

void TestAcpTranscriptTruncation::liveTurnKeepsNewestToFitBudget()
{
    QVector<AcpMessage> msgs{makeMsg("user"), makeMsg("assistant"), makeMsg("user")};
    QVector<AcpTimelineEntry> tl{
        msgEntry(0),
        toolEntry(QStringLiteral("old0")),
        toolEntry(QStringLiteral("old1")),
        msgEntry(1),
        msgEntry(2),
    };
    for (int i = 0; i < 10; ++i)
        tl.append(toolEntry(QStringLiteral("live%1").arg(i)));
    // After completed collapse: user + lastAsst + liveUser + 10 live = 13.
    // Cap 6 → hide 7 oldest live agents, keep live user + 3 newest (incl. last).
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 6);
    QCOMPARE(p.visibleCount, 6);
    QVERIFY(p.visible.at(0));
    QVERIFY(!p.visible.at(1));
    QVERIFY(!p.visible.at(2));
    QVERIFY(p.visible.at(3)); // completed summary
    QVERIFY(p.visible.at(4)); // live user
    for (int i = 5; i < 12; ++i)
        QVERIFY(!p.visible.at(i)); // oldest live hidden
    QVERIFY(p.visible.at(12));
    QVERIFY(p.visible.at(13));
    QVERIFY(p.visible.at(14)); // last live agent
}

void TestAcpTranscriptTruncation::neverHidesUserOrGoalOrSystem()
{
    QVector<AcpMessage> msgs;
    QVector<AcpTimelineEntry> tl;
    for (int i = 0; i < 20; ++i) {
        msgs.append(makeMsg("user"));
        tl.append(msgEntry(i));
    }
    msgs.append(makeMsg("user", true));
    tl.append(msgEntry(20));
    msgs.append(makeMsg("system"));
    tl.append(msgEntry(21));
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 5);
    QCOMPARE(p.visibleCount, 22);
    for (std::uint8_t v : p.visible)
        QVERIFY(v);
    QVERIFY(p.gaps.isEmpty());
}

void TestAcpTranscriptTruncation::systemDoesNotStealCompletedSummary()
{
    // user / t1 / system / t2 / assistant / user2
    // If system were a turn anchor, t1 would be kept as turn-1's "summary".
    QVector<AcpMessage> msgs{
        makeMsg("user"), makeMsg("system"), makeMsg("assistant"), makeMsg("user")
    };
    QVector<AcpTimelineEntry> tl{
        msgEntry(0),
        toolEntry(QStringLiteral("t1")),
        msgEntry(1),
        toolEntry(QStringLiteral("t2")),
        msgEntry(2),
        msgEntry(3),
    };
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 4);
    QVERIFY(p.visible.at(0));  // user
    QVERIFY(!p.visible.at(1)); // t1 hidden — not a fake summary
    QVERIFY(p.visible.at(2));  // system stays
    QVERIFY(!p.visible.at(3)); // t2 hidden
    QVERIFY(p.visible.at(4));  // real last assistant
    QVERIFY(p.visible.at(5));  // user2
    QCOMPARE(p.visibleCount, 4);
}

void TestAcpTranscriptTruncation::goalMessageSplitsTurn()
{
    QVector<AcpMessage> msgs{
        makeMsg("user"),
        makeMsg("assistant"),
        makeMsg("user", true),
        makeMsg("assistant"),
    };
    QVector<AcpTimelineEntry> tl{
        msgEntry(0),
        toolEntry(QStringLiteral("t1")),
        toolEntry(QStringLiteral("t2")),
        msgEntry(1),
        msgEntry(2), // goal
        toolEntry(QStringLiteral("t3")),
        msgEntry(3),
    };
    // 7 events, cap 4. Completed turn hides t1 t2. Live is goal+t3+asst = 3,
    // plus completed user+asst = 5 > 4, so oldest live agent t3 is trimmed.
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 4);
    QVERIFY(p.visible.at(0));  // user
    QVERIFY(!p.visible.at(1));
    QVERIFY(!p.visible.at(2));
    QVERIFY(p.visible.at(3));  // last assistant before goal
    QVERIFY(p.visible.at(4));  // goal — never cut
    QVERIFY(!p.visible.at(5)); // t3 trimmed to fit total cap
    QVERIFY(p.visible.at(6));  // live last assistant
    QCOMPARE(p.visibleCount, 4);
    QCOMPARE(p.gaps.size(), 2);
    QCOMPARE(p.gaps.at(0).beforeIndex, 3);
    QCOMPARE(p.gaps.at(0).hiddenCount, 2);
    QCOMPARE(p.gaps.at(1).beforeIndex, 6);
    QCOMPARE(p.gaps.at(1).hiddenCount, 1);
}

void TestAcpTranscriptTruncation::lastAgentMayBeToolCall()
{
    QVector<AcpMessage> msgs{makeMsg("user"), makeMsg("user")};
    QVector<AcpTimelineEntry> tl{
        msgEntry(0),
        toolEntry(QStringLiteral("old")),
        toolEntry(QStringLiteral("summary")),
        msgEntry(1),
    };
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 2);
    QVERIFY(p.visible.at(0));
    QVERIFY(!p.visible.at(1));
    QVERIFY(p.visible.at(2)); // last agent of completed turn is the tool
    QVERIFY(p.visible.at(3));
    QCOMPARE(p.visibleCount, 3); // pins (2 users + 1 summary) exceed cap 2
}

void TestAcpTranscriptTruncation::emptyTimeline()
{
    const Plan p = AcpTranscriptTruncation::compute({}, {}, 256);
    QCOMPARE(p.visibleCount, 0);
    QVERIFY(p.gaps.isEmpty());
}

void TestAcpTranscriptTruncation::gapsSitBetweenAnchorAndSummary_notAtHead()
{
    QVector<AcpMessage> msgs{
        makeMsg("user"), makeMsg("assistant"),
        makeMsg("user"), makeMsg("assistant")
    };
    QVector<AcpTimelineEntry> tl{
        msgEntry(0),
        toolEntry(QStringLiteral("a")),
        toolEntry(QStringLiteral("b")),
        msgEntry(1),
        msgEntry(2),
        toolEntry(QStringLiteral("c")),
        msgEntry(3),
    };
    // Cap 4: hide a,b in completed turn AND c in live turn (5→4). Two gaps,
    // each in front of that turn's last assistant — not a single blob at head.
    const Plan p = AcpTranscriptTruncation::compute(tl, msgs, 4);
    QCOMPARE(p.gaps.size(), 2);
    QCOMPARE(p.gaps.at(0).hiddenCount, 2);
    QCOMPARE(p.gaps.at(0).beforeIndex, 3);
    QCOMPARE(p.gaps.at(1).hiddenCount, 1);
    QCOMPARE(p.gaps.at(1).beforeIndex, 6);
    QVERIFY(p.visible.at(0)); // user still first visible; no gap before it
}

QTEST_MAIN(TestAcpTranscriptTruncation)
#include "test_acp_transcript_truncation.moc"
