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
#include <QSignalSpy>
#include <QSizePolicy>
#include <QTabBar>
#include <QToolButton>
#include <QVector>

#include "widgets/AiDockSessionStrip.h"

class TestAiDockSessionStrip : public QObject
{
    Q_OBJECT

private slots:
    void captions_areOneBasedDigits();
    void captions_singletonShowsOne();
    void current_isBold_othersRegularPlaceholder();
    void busyNonCurrent_isItalic();
    void click_emitsActivated();
    void middleClick_emitsCloseRequested_notActivated();
    void tooltip_numberShowsLastUserPreview();
    void size_matchesHint_buttonsDoNotShrink();
    void tabBar_tabSizeHint_includesFullStripWidth();
};

static QVector<AiDockSessionStrip::SlotSnapshot> threeSnapshots(bool secondBusy)
{
    QVector<AiDockSessionStrip::SlotSnapshot> snaps(3);
    snaps[0].agent = QStringLiteral("Claude");
    snaps[1].agent = QStringLiteral("Codex");
    snaps[1].busy = secondBusy;
    snaps[1].busySinceMs = secondBusy ? 1 : 0;
    snaps[2].agent = QStringLiteral("Opencode");
    return snaps;
}

static QList<QToolButton *> stripButtons(AiDockSessionStrip *strip)
{
    return strip->findChildren<QToolButton *>(Qt::FindDirectChildrenOnly);
}

void TestAiDockSessionStrip::captions_areOneBasedDigits()
{
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, threeSnapshots(false));
    const auto buttons = stripButtons(&strip);
    QCOMPARE(buttons.size(), 3);
    QCOMPARE(buttons.at(0)->text(), QStringLiteral("1"));
    QCOMPARE(buttons.at(1)->text(), QStringLiteral("2"));
    QCOMPARE(buttons.at(2)->text(), QStringLiteral("3"));
    QVERIFY(buttons.at(0)->autoRaise());
}

void TestAiDockSessionStrip::captions_singletonShowsOne()
{
    AiDockSessionStrip strip;
    QVector<AiDockSessionStrip::SlotSnapshot> snaps(1);
    snaps[0].agent = QStringLiteral("Claude");
    strip.setSlots(1, 0, snaps);
    const auto buttons = stripButtons(&strip);
    QCOMPARE(buttons.size(), 1);
    QCOMPARE(buttons.at(0)->text(), QStringLiteral("1"));
    QVERIFY(buttons.at(0)->font().bold());
}

void TestAiDockSessionStrip::current_isBold_othersRegularPlaceholder()
{
    AiDockSessionStrip strip;
    strip.setSlots(3, 1, threeSnapshots(false));
    const auto buttons = stripButtons(&strip);
    QCOMPARE(buttons.size(), 3);
    QVERIFY(buttons.at(1)->font().bold());
    QVERIFY(!buttons.at(0)->font().bold());
    QVERIFY(!buttons.at(2)->font().bold());

    const QColor currentColor = strip.palette().color(QPalette::ButtonText);
    const QColor muted = strip.palette().color(QPalette::PlaceholderText);
    QCOMPARE(buttons.at(1)->palette().color(QPalette::ButtonText), currentColor);
    QCOMPARE(buttons.at(0)->palette().color(QPalette::ButtonText), muted);
    QCOMPARE(buttons.at(2)->palette().color(QPalette::ButtonText), muted);
}

void TestAiDockSessionStrip::busyNonCurrent_isItalic()
{
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, threeSnapshots(true));
    const auto buttons = stripButtons(&strip);
    QVERIFY(!buttons.at(0)->font().italic());
    QVERIFY(buttons.at(1)->font().italic());
    QVERIFY(!buttons.at(2)->font().italic());
}

void TestAiDockSessionStrip::click_emitsActivated()
{
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, threeSnapshots(false));
    strip.show();
    QVERIFY(QTest::qWaitForWindowExposed(&strip));

    const auto buttons = stripButtons(&strip);
    QSignalSpy activated(&strip, &AiDockSessionStrip::activated);
    QSignalSpy closed(&strip, &AiDockSessionStrip::closeRequested);
    QTest::mouseClick(buttons.at(1), Qt::LeftButton);
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.takeFirst().at(0).toInt(), 1);
    QCOMPARE(closed.count(), 0);
}

void TestAiDockSessionStrip::middleClick_emitsCloseRequested_notActivated()
{
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, threeSnapshots(false));
    strip.show();
    QVERIFY(QTest::qWaitForWindowExposed(&strip));

    const auto buttons = stripButtons(&strip);
    QSignalSpy activated(&strip, &AiDockSessionStrip::activated);
    QSignalSpy closed(&strip, &AiDockSessionStrip::closeRequested);
    QTest::mouseClick(buttons.at(2), Qt::MiddleButton);
    QCOMPARE(closed.count(), 1);
    QCOMPARE(closed.takeFirst().at(0).toInt(), 2);
    QCOMPARE(activated.count(), 0);
}

void TestAiDockSessionStrip::tooltip_numberShowsLastUserPreview()
{
    auto snaps = threeSnapshots(true);
    snaps[0].lastUserPreview = QStringLiteral("fix the crash");
    snaps[2].lastUserPreview = QStringLiteral("hello");
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, snaps);
    QCOMPARE(strip.slotTooltip(0), QStringLiteral("fix the crash"));
    QCOMPARE(strip.slotTooltip(1), QString());
    QCOMPARE(strip.slotTooltip(2), QStringLiteral("hello"));
    QVERIFY(!strip.slotTooltip(0).contains(QStringLiteral("Claude")));
    QVERIFY(!strip.slotTooltip(0).contains(QStringLiteral("idle")));
}

void TestAiDockSessionStrip::size_matchesHint_buttonsDoNotShrink()
{
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, threeSnapshots(false));
    const auto buttons = stripButtons(&strip);
    QCOMPARE(buttons.size(), 3);

    int buttonWidths = 0;
    for (QToolButton *btn : buttons) {
        QCOMPARE(btn->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
        buttonWidths += btn->sizeHint().width();
    }
    buttonWidths += AiDockSessionStrip::kSpacingPx * (buttons.size() - 1);

    QCOMPARE(strip.sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
    QVERIFY(strip.sizeHint().width() >= buttonWidths);
    QCOMPARE(strip.minimumSizeHint().width(), strip.sizeHint().width());
    // QTabBar::tabSizeHint reads size(), not sizeHint (qtabbar.cpp:159).
    QCOMPARE(strip.size().width(), strip.sizeHint().width());
}

void TestAiDockSessionStrip::tabBar_tabSizeHint_includesFullStripWidth()
{
    QTabBar bar;
    bar.addTab(QStringLiteral("notepad-ade"));
    const int textOnly = bar.tabSizeHint(0).width();

    auto *strip = new AiDockSessionStrip;
    QVector<AiDockSessionStrip::SlotSnapshot> snaps(1);
    snaps[0].agent = QStringLiteral("Claude");
    strip->setSlots(1, 0, snaps);
    const int stripW = strip->sizeHint().width();
    QVERIFY(stripW > 0);
    QCOMPARE(strip->size().width(), stripW);

    bar.setTabButton(0, QTabBar::RightSide, strip);
    QCOMPARE(strip->size().width(), stripW);
    QVERIFY(bar.tabSizeHint(0).width() >= textOnly + stripW);
}

QTEST_MAIN(TestAiDockSessionStrip)
#include "test_ai_dock_session_strip.moc"
