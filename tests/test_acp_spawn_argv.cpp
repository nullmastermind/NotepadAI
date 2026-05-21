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

#include "AcpProtocol.h"

class TestAcpSpawnArgv : public QObject
{
    Q_OBJECT

private slots:
    void posixBasic();
    void posixWithSingleQuote();
    void posixUsesShellEnv();
    void posixFallsBackToBinSh();
    void windowsCmd();
    void windowsPs1();
    void windowsExe();
};

void TestAcpSpawnArgv::posixBasic()
{
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("npx"),
                                         QStringList{QStringLiteral("-y"), QStringLiteral("x")},
                                         true);
    // CI hosts may have arbitrary $SHELL — the only guarantee is non-empty
    // program plus the -lc flag plus the quoted command line.
    QVERIFY(!p.first.isEmpty());
    QCOMPARE(p.second.size(), 2);
    QCOMPARE(p.second.at(0), QStringLiteral("-lc"));
    // npx '-y' 'x'
    QVERIFY(p.second.at(1).contains(QStringLiteral("npx")));
    QVERIFY(p.second.at(1).contains(QStringLiteral("'-y'")));
    QVERIFY(p.second.at(1).contains(QStringLiteral("'x'")));
}

void TestAcpSpawnArgv::posixWithSingleQuote()
{
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("foo"),
                                         QStringList{QStringLiteral("a'b")},
                                         true);
    // expect 'a'\''b'
    QVERIFY(p.second.at(1).contains(QStringLiteral("'a'\\''b'")));
}

void TestAcpSpawnArgv::posixUsesShellEnv()
{
    // Snapshot prior value so we can restore.
    const bool hadShell = qEnvironmentVariableIsSet("SHELL");
    const QByteArray prior = hadShell ? qgetenv("SHELL") : QByteArray();

    qputenv("SHELL", "/usr/bin/zsh");
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("npx"),
                                         QStringList{QStringLiteral("a")},
                                         true);
    QCOMPARE(p.first, QStringLiteral("/usr/bin/zsh"));
    QCOMPARE(p.second.at(0), QStringLiteral("-lc"));

    if (hadShell) {
        qputenv("SHELL", prior);
    } else {
        qunsetenv("SHELL");
    }
}

void TestAcpSpawnArgv::posixFallsBackToBinSh()
{
    const bool hadShell = qEnvironmentVariableIsSet("SHELL");
    const QByteArray prior = hadShell ? qgetenv("SHELL") : QByteArray();

    qunsetenv("SHELL");
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("npx"),
                                         QStringList{QStringLiteral("a")},
                                         true);
    QCOMPARE(p.first, QStringLiteral("/bin/sh"));
    QCOMPARE(p.second.at(0), QStringLiteral("-lc"));

    if (hadShell) {
        qputenv("SHELL", prior);
    }
}

void TestAcpSpawnArgv::windowsCmd()
{
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("npx"),
                                         QStringList{QStringLiteral("-y")},
                                         false,
                                         QStringLiteral("C:/x/npx.cmd"));
    QCOMPARE(p.first, QStringLiteral("cmd"));
    QCOMPARE(p.second.size(), 3);
    QCOMPARE(p.second.at(0), QStringLiteral("/C"));
    QCOMPARE(p.second.at(1), QStringLiteral("C:/x/npx.cmd"));
    QCOMPARE(p.second.at(2), QStringLiteral("-y"));
}

void TestAcpSpawnArgv::windowsPs1()
{
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("script"),
                                         QStringList{QStringLiteral("a")},
                                         false,
                                         QStringLiteral("C:/x/script.ps1"));
    QCOMPARE(p.first, QStringLiteral("powershell"));
    QCOMPARE(p.second.size(), 4);
    QCOMPARE(p.second.at(0), QStringLiteral("-NoProfile"));
    QCOMPARE(p.second.at(1), QStringLiteral("-File"));
    QCOMPARE(p.second.at(2), QStringLiteral("C:/x/script.ps1"));
    QCOMPARE(p.second.at(3), QStringLiteral("a"));
}

void TestAcpSpawnArgv::windowsExe()
{
    auto p = AcpProtocol::buildSpawnArgv(QStringLiteral("tool"),
                                         QStringList{QStringLiteral("a"), QStringLiteral("b")},
                                         false,
                                         QStringLiteral("C:/x/tool.exe"));
    QCOMPARE(p.first, QStringLiteral("C:/x/tool.exe"));
    QCOMPARE(p.second, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

QTEST_GUILESS_MAIN(TestAcpSpawnArgv)
#include "test_acp_spawn_argv.moc"
