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

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QTabBar>
#include <QTabWidget>
#include <QVariant>

#include "DockTabContextMenu.h"

using DockTabContextMenu::Scope;

namespace {

// Qt creates the QMainWindowTabBar lazily, while laying out a tabified group,
// so it only exists after the window has been shown and events pumped.
QTabBar *findDockTabBar(QMainWindow *mw)
{
    const auto bars = mw->findChildren<QTabBar *>();
    for (auto *bar : bars) {
        if (bar->count() > 0)
            return bar;
    }
    return nullptr;
}

QStringList titlesOf(const QList<QDockWidget *> &docks)
{
    QStringList out;
    out.reserve(docks.size());
    for (auto *d : docks)
        out << d->windowTitle();
    return out;
}

} // namespace

class TestDockTabContextMenu : public QObject
{
    Q_OBJECT

private slots:
    void tabOrder_tabifiedDocks_resolvesInTabOrder();
    void tabOrder_plainTabBarSharingADockTitle_isEmpty();
    void tabOrder_tabBarNotUnderMainWindow_stillResolves();

    void closeTargets_close_returnsOnlyClicked();
    void closeTargets_closeOthers_returnsEveryOtherTab();
    void closeTargets_closeToLeft_returnsTabsBeforeClicked();
    void closeTargets_closeToLeftOnFirstTab_isEmpty();
    void closeTargets_closeToRight_returnsTabsAfterClicked();
    void closeTargets_closeToRightOnLastTab_isEmpty();
    void closeTargets_closeAll_returnsEveryTab();
    void closeTargets_skipsDockWithoutClosableFeature();
    void closeTargets_closeOnNonClosableDock_isEmpty();
    void closeTargets_indexOutOfRange_isEmpty();

    void populateMenu_singleTab_disablesEmptyScopes();
    void populateMenu_labelsMatchEditorOverlaps();
    void populateMenu_closeAll_survivesDestroyedDock();

    void showMenu_plainTabBar_returnsFalse();
    void showMenu_dockTabBarMiss_consumesWithoutPopup();
};

// --- tabOrder: mapping a real tabified dock group back to its docks ----------

void TestDockTabContextMenu::tabOrder_tabifiedDocks_resolvesInTabOrder()
{
    QMainWindow mw;

    auto *first = new QDockWidget(QStringLiteral("Workspace"), &mw);
    auto *second = new QDockWidget(QStringLiteral("Terminal"), &mw);
    auto *third = new QDockWidget(QStringLiteral("Task — dev"), &mw);
    mw.addDockWidget(Qt::BottomDockWidgetArea, first);
    mw.tabifyDockWidget(first, second);
    mw.tabifyDockWidget(second, third);

    mw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mw));

    QTabBar *bar = findDockTabBar(&mw);
    QVERIFY2(bar != nullptr, "Qt did not create a tab bar for the tabified group");
    QCOMPARE(bar->count(), 3);

    // Order must follow the tab bar, not the order the docks were added — the
    // user can drag tabs around and the left/right actions read tab positions.
    const QList<QDockWidget *> order = DockTabContextMenu::tabOrder(bar);
    QCOMPARE(order.size(), 3);
    QStringList barTitles;
    for (int i = 0; i < bar->count(); ++i)
        barTitles << bar->tabText(i);
    QCOMPARE(titlesOf(order), barTitles);
}

void TestDockTabContextMenu::tabOrder_plainTabBarSharingADockTitle_isEmpty()
{
    // The filter is installed on qApp, so it sees every QTabBar in the process —
    // including the workspace dock's inner "Files"/"Git" QTabWidget. Those tabs
    // carry no dock pointer in tabData and must never resolve to a dock, even
    // when a sibling dock happens to have the same title.
    QMainWindow mw;

    auto *dock = new QDockWidget(QStringLiteral("Files"), &mw);
    mw.addDockWidget(Qt::LeftDockWidgetArea, dock);

    auto *tabs = new QTabWidget;
    tabs->addTab(new QWidget, QStringLiteral("Files"));
    tabs->addTab(new QWidget, QStringLiteral("Git"));
    mw.setCentralWidget(tabs);

    mw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mw));

    QVERIFY(DockTabContextMenu::tabOrder(tabs->tabBar()).isEmpty());
}

void TestDockTabContextMenu::tabOrder_tabBarNotUnderMainWindow_stillResolves()
{
    // Floating tabified docks live in QDockWidgetGroupWindow, which is a
    // QWidget — not a QMainWindow. tabOrder must key off tabData, not on
    // walking the parent chain to a QMainWindow.
    QWidget host;
    QMainWindow owner;
    auto *a = new QDockWidget(QStringLiteral("a"), &owner);
    auto *b = new QDockWidget(QStringLiteral("b"), &owner);

    QTabBar bar(&host);
    bar.addTab(a->windowTitle());
    bar.setTabData(0, QVariant::fromValue(reinterpret_cast<quintptr>(static_cast<QWidget *>(a))));
    bar.addTab(b->windowTitle());
    bar.setTabData(1, QVariant::fromValue(reinterpret_cast<quintptr>(static_cast<QWidget *>(b))));

    QCOMPARE(titlesOf(DockTabContextMenu::tabOrder(&bar)), (QStringList{"a", "b"}));
}

// --- closeTargets: which docks each scope closes -----------------------------
//
// Pure over the tab order, so these need no tab bar and no shown window. The
// owner keeps the docks alive for the duration of each test.

namespace {

QList<QDockWidget *> makeDocks(QWidget *owner, const QStringList &titles)
{
    QList<QDockWidget *> docks;
    docks.reserve(titles.size());
    for (const QString &t : titles)
        docks << new QDockWidget(t, owner);
    return docks;
}

} // namespace

void TestDockTabContextMenu::closeTargets_close_returnsOnlyClicked()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "b", "c"});

    const auto targets = DockTabContextMenu::closeTargets(docks, 1, Scope::Close);

    QCOMPARE(titlesOf(targets), QStringList{"b"});
}

void TestDockTabContextMenu::closeTargets_closeOthers_returnsEveryOtherTab()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "b", "c", "d"});

    const auto targets = DockTabContextMenu::closeTargets(docks, 1, Scope::CloseOthers);

    QCOMPARE(titlesOf(targets), (QStringList{"a", "c", "d"}));
}

void TestDockTabContextMenu::closeTargets_closeToLeft_returnsTabsBeforeClicked()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "b", "c", "d"});

    const auto targets = DockTabContextMenu::closeTargets(docks, 2, Scope::CloseToLeft);

    QCOMPARE(titlesOf(targets), (QStringList{"a", "b"}));
}

void TestDockTabContextMenu::closeTargets_closeToLeftOnFirstTab_isEmpty()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "b"});

    QVERIFY(DockTabContextMenu::closeTargets(docks, 0, Scope::CloseToLeft).isEmpty());
}

void TestDockTabContextMenu::closeTargets_closeToRight_returnsTabsAfterClicked()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "b", "c", "d"});

    const auto targets = DockTabContextMenu::closeTargets(docks, 1, Scope::CloseToRight);

    QCOMPARE(titlesOf(targets), (QStringList{"c", "d"}));
}

void TestDockTabContextMenu::closeTargets_closeToRightOnLastTab_isEmpty()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "b"});

    QVERIFY(DockTabContextMenu::closeTargets(docks, 1, Scope::CloseToRight).isEmpty());
}

void TestDockTabContextMenu::closeTargets_closeAll_returnsEveryTab()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "b", "c"});

    const auto targets = DockTabContextMenu::closeTargets(docks, 1, Scope::CloseAll);

    QCOMPARE(titlesOf(targets), (QStringList{"a", "b", "c"}));
}

void TestDockTabContextMenu::closeTargets_skipsDockWithoutClosableFeature()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "pinned", "c"});
    docks.at(1)->setFeatures(QDockWidget::DockWidgetMovable);

    // "pinned" cannot be closed, so a scope that would sweep it must leave it be
    // rather than calling close() on a dock Qt refuses to close.
    QCOMPARE(titlesOf(DockTabContextMenu::closeTargets(docks, 0, Scope::CloseOthers)),
             QStringList{"c"});
    QCOMPARE(titlesOf(DockTabContextMenu::closeTargets(docks, 2, Scope::CloseToLeft)),
             QStringList{"a"});
    QCOMPARE(titlesOf(DockTabContextMenu::closeTargets(docks, 1, Scope::CloseAll)),
             (QStringList{"a", "c"}));
}

void TestDockTabContextMenu::closeTargets_closeOnNonClosableDock_isEmpty()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "pinned"});
    docks.at(1)->setFeatures(QDockWidget::DockWidgetMovable);

    // Empty is what disables the Close item in the menu.
    QVERIFY(DockTabContextMenu::closeTargets(docks, 1, Scope::Close).isEmpty());
}

void TestDockTabContextMenu::closeTargets_indexOutOfRange_isEmpty()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "b"});

    // QTabBar::tabAt() returns -1 for a click on the empty strip past the tabs.
    QVERIFY(DockTabContextMenu::closeTargets(docks, -1, Scope::Close).isEmpty());
    QVERIFY(DockTabContextMenu::closeTargets(docks, -1, Scope::CloseAll).isEmpty());
    QVERIFY(DockTabContextMenu::closeTargets(docks, 2, Scope::CloseOthers).isEmpty());

    // An empty order is the same out-of-range case: every index misses.
    QVERIFY(DockTabContextMenu::closeTargets({}, 0, Scope::CloseAll).isEmpty());
}

void TestDockTabContextMenu::populateMenu_singleTab_disablesEmptyScopes()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"only"});

    QMenu menu;
    DockTabContextMenu::populateMenu(&menu, docks, 0);

    // Five items always — disable, never hide. Close and Close All still apply
    // to the last tab (a lone terminal/workspace is allowed to go away).
    const auto actions = menu.actions();
    QCOMPARE(actions.size(), 5);
    QVERIFY(actions.at(0)->isEnabled());  // Close
    QVERIFY(!actions.at(1)->isEnabled()); // Except This
    QVERIFY(!actions.at(2)->isEnabled()); // Left
    QVERIFY(!actions.at(3)->isEnabled()); // Right
    QVERIFY(actions.at(4)->isEnabled());  // Close All
}

void TestDockTabContextMenu::populateMenu_labelsMatchEditorOverlaps()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "b", "c"});

    QMenu menu;
    DockTabContextMenu::populateMenu(&menu, docks, 1);

    const auto actions = menu.actions();
    QCOMPARE(actions.size(), 5);
    // Overlaps with MainWindow.ui editor-tab actions: Close, Close All to the
    // Left, Close All to the Right, Close All. "Except This One" is the dock
    // wording — editor says "Except Active Document", which is a lie here.
    QCOMPARE(actions.at(0)->text(),
             QCoreApplication::translate("DockTabContextMenu", "Close"));
    QCOMPARE(actions.at(1)->text(),
             QCoreApplication::translate("DockTabContextMenu", "Close All Except This One"));
    QCOMPARE(actions.at(2)->text(),
             QCoreApplication::translate("DockTabContextMenu", "Close All to the Left"));
    QCOMPARE(actions.at(3)->text(),
             QCoreApplication::translate("DockTabContextMenu", "Close All to the Right"));
    QCOMPARE(actions.at(4)->text(),
             QCoreApplication::translate("DockTabContextMenu", "Close All"));
}

void TestDockTabContextMenu::populateMenu_closeAll_survivesDestroyedDock()
{
    QMainWindow owner;
    const auto docks = makeDocks(&owner, {"a", "b", "c"});
    QPointer<QDockWidget> a = docks.at(0);
    QPointer<QDockWidget> b = docks.at(1);
    QPointer<QDockWidget> c = docks.at(2);

    QMenu menu;
    DockTabContextMenu::populateMenu(&menu, docks, 1);

    delete a.data();
    QVERIFY(a.isNull());

    menu.actions().at(4)->trigger(); // Close All

    QVERIFY(b && !b->isVisible());
    QVERIFY(c && !c->isVisible());
}

void TestDockTabContextMenu::showMenu_plainTabBar_returnsFalse()
{
    QTabWidget tabs;
    tabs.addTab(new QWidget, QStringLiteral("Files"));
    QVERIFY(!DockTabContextMenu::showMenu(tabs.tabBar(), QPoint(0, 0)));
}

void TestDockTabContextMenu::showMenu_dockTabBarMiss_consumesWithoutPopup()
{
    QMainWindow mw;
    auto *first = new QDockWidget(QStringLiteral("Workspace"), &mw);
    auto *second = new QDockWidget(QStringLiteral("Terminal"), &mw);
    mw.addDockWidget(Qt::BottomDockWidgetArea, first);
    mw.tabifyDockWidget(first, second);
    mw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mw));

    QTabBar *bar = findDockTabBar(&mw);
    QVERIFY(bar != nullptr);

    // Far off the tabs: must swallow (true) and must not exec a menu, or this
    // test hangs. Returning true is what keeps QMainWindow from popping the
    // toggle-docks popup on the empty strip.
    QVERIFY(DockTabContextMenu::showMenu(bar, QPoint(-1000, -1000)));
}

QTEST_MAIN(TestDockTabContextMenu)
#include "test_dock_tab_context_menu.moc"
