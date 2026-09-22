/*
 * This file is part of Notepad Next.
 * Copyright 2022 Justin Dailey
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


#include "DebugLogDock.h"
#include "ui_DebugLogDock.h"
#include "DebugManager.h"

#include <QCoreApplication>
#include <QScrollBar>

static QPlainTextEdit *output = Q_NULLPTR;

static void debugLogDockMessageHandler(const QString &msg)
{
    // qWarning reaches this from whatever thread emitted it. Deleting a folder
    // that QFileSystemModel is watching makes QWindowsFileSystemWatcherEngineThread
    // qErrnoWarning ("FindNextChangeNotification failed … Access is denied").
    // appendPlainText on that thread crashes in HarfBuzz (QTextDocument is not
    // thread-safe). Always post to the GUI thread. QueuedConnection also stops a
    // warning emitted during layout from re-entering the same document.
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;
    QMetaObject::invokeMethod(app, [msg]() {
        if (output != nullptr)
            output->appendPlainText(msg);
    }, Qt::QueuedConnection);
}

DebugLogDock::DebugLogDock(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::DebugLogDock)
{
    ui->setupUi(this);

    output = ui->txtDebugOutput;
    DebugManager::addMessageHandler(debugLogDockMessageHandler);

    connect(this, &QDockWidget::visibilityChanged, this, [=](bool visible) {
        if (visible) {
            ui->txtDebugOutput->horizontalScrollBar()->setValue(0);
        }
    });
}

DebugLogDock::~DebugLogDock()
{
    output = Q_NULLPTR;
    delete ui;
}
