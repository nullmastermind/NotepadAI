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

#include "TerminalProcessStats.h"


class QToolButton;
class QLabel;

class TerminalWidget;
namespace ads { class CDockWidget; }
namespace remote { class ExecutionContext; }

class TerminalDock : public QWidget
{
    Q_OBJECT

public:
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
    void setCwdWarning(const QString &warning) { m_cwdWarning = warning; }
    void restartTask();
    bool isRemoteSession() const { return m_remoteSession; }
    ads::CDockWidget *hostDockWidget() const;

    // Prompt if a process is still running. Returns false when the user cancels.
    bool confirmClose();
    // Raise the hosting ADS dock widget (this is no longer a QDockWidget).
    void reveal();
    void setProcessStats(const TerminalProcessStats::TreeStats &stats);
    void setCoreLoad(const QString &label);
    bool cpuLabelUnderMouse() const;

signals:
    void cpuHoverChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void init(const QString &shell, const QString &cwd);
    void setupChrome();
    void wireTerminalStats();


    TerminalWidget *m_terminal = nullptr;
    QString m_initialCwd;
    QString m_shell;
    QString m_taskCommand;
    QString m_taskName;
    QStringList m_taskEnv;
    QString m_cwdWarning;
    QToolButton *m_restartBtn = nullptr;
    QLabel *m_cpuLabel = nullptr;

    QLabel *m_ramLabel = nullptr;
    QPoint m_coreTipAnchor;

    // Captured-at-spawn execution context. Null → local terminal. A QPointer so
    // a context torn down underneath us reads null rather than dangling.
    QPointer<remote::ExecutionContext> m_context;
    bool m_remoteSession = false;
};

#endif
