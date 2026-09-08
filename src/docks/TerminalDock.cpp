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
#include "TerminalWidget.h"

#include "remote/ExecutionContext.h"
#include "remote/RemoteExecutionContext.h"

#include "DockWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

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
    setupTaskTitleBar();
    setWindowTitle(tr("Task — %1").arg(m_taskName));
}

TerminalDock::TerminalDock(remote::ExecutionContext *ctx, const QString &shell, const QString &cwd, QWidget *parent)
    : QWidget(parent)
    , m_initialCwd(cwd)
    , m_shell(shell)
    , m_context(ctx)
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
{
    init(QString(), remoteCwd);
    setupTaskTitleBar();
    setWindowTitle(tr("Task — %1").arg(m_taskName));
}

void TerminalDock::init(const QString &shell, const QString &cwd)
{
    setObjectName(QStringLiteral("TerminalDock_%1").arg(reinterpret_cast<qulonglong>(this), 0, 16));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_terminal = new TerminalWidget(this);
    lay->addWidget(m_terminal);

    if (m_taskCommand.isEmpty()) {
        QString initialTitle;
        if (auto *rctx = qobject_cast<remote::RemoteExecutionContext *>(m_context.data())) {
            const remote::SshProfile &p = rctx->profile();
            initialTitle = p.username.isEmpty() ? p.host
                                                : (p.username + QLatin1Char('@') + p.host);
        }
        if (initialTitle.isEmpty())
            initialTitle = terminalTabTitle(cwd);
        setWindowTitle(initialTitle);
    }

    connect(m_terminal, &TerminalWidget::spawnFailed, this, [this](const QString &msg) {
        QMessageBox::critical(this, tr("Terminal"), msg);
    });

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

    m_terminal->start(shell, cwd, m_taskEnv, m_context.data());

    if (!m_taskCommand.isEmpty()) {
        connect(m_terminal, &TerminalWidget::firstOutputReceived, this, [this]() {
            if (!m_cwdWarning.isEmpty()) {
                QByteArray warning = QStringLiteral("\x1b[33m⚠ %1\x1b[0m\r\n")
                    .arg(m_cwdWarning).toUtf8();
                m_terminal->injectOutput(warning);
            }
            QByteArray cmd = m_taskCommand.toUtf8();
            cmd.append('\r');
            m_terminal->writeToPty(cmd);
        }, Qt::SingleShotConnection);
    }
}

TerminalDock::~TerminalDock() = default;

void TerminalDock::setupTaskTitleBar()
{
    auto *titleBar = new QWidget(this);
    auto *layout = new QHBoxLayout(titleBar);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    auto *label = new QLabel(m_taskName, titleBar);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(label);

    m_restartBtn = new QToolButton(titleBar);
    m_restartBtn->setText(tr("Restart"));
    m_restartBtn->setToolTip(tr("Restart task"));
    connect(m_restartBtn, &QToolButton::clicked, this, &TerminalDock::restartTask);
    layout->addWidget(m_restartBtn);

    auto *outer = qobject_cast<QVBoxLayout *>(this->layout());
    outer->insertWidget(0, titleBar);
}

void TerminalDock::restartTask()
{
    if (!m_terminal) return;

    m_terminal->killProcess();
    auto *lay = qobject_cast<QVBoxLayout *>(layout());
    lay->removeWidget(m_terminal);
    delete m_terminal;

    m_terminal = new TerminalWidget(this);
    lay->addWidget(m_terminal);

    connect(m_terminal, &TerminalWidget::spawnFailed, this, [this](const QString &msg) {
        QMessageBox::critical(this, tr("Terminal"), msg);
    });

    m_terminal->start(m_shell, m_initialCwd, m_taskEnv, m_context.data());

    connect(m_terminal, &TerminalWidget::firstOutputReceived, this, [this]() {
        if (!m_cwdWarning.isEmpty()) {
            QByteArray warning = QStringLiteral("\x1b[33m⚠ %1\x1b[0m\r\n")
                .arg(m_cwdWarning).toUtf8();
            m_terminal->injectOutput(warning);
        }
        QByteArray cmd = m_taskCommand.toUtf8();
        cmd.append('\r');
        m_terminal->writeToPty(cmd);
    }, Qt::SingleShotConnection);

    m_terminal->setFocus();
}

bool TerminalDock::confirmClose()
{
    if (m_terminal && m_terminal->isProcessRunning()) {
        QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("Close terminal?"),
            tr("A process is still running in this terminal. Close anyway?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
        m_terminal->killProcess();
    }
    return true;
}

void TerminalDock::reveal()
{
    QWidget *w = this;
    while (w) {
        if (auto *dw = qobject_cast<ads::CDockWidget *>(w)) {
            dw->toggleView(true);
            dw->raise();
            return;
        }
        w = w->parentWidget();
    }
}
