/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Integration test for the load-on-construct contract from task 3.4: a session
 * survives a "restart" round-trip via the default AppDataLocation history
 * directory (no historyDirOverride). Uses QStandardPaths test mode so the
 * scratch lives under a temp dir that QStandardPaths picks up automatically.
 */

#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include "AcpHistoryStore.h"
#include "AcpProtocol.h"
#include "AcpSessionModel.h"

class TestAcpSessionModelPersistenceIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void roundTripViaDefaultAppDataLocation();

private:
    bool m_priorTestMode = false;
};

void TestAcpSessionModelPersistenceIntegration::initTestCase()
{
    // Redirect AppDataLocation to a process-private temp tree.
    m_priorTestMode = QStandardPaths::isTestModeEnabled();
    QStandardPaths::setTestModeEnabled(true);
}

void TestAcpSessionModelPersistenceIntegration::cleanupTestCase()
{
    QStandardPaths::setTestModeEnabled(m_priorTestMode);
}

void TestAcpSessionModelPersistenceIntegration::roundTripViaDefaultAppDataLocation()
{
    const QString sessionId = QStringLiteral("integ-session");

    // Pre-clean any leftover from a prior run.
    const QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                               + QStringLiteral("/acp-history");
    QFile::remove(defaultDir + QStringLiteral("/") + sessionId + QStringLiteral(".json"));

    AcpHistoryStore store;
    // Do NOT setHistoryDir — exercise the default-path branch.

    {
        AcpSessionModel a(sessionId, QStringLiteral("integProj"));
        a.setHistoryStore(&store);
        a.onPromptStarted();
        a.onMessageChunk(QStringLiteral("ping"));
        a.onPromptEnded();

        QSignalSpy spy(&store, &AcpHistoryStore::flushed);
        QVERIFY(spy.wait(2000));
    }

    AcpSessionModel b(sessionId, QStringLiteral("ignored"));
    QCOMPARE(b.messages().size(), 1);
    QCOMPARE(b.messages().first().content.first().text, QStringLiteral("ping"));
    QCOMPARE(b.projectId(), QStringLiteral("integProj"));

    // Cleanup the on-disk file so subsequent runs start fresh.
    store.deleteHistory(sessionId);
}

QTEST_GUILESS_MAIN(TestAcpSessionModelPersistenceIntegration)
#include "test_acp_session_model_persistence_integration.moc"
