/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QtTest>
#include <QWidget>

#include "BrowserTabPin.h"
#include "DockAreaTabBar.h"
#include "DockAreaTitleBar.h"
#include "DockAreaWidget.h"
#include "DockManager.h"
#include "DockTabReorder.h"
#include "DockWidget.h"
#include "DockWidgetTab.h"

class TestDockTabReorder : public QObject
{
    Q_OBJECT

private slots:
    void nullWidget_returnsFalse()
    {
        QVERIFY(!moveDockWidgetToIndex(nullptr, 0));
    }

    void moveLastToFront_keepsTabAndContentInSync()
    {
        ads::CDockManager manager;
        manager.resize(800, 400);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));

        auto *a = manager.createDockWidget(QStringLiteral("A"));
        a->setWidget(new QWidget);
        ads::CDockAreaWidget *area =
            manager.addDockWidget(ads::CenterDockWidgetArea, a);

        auto *b = manager.createDockWidget(QStringLiteral("B"));
        b->setWidget(new QWidget);
        manager.addDockWidget(ads::CenterDockWidgetArea, b, area);

        auto *c = manager.createDockWidget(QStringLiteral("C"));
        c->setWidget(new QWidget);
        manager.addDockWidget(ads::CenterDockWidgetArea, c, area);

        QCOMPARE(area->dockWidgetsCount(), 3);
        QVERIFY(moveDockWidgetToIndex(c, 0));

        QCOMPARE(area->dockWidget(0), c);
        QCOMPARE(area->dockWidget(1), a);
        QCOMPARE(area->dockWidget(2), b);

        ads::CDockAreaTabBar *tabs = area->titleBar()->tabBar();
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->tab(0)->dockWidget(), c);
        QCOMPARE(tabs->tab(1)->dockWidget(), a);
        QCOMPARE(tabs->tab(2)->dockWidget(), b);
    }

    void alreadyAtIndex_isSuccessWithoutShuffle()
    {
        ads::CDockManager manager;
        manager.resize(800, 400);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));

        auto *a = manager.createDockWidget(QStringLiteral("A"));
        a->setWidget(new QWidget);
        ads::CDockAreaWidget *area =
            manager.addDockWidget(ads::CenterDockWidgetArea, a);

        auto *b = manager.createDockWidget(QStringLiteral("B"));
        b->setWidget(new QWidget);
        manager.addDockWidget(ads::CenterDockWidgetArea, b, area);

        QVERIFY(moveDockWidgetToIndex(a, 0));
        QCOMPARE(area->dockWidget(0), a);
        QCOMPARE(area->dockWidget(1), b);
    }

    void outOfRange_returnsFalse()
    {
        ads::CDockManager manager;
        manager.resize(800, 400);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));

        auto *a = manager.createDockWidget(QStringLiteral("A"));
        a->setWidget(new QWidget);
        manager.addDockWidget(ads::CenterDockWidgetArea, a);

        QVERIFY(!moveDockWidgetToIndex(a, 1));
        QVERIFY(!moveDockWidgetToIndex(a, -1));
    }

    void pinChrome_onRealAdsTab_hidesLabelAndClose()
    {
        ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, true);
        ads::CDockManager manager;
        manager.resize(800, 400);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));

        auto *dw = manager.createDockWidget(QStringLiteral("AWS"));
        dw->setWidget(new QWidget);
        manager.addDockWidget(ads::CenterDockWidgetArea, dw);

        ads::CDockWidgetTab *tab = dw->tabWidget();
        QVERIFY(tab);
        auto *label = tab->findChild<QLabel *>(QStringLiteral("dockWidgetTabLabel"));
        auto *close = tab->findChild<QWidget *>(QStringLiteral("tabCloseButton"));
        QVERIFY(label);
        QVERIFY(close);
        QVERIFY(!label->isHidden());
        QVERIFY(!close->isHidden());

        applyBrowserTabPinChrome(tab, true, QStringLiteral("AWS"));
        QVERIFY(label->isHidden());
        QVERIFY(close->isHidden());
        QCOMPARE(tab->toolTip(), QStringLiteral("AWS"));
        QCOMPARE(tab->text(), QString());

        applyBrowserTabPinChrome(tab, false, QStringLiteral("AWS"));
        QVERIFY(!label->isHidden());
        QCOMPARE(tab->text(), QStringLiteral("AWS"));
        QVERIFY(!close->isHidden());
    }

    void pin_clearsClosable_soActiveTabDoesNotReshowClose()
    {
        ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, true);
        ads::CDockManager manager;
        manager.resize(800, 400);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));

        auto *dw = manager.createDockWidget(QStringLiteral("AWS"));
        dw->setWidget(new QWidget);
        manager.addDockWidget(ads::CenterDockWidgetArea, dw);

        dw->setFeature(ads::CDockWidget::DockWidgetClosable, false);
        applyBrowserTabPinChrome(dw->tabWidget(), true, QStringLiteral("AWS"));
        dw->tabWidget()->setActiveTab(true);

        auto *close = dw->tabWidget()->findChild<QWidget *>(QStringLiteral("tabCloseButton"));
        QVERIFY(close);
        QVERIFY(close->isHidden());
        QCOMPARE(dw->tabWidget()->text(), QString());

        dw->setWindowTitle(QStringLiteral("Amazon"));
        applyBrowserTabPinChrome(dw->tabWidget(), true, dw->windowTitle());
        auto *label = dw->tabWidget()->findChild<QLabel *>(QStringLiteral("dockWidgetTabLabel"));
        QVERIFY(label->isHidden());
        QVERIFY(close->isHidden());
        QCOMPARE(dw->tabWidget()->text(), QString());
        QCOMPARE(dw->tabWidget()->toolTip(), QStringLiteral("Amazon"));
    }

    void pin_withIcon_doesNotClipIcon()
    {
        ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, true);
        ads::CDockManager manager;
        manager.resize(800, 400);
        manager.show();
        QVERIFY(QTest::qWaitForWindowExposed(&manager));

        auto *dw = manager.createDockWidget(QStringLiteral("AWS"));
        dw->setWidget(new QWidget);
        manager.addDockWidget(ads::CenterDockWidgetArea, dw);

        ads::CDockWidgetTab *tab = dw->tabWidget();
        // Production npp_dark.css: padding + L/R borders eat the contents rect.
        // maximumWidth that ignores this chrome clips the favicon in half.
        tab->setStyleSheet(QStringLiteral(
            "ads--CDockWidgetTab {"
            "padding: 2px 2px 0px 2px;"
            "border-left: 1px solid #000;"
            "border-right: 2px solid #000;"
            "}"));
        tab->ensurePolished();

        QPixmap pm(16, 16);
        pm.fill(Qt::blue);
        tab->setIcon(QIcon(pm));

        applyBrowserTabPinChrome(tab, true, QStringLiteral("AWS"));
        tab->updateGeometry();
        QVERIFY(QTest::qWaitFor([&]() { return tab->width() > 0; }, 500));

        QLabel *iconLabel = nullptr;
        for (QLabel *label : tab->findChildren<QLabel *>()) {
            if (label->objectName() != QLatin1String("dockWidgetTabLabel")) {
                iconLabel = label;
                break;
            }
        }
        QVERIFY(iconLabel);
        QVERIFY(!iconLabel->isHidden());
        QCOMPARE(tab->maximumWidth(), browserTabPinnedMaxWidth(tab));
        QCOMPARE(tab->minimumWidth(), tab->maximumWidth());
        QVERIFY2(tab->width() >= tab->minimumWidth(),
                 qPrintable(QStringLiteral("tab width %1 < min %2")
                                .arg(tab->width())
                                .arg(tab->minimumWidth())));
        QVERIFY2(iconLabel->width() >= pm.width(),
                 qPrintable(QStringLiteral("icon widget %1 < pixmap %2")
                                .arg(iconLabel->width())
                                .arg(pm.width())));
        const QRect iconInTab(iconLabel->mapTo(tab, QPoint(0, 0)), iconLabel->size());
        QVERIFY2(tab->contentsRect().contains(iconInTab),
                 qPrintable(QStringLiteral("icon %1,%2 %3x%4 not inside contents %5 maxW %6")
                                .arg(iconInTab.x())
                                .arg(iconInTab.y())
                                .arg(iconInTab.width())
                                .arg(iconInTab.height())
                                .arg(tab->contentsRect().width())
                                .arg(tab->maximumWidth())));
        const QRect contents = tab->contentsRect();
        const int leftGap = iconInTab.left() - contents.left();
        const int rightGap = contents.right() - iconInTab.right();
        QVERIFY2(qAbs(leftGap - rightGap) <= 2,
                 qPrintable(QStringLiteral("icon not centered: leftGap %1 rightGap %2")
                                .arg(leftGap)
                                .arg(rightGap)));
    }
};

QTEST_MAIN(TestDockTabReorder)
#include "test_dock_tab_reorder.moc"
