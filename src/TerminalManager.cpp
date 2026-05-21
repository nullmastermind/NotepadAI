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

#include "TerminalManager.h"

#include "ApplicationSettings.h"
#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "TerminalDock.h"
#include "TerminalWidget.h"
#include "TerminalColorScheme.h"

#include "iptyprocess.h"
#include "ptyqt.h"

#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QMainWindow>
#include <QMessageBox>
#include <QScopedPointer>
#include <QStandardPaths>
#include <QThread>

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

    TerminalDock *existing = nullptr;
    for (const auto &d : m_docks) {
        if (d.isNull()) continue;
        if (d.data() == dock) continue;
        existing = d.data();
        break;
    }

    m_mainWindow->addDockWidget(Qt::BottomDockWidgetArea, dock);
    if (existing) {
        m_mainWindow->tabifyDockWidget(existing, dock);
    }
    dock->setVisible(true);
    dock->raise();
    dock->terminalWidget()->setFocus();
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
    for (auto &d : m_docks) {
        if (d.isNull()) continue;
        if (auto *w = d->terminalWidget()) {
            w->killProcess();
        }
    }
    QThread::msleep(250);
}
