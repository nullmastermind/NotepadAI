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
#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "AcpAgentDefinition.h"
#include "AcpAgentManager.h"
#include "AcpAgentRegistry.h"
#include "AcpConnection.h"
#include "AcpHistoryStore.h"
#include "AcpSessionModel.h"
#include "AiAgentDock.h"
#include "ApplicationSettings.h"

class TestAcpAgentExitRestart : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    // W4.a — agentExited signal fires from handleProcessFinished.
    void agentExited_emittedFromProcessFinished();

    // W4.d — restartSession swaps connection+model but keeps the dock,
    // updates objectName, deletes the old history file.
    void restartSession_swapsInnerStateAndDeletesOldHistory();

private:
    QTemporaryDir m_tempSettings;
};

void TestAcpAgentExitRestart::initTestCase()
{
    QVERIFY(m_tempSettings.isValid());
    QCoreApplication::setOrganizationName("NotepadNextTest");
    QCoreApplication::setApplicationName("NotepadNextTest_AcpAgentExitRestart");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempSettings.path());
}

void TestAcpAgentExitRestart::init()
{
    QSettings s;
    s.remove(QStringLiteral("Ai"));
    s.sync();
}

void TestAcpAgentExitRestart::agentExited_emittedFromProcessFinished()
{
    // Construct a bare AcpConnection — no spawn. The test seam invokes the
    // process-finished handler directly so we don't need to launch a real
    // subprocess in CI.
    AcpConnection conn;
    qRegisterMetaType<QProcess::ExitStatus>("QProcess::ExitStatus");
    QSignalSpy exitedSpy(&conn, &AcpConnection::agentExited);
    QVERIFY(exitedSpy.isValid());

    conn.simulateProcessFinished(7, QProcess::NormalExit);

    QCOMPARE(exitedSpy.count(), 1);
    const QList<QVariant> args = exitedSpy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 7);
}

void TestAcpAgentExitRestart::restartSession_swapsInnerStateAndDeletesOldHistory()
{
    ApplicationSettings settings;
    AcpAgentManager manager(&settings);

    // Re-point the history store at a fresh tmp dir so the test owns the disk.
    QTemporaryDir histDir;
    QVERIFY(histDir.isValid());
    QMetaObject::invokeMethod(manager.historyStore(), "setHistoryDir",
                              Qt::BlockingQueuedConnection,
                              Q_ARG(QString, histDir.path()));

    // Register a dummy agent definition. The command does not need to be a
    // real binary because we will never actually spawn (we simulate the
    // process-finished signal). openAgent will still call conn->spawn() —
    // QProcess::start with a bogus path emits errorOccurred but does not
    // block, so we just ignore the resulting error in the test.
    AcpAgentDefinition def;
    def.id = QStringLiteral("test-agent");
    def.name = QStringLiteral("Test Agent");
    def.command = QStringLiteral("definitely-not-a-real-binary-xyz");
    def.args = QStringList{};
    def.builtin = false;
    QVERIFY(manager.registry()->addAgent(def));

    QTemporaryDir workDir;
    QVERIFY(workDir.isValid());

    AiAgentDock *dock = manager.openAgent(def.id, workDir.path());
    QVERIFY(dock != nullptr);
    QPointer<AiAgentDock> dockPtr(dock);

    const QString oldSessionId = dock->sessionId();
    QVERIFY(!oldSessionId.isEmpty());
    QCOMPARE(dock->objectName(), QStringLiteral("AiAgentDock_%1").arg(oldSessionId));

    // Seed an on-disk history file for the old session so we can observe it
    // disappear after restart.
    AcpHistoryStore *store = manager.historyStore();
    QSignalSpy flushedSpy(store, &AcpHistoryStore::flushed);
    QSignalSpy deletedSpy(store, &AcpHistoryStore::historyDeleted);

    QJsonObject payload;
    payload.insert(QStringLiteral("sessionId"), oldSessionId);
    QMetaObject::invokeMethod(store, "scheduleWrite", Qt::QueuedConnection,
                              Q_ARG(QString, oldSessionId),
                              Q_ARG(QJsonObject, payload));
    QMetaObject::invokeMethod(store, "flushAll", Qt::BlockingQueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(flushedSpy.count() >= 1, 2000);

    const QString oldFile = histDir.path() + QStringLiteral("/") + oldSessionId + QStringLiteral(".json");
    QVERIFY(QFile::exists(oldFile));

    // Trigger restart via the manager.
    manager.restartSession(oldSessionId);

    // Dock survives — same pointer.
    QVERIFY(!dockPtr.isNull());
    QCOMPARE(dockPtr.data(), dock);

    // Session id rotated.
    const QString newSessionId = dock->sessionId();
    QVERIFY(!newSessionId.isEmpty());
    QVERIFY(newSessionId != oldSessionId);
    QCOMPARE(dock->objectName(), QStringLiteral("AiAgentDock_%1").arg(newSessionId));

    // Old history file is deleted (queued through worker thread).
    QTRY_VERIFY_WITH_TIMEOUT(deletedSpy.count() >= 1, 2000);
    QVERIFY(!QFile::exists(oldFile));
}

QTEST_MAIN(TestAcpAgentExitRestart)
#include "test_acp_agent_exit_restart.moc"
