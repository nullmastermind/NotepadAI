/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <QtTest>

#include "TerminalProcessStats.h"

using Sample = TerminalProcessStats::ProcessSample;
using CpuState = TerminalProcessStats::TreeCpuState;
using Stats = TerminalProcessStats::TreeStats;

class TestTerminalProcessStats : public QObject
{
    Q_OBJECT

private slots:
    void zeroRootPid_isInvalid();
    void emptySamples_isInvalid();
    void missingRoot_isInvalid();
    void singleProcess_rssIsThatProcess();
    void firstSample_cpuIsZero();
    void childAndGrandchild_rssIsSum();
    void sibling_isExcluded();
    void cpuPercent_oneCoreOfEight();
    void cpuPercent_clampsAt100();
    void pidReuse_cpuResetsToZero();
    void zeroWallDelta_cpuIsZero();
    void cpuLabel_invalidIsDash();
    void cpuLabel_roundsHalfAway();
    void coreLoadLabel_oneLinePerCoreIncludesIdle();
    void coreLoadLabel_emptyWhenNoSample();
    void coreLoadLabel_listsEveryCore();
    void coreTip_staleVisibleTipNeedsPush();
    void coreTip_unchangedOrClosedDoesNotPush();
    void coreTip_hoverOpensTipOnceTextExists();
    void statsPoll_openTooltipIsFaster();
    void processorLoad_busyIsNonIdleFraction();
    void processorLoad_counterResetIsZero();
    void enumerateProcessorTicks_matchesLogicalCpus();
    void ramLabel_invalidIsDash();
    void ramLabel_mbTruncates();
    void ramLabel_gbAt1024MiB();
    void enumerate_includesThisProcess();

};

static Stats aggregate(const Sample *samples, int n, quint32 root,
                       const CpuState *prev, quint64 now, int cpus,
                       CpuState *out = nullptr)
{
    return TerminalProcessStats::aggregateTree(samples, n, root, prev, now, cpus, out);
}

void TestTerminalProcessStats::zeroRootPid_isInvalid()
{
    const Sample samples[] = {{1, 0, 0, 4096}};
    const Stats s = aggregate(samples, 1, 0, nullptr, 10'000'000, 8);
    QCOMPARE(s.valid, false);
}

void TestTerminalProcessStats::emptySamples_isInvalid()
{
    const Stats s = aggregate(nullptr, 0, 10, nullptr, 10'000'000, 8);
    QCOMPARE(s.valid, false);
}

void TestTerminalProcessStats::missingRoot_isInvalid()
{
    const Sample samples[] = {{2, 1, 0, 4096}};
    const Stats s = aggregate(samples, 1, 10, nullptr, 10'000'000, 8);
    QCOMPARE(s.valid, false);
}

void TestTerminalProcessStats::singleProcess_rssIsThatProcess()
{
    const Sample samples[] = {{10, 1, 0, 184ull * 1024ull * 1024ull}};
    CpuState out{};
    const Stats s = aggregate(samples, 1, 10, nullptr, 10'000'000, 8, &out);
    QCOMPARE(s.valid, true);
    QCOMPARE(s.rssBytes, 184ull * 1024ull * 1024ull);
    QCOMPARE(out.rootPid, 10u);
    QCOMPARE(out.wall100ns, 10'000'000ull);
}

void TestTerminalProcessStats::firstSample_cpuIsZero()
{
    const Sample samples[] = {{10, 1, 50'000'000ull, 4096}};
    const Stats s = aggregate(samples, 1, 10, nullptr, 10'000'000, 8);
    QCOMPARE(s.valid, true);
    QCOMPARE(s.cpuPercent, 0.0);
}

void TestTerminalProcessStats::childAndGrandchild_rssIsSum()
{
    const Sample samples[] = {
        {1, 0, 0, 10},
        {2, 1, 0, 20},
        {3, 2, 0, 30},
        {4, 99, 0, 1000},
    };
    const Stats s = aggregate(samples, 4, 1, nullptr, 1, 1);
    QCOMPARE(s.valid, true);
    QCOMPARE(s.rssBytes, 60ull);
}

void TestTerminalProcessStats::sibling_isExcluded()
{
    const Sample samples[] = {
        {10, 1, 0, 5},
        {11, 1, 0, 7},
    };
    const Stats s = aggregate(samples, 2, 10, nullptr, 1, 1);
    QCOMPARE(s.valid, true);
    QCOMPARE(s.rssBytes, 5ull);
}

void TestTerminalProcessStats::cpuPercent_oneCoreOfEight()
{
    const Sample samples[] = {{10, 1, 20'000'000ull, 1}};
    CpuState prev{};
    prev.rootPid = 10;
    prev.cpuTime100ns = 10'000'000ull;
    prev.wall100ns = 10'000'000ull;
    const Stats s = aggregate(samples, 1, 10, &prev, 20'000'000ull, 8);
    QCOMPARE(s.valid, true);
    QCOMPARE(s.cpuPercent, 12.5);
}

void TestTerminalProcessStats::cpuPercent_clampsAt100()
{
    const Sample samples[] = {{10, 1, 50'000'000ull, 1}};
    CpuState prev{};
    prev.rootPid = 10;
    prev.cpuTime100ns = 0;
    prev.wall100ns = 0;
    // prev wall 0 → no delta. Use a real previous wall.
    prev.wall100ns = 10'000'000ull;
    const Stats s = aggregate(samples, 1, 10, &prev, 20'000'000ull, 1);
    QCOMPARE(s.valid, true);
    QCOMPARE(s.cpuPercent, 100.0);
}

void TestTerminalProcessStats::pidReuse_cpuResetsToZero()
{
    const Sample samples[] = {{10, 1, 1'000ull, 1}};
    CpuState prev{};
    prev.rootPid = 10;
    prev.cpuTime100ns = 9'000'000ull;
    prev.wall100ns = 10'000'000ull;
    const Stats s = aggregate(samples, 1, 10, &prev, 20'000'000ull, 8);
    QCOMPARE(s.valid, true);
    QCOMPARE(s.cpuPercent, 0.0);
}

void TestTerminalProcessStats::zeroWallDelta_cpuIsZero()
{
    const Sample samples[] = {{10, 1, 20'000'000ull, 1}};
    CpuState prev{};
    prev.rootPid = 10;
    prev.cpuTime100ns = 10'000'000ull;
    prev.wall100ns = 20'000'000ull;
    const Stats s = aggregate(samples, 1, 10, &prev, 20'000'000ull, 8);
    QCOMPARE(s.valid, true);
    QCOMPARE(s.cpuPercent, 0.0);
}

void TestTerminalProcessStats::cpuLabel_invalidIsDash()
{
    QCOMPARE(TerminalProcessStats::cpuLabel(false, 12.0), QStringLiteral("CPU —"));
}

void TestTerminalProcessStats::cpuLabel_roundsHalfAway()
{
    QCOMPARE(TerminalProcessStats::cpuLabel(true, 12.4), QStringLiteral("CPU 12%"));
    QCOMPARE(TerminalProcessStats::cpuLabel(true, 12.5), QStringLiteral("CPU 13%"));
    QCOMPARE(TerminalProcessStats::cpuLabel(true, 140.0), QStringLiteral("CPU 100%"));
    QCOMPARE(TerminalProcessStats::cpuLabel(true, -4.0), QStringLiteral("CPU 0%"));
}

void TestTerminalProcessStats::coreLoadLabel_oneLinePerCoreIncludesIdle()
{
    // Breaks if a 0% core is omitted, if indexes stay 0-based,
    // if cores are joined with spaces, or if 80.4 is not half-away to 80.
    const double cores[] = {0.0, 80.4, 0.4};
    QCOMPARE(TerminalProcessStats::coreLoadLabel(cores, 3),
             QStringLiteral("core1: 0%\ncore2: 80%\ncore3: 0%"));
}

void TestTerminalProcessStats::coreLoadLabel_emptyWhenNoSample()
{
    // Breaks if a missing sample still fabricates a core line.
    const double cores[] = {12.0};
    QCOMPARE(TerminalProcessStats::coreLoadLabel(nullptr, 3), QString());
    QCOMPARE(TerminalProcessStats::coreLoadLabel(cores, 0), QString());
}

void TestTerminalProcessStats::coreLoadLabel_listsEveryCore()
{
    // Breaks if even spread collapses or if only the hottest cores stay.
    const double cores[] = {10.4, 10.4, 10.4, 10.4, 10.4, 90.0};
    QCOMPARE(TerminalProcessStats::coreLoadLabel(cores, 6),
             QStringLiteral("core1: 10%\ncore2: 10%\ncore3: 10%\n"
                            "core4: 10%\ncore5: 10%\ncore6: 90%"));
}

void TestTerminalProcessStats::coreTip_staleVisibleTipNeedsPush()
{
    // Breaks if a visible tooltip over the CPU label keeps the text it opened with.
    QVERIFY(TerminalProcessStats::coreTipNeedsPush(true, true,
                                                   QStringLiteral("core1: 1%"),
                                                   QStringLiteral("core1: 9%")));
}

void TestTerminalProcessStats::coreTip_unchangedOrClosedDoesNotPush()
{
    // Breaks if an unchanged tip is reshown, or if we pop a tip the pointer is not on.
    const QString same = QStringLiteral("core1: 1%");
    QVERIFY(!TerminalProcessStats::coreTipNeedsPush(true, true, same, same));
    QVERIFY(!TerminalProcessStats::coreTipNeedsPush(false, false, QString(), same));
    QVERIFY(!TerminalProcessStats::coreTipNeedsPush(true, false, QStringLiteral("other"), same));
    QVERIFY(!TerminalProcessStats::coreTipNeedsPush(false, true, QString(), QString()));
}

void TestTerminalProcessStats::coreTip_hoverOpensTipOnceTextExists()
{
    // Breaks if the first sample arrives while the pointer is on CPU and the tip stays closed.
    QVERIFY(TerminalProcessStats::coreTipNeedsPush(false, true, QString(),
                                                   QStringLiteral("core1: 1%")));
}

void TestTerminalProcessStats::statsPoll_openTooltipIsFaster()
{
    // Breaks if a watched tooltip stays on the idle one-second poll.
    const int idle = TerminalProcessStats::statsPollMs(false);
    const int open = TerminalProcessStats::statsPollMs(true);
    QVERIFY(open < idle);
    QVERIFY(open <= 250);
    QCOMPARE(idle, 1000);
}


void TestTerminalProcessStats::processorLoad_busyIsNonIdleFraction()
{
    // Breaks if idle is treated as busy, if we divide by logical CPU count,
    // or if a fully idle core is reported as 100.
    using Tick = TerminalProcessStats::ProcessorTick;
    const Tick prev[] = {{100, 200}, {50, 100}};
    const Tick now[] = {{100, 300}, {150, 200}};
    double out[2] = {1.0, 1.0};
    TerminalProcessStats::processorLoad(now, 2, prev, out);
    QCOMPARE(out[0], 100.0);
    QCOMPARE(out[1], 0.0);
}

void TestTerminalProcessStats::processorLoad_counterResetIsZero()
{
    // Breaks if a wrapped counter is treated as a huge busy delta.
    using Tick = TerminalProcessStats::ProcessorTick;
    const Tick prev[] = {{10, 1000}};
    const Tick now[] = {{10, 20}};
    double out[1] = {50.0};
    TerminalProcessStats::processorLoad(now, 1, prev, out);
    QCOMPARE(out[0], 0.0);
}


void TestTerminalProcessStats::enumerateProcessorTicks_matchesLogicalCpus()
{
    QVector<TerminalProcessStats::ProcessorTick> ticks;
    QVERIFY(TerminalProcessStats::enumerateProcessorTicks(ticks));
    QCOMPARE(ticks.size(), TerminalProcessStats::logicalCpuCount());
    for (const auto &tick : ticks)
        QVERIFY(tick.total100ns >= tick.idle100ns);
}


void TestTerminalProcessStats::ramLabel_invalidIsDash()
{
    QCOMPARE(TerminalProcessStats::ramLabel(false, 99), QStringLiteral("RAM —"));
}

void TestTerminalProcessStats::ramLabel_mbTruncates()
{
    QCOMPARE(TerminalProcessStats::ramLabel(true, 0), QStringLiteral("RAM 0 MB"));
    QCOMPARE(TerminalProcessStats::ramLabel(true, 184ull * 1024ull * 1024ull),
             QStringLiteral("RAM 184 MB"));
}

void TestTerminalProcessStats::ramLabel_gbAt1024MiB()
{
    QCOMPARE(TerminalProcessStats::ramLabel(true, 1024ull * 1024ull * 1024ull),
             QStringLiteral("RAM 1.0 GB"));
    QCOMPARE(TerminalProcessStats::ramLabel(true, 1024ull * 1024ull * 1024ull
                                                   + 512ull * 1024ull * 1024ull),
             QStringLiteral("RAM 1.5 GB"));
}

void TestTerminalProcessStats::enumerate_includesThisProcess()
{
    QVector<Sample> samples;
    QVERIFY(TerminalProcessStats::enumerateProcesses(samples));
    const quint32 self = static_cast<quint32>(QCoreApplication::applicationPid());
    bool found = false;
    quint64 rss = 0;
    for (const Sample &s : samples) {
        if (s.pid == self) {
            found = true;
            rss = s.rssBytes;
            break;
        }
    }
    QVERIFY(found);
    QVERIFY(rss > 0);
}


QTEST_MAIN(TestTerminalProcessStats)
#include "test_terminal_process_stats.moc"
