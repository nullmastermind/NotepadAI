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

class TestAcpPathSandbox : public QObject
{
    Q_OBJECT

private slots:
    void posixForward();
    void exactRoot();
    void prefixSiblingRejected();
    void unrelatedRejected();
    void deepDescendant();
    void windowsBackslash();
};

void TestAcpPathSandbox::posixForward()
{
    QVERIFY(AcpProtocol::pathIsInsideWorkingDir(QStringLiteral("/proj/file.cpp"),
                                                QStringLiteral("/proj")));
}

void TestAcpPathSandbox::exactRoot()
{
    QVERIFY(AcpProtocol::pathIsInsideWorkingDir(QStringLiteral("/proj"),
                                                QStringLiteral("/proj")));
}

void TestAcpPathSandbox::prefixSiblingRejected()
{
    // The "/proj-fake" path starts with the literal string "/proj" but is
    // not a descendant — the separator boundary must save us.
    QVERIFY(!AcpProtocol::pathIsInsideWorkingDir(QStringLiteral("/proj-fake/file.cpp"),
                                                 QStringLiteral("/proj")));
}

void TestAcpPathSandbox::unrelatedRejected()
{
    QVERIFY(!AcpProtocol::pathIsInsideWorkingDir(QStringLiteral("/etc/passwd"),
                                                 QStringLiteral("/proj")));
}

void TestAcpPathSandbox::deepDescendant()
{
    QVERIFY(AcpProtocol::pathIsInsideWorkingDir(QStringLiteral("/proj/sub/dir/file.cpp"),
                                                QStringLiteral("/proj")));
}

void TestAcpPathSandbox::windowsBackslash()
{
    QVERIFY(AcpProtocol::pathIsInsideWorkingDir(QStringLiteral("C:\\proj\\file.cpp"),
                                                QStringLiteral("C:\\proj")));
    QVERIFY(!AcpProtocol::pathIsInsideWorkingDir(QStringLiteral("C:\\proj-fake\\file.cpp"),
                                                 QStringLiteral("C:\\proj")));
}

QTEST_GUILESS_MAIN(TestAcpPathSandbox)
#include "test_acp_path_sandbox.moc"
