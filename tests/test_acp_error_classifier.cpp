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

#include "AcpErrorClassifier.h"

using AcpErrorClassifier::AcpErrorKind;

class TestAcpErrorClassifier : public QObject
{
    Q_OBJECT

private slots:
    void classify_data();
    void classify();
    void loginHint_data();
    void loginHint();
    void friendlyMessages();
};

void TestAcpErrorClassifier::classify_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<int>("expected");

    QTest::newRow("authReq") << "Authentication required"   << int(AcpErrorKind::AuthRequired);
    QTest::newRow("notLoggedIn") << "not logged in"          << int(AcpErrorKind::AuthRequired);
    QTest::newRow("loginReq") << "login required"            << int(AcpErrorKind::AuthRequired);
    QTest::newRow("unauthorized") << "Unauthorized"          << int(AcpErrorKind::AuthRequired);
    QTest::newRow("authRequired") << "auth required"         << int(AcpErrorKind::AuthRequired);
    QTest::newRow("authFailed") << "auth failed"             << int(AcpErrorKind::AuthRequired);
    QTest::newRow("spawnAgent") << "failed to spawn agent process" << int(AcpErrorKind::SpawnFailed);
    QTest::newRow("spawnBlocking") << "spawn_blocking failed"      << int(AcpErrorKind::SpawnFailed);
    QTest::newRow("random") << "random other error"         << int(AcpErrorKind::InitFailed);
}

void TestAcpErrorClassifier::classify()
{
    QFETCH(QString, input);
    QFETCH(int, expected);
    QCOMPARE(int(AcpErrorClassifier::classify(input)), expected);
}

void TestAcpErrorClassifier::loginHint_data()
{
    QTest::addColumn<QString>("cmd");
    QTest::addColumn<QStringList>("args");
    QTest::addColumn<QString>("expected");

    QTest::newRow("claude") << QStringLiteral("npx")
                            << QStringList{QStringLiteral("-y"),
                                           QStringLiteral("@agentclientprotocol/claude-agent-acp@latest")}
                            << QStringLiteral("claude login");
    QTest::newRow("auggie") << QStringLiteral("auggie")
                            << QStringList{QStringLiteral("acp")}
                            << QStringLiteral("auggie login");
    QTest::newRow("gemini") << QStringLiteral("gemini-cli")
                            << QStringList{}
                            << QStringLiteral("gemini auth login");
    QTest::newRow("custom") << QStringLiteral("/usr/local/bin/customAgent")
                            << QStringList{QStringLiteral("x")}
                            << QStringLiteral("customAgent login");
}

void TestAcpErrorClassifier::loginHint()
{
    QFETCH(QString, cmd);
    QFETCH(QStringList, args);
    QFETCH(QString, expected);
    QCOMPARE(AcpErrorClassifier::loginHint(cmd, args), expected);
}

void TestAcpErrorClassifier::friendlyMessages()
{
    const QString hint = QStringLiteral("claude login");
    QCOMPARE(AcpErrorClassifier::friendlyMessage(AcpErrorKind::AuthRequired,
                                                 QStringLiteral("raw"), hint),
             QString::fromUtf8("Authentication required \xe2\x80\x94 run `claude login` in your terminal"));
    QCOMPARE(AcpErrorClassifier::friendlyMessage(AcpErrorKind::SpawnFailed,
                                                 QStringLiteral("raw"), hint),
             QString::fromUtf8("Could not start the agent \xe2\x80\x94 check that the command is installed"));
    QCOMPARE(AcpErrorClassifier::friendlyMessage(AcpErrorKind::InitFailed,
                                                 QStringLiteral("boom"), hint),
             QString::fromUtf8("Agent initialization failed \xe2\x80\x94 boom"));
}

QTEST_GUILESS_MAIN(TestAcpErrorClassifier)
#include "test_acp_error_classifier.moc"
