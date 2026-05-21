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

#include "TerminalDock.h"
#include "TerminalWidget.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

TerminalDock::TerminalDock(const QString &shell, const QString &cwd, QWidget *parent)
    : QDockWidget(parent)
    , m_initialCwd(cwd)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setObjectName(QStringLiteral("TerminalDock_%1").arg(reinterpret_cast<qulonglong>(this), 0, 16));

    m_terminal = new TerminalWidget(this);
    setWidget(m_terminal);

    QString cwdBasename = QFileInfo(QDir::cleanPath(cwd)).fileName();
    if (cwdBasename.isEmpty()) cwdBasename = cwd;
    setWindowTitle(tr("Terminal — %1").arg(cwdBasename));

    connect(m_terminal, &TerminalWidget::titleChanged, this, [this](const QString &t) {
        if (!t.isEmpty()) {
            setWindowTitle(t);
        }
    });

    connect(m_terminal, &TerminalWidget::spawnFailed, this, [this](const QString &msg) {
        QMessageBox::critical(this, tr("Terminal"), msg);
    });

    m_terminal->start(shell, cwd);
}

TerminalDock::~TerminalDock() = default;

void TerminalDock::closeEvent(QCloseEvent *event)
{
    if (m_terminal && m_terminal->isProcessRunning()) {
        QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            tr("Close terminal?"),
            tr("A process is still running in this terminal. Close anyway?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_terminal->killProcess();
    }
    QDockWidget::closeEvent(event);
}
