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

#include "TerminalDock.h"
#include "TerminalTabTitle.h"
#include "TerminalTaskLauncher.h"
#include "TerminalWidget.h"
#include "NotepadNextApplication.h"
#include "ApplicationSettings.h"

#include "control/ControlEvents.h"
#include "control/ControlCommands.h"
#include "remote/ExecutionContext.h"
#include "remote/RemoteExecutionContext.h"

#include "DockWidget.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {

int childMemoryLimitGb()
{
    if (auto *app = qobject_cast<NotepadNextApplication *>(qApp)) {
        if (ApplicationSettings *settings = app->getSettings()) {
            return settings->terminalChildMemoryLimitGb();
        }
    }
    return 0;
}

constexpr bool kWindowsCommandLine =
#ifdef Q_OS_WIN
    true;
#else
    false;
#endif

} // namespace

TerminalDock::TerminalDock(const QString &shell, const QString &cwd, QWidget *parent)
    : QWidget(parent)
    , m_initialCwd(cwd)
    , m_shell(shell)
{
    init(shell, cwd);
}

TerminalDock::TerminalDock(const QString &shell, const QString &cwd, const QString &taskCommand, const QString &taskName, const QStringList &env, QWidget *parent)
    : QWidget(parent)
    , m_initialCwd(cwd)
    , m_shell(shell)
    , m_taskCommand(taskCommand)
    , m_taskName(taskName.isEmpty() ? taskCommand : taskName)
    , m_taskEnv(env)
{
    init(shell, cwd);
}

TerminalDock::TerminalDock(remote::ExecutionContext *ctx, const QString &shell, const QString &cwd, QWidget *parent)
    : QWidget(parent)
    , m_initialCwd(cwd)
    , m_shell(shell)
    , m_context(ctx)
    , m_remoteSession(true)
{
    init(shell, cwd);
}

TerminalDock::TerminalDock(remote::ExecutionContext *ctx, const QString &remoteCwd,
                           const QString &taskCommand, const QString &taskName, QWidget *parent)
    : QWidget(parent)
    , m_initialCwd(remoteCwd)
    , m_shell(QString()) // remote default $SHELL
    , m_taskCommand(taskCommand)
    , m_taskName(taskName.isEmpty() ? taskCommand : taskName)
    , m_context(ctx)
    , m_remoteSession(true)
{
    init(QString(), remoteCwd);
}

TerminalDock::~TerminalDock() = default;

void TerminalDock::init(const QString &shell, const QString &cwd)
{
    // Each terminal sits under its own dock (`dock:<uuid>/terminalDock/terminal`).
    setObjectName(QStringLiteral("terminalDock"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_terminal = new TerminalWidget(this);
    m_terminal->setObjectName(QStringLiteral("terminal"));
    lay->addWidget(m_terminal);

    if (auto *rctx = qobject_cast<remote::RemoteExecutionContext *>(m_context.data())) {
        const remote::SshProfile &p = rctx->profile();
        m_remoteLabel = p.username.isEmpty() ? p.host : (p.username + QLatin1Char('@') + p.host);
    }

    // Surface SSH connection loss visibly rather than hanging silently: inject a
    // red marked line via the same injectOutput mechanism as the task cwd
    // warning. The captured context is the one this terminal was spawned
    // against (never re-resolved). SshPtyProcess.finished(-1) still drives the
    // normal process-exit path, so the terminal reaches its exited state.
    if (m_context && m_context->isRemote()) {
        connect(m_context, &remote::ExecutionContext::connectionLost, this,
                [this](const QString &) {
                    if (m_terminal) {
                        m_terminal->injectOutput(
                            QByteArrayLiteral("\x1b[31m\xe2\x9a\xa0 SSH connection lost\x1b[0m\r\n"));
                    }
                });
    }

    connectTerminal();
    startTerminal(shell, cwd);
    refreshTitle();
}

void TerminalDock::connectTerminal()
{
    connect(m_terminal, &TerminalWidget::spawnFailed, this, [this](const QString &msg) {
        QMessageBox::critical(this, tr("Terminal"), msg);
    });
    connect(m_terminal, &TerminalWidget::titleChanged, this, [this](const QString &title) {
        // Progress bars re-send the same title many times a second.
        if (title == m_programTitle) return;
        m_programTitle = title;
        refreshTitle();
    });
    connect(m_terminal, &TerminalWidget::cwdChanged, this, &TerminalDock::refreshTitle);
    connect(m_terminal, &TerminalWidget::clearRequested, this, &TerminalDock::clearTerminal);
    connect(m_terminal, &TerminalWidget::taskExited, this, [this](int code) {
        if (isTask() && m_taskReportsExit) {
            m_taskExitCode = code;
            setTaskState(code == 0 ? TaskState::Succeeded : TaskState::Failed);
        }
        if (control::events::wanted())
            control::events::publish(QStringLiteral("terminal.taskExited"),
                                     {{QStringLiteral("tab"), control::tabIdOf(hostDockWidget())},
                                      {QStringLiteral("exitCode"), code}});
    });
    connect(m_terminal, &TerminalWidget::bellRung, this, [this]() {
        // Only a tab the user is not looking at needs the reminder.
        if (!isVisible() && !m_bellPending) {
            m_bellPending = true;
            emit presentationChanged();
        }
    });
    connect(m_terminal, &TerminalWidget::processExited, this, [this](int code) {
        setProcessStats({});
        if (control::events::wanted())
            control::events::publish(QStringLiteral("terminal.exited"),
                                     {{QStringLiteral("tab"), control::tabIdOf(hostDockWidget())},
                                      {QStringLiteral("exitCode"), code}});
        if (m_taskState == TaskState::Running) {
            m_taskExitCode = code;
            setTaskState(code == 0 ? TaskState::Succeeded : TaskState::Failed);
        }
    });
}

void TerminalDock::startTerminal(const QString &shell, const QString &cwd)
{
    m_terminal->setChildMemoryLimitGb(childMemoryLimitGb());
    // Dropped paths are quoted for this shell; relative paths in the output
    // resolve against the spawn directory once the shell's own cwd is unknown.
    // A remote shell's paths are not on this machine, so only URLs link there.
    m_terminal->setShellKind(m_remoteSession ? TerminalShellKind::Posix : terminalShellKind(shell));
    m_terminal->setLinkBaseDirs(m_remoteSession ? QStringList() : QStringList{cwd});
    m_terminal->setLocalPathLinks(!m_remoteSession);

    // Local tasks run as a shell argument so their exit code comes back; an
    // unknown shell (or a remote one) gets the command typed in instead.
    bool typeCommand = isTask();
    m_taskReportsExit = false;
    if (isTask() && !m_remoteSession) {
        const TerminalTaskLaunch launch = terminalTaskLaunch(
            shell, m_taskCommand, tr("Task finished (exit code %1)"), kWindowsCommandLine);
        if (!launch.arguments.isEmpty()) {
            m_terminal->setLaunchArguments(launch.arguments);
            typeCommand = false;
            m_taskReportsExit = launch.reportsExit;
        }
    }
    if (isTask()) {
        setTaskState(m_taskReportsExit ? TaskState::Running : TaskState::None);
    }

    m_terminal->start(shell, cwd, m_taskEnv, m_context.data());

    if (isTask()) {
        connect(m_terminal, &TerminalWidget::firstOutputReceived, this, [this, typeCommand]() {
            if (!m_cwdWarning.isEmpty()) {
                const QByteArray warning = QStringLiteral("\x1b[33m⚠ %1\x1b[0m\r\n")
                    .arg(m_cwdWarning).toUtf8();
                m_terminal->injectOutput(warning);
            }
            if (typeCommand) {
                QByteArray cmd = m_taskCommand.toUtf8();
                cmd.append('\r');
                m_terminal->writeToPty(cmd);
            }
        }, Qt::SingleShotConnection);
    }
}

remote::ExecutionContext *TerminalDock::executionContext() const
{
    return m_context.data();
}

QString TerminalDock::currentCwd() const
{
    const QString reported = m_terminal ? m_terminal->reportedCwd() : QString();
    return reported.isEmpty() ? m_initialCwd : reported;
}

void TerminalDock::refreshTitle()
{
    QString title;
    if (isTask()) {
        title = elideTerminalTitle(m_taskName);
    } else if (m_remoteSession && m_customName.isEmpty()
               && isShellImageTitle(m_programTitle, m_shell) && !m_remoteLabel.isEmpty()) {
        title = m_remoteLabel;
    } else {
        title = terminalDisplayTitle(m_customName, m_programTitle, m_shell, currentCwd());
    }
    const QString toolTip = tabToolTip();
    if (title == windowTitle() && toolTip == m_lastToolTip) return;
    m_lastToolTip = toolTip;
    setWindowTitle(title);
    emit presentationChanged();
}

QString TerminalDock::tabToolTip() const
{
    QStringList lines;
    if (isTask()) {
        lines << tr("Task: %1").arg(m_taskName) << m_taskCommand;
    } else if (!m_programTitle.trimmed().isEmpty() && !isShellImageTitle(m_programTitle, m_shell)) {
        lines << m_programTitle.trimmed();
    }
    if (!m_remoteLabel.isEmpty()) {
        lines << m_remoteLabel;
    }
    const QString cwd = currentCwd();
    if (!cwd.isEmpty()) {
        lines << (m_remoteSession ? cwd : terminalTabTitle(cwd));
    }
    QString shellLine = m_shell.isEmpty() ? tr("Default shell") : QFileInfo(m_shell).fileName();
    if (m_terminal && m_terminal->ptyPid() > 0 && !m_remoteSession) {
        shellLine += tr(" · PID %1").arg(m_terminal->ptyPid());
    }
    lines << shellLine;
    switch (m_taskState) {
    case TaskState::Running:   lines << tr("Running"); break;
    case TaskState::Succeeded: lines << tr("Finished successfully"); break;
    case TaskState::Failed:    lines << tr("Failed"); break;
    default: break;
    }
    if (m_bellPending) {
        lines << tr("Bell rang");
    }
    return lines.join(QLatin1Char('\n'));
}

void TerminalDock::setTaskState(TaskState state)
{
    if (state == m_taskState) return;
    if (state == TaskState::Running)
        m_taskExitCode = -1;
    m_taskState = state;
    emit presentationChanged();
}

void TerminalDock::setCustomName(const QString &name)
{
    if (isTask()) return;
    const QString trimmed = name.trimmed();
    if (trimmed == m_customName) return;
    m_customName = trimmed;
    refreshTitle();
}

void TerminalDock::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_bellPending) {
        m_bellPending = false;
        emit presentationChanged();
    }
}

void TerminalDock::setProcessStats(const TerminalProcessStats::TreeStats &stats)
{
    m_stats = stats;
    if (!stats.valid) {
        m_coreLoad.clear();
    }
    emit statsChanged();
}

void TerminalDock::setStats(const TerminalProcessStats::TreeStats &stats, const QString &coreLoad)
{
    m_stats = stats;
    m_coreLoad = stats.valid ? coreLoad : QString();
    emit statsChanged();
}

void TerminalDock::setCoreLoad(const QString &label)
{
    if (label == m_coreLoad) return;
    m_coreLoad = label;
    emit statsChanged();
}

void TerminalDock::restartTask()
{
    if (!m_terminal) return;

    m_terminal->killProcess();
    auto *lay = qobject_cast<QVBoxLayout *>(layout());
    lay->removeWidget(m_terminal);
    delete m_terminal;

    m_terminal = new TerminalWidget(this);
    m_terminal->setObjectName(QStringLiteral("terminal"));
    lay->addWidget(m_terminal);
    m_programTitle.clear();
    emit terminalRecreated();

    connectTerminal();
    startTerminal(m_shell, m_initialCwd);
    m_terminal->setFocus();
    setProcessStats({});
    refreshTitle();
}

void TerminalDock::clearTerminal()
{
    if (!m_terminal) return;
    m_terminal->clearScrollback();
    if (m_terminal->hasExited()) return;
    if (!m_remoteSession && terminalShellKind(m_shell) == TerminalShellKind::Cmd) {
        // cmd.exe has no clear-screen key: wipe locally, then an empty
        // command line makes it print a fresh prompt.
        m_terminal->injectOutput(QByteArrayLiteral("\x1b[H\x1b[2J"));
        m_terminal->writeToPty(QByteArrayLiteral("\r"));
    } else {
        // Ctrl+L: bash, zsh, fish and PSReadLine clear and redraw the prompt.
        m_terminal->writeToPty(QByteArrayLiteral("\x0c"));
    }
}

bool TerminalDock::confirmClose()
{
    if (!m_terminal || !m_terminal->isProcessRunning())
        return true;
    if (isBusy()) {
        QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("Close terminal?"),
            tr("A process is still running in this terminal. Close anyway?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
    }
    m_terminal->killProcess();
    return true;
}

bool TerminalDock::isBusy() const
{
    if (!m_terminal || !m_terminal->isProcessRunning())
        return false;
    // A local shell idle at its prompt closes silently; only a running
    // command (a child of the shell) is worth asking about. Remote shells
    // cannot be inspected, and a running task may be pure shell work (a
    // PowerShell cmdlet has no child process), so both always count.
    if (m_remoteSession || m_taskState == TaskState::Running)
        return true;
    QVector<TerminalProcessStats::ProcessSample> samples;
    const auto shellPid = static_cast<quint32>(m_terminal->ptyPid());
    return !TerminalProcessStats::enumerateProcesses(samples)
        || TerminalProcessStats::hasChildProcess(samples.constData(), static_cast<int>(samples.size()), shellPid);
}

void TerminalDock::killProcess()
{
    if (m_terminal && m_terminal->isProcessRunning())
        m_terminal->killProcess();
}

ads::CDockWidget *TerminalDock::hostDockWidget() const
{
    for (QWidget *w = const_cast<TerminalDock *>(this); w; w = w->parentWidget()) {
        if (auto *dw = qobject_cast<ads::CDockWidget *>(w))
            return dw;
    }
    return nullptr;
}

void TerminalDock::reveal()
{
    if (ads::CDockWidget *dw = hostDockWidget()) {
        dw->toggleView(true);
        dw->raise();
    }
}
