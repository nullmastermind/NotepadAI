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

#ifndef TERMINALMANAGER_H
#define TERMINALMANAGER_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include "TerminalTaskRegistry.h"

class NotepadNextApplication;
class MainWindow;
class TerminalDock;
namespace remote { class ExecutionContext; }

class TerminalManager : public QObject
{
    Q_OBJECT

public:
    TerminalManager(NotepadNextApplication *app, MainWindow *mainWindow);
    ~TerminalManager() override;

    QString resolveShellCommand() const;

    // Per-workspace task registry. workspacePath must be QDir::cleanPath'd.
    QList<TerminalTask> tasksForWorkspace(const QString &workspacePath) const;
    void addTask(const QString &workspacePath, const TerminalTask &task);
    void setTasks(const QString &workspacePath, const QList<TerminalTask> &tasks);

public slots:
    void openTerminal(const QString &cwd);
    // Remote terminal: PTY over an SSH channel on `ctx` (captured at spawn into
    // the dock). `remoteCwd` is a POSIX path on the host; `shell` empty → the
    // remote default ($SHELL). No local PTY probe (the backend is remote).
    void openRemoteTerminal(remote::ExecutionContext *ctx, const QString &remoteCwd,
                            const QString &shell = QString());
    void openTask(const QString &workspaceCwd, const TerminalTask &task);
    // Remote task execution: run `task` on the remote host via an SSH PTY channel.
    // `ctx` must be Connected. `remoteCwd` is the POSIX workspace root on the host.
    void openRemoteTask(remote::ExecutionContext *ctx, const QString &remoteCwd,
                        const TerminalTask &task);
    void runOrRestartTask(const QString &cwd, const TerminalTask &task,
                          remote::ExecutionContext *context = nullptr);
    void applyTheme();
    void applyFont();
    void shutdown();

    TerminalDock *findTaskDock(const QString &command, const QString &cwd) const;

private:
    void wireContextMenu(TerminalDock *dock);
    void placeInEditor(TerminalDock *dock);

    NotepadNextApplication *m_app;
    MainWindow *m_mainWindow;
    QList<QPointer<TerminalDock>> m_docks;
};

#endif
