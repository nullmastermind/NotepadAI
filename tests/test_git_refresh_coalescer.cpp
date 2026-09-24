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

#include "GitRefreshCoalescer.h"

class TestGitRefreshCoalescer : public QObject
{
    Q_OBJECT

private slots:
    void burstWiderThanOldLatch_holdsUntilQuietAfterLastEvent();
    void continuousBurst_firesWhenMaxWaitElapses_notOnEveryEdit();
    void drainWhileBurstIsYoung_holdsInsteadOfChaining();
    void drainAfterMaxWait_firesOnceWithoutOverlapping();
    void drainAfterQuietAlreadyElapsed_firesOnce();
    void explicitFollowUp_firesOnDrainEvenWhenBurstIsYoung();
    void acknowledgeRefresh_dropsPendingSoAStartedRefreshDoesNotTwin();
    void quietElapsedWhileInFlight_keepsTheFollowUp();
    void idle_drainAndQuietHold();
    void immediateFollowUpWithoutPriorDirty_firesOnceOnDrain();
    void acknowledgeRefresh_dropsImmediateFollowUp();
    void acknowledge_thenNewDirty_doesNotInheritOldBurst();
    void clockMovedBackwards_holdsAndDoesNotCrash();
};

void TestGitRefreshCoalescer::burstWiderThanOldLatch_holdsUntilQuietAfterLastEvent()
{
    GitRefreshCoalescer c;

    // 400ms gaps are wider than the old 200ms leading latch, which armed a
    // refresh on the first dirty and let it fire before the next edit landed.
    QCOMPARE(c.onDirty(0, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onDirty(400, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onDirty(800, false), GitRefreshCoalescer::Decision::Hold);

    // Quiet is measured from the last dirty (t=800), not the first. 200ms
    // after that last edit the old window would already have fired.
    QCOMPARE(c.onQuietElapsed(1000, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onQuietElapsed(1799, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onQuietElapsed(1800, false), GitRefreshCoalescer::Decision::Fire);
    QVERIFY(!c.pending());
    QCOMPARE(c.onQuietElapsed(1800, false), GitRefreshCoalescer::Decision::Hold);
}

void TestGitRefreshCoalescer::continuousBurst_firesWhenMaxWaitElapses_notOnEveryEdit()
{
    GitRefreshCoalescer c;

    // A swarm that never pauses must still update, but not on every edit.
    // 2800ms into a burst is inside the 3000ms ceiling; 3200ms is past it.
    QCOMPARE(c.onDirty(0, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onDirty(2800, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onDirty(3200, false), GitRefreshCoalescer::Decision::Fire);
    QVERIFY(!c.pending());

    // The ceiling restarts after a fire, so the next burst is not immediate.
    QCOMPARE(c.onDirty(3600, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onDirty(6400, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onDirty(6800, false), GitRefreshCoalescer::Decision::Fire);
}

void TestGitRefreshCoalescer::drainWhileBurstIsYoung_holdsInsteadOfChaining()
{
    GitRefreshCoalescer c;

    QCOMPARE(c.onDirty(0, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onQuietElapsed(1000, false), GitRefreshCoalescer::Decision::Fire);

    // More edits land while that refresh is still running.
    QCOMPARE(c.onDirty(1100, true), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onDirty(1500, true), GitRefreshCoalescer::Decision::Hold);

    // Queue drains at t=2000. The burst is 900ms old — the old controller
    // started the next full refresh here, with no quiet gap.
    QCOMPARE(c.onRefreshDrained(2000), GitRefreshCoalescer::Decision::Hold);
    QVERIFY(c.pending());

    QCOMPARE(c.onQuietElapsed(2499, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onQuietElapsed(2500, false), GitRefreshCoalescer::Decision::Fire);
}

void TestGitRefreshCoalescer::drainAfterMaxWait_firesOnceWithoutOverlapping()
{
    GitRefreshCoalescer c;

    // Edits keep arriving while a refresh is in flight, so onDirty cannot fire
    // without overlapping that refresh. The ceiling is honored when it drains.
    QCOMPARE(c.onDirty(0, true), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onDirty(2900, true), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onDirty(3200, true), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onRefreshDrained(3300), GitRefreshCoalescer::Decision::Fire);
    QVERIFY(!c.pending());
}

void TestGitRefreshCoalescer::drainAfterQuietAlreadyElapsed_firesOnce()
{
    GitRefreshCoalescer c;

    QCOMPARE(c.onDirty(100, true), GitRefreshCoalescer::Decision::Hold);
    // Refresh finishes at 2000. Quiet ended at 1100, and the burst is still
    // under the 3000ms ceiling, so this is not the max-wait path.
    QCOMPARE(c.onRefreshDrained(2000), GitRefreshCoalescer::Decision::Fire);
    QVERIFY(!c.pending());
}

void TestGitRefreshCoalescer::explicitFollowUp_firesOnDrainEvenWhenBurstIsYoung()
{
    GitRefreshCoalescer c;

    // A user refresh() that arrives while a refresh is already running must
    // still run once that one finishes, even though the agent burst is young.
    QCOMPARE(c.onDirty(0, true), GitRefreshCoalescer::Decision::Hold);
    c.requestImmediateFollowUp();
    QCOMPARE(c.onRefreshDrained(200), GitRefreshCoalescer::Decision::Fire);
    QVERIFY(!c.pending());
}

void TestGitRefreshCoalescer::acknowledgeRefresh_dropsPendingSoAStartedRefreshDoesNotTwin()
{
    GitRefreshCoalescer c;

    QCOMPARE(c.onDirty(0, false), GitRefreshCoalescer::Decision::Hold);
    c.acknowledgeRefresh();
    QVERIFY(!c.pending());
    QCOMPARE(c.onQuietElapsed(1000, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onRefreshDrained(1000), GitRefreshCoalescer::Decision::Hold);
}

void TestGitRefreshCoalescer::quietElapsedWhileInFlight_keepsTheFollowUp()
{
    GitRefreshCoalescer c;

    // The controller's quiet timer can fire while git status is still running.
    // That must not drop the follow-up, and must not start a second refresh.
    QCOMPARE(c.onDirty(0, true), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onQuietElapsed(1000, true), GitRefreshCoalescer::Decision::Hold);
    QVERIFY(c.pending());
    QCOMPARE(c.onRefreshDrained(1100), GitRefreshCoalescer::Decision::Fire);
    QVERIFY(!c.pending());
}

void TestGitRefreshCoalescer::idle_drainAndQuietHold()
{
    GitRefreshCoalescer c;

    QVERIFY(!c.pending());
    QCOMPARE(c.onQuietElapsed(5000, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onRefreshDrained(5000), GitRefreshCoalescer::Decision::Hold);
    QVERIFY(!c.pending());
}

void TestGitRefreshCoalescer::immediateFollowUpWithoutPriorDirty_firesOnceOnDrain()
{
    GitRefreshCoalescer c;

    // refresh() while busy, before any agent dirty, still owes one follow-up.
    QVERIFY(!c.pending());
    c.requestImmediateFollowUp();
    QVERIFY(c.pending());
    QCOMPARE(c.onRefreshDrained(0), GitRefreshCoalescer::Decision::Fire);
    QVERIFY(!c.pending());
    QCOMPARE(c.onRefreshDrained(0), GitRefreshCoalescer::Decision::Hold);
}

void TestGitRefreshCoalescer::acknowledgeRefresh_dropsImmediateFollowUp()
{
    GitRefreshCoalescer c;

    c.requestImmediateFollowUp();
    c.acknowledgeRefresh();
    QVERIFY(!c.pending());
    QCOMPARE(c.onRefreshDrained(0), GitRefreshCoalescer::Decision::Hold);
}

void TestGitRefreshCoalescer::acknowledge_thenNewDirty_doesNotInheritOldBurst()
{
    GitRefreshCoalescer c;

    QCOMPARE(c.onDirty(0, false), GitRefreshCoalescer::Decision::Hold);
    c.acknowledgeRefresh();
    // A leaked burst start of 0 would make this 3200ms-old and Fire.
    QCOMPARE(c.onDirty(3200, false), GitRefreshCoalescer::Decision::Hold);
    QVERIFY(c.pending());
}

void TestGitRefreshCoalescer::clockMovedBackwards_holdsAndDoesNotCrash()
{
    GitRefreshCoalescer c;

    QCOMPARE(c.onDirty(1000, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onDirty(100, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onRefreshDrained(200), GitRefreshCoalescer::Decision::Hold);
    QVERIFY(c.pending());
    // Quiet is measured from the later dirty that arrived at t=100.
    QCOMPARE(c.onQuietElapsed(1099, false), GitRefreshCoalescer::Decision::Hold);
    QCOMPARE(c.onQuietElapsed(1100, false), GitRefreshCoalescer::Decision::Fire);
}

QTEST_MAIN(TestGitRefreshCoalescer)
#include "test_git_refresh_coalescer.moc"
