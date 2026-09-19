/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QtTest>
#include <QWidget>

#include "BrowserTabPin.h"

class TestBrowserTabPin : public QObject
{
    Q_OBJECT

private slots:
    void pin_hidesTitleLabel()
    {
        QWidget tab;
        auto *layout = new QHBoxLayout(&tab);
        auto *label = new QLabel(QStringLiteral("AWS"), &tab);
        label->setObjectName(QStringLiteral("dockWidgetTabLabel"));
        auto *close = new QToolButton(&tab);
        close->setObjectName(QStringLiteral("tabCloseButton"));
        layout->addWidget(label);
        layout->addWidget(close);

        applyBrowserTabPinChrome(&tab, true, QStringLiteral("AWS"));

        QVERIFY(label->isHidden());
        QVERIFY(close->isHidden());
        QCOMPARE(tab.toolTip(), QStringLiteral("AWS"));
        QCOMPARE(tab.maximumWidth(), browserTabPinnedMaxWidth(&tab));
        QCOMPARE(tab.minimumWidth(), tab.maximumWidth());
    }

    void unpin_restoresTitleAndClose()
    {
        QWidget tab;
        auto *layout = new QHBoxLayout(&tab);
        auto *label = new QLabel(QStringLiteral("AWS"), &tab);
        label->setObjectName(QStringLiteral("dockWidgetTabLabel"));
        auto *close = new QToolButton(&tab);
        close->setObjectName(QStringLiteral("tabCloseButton"));
        layout->addWidget(label);
        layout->addWidget(close);

        const int oldMin = tab.minimumWidth();
        const int oldMax = tab.maximumWidth();
        applyBrowserTabPinChrome(&tab, true, QStringLiteral("AWS"));
        applyBrowserTabPinChrome(&tab, false, QStringLiteral("AWS"));

        QVERIFY(!label->isHidden());
        QCOMPARE(label->text(), QStringLiteral("AWS"));
        QVERIFY(!close->isHidden());
        QCOMPARE(tab.minimumWidth(), oldMin);
        QCOMPARE(tab.maximumWidth(), oldMax);
    }

    void pin_titleUpdateKeepsLabelHiddenAndRefreshesTooltip()
    {
        QWidget tab;
        auto *layout = new QHBoxLayout(&tab);
        auto *label = new QLabel(QStringLiteral("AWS"), &tab);
        label->setObjectName(QStringLiteral("dockWidgetTabLabel"));
        auto *close = new QToolButton(&tab);
        close->setObjectName(QStringLiteral("tabCloseButton"));
        layout->addWidget(label);
        layout->addWidget(close);

        applyBrowserTabPinChrome(&tab, true, QStringLiteral("AWS"));
        applyBrowserTabPinChrome(&tab, true, QStringLiteral("Amazon Web Services"));

        QVERIFY(label->isHidden());
        QVERIFY(close->isHidden());
        QCOMPARE(tab.toolTip(), QStringLiteral("Amazon Web Services"));
    }

    void unpin_afterTitleChange_restoresNewTitleAndClose()
    {
        QWidget tab;
        auto *layout = new QHBoxLayout(&tab);
        auto *label = new QLabel(QStringLiteral("AWS"), &tab);
        label->setObjectName(QStringLiteral("dockWidgetTabLabel"));
        auto *close = new QToolButton(&tab);
        close->setObjectName(QStringLiteral("tabCloseButton"));
        layout->addWidget(label);
        layout->addWidget(close);

        applyBrowserTabPinChrome(&tab, true, QStringLiteral("AWS"));
        applyBrowserTabPinChrome(&tab, true, QStringLiteral("Amazon Web Services"));
        // ADS setActiveTab / WindowTitleChange can re-show the close button
        // while the tab is still conceptually pinned; unpin must not stay
        // icon-only even after that fight.
        close->show();
        applyBrowserTabPinChrome(&tab, false, QStringLiteral("Amazon Web Services"));

        QVERIFY(!label->isHidden());
        QCOMPARE(label->text(), QStringLiteral("Amazon Web Services"));
        QVERIFY(!close->isHidden());
    }

    void pin_firstTabJumpsToLeft()
    {
        QCOMPARE(browserTabPinMoveTarget({false, false, false}, 2, true), 0);
    }

    void pin_appendsToExistingPinnedCluster()
    {
        QCOMPARE(browserTabPinMoveTarget({true, false, false}, 2, true), 1);
    }

    void pin_alreadyAtClusterEnd_stays()
    {
        QCOMPARE(browserTabPinMoveTarget({true, true, false}, 1, true), 1);
    }

    void unpin_movesAfterRemainingPinned()
    {
        QCOMPARE(browserTabPinMoveTarget({true, true, false}, 0, false), 1);
    }

    void unpin_solePinned_stays()
    {
        QCOMPARE(browserTabPinMoveTarget({true, false, false}, 0, false), 0);
    }

    void persist_keysRoundTrip()
    {
        QCOMPARE(browserPinKeyForQuickBrowser(QStringLiteral("https://a.com")),
                 QStringLiteral("qb:https://a.com"));
        QCOMPARE(browserPinKeyForMiniApp(QStringLiteral("aws")),
                 QStringLiteral("ma:aws"));
        QVERIFY(isQuickBrowserPinKey(QStringLiteral("qb:https://a.com")));
        QVERIFY(isMiniAppPinKey(QStringLiteral("ma:aws")));
        QCOMPARE(browserPinKeyIdentity(QStringLiteral("qb:https://a.com")),
                 QStringLiteral("https://a.com"));
        QCOMPARE(browserPinKeyIdentity(QStringLiteral("ma:aws")),
                 QStringLiteral("aws"));

        QStringList keys;
        keys = addBrowserPinKey(keys, QStringLiteral("qb:https://a.com"));
        keys = addBrowserPinKey(keys, QStringLiteral("ma:aws"));
        QCOMPARE(keys.size(), 2);
        keys = replaceBrowserPinKey(keys, QStringLiteral("qb:https://a.com"),
                                    QStringLiteral("qb:https://b.com"));
        QCOMPARE(keys, QStringList() << QStringLiteral("qb:https://b.com")
                                     << QStringLiteral("ma:aws"));
        keys = removeBrowserPinKey(keys, QStringLiteral("ma:aws"));
        QCOMPARE(keys, QStringList() << QStringLiteral("qb:https://b.com"));
    }

    void persist_emptyKeyIsIgnored()
    {
        QStringList keys;
        keys = addBrowserPinKey(keys, QString());
        QVERIFY(keys.isEmpty());
        QCOMPARE(browserPinKeyForQuickBrowser(QString()), QString());
        QCOMPARE(browserPinKeyForMiniApp(QString()), QString());
    }

    void persist_dropsUnknownMiniAppKeys()
    {
        const QStringList keys = {
            QStringLiteral("qb:https://a.com"),
            QStringLiteral("ma:gone"),
            QStringLiteral("ma:aws"),
            QStringLiteral("nope"),
        };
        const QStringList known{QStringLiteral("aws")};
        QCOMPARE(pruneStaleMiniAppPinKeys(keys, known),
                 QStringList() << QStringLiteral("qb:https://a.com")
                              << QStringLiteral("ma:aws"));
    }
};

QTEST_MAIN(TestBrowserTabPin)
#include "test_browser_tab_pin.moc"
