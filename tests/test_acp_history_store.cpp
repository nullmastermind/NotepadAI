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
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QThread>

#include "AcpHistoryStore.h"

class TestAcpHistoryStore : public QObject
{
    Q_OBJECT

private slots:
    void writeCreatesDirectoryAndFile();
    void debounceCoalescesRapidWrites();
    void deleteMissingIsSuccess();
    void deleteExistingRemovesFile();
    void flushAllForcesImmediateWrites();
    void worksOnWorkerThread();
};

static QJsonObject makePayload(int value)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("value"), value);
    obj.insert(QStringLiteral("messages"), QJsonValue::fromVariant(QVariantList()));
    return obj;
}

void TestAcpHistoryStore::writeCreatesDirectoryAndFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dir = tmp.path() + QStringLiteral("/fresh-sub");

    AcpHistoryStore store;
    store.setHistoryDir(dir);

    QSignalSpy spy(&store, &AcpHistoryStore::flushed);
    store.scheduleWrite(QStringLiteral("s1"), makePayload(42));
    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("s1"));

    const QString path = dir + QStringLiteral("/s1.json");
    QVERIFY(QFile::exists(path));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    QVERIFY(doc.isObject());
    QCOMPARE(doc.object().value(QStringLiteral("value")).toInt(), 42);
}

void TestAcpHistoryStore::debounceCoalescesRapidWrites()
{
    QTemporaryDir tmp;
    AcpHistoryStore store;
    store.setHistoryDir(tmp.path());

    QSignalSpy spy(&store, &AcpHistoryStore::flushed);
    for (int i = 0; i < 10; ++i) {
        store.scheduleWrite(QStringLiteral("s1"), makePayload(i));
    }
    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.count(), 1);

    QFile f(tmp.path() + QStringLiteral("/s1.json"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(obj.value(QStringLiteral("value")).toInt(), 9);
}

void TestAcpHistoryStore::deleteMissingIsSuccess()
{
    QTemporaryDir tmp;
    AcpHistoryStore store;
    store.setHistoryDir(tmp.path());
    // Just verify no crash / no infinite loop. We don't capture warnings —
    // missing-file path explicitly returns without calling qWarning.
    store.deleteHistory(QStringLiteral("never-existed"));
    QVERIFY(!QFile::exists(tmp.path() + QStringLiteral("/never-existed.json")));
}

void TestAcpHistoryStore::deleteExistingRemovesFile()
{
    QTemporaryDir tmp;
    AcpHistoryStore store;
    store.setHistoryDir(tmp.path());

    QSignalSpy spy(&store, &AcpHistoryStore::flushed);
    store.scheduleWrite(QStringLiteral("s1"), makePayload(1));
    QVERIFY(spy.wait(2000));
    const QString path = tmp.path() + QStringLiteral("/s1.json");
    QVERIFY(QFile::exists(path));

    store.deleteHistory(QStringLiteral("s1"));
    QVERIFY(!QFile::exists(path));
}

void TestAcpHistoryStore::flushAllForcesImmediateWrites()
{
    QTemporaryDir tmp;
    AcpHistoryStore store;
    store.setHistoryDir(tmp.path());

    store.scheduleWrite(QStringLiteral("a"), makePayload(1));
    store.scheduleWrite(QStringLiteral("b"), makePayload(2));
    store.scheduleWrite(QStringLiteral("c"), makePayload(3));

    // No spy.wait — flushAll is synchronous.
    store.flushAll();

    QVERIFY(QFile::exists(tmp.path() + QStringLiteral("/a.json")));
    QVERIFY(QFile::exists(tmp.path() + QStringLiteral("/b.json")));
    QVERIFY(QFile::exists(tmp.path() + QStringLiteral("/c.json")));
}

void TestAcpHistoryStore::worksOnWorkerThread()
{
    QTemporaryDir tmp;
    auto *store = new AcpHistoryStore();
    store->setHistoryDir(tmp.path());

    QThread workerThread;
    store->moveToThread(&workerThread);
    workerThread.start();

    QSignalSpy spy(store, &AcpHistoryStore::flushed);
    // Cross-thread invoke — emulates the model -> store usage pattern.
    QMetaObject::invokeMethod(store,
                              "scheduleWrite",
                              Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("wt")),
                              Q_ARG(QJsonObject, makePayload(7)));
    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.count(), 1);
    QVERIFY(QFile::exists(tmp.path() + QStringLiteral("/wt.json")));

    // Shut the worker down cleanly.
    QMetaObject::invokeMethod(store, "deleteLater", Qt::QueuedConnection);
    workerThread.quit();
    QVERIFY(workerThread.wait(3000));
}

QTEST_GUILESS_MAIN(TestAcpHistoryStore)
#include "test_acp_history_store.moc"
