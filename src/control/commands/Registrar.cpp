/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "Registrar.h"

#include "control/ControlApi.h"

#include "DockedEditor.h"
#include "NativeWindowHost.h"
#include "NotepadNextApplication.h"
#include "ScintillaNext.h"
#include "TerminalManager.h"
#include "dialogs/MainWindow.h"
#include "docks/FolderAsWorkspaceDock.h"
#include "docks/TerminalDock.h"
#include "pm2/Pm2ProcessesDialog.h"
#include "remote/SshProfile.h"
#include "widgets/WebViewWidget.h"

#include "DockWidget.h"

#include <QDir>
#include <QJsonArray>

namespace control::commands {

Registrar::Registrar(MainWindow *window, ControlApi *controlApi)
    : w(window)
    , api(controlApi)
{
}

void Registrar::registerAll()
{
    app();
    menu();
    settings();
    scripting();
    workspace();
    tabs();
    files();
    editor();
    search();
    preview();
    terminal();
    pm2();
    git();
    ai();
    goal();
    schedule();
    transfer();
    csv();
}

void Registrar::add(const QString &name, const QString &summary, bool changes, QList<Arg> args, CommandHandler handler)
{
    Command c;
    c.name = name;
    c.summary = summary;
    c.changes = changes;
    c.args = std::move(args);
    c.handler = std::move(handler);
    api->commands().add(std::move(c));
}

// --- MainWindow access ----------------------------------------------------------

NotepadNextApplication *Registrar::application(MainWindow *w)
{
    return w->app;
}

QString Registrar::workspaceRoot(MainWindow *w)
{
    return w->currentWorkspaceRoot();
}

FolderAsWorkspaceDock *Registrar::activeWorkspace(MainWindow *w)
{
    return w->activeWorkspaceDock();
}

Pm2ProcessesDialog *Registrar::openPm2Window(MainWindow *w)
{
    w->showPm2Processes();
    return w->m_pm2Dialog;
}

// --- Arguments ----------------------------------------------------------------

Arg Registrar::required(const QString &name, ArgType type, const QString &description)
{
    Arg a;
    a.name = name;
    a.type = type;
    a.required = true;
    a.description = description;
    return a;
}

Arg Registrar::optional(const QString &name, ArgType type, const QString &description)
{
    Arg a = required(name, type, description);
    a.required = false;
    return a;
}

Arg Registrar::oneOf(const QString &name, const QStringList &values, const QString &description, bool isRequired)
{
    Arg a = required(name, ArgType::Enum, description);
    a.values = values;
    a.required = isRequired;
    return a;
}

Arg Registrar::tabArg(const QString &what)
{
    return optional(QStringLiteral("tab"), ArgType::Int, what);
}

Arg Registrar::workspaceArgSpec()
{
    return optional(QStringLiteral("workspace"), ArgType::String,
                    QStringLiteral("workspace root as workspace.list shows it; default: the active workspace"));
}

QString Registrar::pathArg(MainWindow *w, const QString &path)
{
    if (path.isEmpty() || remote::isSshUri(path))
        return path;
    QString p = QDir::fromNativeSeparators(path);
    if (QDir::isRelativePath(p)) {
        const QString root = w->currentWorkspaceRoot();
        if (!root.isEmpty() && !remote::isSshUri(root))
            p = root + QLatin1Char('/') + p;
    }
    return QDir::cleanPath(p);
}

QString Registrar::displayPath(const QString &path)
{
    return remote::isSshUri(path) ? path : QDir::fromNativeSeparators(path);
}

// --- Tabs ---------------------------------------------------------------------

int Registrar::tabId(ads::CDockWidget *dock)
{
    return tabIdOf(dock);
}

ads::CDockWidget *Registrar::tabById(MainWindow *w, int id)
{
    const QList<ads::CDockWidget *> tabs = w->getDockedEditor()->allTabs();
    for (ads::CDockWidget *dock : tabs) {
        if (tabId(dock) == id)
            return dock;
    }
    return nullptr;
}

ads::CDockWidget *Registrar::tabOf(const QWidget *widget)
{
    for (const QWidget *p = widget; p; p = p->parentWidget()) {
        if (auto *dock = qobject_cast<ads::CDockWidget *>(const_cast<QWidget *>(p)))
            return dock;
    }
    return nullptr;
}

QString Registrar::tabKind(ads::CDockWidget *dock)
{
    QWidget *content = dock->widget();
    if (auto *editor = qobject_cast<ScintillaNext *>(content))
        return editor->QObject::property("nn_diffView").toBool() ? QStringLiteral("diff") : QStringLiteral("editor");
    if (qobject_cast<TerminalDock *>(content))
        return QStringLiteral("terminal");
    if (content && (qobject_cast<WebViewWidget *>(content) || content->findChild<WebViewWidget *>()))
        return QStringLiteral("browser");
    if (dynamic_cast<NativeWindowHost *>(content))
        return QStringLiteral("embed");
    // A tool tab with no terminal in it (the empty Terminal panel).
    if (dock->property("nn_toolTab").toBool())
        return QStringLiteral("panel");
    return QStringLiteral("preview");
}

QJsonObject Registrar::tabJson(MainWindow *w, ads::CDockWidget *dock)
{
    QJsonObject o{{QStringLiteral("tab"), tabId(dock)},
                  {QStringLiteral("kind"), tabKind(dock)},
                  {QStringLiteral("title"), dock->windowTitle()},
                  {QStringLiteral("current"), dock->isCurrentTab()}};
    if (dock->isFloating())
        o.insert(QStringLiteral("floating"), true);
    QWidget *content = dock->widget();
    if (auto *editor = qobject_cast<ScintillaNext *>(content)) {
        if (editor->isFile())
            o.insert(QStringLiteral("path"), displayPath(editor->getFilePath()));
        o.insert(QStringLiteral("modified"), editor->modify());
        o.insert(QStringLiteral("focused"), editor == w->currentEditor());
    } else if (auto *term = qobject_cast<TerminalDock *>(content)) {
        o.insert(QStringLiteral("cwd"), displayPath(term->currentCwd()));
        if (term->isTask())
            o.insert(QStringLiteral("task"), term->taskCommand());
    } else if (content) {
        if (auto *web = qobject_cast<WebViewWidget *>(content) ? qobject_cast<WebViewWidget *>(content)
                                                                : content->findChild<WebViewWidget *>())
            o.insert(QStringLiteral("url"), web->currentUrl());
    }
    return o;
}

ScintillaNext *Registrar::editorArg(const CommandContext &ctx, MainWindow *w, const QString &arg)
{
    if (!ctx.has(arg)) {
        ScintillaNext *editor = w->currentEditor();
        if (!editor)
            ctx.fail(404, QStringLiteral("no editor is open; open one with files.open or files.new"));
        return editor;
    }
    const int id = int(ctx.integer(arg));
    ads::CDockWidget *dock = tabById(w, id);
    if (!dock) {
        ctx.fail(404, QStringLiteral("no tab %1; tabs.list lists them").arg(id));
        return nullptr;
    }
    auto *editor = qobject_cast<ScintillaNext *>(dock->widget());
    if (!editor)
        ctx.fail(409, QStringLiteral("tab %1 is kind '%2', not an editor").arg(id).arg(tabKind(dock)));
    return editor;
}

TerminalDock *Registrar::terminalArg(const CommandContext &ctx, MainWindow *w, const QString &arg)
{
    if (!ctx.has(arg)) {
        // The terminal tab in front, else the newest one.
        const QList<TerminalDock *> docks = w->terminalManager->terminalDocks();
        for (auto it = docks.crbegin(); it != docks.crend(); ++it) {
            if (ads::CDockWidget *host = (*it)->hostDockWidget(); host && host->isCurrentTab())
                return *it;
        }
        if (!docks.isEmpty())
            return docks.constLast();
        ctx.fail(404, QStringLiteral("no terminal is open; open one with terminal.open"));
        return nullptr;
    }
    const int id = int(ctx.integer(arg));
    ads::CDockWidget *dock = tabById(w, id);
    if (!dock) {
        ctx.fail(404, QStringLiteral("no tab %1; tabs.list lists them").arg(id));
        return nullptr;
    }
    auto *term = qobject_cast<TerminalDock *>(dock->widget());
    if (!term)
        ctx.fail(409, QStringLiteral("tab %1 is kind '%2', not a terminal").arg(id).arg(tabKind(dock)));
    return term;
}

// --- Workspaces -----------------------------------------------------------------

FolderAsWorkspaceDock *Registrar::workspaceFor(MainWindow *w, const QString &root)
{
    const QString want = remote::isSshUri(root) ? root : QDir::cleanPath(QDir::fromNativeSeparators(root));
    const QList<FolderAsWorkspaceDock *> docks =
        w->findChildren<FolderAsWorkspaceDock *>(QString(), Qt::FindDirectChildrenOnly);
    for (FolderAsWorkspaceDock *dock : docks) {
        const QString have = dock->rootPath();
        if (have.isEmpty())
            continue;
        const QString clean = remote::isSshUri(have) ? have : QDir::cleanPath(QDir::fromNativeSeparators(have));
#ifdef Q_OS_WIN
        if (clean.compare(want, Qt::CaseInsensitive) == 0)
#else
        if (clean == want)
#endif
            return dock;
    }
    return nullptr;
}

FolderAsWorkspaceDock *Registrar::workspaceArg(const CommandContext &ctx, MainWindow *w, const QString &arg)
{
    if (!ctx.has(arg)) {
        FolderAsWorkspaceDock *dock = w->activeWorkspaceDock();
        if (!dock)
            ctx.fail(404, QStringLiteral("no workspace is open; open one with workspace.open"));
        return dock;
    }
    FolderAsWorkspaceDock *dock = workspaceFor(w, ctx.str(arg));
    if (!dock)
        ctx.fail(404, QStringLiteral("no open workspace '%1'; workspace.list lists them").arg(ctx.str(arg)));
    return dock;
}

QJsonObject Registrar::workspaceJson(MainWindow *w, FolderAsWorkspaceDock *dock)
{
    const QString root = dock->rootPath();
    return {{QStringLiteral("root"), displayPath(root)},
            {QStringLiteral("name"), dock->windowTitle()},
            {QStringLiteral("active"), dock == w->activeWorkspaceDock()},
            {QStringLiteral("ssh"), remote::isSshUri(root)}};
}

QJsonObject Registrar::appState(MainWindow *w)
{
    QJsonArray workspaces;
    const QList<FolderAsWorkspaceDock *> docks =
        w->findChildren<FolderAsWorkspaceDock *>(QString(), Qt::FindDirectChildrenOnly);
    for (FolderAsWorkspaceDock *dock : docks) {
        if (!dock->rootPath().isEmpty())
            workspaces.append(workspaceJson(w, dock));
    }
    QJsonObject o{{QStringLiteral("windowTitle"), w->windowTitle()},
                  {QStringLiteral("workspaces"), workspaces},
                  {QStringLiteral("activeWorkspace"), displayPath(w->currentWorkspaceRoot())},
                  {QStringLiteral("tabCount"), int(w->getDockedEditor()->allTabs().size())}};
    if (ScintillaNext *editor = w->currentEditor()) {
        if (ads::CDockWidget *dock = tabOf(editor))
            o.insert(QStringLiteral("currentEditor"), tabJson(w, dock));
    }
    return o;
}

} // namespace control::commands
