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
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include "AcpAgentDefinition.h"
#include "AcpAgentManager.h"
#include "AcpAgentRegistry.h"
#include "AcpHistoryStore.h"
#include "ApplicationSettings.h"

class TestAcpAgentManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void construct_exposesRegistry_andSeedsBuiltin();
    void historyStore_runsOnWorkerThread();
    void shutdown_joinsWorkerThread();
    void registry_pointerIsStableAcrossCalls();
    void deleteSessionHistory_removesFile();

private:
    QTemporaryDir tempDir;
};

void TestAcpAgentManager::initTestCase()
{
    QVERIFY(tempDir.isValid());
    QCoreApplication::setOrganizationName("NotepadNextTest");
    QCoreApplication::setApplicationName("NotepadNextTest_AcpAgentManager");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
}

void TestAcpAgentManager::init()
{
    // Reset persistent Ai/* keys between tests.
    QSettings s;
    s.remove(QStringLiteral("Ai"));
    s.sync();
}

void TestAcpAgentManager::construct_exposesRegistry_andSeedsBuiltin()
{
    ApplicationSettings settings;
    AcpAgentManager manager(&settings);
    AcpAgentRegistry *reg = manager.registry();
    QVERIFY(reg != nullptr);
    QVERIFY(reg->contains(AcpAgentRegistry::builtinClaudeCodeId()));
}

void TestAcpAgentManager::historyStore_runsOnWorkerThread()
{
    ApplicationSettings settings;
    AcpAgentManager manager(&settings);
    QThread *historyThread = manager.historyStoreThread();
    QVERIFY(historyThread != nullptr);
    QVERIFY(historyThread->isRunning());
    QVERIFY(historyThread != QThread::currentThread());

    QVERIFY(manager.historyStore() != nullptr);
    QCOMPARE(manager.historyStore()->thread(), historyThread);
}

void TestAcpAgentManager::shutdown_joinsWorkerThread()
{
    ApplicationSettings settings;
    AcpAgentManager manager(&settings);
    QThread *historyThread = manager.historyStoreThread();
    QVERIFY(historyThread->isRunning());

    manager.shutdown();

    // After shutdown the worker thread has been quit() + wait()ed.
    QVERIFY(!historyThread->isRunning());
}

void TestAcpAgentManager::registry_pointerIsStableAcrossCalls()
{
    ApplicationSettings settings;
    AcpAgentManager manager(&settings);
    QVERIFY(manager.registry() == manager.registry());
}

void TestAcpAgentManager::deleteSessionHistory_removesFile()
{
    ApplicationSettings settings;
    AcpAgentManager manager(&settings);

    AcpHistoryStore *store = manager.historyStore();
    QVERIFY(store != nullptr);

    // Point the store at a fresh tmp dir so we don't collide with real data.
    QTemporaryDir histDir;
    QVERIFY(histDir.isValid());
    QMetaObject::invokeMethod(store, "setHistoryDir", Qt::BlockingQueuedConnection,
                              Q_ARG(QString, histDir.path()));

    const QString sid = QStringLiteral("session-to-delete");
    QSignalSpy flushedSpy(store, &AcpHistoryStore::flushed);
    QSignalSpy deletedSpy(store, &AcpHistoryStore::historyDeleted);

    // Schedule a write and flush it so a real file lands on disk.
    QJsonObject payload;
    payload.insert(QStringLiteral("sessionId"), sid);
    QMetaObject::invokeMethod(store, "scheduleWrite", Qt::QueuedConnection,
                              Q_ARG(QString, sid),
                              Q_ARG(QJsonObject, payload));
    QMetaObject::invokeMethod(store, "flushAll", Qt::BlockingQueuedConnection);
    QVERIFY(flushedSpy.wait(2000) || flushedSpy.count() >= 1);

    const QString filePath = histDir.path() + QStringLiteral("/") + sid + QStringLiteral(".json");
    QVERIFY(QFile::exists(filePath));

    // Drive the manager API under test — must route to the store on its
    // worker thread and ultimately delete the file.
    manager.deleteSessionHistory(sid);

    QVERIFY(deletedSpy.wait(2000));
    QVERIFY(!QFile::exists(filePath));
}

QTEST_MAIN(TestAcpAgentManager)
#include "test_acp_agent_manager.moc"
