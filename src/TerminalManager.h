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

#ifndef TERMINALMANAGER_H
#define TERMINALMANAGER_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class NotepadNextApplication;
class MainWindow;
class TerminalDock;

class TerminalManager : public QObject
{
    Q_OBJECT

public:
    TerminalManager(NotepadNextApplication *app, MainWindow *mainWindow);
    ~TerminalManager() override;

    QString resolveShellCommand() const;

public slots:
    void openTerminal(const QString &cwd);
    void applyTheme();
    void applyFont();
    void shutdown();

private:
    NotepadNextApplication *m_app;
    MainWindow *m_mainWindow;
    QList<QPointer<TerminalDock>> m_docks;
};

#endif
