/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QtTest>

#include "EmbeddedWindowController.h"
#include "PendingCloseState.h"

class FakeEmbeddedController final : public EmbeddedWindowController
{
public:
    bool captureOk = true;
    bool createOk = true;
    bool markOk = true;
    bool prepareOk = true;
    bool tokenOwned = true;
    bool expectedAttached = false;
    bool hostForeignChild = false;
    bool restoreOk = false;
    bool failSafeOk = false;

    int captureCalls = 0;
    int createCalls = 0;
    int markCalls = 0;
    int prepareCalls = 0;
    int tokenOwnershipCalls = 0;
    int expectedAttachedCalls = 0;
    int hierarchyCalls = 0;
    int restoreCalls = 0;
    int failSafeCalls = 0;
    int detachCalls = 0;
    int clearCallbackCalls = 0;
    int forceCloseCalls = 0;
    int scratchCloseCalls = 0;
    int embedRefusedNotices = 0;
    int closeVetoNotices = 0;
    quintptr lastToken = 0;

    bool requestClose(bool failSafe = false)
    {
        return closeByToken(lastToken, failSafe ? ReleaseMode::FailSafeDetach
                                               : ReleaseMode::RestoreExact);
    }

protected:
    bool platCaptureState(quintptr handle, EmbeddedWindowWin32::NativeWindowState *out) override
    {
        ++captureCalls;
        if (!captureOk)
            return false;
        out->handle = handle;
        out->processId = 11;
        out->threadId = 22;
        return true;
    }

    quintptr platCreateTab(quintptr token, const QString &) override
    {
        ++createCalls;
        lastToken = token;
        return createOk ? 0xCAFE : 0;
    }

    bool platMarkWindow(quintptr, quintptr) override { ++markCalls; return markOk; }
    bool platPrepareAndAttach(quintptr, const EmbeddedWindowWin32::NativeWindowState &,
                              quintptr) override
    {
        ++prepareCalls;
        return prepareOk;
    }
    bool platTargetOwnsToken(
        const EmbeddedWindowWin32::NativeWindowState &, quintptr) override
    {
        ++tokenOwnershipCalls;
        return tokenOwned;
    }
    bool platExpectedTargetAttached(
        const EmbeddedWindowWin32::NativeWindowState &, quintptr) override
    {
        ++expectedAttachedCalls;
        return expectedAttached;
    }
    bool platHostRetainsForeignChild(quintptr) override
    {
        ++hierarchyCalls;
        return hostForeignChild;
    }
    bool platRestoreExact(const EmbeddedWindowWin32::NativeWindowState &, quintptr) override
    {
        ++restoreCalls;
        return restoreOk;
    }
    bool platFailSafeDetach(const EmbeddedWindowWin32::NativeWindowState &, quintptr) override
    {
        ++failSafeCalls;
        return failSafeOk;
    }
    void platDetachHost(quintptr) override { ++detachCalls; }
    void platClearHostCallback(quintptr) override { ++clearCallbackCalls; }
    void platForceCloseTab(quintptr) override { ++forceCloseCalls; }
    void platCloseScratchTab() override { ++scratchCloseCalls; }
    void platNotifyEmbedRefused() override { ++embedRefusedNotices; }
    void platNotifyCloseVetoed() override { ++closeVetoNotices; }
};

class TestEmbeddedWindowController : public QObject
{
    Q_OBJECT

private slots:
    void releaseSuccess_isSoleForceClosePredecessor();
    void releaseFailure_retainsEverything();
    void failSafeClose_fallsBackImmediately();
    void watchdogRetry_closesExactlyOnce();
    void shutdown_isIdempotent();
    void recycledHwnd_notAttached_reapsWithoutMutation();
    void tokenLost_targetStillChild_retainsHierarchy();
    void targetGone_reapsWithoutNativeMutation();
    void differentForeignChild_retainsHostAfterTargetRelease();
    void failuresBeforeAndAfterRegistration_unwindSafely();
    void scratchClosesOnlyAfterAttachSuccess();
    void pendingExitAndRestart_completeAfterRegistryDrain();
};

void TestEmbeddedWindowController::releaseSuccess_isSoleForceClosePredecessor()
{
    FakeEmbeddedController controller;
    QSignalSpy drained(&controller, &EmbeddedWindowController::allEmbedsReleased);
    controller.embedWindow(100, QStringLiteral("target"));
    QCOMPARE(controller.embedCount(), 1);
    controller.restoreOk = true;

    QVERIFY(controller.requestClose());
    QCOMPARE(controller.detachCalls, 1);
    QCOMPARE(controller.clearCallbackCalls, 1);
    QCOMPARE(controller.forceCloseCalls, 1);
    QCOMPARE(controller.embedCount(), 0);
    QCOMPARE(drained.count(), 1);
}

void TestEmbeddedWindowController::releaseFailure_retainsEverything()
{
    FakeEmbeddedController controller;
    controller.embedWindow(100, QStringLiteral("target"));

    QVERIFY(!controller.requestClose());
    QCOMPARE(controller.forceCloseCalls, 0);
    QCOMPARE(controller.clearCallbackCalls, 0);
    QCOMPARE(controller.detachCalls, 0);
    QCOMPARE(controller.embedCount(), 1);
    QCOMPARE(controller.closeVetoNotices, 1);

    controller.cancelPendingClose();
    controller.failSafeOk = true;
    controller.poll();
    QCOMPARE(controller.forceCloseCalls, 0); // canceled retry leaves hierarchy untouched
    controller.restoreOk = true;
    controller.shutdown();
}

void TestEmbeddedWindowController::failSafeClose_fallsBackImmediately()
{
    FakeEmbeddedController controller;
    controller.embedWindow(100, QStringLiteral("target"));
    controller.failSafeOk = true;

    QVERIFY(controller.requestClose(true));
    QCOMPARE(controller.restoreCalls, 1);
    QCOMPARE(controller.failSafeCalls, 1);
    QCOMPARE(controller.detachCalls, 1);
    QCOMPARE(controller.forceCloseCalls, 1);
    QCOMPARE(controller.embedCount(), 0);
}

void TestEmbeddedWindowController::watchdogRetry_closesExactlyOnce()
{
    FakeEmbeddedController controller;
    QSignalSpy drained(&controller, &EmbeddedWindowController::allEmbedsReleased);
    controller.embedWindow(100, QStringLiteral("target"));
    QVERIFY(!controller.requestClose());
    controller.failSafeOk = true;

    controller.poll();
    QCOMPARE(controller.restoreCalls, 2); // interactive attempt + retry
    QCOMPARE(controller.failSafeCalls, 1);
    QCOMPARE(controller.forceCloseCalls, 1);
    QCOMPARE(controller.embedCount(), 0);
    QCOMPARE(drained.count(), 1);
    controller.poll();
    QCOMPARE(controller.forceCloseCalls, 1);
    QCOMPARE(drained.count(), 1);
}

void TestEmbeddedWindowController::shutdown_isIdempotent()
{
    FakeEmbeddedController controller;
    controller.embedWindow(100, QStringLiteral("target"));
    controller.restoreOk = true;
    controller.shutdown();
    controller.shutdown();
    QCOMPARE(controller.forceCloseCalls, 1);
    QCOMPARE(controller.detachCalls, 1);
    QCOMPARE(controller.embedCount(), 0);
}

void TestEmbeddedWindowController::recycledHwnd_notAttached_reapsWithoutMutation()
{
    FakeEmbeddedController controller;
    controller.embedWindow(100, QStringLiteral("target"));
    controller.tokenOwned = false; // token mismatch despite same PID/TID-shaped state
    controller.hostForeignChild = false; // recycled HWND is not under our host

    controller.shutdown();
    QCOMPARE(controller.restoreCalls, 0);
    QCOMPARE(controller.failSafeCalls, 0);
    QCOMPARE(controller.detachCalls, 1);
    QCOMPARE(controller.forceCloseCalls, 1);
    QCOMPARE(controller.embedCount(), 0);
    controller.poll();
    QCOMPARE(controller.forceCloseCalls, 1); // reaped exactly once
}

void TestEmbeddedWindowController::tokenLost_targetStillChild_retainsHierarchy()
{
    FakeEmbeddedController controller;
    controller.embedWindow(100, QStringLiteral("target"));
    controller.tokenOwned = false; // token was externally removed: never mutate target
    controller.expectedAttached = true; // expected HWND is still a direct host child
    controller.hostForeignChild = true; // production aggregate agrees

    controller.shutdown();
    QCOMPARE(controller.restoreCalls, 0);
    QCOMPARE(controller.failSafeCalls, 0);
    QCOMPARE(controller.detachCalls, 0);
    QCOMPARE(controller.forceCloseCalls, 0);
    QCOMPARE(controller.embedCount(), 1);
}

void TestEmbeddedWindowController::targetGone_reapsWithoutNativeMutation()
{
    FakeEmbeddedController controller;
    controller.embedWindow(100, QStringLiteral("target"));
    controller.tokenOwned = false;
    controller.hostForeignChild = false;

    controller.poll();
    QCOMPARE(controller.restoreCalls, 0);
    QCOMPARE(controller.failSafeCalls, 0);
    QCOMPARE(controller.detachCalls, 1);
    QCOMPARE(controller.forceCloseCalls, 1);
    QCOMPARE(controller.embedCount(), 0);
}

void TestEmbeddedWindowController::differentForeignChild_retainsHostAfterTargetRelease()
{
    FakeEmbeddedController controller;
    controller.embedWindow(100, QStringLiteral("target"));
    controller.restoreOk = true; // expected target was safely restored
    controller.hostForeignChild = true; // unrelated foreign child remains

    controller.shutdown();
    QCOMPARE(controller.restoreCalls, 1);
    QCOMPARE(controller.failSafeCalls, 0);
    QCOMPARE(controller.detachCalls, 1); // tracking target stopped after restore
    QCOMPARE(controller.forceCloseCalls, 0); // host cannot be destroyed
    QCOMPARE(controller.embedCount(), 1);
}

void TestEmbeddedWindowController::failuresBeforeAndAfterRegistration_unwindSafely()
{
    {
        FakeEmbeddedController c;
        c.captureOk = false;
        c.embedWindow(1, QStringLiteral("x"));
        QCOMPARE(c.createCalls, 0);
        QCOMPARE(c.forceCloseCalls, 0);
        QCOMPARE(c.embedCount(), 0);
    }
    {
        FakeEmbeddedController c;
        c.createOk = false;
        c.embedWindow(1, QStringLiteral("x"));
        QCOMPARE(c.markCalls, 0);
        QCOMPARE(c.forceCloseCalls, 0); // platCreateTab owns its failed resources
        QCOMPARE(c.embedCount(), 0);
    }
    {
        FakeEmbeddedController c;
        c.markOk = false;
        c.embedWindow(1, QStringLiteral("x"));
        QCOMPARE(c.prepareCalls, 0);
        QCOMPARE(c.forceCloseCalls, 1);
        QCOMPARE(c.embedCount(), 0);
    }
    {
        FakeEmbeddedController c;
        c.prepareOk = false;
        c.restoreOk = true;
        c.embedWindow(1, QStringLiteral("x"));
        QCOMPARE(c.forceCloseCalls, 1);
        QCOMPARE(c.scratchCloseCalls, 0);
        QCOMPARE(c.embedCount(), 0);
    }
    {
        FakeEmbeddedController c;
        c.prepareOk = false;
        c.embedWindow(1, QStringLiteral("x"));
        QCOMPARE(c.forceCloseCalls, 0);
        QCOMPARE(c.scratchCloseCalls, 0);
        QCOMPARE(c.embedCount(), 1); // partial attach retained until retry
    }
}

void TestEmbeddedWindowController::scratchClosesOnlyAfterAttachSuccess()
{
    FakeEmbeddedController controller;
    controller.embedWindow(100, QStringLiteral("target"));
    QCOMPARE(controller.scratchCloseCalls, 1);
    QCOMPARE(controller.embedCount(), 1);
    controller.restoreOk = true;
    controller.shutdown();
}

void TestEmbeddedWindowController::pendingExitAndRestart_completeAfterRegistryDrain()
{
    FakeEmbeddedController controller;
    PendingCloseState closeState;
    int queuedRetries = 0;
    connect(&controller, &EmbeddedWindowController::allEmbedsReleased, this, [&]() {
        if (closeState.requestRetry())
            ++queuedRetries;
    });

    controller.embedWindow(100, QStringLiteral("target"));
    closeState.begin(PendingCloseState::Intent::Restart);
    QVERIFY(!controller.requestClose());
    QCOMPARE(queuedRetries, 0);
    controller.failSafeOk = true;
    controller.poll();
    QCOMPARE(queuedRetries, 1);
    QVERIFY(!closeState.requestRetry()); // anti-reentrancy / no duplicate queue
    closeState.retryDequeued();
    QCOMPARE(closeState.accept(), PendingCloseState::Intent::Restart);
    QCOMPARE(closeState.accept(), PendingCloseState::Intent::None); // launch exactly once

    closeState.begin(PendingCloseState::Intent::Exit);
    closeState.cancel();
    QVERIFY(!closeState.requestRetry());
}

QTEST_GUILESS_MAIN(TestEmbeddedWindowController)
#include "test_embedded_window_controller.moc"
