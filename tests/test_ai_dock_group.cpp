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

#include "AiDockGroup.h"

class TestAiDockGroup : public QObject
{
    Q_OBJECT

private slots:
    void groupKey_localUsesCleanPathPrefix();
    void groupKey_sshIncludesProfileId();
    void groupKey_differentProfilesAreDistinct();
    void basename_usesLastPathSegment();
    void basename_emptyFallsBackToCwd();
    void windowTitle_singletonIsBasenameOnly();
    void windowTitle_groupedAppendsColon();
    void objectName_knownSha256Vector();
    void objectName_stableAndDistinct();
    void compact_closingCurrentPrefersLeft();
    void compact_closingFirstCurrentStaysZero();
    void compact_closingNonCurrentShiftsIndex();
    void tooltipPreview_emptyAndWhitespace();
    void tooltipPreview_shortUnchanged();
    void tooltipPreview_collapsesWhitespace();
    void tooltipPreview_truncatesLongWithEllipsis();
    void tooltipPreview_chopsHighSurrogateAtCut();
};

void TestAiDockGroup::groupKey_localUsesCleanPathPrefix()
{
    QCOMPARE(aiDockGroupKey(QStringLiteral("/home/me/sandbox-2d"), QString()),
             QStringLiteral("local:/home/me/sandbox-2d"));
}

void TestAiDockGroup::groupKey_sshIncludesProfileId()
{
    QCOMPARE(aiDockGroupKey(QStringLiteral("/home/me/app"), QStringLiteral("prof-a")),
             QStringLiteral("ssh:prof-a:/home/me/app"));
}

void TestAiDockGroup::groupKey_differentProfilesAreDistinct()
{
    const QString a = aiDockGroupKey(QStringLiteral("/srv/app"), QStringLiteral("p1"));
    const QString b = aiDockGroupKey(QStringLiteral("/srv/app"), QStringLiteral("p2"));
    QVERIFY(a != b);
}

void TestAiDockGroup::basename_usesLastPathSegment()
{
    QCOMPARE(aiDockProjectBasename(QStringLiteral("/home/me/sandbox-2d")),
             QStringLiteral("sandbox-2d"));
}

void TestAiDockGroup::basename_emptyFallsBackToCwd()
{
    QCOMPARE(aiDockProjectBasename(QString()), QString());
}

void TestAiDockGroup::windowTitle_singletonIsBasenameOnly()
{
    QCOMPARE(aiDockWindowTitle(QStringLiteral("sandbox-2d"), 1),
             QStringLiteral("sandbox-2d"));
    QCOMPARE(aiDockWindowTitle(QStringLiteral("sandbox-2d"), 0),
             QStringLiteral("sandbox-2d"));
}

void TestAiDockGroup::windowTitle_groupedAppendsColon()
{
    QCOMPARE(aiDockWindowTitle(QStringLiteral("sandbox-2d"), 2),
             QStringLiteral("sandbox-2d:"));
    QCOMPARE(aiDockWindowTitle(QStringLiteral("sandbox-2d"), 3),
             QStringLiteral("sandbox-2d:"));
}

void TestAiDockGroup::objectName_knownSha256Vector()
{
    // FIPS 180-2 SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    QCOMPARE(aiDockObjectName(QStringLiteral("abc")),
             QStringLiteral("AiAgentDock_ba7816bf8f01cfea"));
}

void TestAiDockGroup::objectName_stableAndDistinct()
{
    const QString a = aiDockObjectName(QStringLiteral("local:/tmp/foo"));
    QCOMPARE(aiDockObjectName(QStringLiteral("local:/tmp/foo")), a);
    QVERIFY(aiDockObjectName(QStringLiteral("local:/tmp/bar")) != a);
    QCOMPARE(a.left(12), QStringLiteral("AiAgentDock_"));
    QCOMPARE(a.size(), 12 + 16);
}

void TestAiDockGroup::compact_closingCurrentPrefersLeft()
{
    // [1][2][3], current=2 (index 1), close 2 → current becomes 1 (index 0)
    QCOMPARE(aiDockCompactAfterClose(1, 1, 3), 0);
    // current=3 (index 2), close 3 → current becomes 2 (index 1)
    QCOMPARE(aiDockCompactAfterClose(2, 2, 3), 1);
}

void TestAiDockGroup::compact_closingFirstCurrentStaysZero()
{
    // current=1 (index 0), close 1 of 3 → old 2 is now index 0
    QCOMPARE(aiDockCompactAfterClose(0, 0, 3), 0);
}

void TestAiDockGroup::compact_closingNonCurrentShiftsIndex()
{
    // current=3 (index 2), close 1 (index 0) → current shifts to 1
    QCOMPARE(aiDockCompactAfterClose(2, 0, 3), 1);
    // current=1 (index 0), close 3 (index 2) → current stays 0
    QCOMPARE(aiDockCompactAfterClose(0, 2, 3), 0);
}

void TestAiDockGroup::tooltipPreview_emptyAndWhitespace()
{
    QCOMPARE(aiDockSessionTooltipPreview(QString()), QString());
    QCOMPARE(aiDockSessionTooltipPreview(QStringLiteral("  \n\t ")), QString());
}

void TestAiDockGroup::tooltipPreview_shortUnchanged()
{
    QCOMPARE(aiDockSessionTooltipPreview(QStringLiteral("fix crash")),
             QStringLiteral("fix crash"));
}

void TestAiDockGroup::tooltipPreview_collapsesWhitespace()
{
    QCOMPARE(aiDockSessionTooltipPreview(QStringLiteral("hello\n\n  world\t!")),
             QStringLiteral("hello world !"));
}

void TestAiDockGroup::tooltipPreview_truncatesLongWithEllipsis()
{
    const QString raw(kAiDockSessionTooltipMaxChars + 10, QLatin1Char('x'));
    const QString got = aiDockSessionTooltipPreview(raw);
    QCOMPARE(got.size(), kAiDockSessionTooltipMaxChars + 1);
    QCOMPARE(got.left(kAiDockSessionTooltipMaxChars),
             QString(kAiDockSessionTooltipMaxChars, QLatin1Char('x')));
    QCOMPARE(got.back(), QChar(0x2026));
}

void TestAiDockGroup::tooltipPreview_chopsHighSurrogateAtCut()
{
    QString raw(kAiDockSessionTooltipMaxChars - 1, QLatin1Char('a'));
    raw.append(QChar(0xD83D));
    raw.append(QChar(0xDE00));
    const QString got = aiDockSessionTooltipPreview(raw);
    QCOMPARE(got.left(kAiDockSessionTooltipMaxChars - 1),
             QString(kAiDockSessionTooltipMaxChars - 1, QLatin1Char('a')));
    QCOMPARE(got.back(), QChar(0x2026));
    QVERIFY(!got.at(got.size() - 2).isHighSurrogate());
}

QTEST_APPLESS_MAIN(TestAiDockGroup)
#include "test_ai_dock_group.moc"
