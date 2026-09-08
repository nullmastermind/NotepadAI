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

#include "TerminalManager.h"

#include "AiAgentDock.h"
#include "ApplicationSettings.h"
#include "DockedEditor.h"
#include "DockWidget.h"
#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "TerminalAiHelper.h"
#include "TerminalDock.h"
#include "TerminalTaskRegistry.h"
#include "TerminalWidget.h"
#include "TerminalColorScheme.h"

#include "iptyprocess.h"
#include "ptyqt.h"

#include "remote/ExecutionContext.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QScopedPointer>
#include <QStandardPaths>

TerminalManager::TerminalManager(NotepadNextApplication *app, MainWindow *mainWindow)
    : QObject(mainWindow)
    , m_app(app)
    , m_mainWindow(mainWindow)
{
}

TerminalManager::~TerminalManager() = default;

QString TerminalManager::resolveShellCommand() const
{
    if (!m_app || !m_app->getSettings()) {
#ifdef Q_OS_WIN
        return qEnvironmentVariable("COMSPEC", QStringLiteral("cmd.exe"));
#else
        return qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
#endif
    }

    QString configured = m_app->getSettings()->shellCommand();
    if (!configured.isEmpty()) {
        return configured;
    }

#ifdef Q_OS_WIN
    return qEnvironmentVariable("COMSPEC", QStringLiteral("cmd.exe"));
#else
    return qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
#endif
}

void TerminalManager::openTerminal(const QString &cwd)
{
    // Fail fast if the platform PTY backend is unavailable (e.g. Windows
    // pre-1809 without ConPTY). Probing here avoids creating an empty dock
    // that would otherwise be left attached after the error dialog is dismissed.
    {
        QScopedPointer<IPtyProcess> probe(PtyQt::createPtyProcess(IPtyProcess::AutoPty));
        if (!probe || !probe->isAvailable()) {
#ifdef Q_OS_WIN
            QMessageBox::critical(
                m_mainWindow,
                tr("Terminal"),
                tr("Cannot open terminal: ConPTY is unavailable. The embedded terminal requires Windows 10 build 17763 (version 1809) or later."));
#else
            QMessageBox::critical(
                m_mainWindow,
                tr("Terminal"),
                tr("Cannot open terminal: the platform PTY backend is unavailable."));
#endif
            return;
        }
    }

    const QString configured = resolveShellCommand();
    QString shell = configured;
    if (!shell.isEmpty() && !shell.contains(QLatin1Char('/')) && !shell.contains(QLatin1Char('\\'))) {
        const QString resolved = QStandardPaths::findExecutable(shell);
        if (!resolved.isEmpty()) {
            shell = resolved;
        }
    }

    if (shell.isEmpty() || !QFileInfo::exists(shell)) {
        QMessageBox::critical(
            m_mainWindow,
            tr("Terminal"),
            tr("Cannot launch terminal: shell '%1' was not found. Set Terminal/ShellCommand in Preferences.").arg(configured));
        return;
    }

    auto *dock = new TerminalDock(shell, cwd, m_mainWindow);
    wireContextMenu(dock);

    QPointer<TerminalDock> p(dock);
    m_docks.append(p);

    connect(dock, &QObject::destroyed, this, [this](QObject *obj) {
        for (int i = m_docks.size() - 1; i >= 0; --i) {
            if (m_docks[i].isNull() || m_docks[i].data() == obj) {
                m_docks.removeAt(i);
            }
        }
    });

    if (m_app) {
        const TerminalColorScheme scheme = m_app->isEffectiveThemeDark()
            ? TerminalColorScheme::darkScheme()
            : TerminalColorScheme::lightScheme();
        dock->terminalWidget()->setColorScheme(scheme);

        if (m_app->getSettings()) {
            QFont f;
            const QString fontStr = m_app->getSettings()->terminalFont();
            if (!fontStr.isEmpty() && f.fromString(fontStr)) {
                dock->terminalWidget()->setTerminalFont(f);
            } else {
                dock->terminalWidget()->setTerminalFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            }
        }
    }

    placeInEditor(dock);
}

void TerminalManager::openRemoteTerminal(remote::ExecutionContext *ctx,
                                         const QString &remoteCwd, const QString &shell)
{
    if (!ctx) {
        return;
    }
    // No local PTY probe — the backend is an SSH channel on `ctx`. Pass an empty
    // shell through as-is so SshPtyProcess defaults to the remote $SHELL.
    auto *dock = new TerminalDock(ctx, shell, remoteCwd, m_mainWindow);
    wireContextMenu(dock);

    QPointer<TerminalDock> p(dock);
    m_docks.append(p);

    connect(dock, &QObject::destroyed, this, [this](QObject *obj) {
        for (int i = m_docks.size() - 1; i >= 0; --i) {
            if (m_docks[i].isNull() || m_docks[i].data() == obj) {
                m_docks.removeAt(i);
            }
        }
    });

    if (m_app) {
        const TerminalColorScheme scheme = m_app->isEffectiveThemeDark()
            ? TerminalColorScheme::darkScheme()
            : TerminalColorScheme::lightScheme();
        dock->terminalWidget()->setColorScheme(scheme);

        if (m_app->getSettings()) {
            QFont f;
            const QString fontStr = m_app->getSettings()->terminalFont();
            if (!fontStr.isEmpty() && f.fromString(fontStr)) {
                dock->terminalWidget()->setTerminalFont(f);
            } else {
                dock->terminalWidget()->setTerminalFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            }
        }
    }

    placeInEditor(dock);
}

static QStringList parseEnvText(const QString &envText)
{
    QStringList result;
    if (envText.isEmpty()) return result;
    const QStringList lines = envText.split(QLatin1Char('\n'));
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        if (key.isEmpty())
            continue;
        QString value = line.mid(eq + 1).trimmed();
        // Strip surrounding quotes
        if (value.size() >= 2) {
            const QChar first = value.front();
            const QChar last = value.back();
            if ((first == QLatin1Char('"') && last == QLatin1Char('"')) ||
                (first == QLatin1Char('\'') && last == QLatin1Char('\''))) {
                value = value.mid(1, value.size() - 2);
            }
        }
        result.append(key + QLatin1Char('=') + value);
    }
    return result;
}

void TerminalManager::openTask(const QString &workspaceCwd, const TerminalTask &task)
{
    {
        QScopedPointer<IPtyProcess> probe(PtyQt::createPtyProcess(IPtyProcess::AutoPty));
        if (!probe || !probe->isAvailable()) {
#ifdef Q_OS_WIN
            QMessageBox::critical(
                m_mainWindow,
                tr("Terminal"),
                tr("Cannot open terminal: ConPTY is unavailable. The embedded terminal requires Windows 10 build 17763 (version 1809) or later."));
#else
            QMessageBox::critical(
                m_mainWindow,
                tr("Terminal"),
                tr("Cannot open terminal: the platform PTY backend is unavailable."));
#endif
            return;
        }
    }

    const QString configured = resolveShellCommand();
    QString shell = configured;
    if (!shell.isEmpty() && !shell.contains(QLatin1Char('/')) && !shell.contains(QLatin1Char('\\'))) {
        const QString resolved = QStandardPaths::findExecutable(shell);
        if (!resolved.isEmpty()) {
            shell = resolved;
        }
    }

    if (shell.isEmpty() || !QFileInfo::exists(shell)) {
        QMessageBox::critical(
            m_mainWindow,
            tr("Terminal"),
            tr("Cannot launch terminal: shell '%1' was not found. Set Terminal/ShellCommand in Preferences.").arg(configured));
        return;
    }

    // Resolve cwd: relative → join with workspace, absolute → use directly, empty → workspace
    QString resolvedCwd = workspaceCwd;
    QString cwdWarning;
    if (!task.cwd.isEmpty()) {
        QString taskCwd = task.cwd;
        if (QDir::isRelativePath(taskCwd)) {
            taskCwd = QDir::cleanPath(workspaceCwd + QLatin1Char('/') + taskCwd);
        }
        if (QDir(taskCwd).exists()) {
            resolvedCwd = taskCwd;
        } else {
            cwdWarning = tr("Directory '%1' not found, using workspace root.").arg(task.cwd);
        }
    }

    const QStringList env = parseEnvText(task.env);

    auto *dock = new TerminalDock(shell, resolvedCwd, task.command, task.name, env, m_mainWindow);
    if (!cwdWarning.isEmpty())
        dock->setCwdWarning(cwdWarning);
    wireContextMenu(dock);

    QPointer<TerminalDock> p(dock);
    m_docks.append(p);

    connect(dock, &QObject::destroyed, this, [this](QObject *obj) {
        for (int i = m_docks.size() - 1; i >= 0; --i) {
            if (m_docks[i].isNull() || m_docks[i].data() == obj) {
                m_docks.removeAt(i);
            }
        }
    });

    if (m_app) {
        const TerminalColorScheme scheme = m_app->isEffectiveThemeDark()
            ? TerminalColorScheme::darkScheme()
            : TerminalColorScheme::lightScheme();
        dock->terminalWidget()->setColorScheme(scheme);

        if (m_app->getSettings()) {
            QFont f;
            const QString fontStr = m_app->getSettings()->terminalFont();
            if (!fontStr.isEmpty() && f.fromString(fontStr)) {
                dock->terminalWidget()->setTerminalFont(f);
            } else {
                dock->terminalWidget()->setTerminalFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            }
        }
    }

    placeInEditor(dock);
}

void TerminalManager::openRemoteTask(remote::ExecutionContext *ctx,
                                     const QString &remoteCwd,
                                     const TerminalTask &task)
{
    if (!ctx || !ctx->isRemote()) {
        return;
    }
    if (ctx->state() != remote::ExecutionContext::State::Connected) {
        QMessageBox::warning(
            m_mainWindow,
            tr("Terminal"),
            tr("Cannot run task '%1': remote workspace is not connected.").arg(task.name));
        return;
    }

    auto *dock = new TerminalDock(ctx, remoteCwd, task.command, task.name, m_mainWindow);
    wireContextMenu(dock);

    QPointer<TerminalDock> p(dock);
    m_docks.append(p);

    connect(dock, &QObject::destroyed, this, [this](QObject *obj) {
        for (int i = m_docks.size() - 1; i >= 0; --i) {
            if (m_docks[i].isNull() || m_docks[i].data() == obj) {
                m_docks.removeAt(i);
            }
        }
    });

    if (m_app) {
        const TerminalColorScheme scheme = m_app->isEffectiveThemeDark()
            ? TerminalColorScheme::darkScheme()
            : TerminalColorScheme::lightScheme();
        dock->terminalWidget()->setColorScheme(scheme);

        if (m_app->getSettings()) {
            QFont f;
            const QString fontStr = m_app->getSettings()->terminalFont();
            if (!fontStr.isEmpty() && f.fromString(fontStr)) {
                dock->terminalWidget()->setTerminalFont(f);
            } else {
                dock->terminalWidget()->setTerminalFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
            }
        }
    }

    placeInEditor(dock);
}

void TerminalManager::applyTheme()
{
    if (!m_app) return;
    const TerminalColorScheme scheme = m_app->isEffectiveThemeDark()
        ? TerminalColorScheme::darkScheme()
        : TerminalColorScheme::lightScheme();
    for (const auto &d : m_docks) {
        if (d.isNull()) continue;
        if (auto *w = d->terminalWidget()) {
            w->setColorScheme(scheme);
        }
    }
}

void TerminalManager::applyFont()
{
    if (!m_app || !m_app->getSettings()) return;
    QFont f;
    const QString fontStr = m_app->getSettings()->terminalFont();
    if (fontStr.isEmpty() || !f.fromString(fontStr)) {
        f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    for (const auto &d : m_docks) {
        if (d.isNull()) continue;
        if (auto *w = d->terminalWidget()) {
            w->setTerminalFont(f);
        }
    }
}

void TerminalManager::shutdown()
{
    // killProcess() routes to TerminateProcess (Windows) / SIGKILL (POSIX)
    // which mark the child for teardown synchronously at the kernel level.
    // The app is exiting — no downstream operation needs the child actually
    // reaped, and QPointer guards in m_docks survive a late destroyed signal.
    for (auto &d : m_docks) {
        if (d.isNull()) continue;
        if (auto *w = d->terminalWidget()) {
            w->killProcess();
        }
    }
}

TerminalDock *TerminalManager::findTaskDock(const QString &command, const QString &cwd) const
{
    const QString cleanCwd = QDir::cleanPath(cwd);
    for (const auto &d : m_docks) {
        if (d.isNull()) continue;
        if (!d->isTask()) continue;
        if (d->taskCommand() == command &&
            QDir::cleanPath(d->initialCwd()).compare(cleanCwd, Qt::CaseInsensitive) == 0) {
            return d.data();
        }
    }
    return nullptr;
}

void TerminalManager::runOrRestartTask(const QString &cwd, const TerminalTask &task,
                                       remote::ExecutionContext *context)
{
    TerminalDock *existing = findTaskDock(task.command, cwd);
    if (existing) {
        existing->reveal();
        existing->restartTask();
        if (auto *w = existing->terminalWidget()) {
            w->setFocus();
        }
        return;
    }
    if (context && context->isRemote()) {
        openRemoteTask(context, cwd, task);
    } else {
        openTask(cwd, task);
    }
}

QList<TerminalTask> TerminalManager::tasksForWorkspace(const QString &workspacePath) const
{
    if (!m_app || !m_app->getSettings())
        return {};
    return TerminalTaskRegistry(m_app->getSettings()).tasksForWorkspace(workspacePath);
}

void TerminalManager::addTask(const QString &workspacePath, const TerminalTask &task)
{
    if (!m_app || !m_app->getSettings())
        return;
    TerminalTaskRegistry(m_app->getSettings()).addTask(workspacePath, task);
}

void TerminalManager::setTasks(const QString &workspacePath, const QList<TerminalTask> &tasks)
{
    if (!m_app || !m_app->getSettings())
        return;
    TerminalTaskRegistry(m_app->getSettings()).setTasks(workspacePath, tasks);
}

void TerminalManager::placeInEditor(TerminalDock *dock)
{
    DockedEditor *editor = m_mainWindow->getDockedEditor();
    ads::CDockWidget *dw = editor->addBottomToolTab(dock, dock->windowTitle(), QIcon());

    connect(dock, &QWidget::windowTitleChanged, dw, [dw](const QString &title) {
        dw->setWindowTitle(title);
    });

    dw->setFeature(ads::CDockWidget::DockWidgetFeature::CustomCloseHandling, true);
    connect(dw, &ads::CDockWidget::closeRequested, dock, [dock, dw]() {
        if (dock->confirmClose())
            dw->closeDockWidget();
    });

    dw->toggleView(true);
    dw->raise();
    if (auto *tw = dock->terminalWidget())
        tw->setFocus();
}

void TerminalManager::wireContextMenu(TerminalDock *dock)
{
    TerminalWidget *tw = dock->terminalWidget();
    connect(tw, &TerminalWidget::contextMenuAboutToShow,
            this, [this, tw](QMenu *menu) {
        if (!tw->hasSelection()) return;
        AiAgentDock *aiDock = m_mainWindow->activeAiDock();
        if (!aiDock) return;
        menu->addSeparator();
        QAction *sendAi = menu->addAction(tr("Send to AI"));
        connect(sendAi, &QAction::triggered, this, [this, tw]() {
            AiAgentDock *aiDock = m_mainWindow->activeAiDock();
            if (!aiDock) return;
            QString wrapped = wrapInCodeblock(tw->selectedText());
            aiDock->insertTextToInput(wrapped);
            aiDock->setVisible(true);
            aiDock->raise();
        });
    });
}
