/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
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

#include "TerminalTabTitle.h"

// Tab title is the shell cwd, never the process image (Windows ConPTY reports
// pwsh.exe / cmd.exe as the window title). Drive roots stay "d:\" not empty
// basename; empty cwd falls back to "Terminal".
class TestTerminalTabTitle : public QObject
{
    Q_OBJECT

private slots:
    void emptyCwd_fallsBackToTerminal();
    void driveRoot_keepsNativeRoot();
    void nestedPath_isFullNativeCwd();
};

void TestTerminalTabTitle::emptyCwd_fallsBackToTerminal()
{
    QCOMPARE(terminalTabTitle(QString()), QStringLiteral("Terminal"));
}

void TestTerminalTabTitle::driveRoot_keepsNativeRoot()
{
#ifdef Q_OS_WIN
    QCOMPARE(terminalTabTitle(QStringLiteral("d:/")), QStringLiteral("d:\\"));
    QCOMPARE(terminalTabTitle(QStringLiteral("d:\\")), QStringLiteral("d:\\"));
#else
    QCOMPARE(terminalTabTitle(QStringLiteral("/")), QStringLiteral("/"));
#endif
}

void TestTerminalTabTitle::nestedPath_isFullNativeCwd()
{
#ifdef Q_OS_WIN
    QCOMPARE(terminalTabTitle(QStringLiteral("C:/Users/foo/proj")),
             QStringLiteral("C:\\Users\\foo\\proj"));
#else
    QCOMPARE(terminalTabTitle(QStringLiteral("/home/foo/proj")),
             QStringLiteral("/home/foo/proj"));
#endif
}

QTEST_MAIN(TestTerminalTabTitle)
#include "test_terminal_tab_title.moc"
