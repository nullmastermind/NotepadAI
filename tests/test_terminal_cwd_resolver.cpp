/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "TerminalCwdResolver.h"


class TestTerminalCwdResolver : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void workspaceOnly_canOpenInWorkspace_isTrue();
    void workspaceOnly_canOpenInFolder_fallsBackToWorkspace();
    void workspaceOnly_resolveFolder_returnsWorkspace();

    void fileOnly_canOpenInWorkspace_isFalse();
    void fileOnly_canOpenInFolder_isTrue();
    void fileOnly_resolveFolder_returnsParent();

    void fileAndWorkspace_resolveFolder_prefersFileParent();

    void neither_allDisabled();

    void untitledBufferWithWorkspace_resolveFolder_fallsBack();

    void emptyPaths_disabled();
    void nonExistingWorkspace_disabled();
    void nonExistingFileParentWithWorkspace_fallsBack();

private:
    QTemporaryDir tmp;
    QString workspacePath;
    QString filePath;
    QString fileParent;
};

void TestTerminalCwdResolver::initTestCase()
{
    QVERIFY(tmp.isValid());
    QDir root(tmp.path());

    QVERIFY(root.mkpath(QStringLiteral("workspace")));
    workspacePath = root.absoluteFilePath(QStringLiteral("workspace"));

    QVERIFY(root.mkpath(QStringLiteral("project/src")));
    fileParent = root.absoluteFilePath(QStringLiteral("project/src"));
    filePath = fileParent + QStringLiteral("/main.cpp");

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("int main(){}\n");
    f.close();
}

void TestTerminalCwdResolver::cleanupTestCase()
{
}

void TestTerminalCwdResolver::workspaceOnly_canOpenInWorkspace_isTrue()
{
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(workspacePath), true);
    QCOMPARE(TerminalCwdResolver::resolveWorkspace(workspacePath), QDir::cleanPath(workspacePath));
}

void TestTerminalCwdResolver::workspaceOnly_canOpenInFolder_fallsBackToWorkspace()
{
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(QString(), false, workspacePath), true);
}

void TestTerminalCwdResolver::workspaceOnly_resolveFolder_returnsWorkspace()
{
    QCOMPARE(TerminalCwdResolver::resolveFolder(QString(), false, workspacePath), QDir::cleanPath(workspacePath));
}

void TestTerminalCwdResolver::fileOnly_canOpenInWorkspace_isFalse()
{
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(QString()), false);
    QCOMPARE(TerminalCwdResolver::resolveWorkspace(QString()), QString());
}

void TestTerminalCwdResolver::fileOnly_canOpenInFolder_isTrue()
{
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(filePath, true, QString()), true);
}

void TestTerminalCwdResolver::fileOnly_resolveFolder_returnsParent()
{
    QCOMPARE(TerminalCwdResolver::resolveFolder(filePath, true, QString()), QDir::cleanPath(fileParent));
}

void TestTerminalCwdResolver::fileAndWorkspace_resolveFolder_prefersFileParent()
{
    QCOMPARE(TerminalCwdResolver::resolveFolder(filePath, true, workspacePath), QDir::cleanPath(fileParent));
}

void TestTerminalCwdResolver::neither_allDisabled()
{
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(QString()), false);
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(QString(), false, QString()), false);
    QCOMPARE(TerminalCwdResolver::resolveFolder(QString(), false, QString()), QString());
}

void TestTerminalCwdResolver::untitledBufferWithWorkspace_resolveFolder_fallsBack()
{
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(QString(), false, workspacePath), true);
    QCOMPARE(TerminalCwdResolver::resolveFolder(QString(), false, workspacePath), QDir::cleanPath(workspacePath));
}

void TestTerminalCwdResolver::emptyPaths_disabled()
{
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(QStringLiteral("")), false);
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(QStringLiteral(""), true, QStringLiteral("")), false);
}

void TestTerminalCwdResolver::nonExistingWorkspace_disabled()
{
    const QString bogus = tmp.path() + QStringLiteral("/does/not/exist");
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(bogus), false);
    QCOMPARE(TerminalCwdResolver::resolveWorkspace(bogus), QString());
}

void TestTerminalCwdResolver::nonExistingFileParentWithWorkspace_fallsBack()
{
    const QString bogusFile = tmp.path() + QStringLiteral("/does/not/exist/file.txt");
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(bogusFile, true, workspacePath), true);
    QCOMPARE(TerminalCwdResolver::resolveFolder(bogusFile, true, workspacePath), QDir::cleanPath(workspacePath));
}

QTEST_APPLESS_MAIN(TestTerminalCwdResolver)

#include "test_terminal_cwd_resolver.moc"
