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
#include <QApplication>
#include <QContextMenuEvent>
#include <QPointer>
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
    void busySlot_hasFill_idleDoesNot();
    void busyCurrentSlot_alsoFilled();
    void click_emitsActivated();
    void middleClick_emitsCloseRequested_notActivated();
    void drag_overAnotherNumber_emitsMoveRequested_notActivated();
    void waitingSlot_showsDot_othersDoNot();
    void waitingSlot_dotCoexistsWithFill();
    void waitingSlot_tooltipSaysWaiting_keepsPreview();
    void attentionColor_isAVisibleRed();
    void tooltip_numberShowsLastUserPreview();
    void tooltip_nameComesFirst();
    void rightClick_requestsContextMenuForThatNumber();
    void size_matchesHint_buttonsDoNotShrink();
    void tabBar_tabSizeHint_includesFullStripWidth();
    void deferredDelete_afterMove_detachesFromNewBar();
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

// Pixel just inside the top edge, clear of the digit and the current border.
static QColor fillProbe(QToolButton *btn)
{
    return btn->grab().toImage().pixelColor(btn->width() / 2, 2);
}

void TestAiDockSessionStrip::busySlot_hasFill_idleDoesNot()
{
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, threeSnapshots(true));
    strip.show();
    QVERIFY(QTest::qWaitForWindowExposed(&strip));
    QVERIFY(strip.busyAt(1));
    QVERIFY(!strip.busyAt(2));
    QVERIFY(!strip.busyAt(7)); // out of range is simply "no"

    // Slots 1 and 2 differ only in being busy: same digit width, both muted.
    const auto buttons = stripButtons(&strip);
    const QColor busy = fillProbe(buttons.at(1));
    const QColor idle = fillProbe(buttons.at(2));
    QVERIFY2(busy != idle, qPrintable(busy.name() + QLatin1String(" == ") + idle.name()));
    // A muted digit would fade into the fill.
    QCOMPARE(buttons.at(1)->palette().color(QPalette::ButtonText),
             strip.palette().color(QPalette::ButtonText));
    QCOMPARE(buttons.at(2)->palette().color(QPalette::ButtonText),
             strip.palette().color(QPalette::PlaceholderText));

    strip.setSlots(3, 0, threeSnapshots(false));
    QVERIFY(!strip.busyAt(1));
    QCOMPARE(fillProbe(buttons.at(1)), idle);
}

void TestAiDockSessionStrip::busyCurrentSlot_alsoFilled()
{
    AiDockSessionStrip strip;
    strip.setSlots(3, 1, threeSnapshots(true)); // the busy slot is the current one
    QVERIFY(strip.busyAt(1));
    QVERIFY(stripButtons(&strip).at(1)->font().bold());
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

// Press on button `from`, pull past the drag threshold, release over `to`.
// Qt grabs the mouse on the pressed button, so every move goes to it.
static void dragButton(AiDockSessionStrip *strip, int from, int to)
{
    const auto buttons = stripButtons(strip);
    QToolButton *src = buttons.at(from);
    QToolButton *dst = buttons.at(to);
    const QPoint start = src->rect().center();
    const QPoint target = src->mapFromGlobal(dst->mapToGlobal(dst->rect().center()));
    QTest::mousePress(src, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(src, start + QPoint(QApplication::startDragDistance() + 2, 0));
    QTest::mouseMove(src, target);
    QTest::mouseRelease(src, Qt::LeftButton, Qt::NoModifier, target);
}

void TestAiDockSessionStrip::drag_overAnotherNumber_emitsMoveRequested_notActivated()
{
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, threeSnapshots(false));
    strip.show();
    QVERIFY(QTest::qWaitForWindowExposed(&strip));

    QSignalSpy moved(&strip, &AiDockSessionStrip::moveRequested);
    QSignalSpy activated(&strip, &AiDockSessionStrip::activated);
    dragButton(&strip, 0, 2);

    QCOMPARE(moved.count(), 1);
    const QList<QVariant> args = moved.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0);
    QCOMPARE(args.at(1).toInt(), 2);
    QCOMPARE(activated.count(), 0);
}

void TestAiDockSessionStrip::waitingSlot_showsDot_othersDoNot()
{
    auto snaps = threeSnapshots(false);
    snaps[2].waiting = true;
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, snaps);
    QVERIFY(!strip.waitingAt(0));
    QVERIFY(!strip.waitingAt(1));
    QVERIFY(strip.waitingAt(2));
    QVERIFY(!strip.waitingAt(7)); // out of range is simply "no"
}

void TestAiDockSessionStrip::waitingSlot_dotCoexistsWithFill()
{
    auto snaps = threeSnapshots(true); // slot 1 is working
    snaps[1].waiting = true;           // and blocked on a permission request
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, snaps);
    QVERIFY(strip.waitingAt(1));
    QVERIFY(strip.busyAt(1));
}

void TestAiDockSessionStrip::waitingSlot_tooltipSaysWaiting_keepsPreview()
{
    auto snaps = threeSnapshots(false);
    snaps[0].waiting = true;
    snaps[2].waiting = true;
    snaps[2].lastUserPreview = QStringLiteral("hello");
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, snaps);

    QVERIFY(strip.slotTooltip(0).contains(QStringLiteral("Waiting for your answer")));
    QVERIFY(strip.slotTooltip(2).contains(QStringLiteral("Waiting for your answer")));
    QVERIFY(strip.slotTooltip(2).contains(QStringLiteral("hello")));
    QVERIFY(!strip.slotTooltip(1).contains(QStringLiteral("Waiting")));
}

void TestAiDockSessionStrip::attentionColor_isAVisibleRed()
{
    const QColor c = AiDockSessionStrip::attentionColor();
    QVERIFY(c.isValid());
    QVERIFY(c.red() > 180);
    QVERIFY(c.green() < 110);
    QVERIFY(c.blue() < 110);
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

void TestAiDockSessionStrip::tooltip_nameComesFirst()
{
    auto snaps = threeSnapshots(false);
    snaps[0].name = QStringLiteral("api-worker");
    snaps[0].lastUserPreview = QStringLiteral("fix the crash");
    snaps[1].name = QStringLiteral("docs");
    snaps[1].waiting = true;
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, snaps);
    QCOMPARE(strip.slotTooltip(0), QStringLiteral("api-worker\nfix the crash"));
    QCOMPARE(strip.slotTooltip(1), QStringLiteral("docs\nWaiting for your answer"));
    QCOMPARE(strip.slotTooltip(2), QString());
}

void TestAiDockSessionStrip::rightClick_requestsContextMenuForThatNumber()
{
    AiDockSessionStrip strip;
    strip.setSlots(3, 0, threeSnapshots(false));
    const auto buttons = stripButtons(&strip);
    QCOMPARE(buttons.size(), 3);
    QSignalSpy menu(&strip, &AiDockSessionStrip::contextMenuRequested);
    QSignalSpy activated(&strip, &AiDockSessionStrip::activated);

    const QPoint local = buttons.at(2)->rect().center();
    QContextMenuEvent ev(QContextMenuEvent::Mouse, local, buttons.at(2)->mapToGlobal(local));
    QApplication::sendEvent(buttons.at(2), &ev);

    QCOMPARE(menu.count(), 1);
    QCOMPARE(menu.at(0).at(0).toInt(), 2);
    QCOMPARE(menu.at(0).at(1).toPoint(), buttons.at(2)->mapToGlobal(local));
    QCOMPARE(activated.count(), 0);
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

namespace {
// tabSizeHint is protected in QTabBar.
class ProbeTabBar : public QTabBar
{
public:
    using QTabBar::tabSizeHint;
};
} // namespace

void TestAiDockSessionStrip::tabBar_tabSizeHint_includesFullStripWidth()
{
    ProbeTabBar bar;
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

// Crash 2026-10-12: the QMainWindow dock bar removeTab()s the strip's tab
// (deleteLater), AiAgentDock moves the strip onto its local bar before the
// event lands, and the local bar's next removeTab() hide()s freed memory.
void TestAiDockSessionStrip::deferredDelete_afterMove_detachesFromNewBar()
{
    QTabBar host;
    host.addTab(QStringLiteral("a"));
    host.addTab(QStringLiteral("b"));
    QTabBar local;
    local.addTab(QStringLiteral("notepad-ade"));

    auto *strip = new AiDockSessionStrip;
    QPointer<AiDockSessionStrip> guard(strip);
    host.setTabButton(1, QTabBar::RightSide, strip);
    host.removeTab(1);
    local.setTabButton(0, QTabBar::RightSide, strip);
    QCOMPARE(local.tabButton(0, QTabBar::RightSide), strip);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QVERIFY(guard.isNull());
    QCOMPARE(local.tabButton(0, QTabBar::RightSide), nullptr);
    local.removeTab(0);
    QCOMPARE(local.count(), 0);
}

QTEST_MAIN(TestAiDockSessionStrip)
#include "test_ai_dock_session_strip.moc"
