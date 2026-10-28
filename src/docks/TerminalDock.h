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

#ifndef TERMINALDOCK_H
#define TERMINALDOCK_H

#include <QWidget>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <cstdint>

#include "TerminalProcessStats.h"

class TerminalWidget;
namespace ads { class CDockWidget; }
namespace remote { class ExecutionContext; }

// One terminal tab: the TerminalWidget plus what the tab shows about it
// (title, icon state, tooltip, bell) and its process stats. The buttons and
// the CPU/RAM readout live in the dock area's title bar (TerminalAreaToolbar).
class TerminalDock : public QWidget
{
    Q_OBJECT

public:
    enum class TaskState : uint8_t { None, Running, Succeeded, Failed };

    TerminalDock(const QString &shell, const QString &cwd, QWidget *parent = nullptr);
    TerminalDock(const QString &shell, const QString &cwd, const QString &taskCommand, const QString &taskName, const QStringList &env, QWidget *parent = nullptr);
    // Remote terminal: the execution context is CAPTURED AT SPAWN into this dock
    // and never re-resolved (a workspace switch must not rebase a live session).
    TerminalDock(remote::ExecutionContext *ctx, const QString &shell, const QString &cwd, QWidget *parent = nullptr);
    // Remote task terminal: run a task command on the remote host via SSH PTY.
    TerminalDock(remote::ExecutionContext *ctx, const QString &remoteCwd,
                 const QString &taskCommand, const QString &taskName, QWidget *parent = nullptr);
    ~TerminalDock() override;

    TerminalWidget *terminalWidget() const { return m_terminal; }
    bool isTask() const { return !m_taskCommand.isEmpty(); }
    QString taskCommand() const { return m_taskCommand; }
    QString initialCwd() const { return m_initialCwd; }
    // The shell's current directory when it reports one (OSC 7), else the spawn cwd.
    QString currentCwd() const;
    void setCwdWarning(const QString &warning) { m_cwdWarning = warning; }
    void restartTask();
    // Drop the scrollback and have the shell redraw a clean screen.
    void clearTerminal();
    bool isRemoteSession() const { return m_remoteSession; }
    remote::ExecutionContext *executionContext() const;
    ads::CDockWidget *hostDockWidget() const;

    // Tab presentation.
    QString tabToolTip() const;
    TaskState taskState() const { return m_taskState; }
    // Exit code of the last task run; -1 while it runs or before it ends.
    int taskExitCode() const { return m_taskExitCode; }
    bool bellPending() const { return m_bellPending; }
    QString customName() const { return m_customName; }
    // Empty restores the automatic title. Tasks keep their name.
    void setCustomName(const QString &name);

    // Prompt if a command is still running. Returns false when the user cancels.
    bool confirmClose();
    // Closing would stop work: a command under a local shell, a running task,
    // or a live remote shell (which cannot be inspected).
    bool isBusy() const;
    // Stops the shell and everything it runs.
    void killProcess();
    // Raise the hosting ADS dock widget (this is no longer a QDockWidget).
    void reveal();

    void setProcessStats(const TerminalProcessStats::TreeStats &stats);
    TerminalProcessStats::TreeStats processStats() const { return m_stats; }
    void setCoreLoad(const QString &label);
    // One update (one repaint of the tab-bar readout) per sampling tick.
    void setStats(const TerminalProcessStats::TreeStats &stats, const QString &coreLoad);
    QString coreLoad() const { return m_coreLoad; }

signals:
    // restartTask() replaced terminalWidget(); the owner re-applies its setup.
    void terminalRecreated();
    // Title, tooltip, task state or bell changed.
    void presentationChanged();
    void statsChanged();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void init(const QString &shell, const QString &cwd);
    void connectTerminal();
    void startTerminal(const QString &shell, const QString &cwd);
    void refreshTitle();
    void setTaskState(TaskState state);

    TerminalWidget *m_terminal = nullptr;
    QString m_initialCwd;
    QString m_shell;
    QString m_taskCommand;
    QString m_taskName;
    QStringList m_taskEnv;
    QString m_cwdWarning;

    QString m_customName;
    QString m_programTitle;
    QString m_remoteLabel;
    QString m_lastToolTip;
    TaskState m_taskState = TaskState::None;
    bool m_taskReportsExit = false;
    int m_taskExitCode = -1;
    bool m_bellPending = false;

    TerminalProcessStats::TreeStats m_stats;
    QString m_coreLoad;

    // Captured-at-spawn execution context. Null → local terminal. A QPointer so
    // a context torn down underneath us reads null rather than dangling.
    QPointer<remote::ExecutionContext> m_context;
    bool m_remoteSession = false;
};

#endif
