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
#include <QCoreApplication>
#include <QDockWidget>
#include <QMainWindow>
#include <QPointer>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QVariant>
#include <QWidget>

#include "AcpSessionModel.h"
#include "AiAgentDock.h"
#include "AiDockGroup.h"
#include "widgets/AiDockSessionStrip.h"

// Test seam — overrides confirmCloseWhileRunning() so the modal QMessageBox
// is never created during the close path.
class TestableAiAgentDock : public AiAgentDock
{
public:
    TestableAiAgentDock(const QString &sessionId,
                        AcpSessionModel *model,
                        bool confirmResult)
        : AiAgentDock(sessionId,
                      QStringLiteral("test-agent"),
                      QStringLiteral("/tmp"),
                      model,
                      /*connection=*/nullptr,
                      /*registry=*/nullptr,
                      /*agentManager=*/nullptr,
                      /*appSettings=*/nullptr,
                      /*parent=*/nullptr)
        , m_confirmResult(confirmResult)
    {
    }

    int confirmCallCount = 0;

protected:
    bool confirmCloseWhileRunning() override
    {
        ++confirmCallCount;
        return m_confirmResult;
    }

private:
    bool m_confirmResult;
};

namespace {

QTabBar *areaTabBarForDock(QMainWindow *mw, QDockWidget *dock, int *tabIndex)
{
    if (tabIndex)
        *tabIndex = -1;
    const auto bars = mw->findChildren<QTabBar *>();
    for (auto *bar : bars) {
        if (bar->objectName() == QLatin1String("nn_aiLocalTabBar"))
            continue;
        for (int i = 0; i < bar->count(); ++i) {
            const QVariant data = bar->tabData(i);
            if (!data.isValid())
                continue;
            auto *widget = reinterpret_cast<QWidget *>(qvariant_cast<quintptr>(data));
            if (widget == dock) {
                if (tabIndex)
                    *tabIndex = i;
                return bar;
            }
        }
    }
    return nullptr;
}

QString projectTooltipText(const QString &cwd, int n)
{
    return QStringLiteral("%1\n%2")
        .arg(cwd, QCoreApplication::translate("AiAgentDock", "%n session(s)", "", n));
}

} // namespace

class TestAcpDockCloseEvent : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void close_whenIdle_destroysDock();
    void close_whileProcessingAndUserDeclines_keepsDock();
    void close_whileProcessingAndUserConfirms_destroysDock();
    void close_whenTwoSlots_closesCurrentKeepsDock();
    void closeGroup_destroysMultiSlotDock();
    void title_singletonIsBasename_groupedAppendsColon();
    void tooltip_isCwdAndSessionCount_notWindowTitle();
    void tooltip_numberShowsLastUserMessage();
    void singleton_showsBottomTab_evenWhenAloneInArea();
    void siblingTabify_inactiveDockStillHasStripOnAreaBar();
    void closeSibling_remainingShowsLocalTab();
    void shutdown_deletingMainWindowWithAiDocks_doesNotAssert();
    void defaults_areaIsRightAndAllowedIsAll();

private:
    QTemporaryDir m_historyDir;
};

void TestAcpDockCloseEvent::initTestCase()
{
    QVERIFY(m_historyDir.isValid());
}

void TestAcpDockCloseEvent::close_whenIdle_destroysDock()
{
    AcpSessionModel model(QStringLiteral("sess-idle"),
                          QStringLiteral("/proj"),
                          m_historyDir.path());
    QVERIFY(!model.isProcessing());

    auto *dock = new TestableAiAgentDock(QStringLiteral("sess-idle"), &model, /*confirm=*/false);
    dock->show();
    QPointer<TestableAiAgentDock> dockPtr(dock);
    dock->close();
    // Allow deferred deletion (WA_DeleteOnClose) to run.
    QTRY_VERIFY_WITH_TIMEOUT(dockPtr.isNull(), 2000);
}

void TestAcpDockCloseEvent::close_whileProcessingAndUserDeclines_keepsDock()
{
    AcpSessionModel model(QStringLiteral("sess-busy-no"),
                          QStringLiteral("/proj"),
                          m_historyDir.path());
    model.onPromptStarted();
    QVERIFY(model.isProcessing());

    auto *dock = new TestableAiAgentDock(QStringLiteral("sess-busy-no"), &model, /*confirm=*/false);
    dock->show();
    QPointer<TestableAiAgentDock> dockPtr(dock);
    dock->close();
    // Give the event loop a chance to process WA_DeleteOnClose if it were
    // going to fire — it must NOT fire when confirm returns false.
    QTest::qWait(100);
    QVERIFY(!dockPtr.isNull());
    QCOMPARE(dock->confirmCallCount, 1);

    // Reset to idle so we can close cleanly for teardown.
    model.onPromptEnded();
    dock->close();
    QTRY_VERIFY_WITH_TIMEOUT(dockPtr.isNull(), 2000);
}

void TestAcpDockCloseEvent::close_whileProcessingAndUserConfirms_destroysDock()
{
    AcpSessionModel model(QStringLiteral("sess-busy-yes"),
                          QStringLiteral("/proj"),
                          m_historyDir.path());
    model.onPromptStarted();
    QVERIFY(model.isProcessing());

    auto *dock = new TestableAiAgentDock(QStringLiteral("sess-busy-yes"), &model, /*confirm=*/true);
    dock->show();
    QPointer<TestableAiAgentDock> dockPtr(dock);
    dock->close();
    QTRY_VERIFY_WITH_TIMEOUT(dockPtr.isNull(), 2000);
}

void TestAcpDockCloseEvent::close_whenTwoSlots_closesCurrentKeepsDock()
{
    AcpSessionModel model1(QStringLiteral("sess-a"),
                           QStringLiteral("/proj"),
                           m_historyDir.path());
    AcpSessionModel model2(QStringLiteral("sess-b"),
                           QStringLiteral("/proj"),
                           m_historyDir.path());
    auto *dock = new TestableAiAgentDock(QStringLiteral("sess-a"), &model1, /*confirm=*/false);
    dock->addSlot(QStringLiteral("sess-b"), QStringLiteral("test-agent"), &model2, nullptr, true);
    QCOMPARE(dock->slotCount(), 2);
    QCOMPARE(dock->currentIndex(), 1);
    QCOMPARE(dock->windowTitle(),
             aiDockWindowTitle(aiDockProjectBasename(QStringLiteral("/tmp")), 2));

    QPointer<TestableAiAgentDock> dockPtr(dock);
    dock->close();
    QTest::qWait(50);
    QVERIFY(!dockPtr.isNull());
    QCOMPARE(dock->slotCount(), 1);
    QCOMPARE(dock->windowTitle(),
             aiDockWindowTitle(aiDockProjectBasename(QStringLiteral("/tmp")), 1));

    dock->close();
    QTRY_VERIFY_WITH_TIMEOUT(dockPtr.isNull(), 2000);
}

void TestAcpDockCloseEvent::closeGroup_destroysMultiSlotDock()
{
    AcpSessionModel model1(QStringLiteral("sess-g1"),
                           QStringLiteral("/proj"),
                           m_historyDir.path());
    AcpSessionModel model2(QStringLiteral("sess-g2"),
                           QStringLiteral("/proj"),
                           m_historyDir.path());
    auto *dock = new TestableAiAgentDock(QStringLiteral("sess-g1"), &model1, /*confirm=*/false);
    dock->addSlot(QStringLiteral("sess-g2"), QStringLiteral("test-agent"), &model2, nullptr, true);
    QCOMPARE(dock->slotCount(), 2);

    QPointer<TestableAiAgentDock> dockPtr(dock);
    dock->closeGroup();
    QTRY_VERIFY_WITH_TIMEOUT(dockPtr.isNull(), 2000);
}

void TestAcpDockCloseEvent::title_singletonIsBasename_groupedAppendsColon()
{
    AcpSessionModel model(QStringLiteral("sess-title"),
                          QStringLiteral("/proj"),
                          m_historyDir.path());
    auto *dock = new TestableAiAgentDock(QStringLiteral("sess-title"), &model, /*confirm=*/false);
    QCOMPARE(dock->windowTitle(),
             aiDockWindowTitle(aiDockProjectBasename(QStringLiteral("/tmp")), 1));
    QCOMPARE(dock->slotCount(), 1);

    AcpSessionModel model2(QStringLiteral("sess-title-2"),
                           QStringLiteral("/proj"),
                           m_historyDir.path());
    dock->addSlot(QStringLiteral("sess-title-2"), QStringLiteral("test-agent"), &model2, nullptr, true);
    QCOMPARE(dock->windowTitle(),
             aiDockWindowTitle(aiDockProjectBasename(QStringLiteral("/tmp")), 2));

    QPointer<TestableAiAgentDock> dockPtr(dock);
    dock->closeGroup();
    QTRY_VERIFY_WITH_TIMEOUT(dockPtr.isNull(), 2000);
}

void TestAcpDockCloseEvent::tooltip_isCwdAndSessionCount_notWindowTitle()
{
    AcpSessionModel model(QStringLiteral("sess-tip"),
                          QStringLiteral("/proj"),
                          m_historyDir.path());
    auto *dock = new TestableAiAgentDock(QStringLiteral("sess-tip"), &model, /*confirm=*/false);
    const QString cwd = QStringLiteral("/tmp");
    const QString one = projectTooltipText(cwd, 1);
    QCOMPARE(dock->toolTip(), one);
    QVERIFY(dock->toolTip() != dock->windowTitle());

    QMainWindow mw;
    auto *other = new QDockWidget(QStringLiteral("Other"), &mw);
    mw.addDockWidget(Qt::RightDockWidgetArea, other);
    mw.addDockWidget(Qt::RightDockWidgetArea, dock);
    mw.tabifyDockWidget(other, dock);
    mw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mw));

    int tabIndex = -1;
    QTabBar *bar = nullptr;
    QTRY_VERIFY((bar = areaTabBarForDock(&mw, dock, &tabIndex)) != nullptr);
    QTRY_COMPARE(bar->tabToolTip(tabIndex), one);
    QVERIFY(bar->tabToolTip(tabIndex) != dock->windowTitle());

    AcpSessionModel model2(QStringLiteral("sess-tip-2"),
                           QStringLiteral("/proj"),
                           m_historyDir.path());
    dock->addSlot(QStringLiteral("sess-tip-2"), QStringLiteral("test-agent"), &model2, nullptr, true);
    const QString two = projectTooltipText(cwd, 2);
    QCOMPARE(dock->toolTip(), two);
    QTRY_VERIFY((bar = areaTabBarForDock(&mw, dock, &tabIndex)) != nullptr);
    QTRY_COMPARE(bar->tabToolTip(tabIndex), two);
    QVERIFY(bar->tabToolTip(tabIndex) != dock->windowTitle());

    QPointer<TestableAiAgentDock> dockPtr(dock);
    dock->closeGroup();
    QTRY_VERIFY_WITH_TIMEOUT(dockPtr.isNull(), 2000);
}

void TestAcpDockCloseEvent::tooltip_numberShowsLastUserMessage()
{
    AcpSessionModel model(QStringLiteral("sess-user-tip"),
                          QStringLiteral("/proj"),
                          m_historyDir.path());
    auto *dock = new TestableAiAgentDock(QStringLiteral("sess-user-tip"), &model, /*confirm=*/false);
    QMainWindow mw;
    mw.addDockWidget(Qt::RightDockWidgetArea, dock);
    mw.resize(800, 600);
    mw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mw));

    AiDockSessionStrip *strip = nullptr;
    QTRY_VERIFY((strip = dock->findChild<AiDockSessionStrip *>()) != nullptr);
    QCOMPARE(strip->slotTooltip(0), QString());

    model.appendUserMessage(QStringLiteral("fix the hover tooltip"), {});
    QCOMPARE(strip->slotTooltip(0), QStringLiteral("fix the hover tooltip"));

    model.appendUserMessage(QStringLiteral("goal injected"), {}, /*fromGoalAgent=*/true);
    QCOMPARE(strip->slotTooltip(0), QStringLiteral("fix the hover tooltip"));

    model.appendUserMessage(QStringLiteral("real follow-up"), {});
    QCOMPARE(strip->slotTooltip(0), QStringLiteral("real follow-up"));

    QVERIFY(!strip->slotTooltip(0).contains(QStringLiteral("idle")));
    QVERIFY(!strip->slotTooltip(0).contains(QStringLiteral("test-agent")));

    QPointer<TestableAiAgentDock> dockPtr(dock);
    dock->close();
    QTRY_VERIFY_WITH_TIMEOUT(dockPtr.isNull(), 2000);
}

void TestAcpDockCloseEvent::singleton_showsBottomTab_evenWhenAloneInArea()
{
    // Qt hides the dock-area tab bar when count() <= 1
    // (QDockAreaLayoutInfo::updateTabBar). The dock must still show a South
    // tab with the project title and session strip.
    AcpSessionModel model(QStringLiteral("sess-tab"),
                          QStringLiteral("/proj"),
                          m_historyDir.path());
    auto *dock = new TestableAiAgentDock(QStringLiteral("sess-tab"), &model, /*confirm=*/false);
    QMainWindow mw;
    mw.addDockWidget(Qt::RightDockWidgetArea, dock);
    mw.resize(800, 600);
    mw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mw));

    QTabBar *local = nullptr;
    QTRY_VERIFY((local = dock->findChild<QTabBar *>(QStringLiteral("nn_aiLocalTabBar"))) != nullptr);
    QTRY_VERIFY(local->isVisible());
    QCOMPARE(local->count(), 1);
    QCOMPARE(local->tabText(0), dock->windowTitle());
    QVERIFY(local->tabButton(0, QTabBar::RightSide) != nullptr);
    QCOMPARE(local->shape(), QTabBar::RoundedSouth);

    if (auto *header = dock->findChild<QWidget *>(QStringLiteral("nn_aiSessionStripHeader")))
        QVERIFY(!header->isVisible());

    QTRY_VERIFY(dock->height() > 0);
    const QPoint barInDock = local->mapTo(dock, QPoint(0, 0));
    QVERIFY(barInDock.y() > dock->height() / 2);

    QPointer<TestableAiAgentDock> dockPtr(dock);
    dock->close();
    QTRY_VERIFY_WITH_TIMEOUT(dockPtr.isNull(), 2000);
}

void TestAcpDockCloseEvent::siblingTabify_inactiveDockStillHasStripOnAreaBar()
{
    // Opening a second project rebuilds the area tab bar. The first project's
    // session buttons must land on that bar even while the first dock is hidden.
    AcpSessionModel model1(QStringLiteral("sess-a1"),
                           QStringLiteral("/proj-a"),
                           m_historyDir.path());
    AcpSessionModel model1b(QStringLiteral("sess-a2"),
                            QStringLiteral("/proj-a"),
                            m_historyDir.path());
    AcpSessionModel model2(QStringLiteral("sess-b1"),
                           QStringLiteral("/proj-b"),
                           m_historyDir.path());
    auto *dock1 = new TestableAiAgentDock(QStringLiteral("sess-a1"), &model1, /*confirm=*/false);
    dock1->addSlot(QStringLiteral("sess-a2"), QStringLiteral("test-agent"), &model1b, nullptr, true);
    auto *dock2 = new TestableAiAgentDock(QStringLiteral("sess-b1"), &model2, /*confirm=*/false);

    QMainWindow mw;
    mw.addDockWidget(Qt::RightDockWidgetArea, dock1);
    mw.addDockWidget(Qt::RightDockWidgetArea, dock2);
    mw.tabifyDockWidget(dock1, dock2);
    mw.resize(800, 600);
    mw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mw));
    dock2->raise();
    dock1->refreshSessionStripPlacement();
    dock2->refreshSessionStripPlacement();

    int idx1 = -1;
    QTabBar *area = nullptr;
    QTRY_VERIFY((area = areaTabBarForDock(&mw, dock1, &idx1)) != nullptr);
    QTRY_VERIFY(area->isVisible());
    QVERIFY(area->count() >= 2);
    QTRY_VERIFY(qobject_cast<AiDockSessionStrip *>(
                    area->tabButton(idx1, QTabBar::RightSide))
                != nullptr);
    auto *strip1 = qobject_cast<AiDockSessionStrip *>(
        area->tabButton(idx1, QTabBar::RightSide));
    QCOMPARE(strip1->slotCount(), 2);

    int idx2 = -1;
    QVERIFY(areaTabBarForDock(&mw, dock2, &idx2) == area);
    QTRY_VERIFY(qobject_cast<AiDockSessionStrip *>(
                    area->tabButton(idx2, QTabBar::RightSide))
                != nullptr);

    QPointer<TestableAiAgentDock> p1(dock1);
    QPointer<TestableAiAgentDock> p2(dock2);
    dock1->closeGroup();
    dock2->closeGroup();
    QTRY_VERIFY_WITH_TIMEOUT(p1.isNull(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(p2.isNull(), 2000);
}

void TestAcpDockCloseEvent::closeSibling_remainingShowsLocalTab()
{
    // After tabify, Qt leaves a count==1 area bar at 0 size. Closing the other
    // project must fall back to the remaining dock's South stand-in.
    AcpSessionModel model1(QStringLiteral("sess-keep"),
                           QStringLiteral("/proj-keep"),
                           m_historyDir.path());
    AcpSessionModel model1b(QStringLiteral("sess-keep-2"),
                            QStringLiteral("/proj-keep"),
                            m_historyDir.path());
    AcpSessionModel model2(QStringLiteral("sess-gone"),
                           QStringLiteral("/proj-gone"),
                           m_historyDir.path());
    auto *dock1 = new TestableAiAgentDock(QStringLiteral("sess-keep"), &model1, /*confirm=*/false);
    dock1->addSlot(QStringLiteral("sess-keep-2"), QStringLiteral("test-agent"), &model1b, nullptr, true);
    auto *dock2 = new TestableAiAgentDock(QStringLiteral("sess-gone"), &model2, /*confirm=*/false);

    QMainWindow mw;
    mw.addDockWidget(Qt::RightDockWidgetArea, dock1);
    mw.addDockWidget(Qt::RightDockWidgetArea, dock2);
    mw.tabifyDockWidget(dock1, dock2);
    mw.resize(800, 600);
    mw.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mw));
    dock1->raise();

    QPointer<TestableAiAgentDock> p2(dock2);
    dock2->close();
    QTRY_VERIFY_WITH_TIMEOUT(p2.isNull(), 2000);

    QTabBar *local = nullptr;
    QTRY_VERIFY((local = dock1->findChild<QTabBar *>(QStringLiteral("nn_aiLocalTabBar")))
                != nullptr);
    QTRY_VERIFY(local->isVisible());
    QCOMPARE(local->count(), 1);
    QVERIFY(local->tabButton(0, QTabBar::RightSide) != nullptr);
    QCOMPARE(local->shape(), QTabBar::RoundedSouth);
    QTRY_VERIFY(dock1->height() > 0);
    const QPoint barInDock = local->mapTo(dock1, QPoint(0, 0));
    QVERIFY(barInDock.y() > dock1->height() / 2);

    QPointer<TestableAiAgentDock> p1(dock1);
    dock1->closeGroup();
    QTRY_VERIFY_WITH_TIMEOUT(p1.isNull(), 2000);
}

void TestAcpDockCloseEvent::shutdown_deletingMainWindowWithAiDocks_doesNotAssert()
{
    // App close destroys docks via QObjectPrivate::deleteChildren, which nulls
    // sibling slots before delete. findChildren from ~AiAgentDock hit
    // Q_ASSERT(parent) (qobject.cpp:2149).
    AcpSessionModel model1(QStringLiteral("sess-sd1"),
                           QStringLiteral("/proj-sd"),
                           m_historyDir.path());
    AcpSessionModel model2(QStringLiteral("sess-sd2"),
                           QStringLiteral("/proj-sd2"),
                           m_historyDir.path());
    auto *mw = new QMainWindow;
    auto *dock1 = new TestableAiAgentDock(QStringLiteral("sess-sd1"), &model1, /*confirm=*/false);
    auto *dock2 = new TestableAiAgentDock(QStringLiteral("sess-sd2"), &model2, /*confirm=*/false);
    mw->addDockWidget(Qt::RightDockWidgetArea, dock1);
    mw->addDockWidget(Qt::RightDockWidgetArea, dock2);
    mw->tabifyDockWidget(dock1, dock2);
    mw->resize(800, 600);
    mw->show();
    QVERIFY(QTest::qWaitForWindowExposed(mw));
    delete mw;
}

void TestAcpDockCloseEvent::defaults_areaIsRightAndAllowedIsAll()
{
    // W2: dock advertises its default area as static class info, and at
    // construction time it constrains itself to allow movement to any side.
    QCOMPARE(AiAgentDock::defaultArea(), Qt::RightDockWidgetArea);

    AcpSessionModel model(QStringLiteral("sess-defaults"),
                          QStringLiteral("/proj"),
                          m_historyDir.path());
    auto *dock = new TestableAiAgentDock(QStringLiteral("sess-defaults"), &model, /*confirm=*/false);
    QPointer<TestableAiAgentDock> dockPtr(dock);
    QCOMPARE(dock->allowedAreas(), Qt::AllDockWidgetAreas);
    dock->close();
    QTRY_VERIFY_WITH_TIMEOUT(dockPtr.isNull(), 2000);
}

QTEST_MAIN(TestAcpDockCloseEvent)
#include "test_acp_dock_close_event.moc"
