/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// Regression test for the intermittent crash when deleting a folder inside a
// git submodule. SubmoduleStatusFetcher spawns one QProcess per submodule; a
// killed/crashed process emits BOTH errorOccurred(Crashed) and
// finished(_, CrashExit). The old onTaskFinished was not idempotent — the
// second signal dereferenced an already-freed/nulled QProcess (UAF / null
// deref) and double-decremented m_inflight. These tests drive the kill() path
// deterministically via the test seam (no real submodule, no git needed) and
// assert: no crash, entriesReady emitted exactly once, inflight never stranded.

#include <QtTest>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "SubmoduleStatusFetcher.h"

namespace {
// A long-lived, killable child process that needs no git and no repo, so the
// per-task timeout reliably fires kill() while it is still running — producing
// the errorOccurred + finished signal pair on one QProcess.
void longLivedSpawn(QString &program, QStringList &args)
{
#ifdef Q_OS_WIN
    // ping -n N 127.0.0.1 sleeps ~ (N-1) seconds without extra tooling.
    program = QStringLiteral("ping");
    args = { QStringLiteral("-n"), QStringLiteral("30"), QStringLiteral("127.0.0.1") };
#else
    program = QStringLiteral("sleep");
    args = { QStringLiteral("30") };
#endif
}
} // namespace

class TestSubmoduleStatusFetcher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void killedSingleTask_emitsOnceNoCrash();
    void killedMultipleTasks_inflightExactlyOnceEach();
    void refetchDuringInflight_noCrash();
    void cwdDeletedDuringInflight_noCrash();
};

void TestSubmoduleStatusFetcher::initTestCase()
{
    // entriesReady carries GitStatusEntries (QVector<GitStatusEntry>). Production
    // uses a same-thread direct connection so the type is never marshalled, but
    // QSignalSpy requires a registered metatype to record the argument and
    // connect at all — register it here so spy.wait() actually observes emits.
    qRegisterMetaType<GitStatusEntries>("GitStatusEntries");
}

void TestSubmoduleStatusFetcher::killedSingleTask_emitsOnceNoCrash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SubmoduleStatusFetcher fetcher;
    QString prog;
    QStringList args;
    longLivedSpawn(prog, args);
    fetcher.setSpawnOverrideForTesting(prog, args);
    fetcher.setTimeoutMsForTesting(150); // kill fast → drive the double-signal

    QSignalSpy spy(&fetcher, &SubmoduleStatusFetcher::entriesReady);

    SubmoduleStatusFetcher::Submodule s;
    s.absPath = dir.path();
    s.relFromRoot = QStringLiteral("sub");
    fetcher.fetch({ s });

    // The timeout (150ms) kills the process, which emits BOTH errorOccurred and
    // finished. Wait long enough for both to be delivered. Pre-fix this either
    // crashed or double-counted m_inflight.
    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 1);              // emitted exactly once
    QVERIFY(!fetcher.isRunning());          // inflight drained, not stranded
}

void TestSubmoduleStatusFetcher::killedMultipleTasks_inflightExactlyOnceEach()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SubmoduleStatusFetcher fetcher;
    QString prog;
    QStringList args;
    longLivedSpawn(prog, args);
    fetcher.setSpawnOverrideForTesting(prog, args);
    fetcher.setTimeoutMsForTesting(150);

    QSignalSpy spy(&fetcher, &SubmoduleStatusFetcher::entriesReady);

    // Three concurrent tasks all get killed by the timeout, each producing the
    // double-signal pair. If any one double-decrements m_inflight, the counter
    // hits zero early and entriesReady fires before the others are done — or
    // fires more than once. Assert exactly one emission after all three settle.
    QVector<SubmoduleStatusFetcher::Submodule> subs;
    for (int i = 0; i < 3; ++i) {
        SubmoduleStatusFetcher::Submodule s;
        s.absPath = dir.path();
        s.relFromRoot = QStringLiteral("sub%1").arg(i);
        subs.append(s);
    }
    fetcher.fetch(subs);

    QVERIFY(spy.wait(5000));
    // Give any erroneous extra emission a chance to surface, then assert once.
    QTest::qWait(300);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!fetcher.isRunning());
}

void TestSubmoduleStatusFetcher::refetchDuringInflight_noCrash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    SubmoduleStatusFetcher fetcher;
    QString prog;
    QStringList args;
    longLivedSpawn(prog, args);
    fetcher.setSpawnOverrideForTesting(prog, args);
    fetcher.setTimeoutMsForTesting(150);

    QSignalSpy spy(&fetcher, &SubmoduleStatusFetcher::entriesReady);

    SubmoduleStatusFetcher::Submodule s;
    s.absPath = dir.path();
    s.relFromRoot = QStringLiteral("sub");

    // Start a round, then immediately start another while the first is still
    // in-flight. cancelAll() must tear down the first round's processes and
    // pending timers without a use-after-free when those timers later fire.
    fetcher.fetch({ s });
    fetcher.fetch({ s });

    QVERIFY(spy.wait(5000));
    // Only the second round should emit; the first was cancelled. Wait past the
    // first round's timeout window to prove no stale timer crashes us.
    QTest::qWait(300);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!fetcher.isRunning());
}

void TestSubmoduleStatusFetcher::cwdDeletedDuringInflight_noCrash()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sub = dir.path() + QStringLiteral("/sub");
    QVERIFY(QDir().mkpath(sub));

    SubmoduleStatusFetcher fetcher;
    QString prog;
    QStringList args;
    longLivedSpawn(prog, args);
    fetcher.setSpawnOverrideForTesting(prog, args);
    fetcher.setTimeoutMsForTesting(2000);

    QSignalSpy spy(&fetcher, &SubmoduleStatusFetcher::entriesReady);

    SubmoduleStatusFetcher::Submodule s;
    s.absPath = sub;
    s.relFromRoot = QStringLiteral("sub");
    fetcher.fetch({ s });

    // Agent rm -rf of the submodule working tree while git status is running.
    QVERIFY(QDir(sub).removeRecursively());
    fetcher.fetch({ s });

    // Failed-to-start (deleted cwd) can emit entriesReady synchronously inside
    // fetch(); spy.wait() would then hang waiting for a second emission.
    if (spy.count() == 0)
        QVERIFY(spy.wait(5000));
    QTest::qWait(300);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!fetcher.isRunning());
}

QTEST_MAIN(TestSubmoduleStatusFetcher)
#include "test_submodule_status_fetcher.moc"
