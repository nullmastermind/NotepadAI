/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtTest>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "AcpSessionModel.h"
#include "AiAgentDock.h"

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

class TestAcpDockCloseEvent : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void close_whenIdle_destroysDock();
    void close_whileProcessingAndUserDeclines_keepsDock();
    void close_whileProcessingAndUserConfirms_destroysDock();
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
