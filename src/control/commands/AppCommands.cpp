/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// app.*, view.*, panels.*, menu.* (docs/adr/0007).

#include "Registrar.h"

#include "control/ControlApi.h"
#include "control/ControlFeedback.h"

#include "DockedEditor.h"
#include "EmbeddedWindowManager.h"
#include "NativeWindowHost.h"
#include "NotepadNextApplication.h"
#include "ScintillaNext.h"
#include "EditorManager.h"
#include "SessionManager.h"
#include "dialogs/MainWindow.h"

#include <QApplication>
#include <QDialog>
#include <QDockWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVersionNumber>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QSet>
#include <QTimer>

namespace control::commands {

namespace {

// A tabified panel behind its tab is moved off-screen, not hidden.
bool panelShown(const QDockWidget *dock)
{
    if (!dock->isVisible())
        return false;
    if (dock->isFloating())
        return true;
    const QRect g = dock->geometry();
    return g.right() >= 0 && g.bottom() >= 0;
}

QList<QDockWidget *> panelsOf(MainWindow *w)
{
    return w->findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly);
}

QDockWidget *panelArg(const CommandContext &ctx, MainWindow *w)
{
    const QString id = ctx.str(QStringLiteral("panel"));
    for (QDockWidget *dock : panelsOf(w)) {
        if (dock->objectName() == id)
            return dock;
    }
    ctx.fail(404, QStringLiteral("no panel '%1'; panels.list lists them").arg(id));
    return nullptr;
}

// One of the window's named actions (MainWindow.ui).
QAction *named(MainWindow *w, const char *id)
{
    return w->findChild<QAction *>(QLatin1String(id));
}

QString plainText(QString text)
{
    // "&Open..." -> "Open...", "&&" stays one "&".
    text.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    text.remove(QLatin1Char('&'));
    text.replace(QStringLiteral("\x01"), QStringLiteral("&"));
    return text;
}

struct MenuEntry
{
    QAction *action = nullptr;
    QStringList path; // menu titles from the menu bar down
};

// Named actions reachable from the menu bar, in menu order. Menus that fill
// themselves on opening are filled first (their enabled states too).
void collectMenu(QMenu *menu, QStringList path, QList<MenuEntry> &out)
{
    emit menu->aboutToShow();
    path.append(plainText(menu->title()));
    const QList<QAction *> actions = menu->actions();
    for (QAction *a : actions) {
        if (a->isSeparator())
            continue;
        if (QMenu *sub = a->menu()) {
            collectMenu(sub, path, out);
            continue;
        }
        if (!a->objectName().isEmpty())
            out.append({a, path});
    }
}

QList<MenuEntry> menuEntries(MainWindow *w)
{
    QList<MenuEntry> out;
    const QList<QAction *> top = w->menuBar()->actions();
    for (QAction *a : top) {
        if (QMenu *menu = a->menu())
            collectMenu(menu, {}, out);
    }
    return out;
}

QJsonObject menuJson(const MenuEntry &e)
{
    QJsonObject o{{QStringLiteral("id"), e.action->objectName()},
                  {QStringLiteral("label"), plainText(e.action->text())},
                  {QStringLiteral("menu"), e.path.join(QStringLiteral(" ▸ "))},
                  {QStringLiteral("enabled"), e.action->isEnabled()}};
    if (!e.action->shortcut().isEmpty())
        o.insert(QStringLiteral("shortcut"), e.action->shortcut().toString(QKeySequence::PortableText));
    if (e.action->isCheckable())
        o.insert(QStringLiteral("checked"), e.action->isChecked());
    const QString command = commandFor(e.action);
    if (command == QLatin1String("-"))
        o.insert(QStringLiteral("dialogOnly"), true);
    else if (!command.isEmpty())
        o.insert(QStringLiteral("useCommand"), command);
    return o;
}

} // namespace

void Registrar::app()
{
    add(QStringLiteral("app.state"),
        QStringLiteral("Read the window title, open workspaces, the active workspace and the current editor"), false, {},
        [w = w](const CommandContext &ctx) { ctx.ok(appState(w)); });

    add(QStringLiteral("app.window"), QStringLiteral("Change the main window's state"), true,
        {oneOf(QStringLiteral("state"), {QStringLiteral("normal"), QStringLiteral("maximized"),
                                         QStringLiteral("minimized"), QStringLiteral("fullscreen")},
               QStringLiteral("normal, maximized, minimized or fullscreen (menus and toolbars hidden)"), true)},
        [w = w](const CommandContext &ctx) {
            const QString state = ctx.str(QStringLiteral("state"));
            const bool full = named(w, "actionFullScreen")->isChecked();
            if ((state == QLatin1String("fullscreen")) != full)
                named(w, "actionFullScreen")->trigger();
            if (state == QLatin1String("maximized"))
                w->showMaximized();
            else if (state == QLatin1String("minimized"))
                w->showMinimized();
            else if (state == QLatin1String("normal"))
                w->showNormal();
            ctx.ok({{QStringLiteral("state"), state}});
        });

    add(QStringLiteral("app.quit"), QStringLiteral("Quit NotepadAI (this ends the Control API too)"), true,
        {oneOf(QStringLiteral("unsaved"), {QStringLiteral("save"), QStringLiteral("discard")},
               QStringLiteral("what to do with unsaved editors the session does not keep; required when there are any"))},
        [w = w](const CommandContext &ctx) {
            // The editors the window's close would ask about (checkEditorsBeforeClose),
            // a file deleted on disk included.
            const SessionManager *session = w->app->getSessionManager();
            EditorManager *em = w->app->getEditorManager();
            QVector<ScintillaNext *> unsaved;
            for (ScintillaNext *e : w->editors()) {
                // The file watch learns of a deleted file a moment later; look
                // now (one stat each, no prompt) so the close sees the same.
                e->checkFileForStateChange();
                if (!em->isDiffView(e) && !session->willFileGetStoredInSession(e) && !e->isSavedToDisk())
                    unsaved.append(e);
            }
            const QString choice = ctx.str(QStringLiteral("unsaved"));
            if (!unsaved.isEmpty() && choice.isEmpty()) {
                QJsonArray tabs;
                for (ScintillaNext *e : unsaved)
                    tabs.append(tabJson(w, tabOf(e)));
                ctx.fail(409, QStringLiteral("%1 editor(s) have unsaved changes; pass \"unsaved\": \"save\" or \"discard\"")
                                  .arg(unsaved.size()),
                         {{QStringLiteral("tabs"), tabs}, {QStringLiteral("needs"), QStringLiteral("unsaved")}});
                return;
            }
            for (ScintillaNext *e : unsaved) {
                if (choice == QLatin1String("save")) {
                    if (!e->isFile()) {
                        ctx.fail(409, QStringLiteral("'%1' has never been saved; save it with files.save and a path first")
                                          .arg(e->getName()));
                        return;
                    }
                    if (e->save() != QFileDevice::NoError) {
                        ctx.fail(422, QStringLiteral("could not save %1").arg(displayPath(e->getFilePath())));
                        return;
                    }
                }
            }
            const bool discard = !unsaved.isEmpty() && choice == QLatin1String("discard");
            ctx.ok();
            // After the answer is on its way: closing ends the server. The
            // window's save prompt gets the discard instead of opening, for
            // this close only (one an embed refuses must still ask the user).
            QTimer::singleShot(100, w, [w, discard] {
                if (discard)
                    w->m_closeSaveAnswer = MainWindow::UserSaveAction::DiscardAll;
                const QPointer<MainWindow> window(w);
                w->close();
                if (window)
                    window->m_closeSaveAnswer.reset();
            });
        });

    add(QStringLiteral("app.debugInfo"),
        QStringLiteral("Read the build and environment details Help > Debug Info shows"), false, {},
        [w = w](const CommandContext &ctx) {
            ctx.ok({{QStringLiteral("lines"), QJsonArray::fromStringList(w->app->debugInfo())}});
        });

    add(QStringLiteral("app.updates"),
        QStringLiteral("Check GitHub for a newer release, as Help > Check for Updates does (installing stays the "
                       "user's: open `url` in the browser for them)"),
        false, {}, [w = w](const CommandContext &ctx) {
            auto *manager = new QNetworkAccessManager(w);
            QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/nullmastermind/NotepadAI/releases/latest")));
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NotepadAI/%1").arg(APP_VERSION));
            request.setRawHeader("Accept", "application/vnd.github+json");
            request.setTransferTimeout(30000);
            QNetworkReply *reply = manager->get(request);
            QObject::connect(reply, &QNetworkReply::finished, manager, [ctx, reply, manager] {
                reply->deleteLater();
                manager->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    ctx.fail(422, QStringLiteral("could not reach GitHub: %1").arg(reply->errorString()));
                    return;
                }
                const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
                const QString tag = o.value(QStringLiteral("tag_name")).toString();
                if (tag.isEmpty()) {
                    ctx.fail(422, QStringLiteral("GitHub answered without a release tag"));
                    return;
                }
                auto version = [](QString text) {
                    if (text.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
                        text.remove(0, 1);
                    return QVersionNumber::fromString(text);
                };
                const QString current = QStringLiteral(APP_VERSION);
                ctx.ok({{QStringLiteral("current"), current},
                        {QStringLiteral("latest"), tag},
                        {QStringLiteral("newer"), version(tag) > version(current)},
                        {QStringLiteral("url"), o.value(QStringLiteral("html_url")).toString()},
                        {QStringLiteral("notes"), o.value(QStringLiteral("body")).toString().left(4000)}});
            });
        });

    // Embed menu: another app's window kept inside a tab (Windows).
    add(QStringLiteral("embed.list"), QStringLiteral("List the other apps' windows the Embed menu offers"), false, {},
        [w = w](const CommandContext &ctx) {
            QJsonArray windows;
            for (const EmbeddedWindowManager::WindowInfo &info : w->m_embeddedWindowManager->enumerateEmbeddableWindows())
                windows.append(QJsonObject{{QStringLiteral("handle"), QString::number(info.handle)},
                                           {QStringLiteral("title"), info.title}});
            ctx.ok({{QStringLiteral("windows"), windows}});
        });

    add(QStringLiteral("embed.window"),
        QStringLiteral("Embed another app's window in a tab, as picking it in the Embed menu does; tabs.close releases it"),
        true,
        {optional(QStringLiteral("handle"), ArgType::String, QStringLiteral("the window's handle from embed.list")),
         optional(QStringLiteral("title"), ArgType::String,
                  QStringLiteral("instead: the one window whose title contains this text (case-insensitive)"))},
        [w = w](const CommandContext &ctx) {
            const QList<EmbeddedWindowManager::WindowInfo> windows = w->m_embeddedWindowManager->enumerateEmbeddableWindows();
            QList<EmbeddedWindowManager::WindowInfo> picked;
            for (const EmbeddedWindowManager::WindowInfo &info : windows) {
                if (ctx.has(QStringLiteral("handle")) ? QString::number(info.handle) == ctx.str(QStringLiteral("handle"))
                                                      : info.title.contains(ctx.str(QStringLiteral("title")), Qt::CaseInsensitive))
                    picked.append(info);
            }
            if (!ctx.has(QStringLiteral("handle")) && !ctx.has(QStringLiteral("title"))) {
                ctx.fail(400, QStringLiteral("pass `handle` or `title` (embed.list lists the windows)"));
                return;
            }
            if (picked.isEmpty()) {
                ctx.fail(404, QStringLiteral("no embeddable window matches; embed.list lists them"));
                return;
            }
            if (picked.size() > 1) {
                QJsonArray titles;
                for (const auto &info : picked)
                    titles.append(QJsonObject{{QStringLiteral("handle"), QString::number(info.handle)},
                                              {QStringLiteral("title"), info.title}});
                ctx.fail(409, QStringLiteral("%1 windows match; pass `handle`").arg(picked.size()),
                         {{QStringLiteral("needs"), QStringLiteral("handle")}, {QStringLiteral("windows"), titles}});
                return;
            }
            QSet<ads::CDockWidget *> before;
            for (ads::CDockWidget *d : w->getDockedEditor()->allTabs())
                before.insert(d);
            w->m_embeddedWindowManager->embedWindow(picked.first().handle, picked.first().title);
            ads::CDockWidget *dock = nullptr;
            for (ads::CDockWidget *d : w->getDockedEditor()->allTabs()) {
                if (!before.contains(d) && dynamic_cast<NativeWindowHost *>(d->widget()))
                    dock = d;
            }
            if (!dock) {
                ctx.fail(422, QStringLiteral("Windows refused to embed '%1' (it may run elevated or be gone)")
                                  .arg(picked.first().title));
                return;
            }
            ctx.reveal(dock);
            ctx.ok(tabJson(w, dock));
        });

    add(QStringLiteral("view.zoom"), QStringLiteral("Zoom every editor in or out, or reset the zoom"), true,
        {oneOf(QStringLiteral("step"), {QStringLiteral("in"), QStringLiteral("out"), QStringLiteral("reset")},
               QStringLiteral("in, out or reset"), true)},
        [w = w](const CommandContext &ctx) {
            const QString step = ctx.str(QStringLiteral("step"));
            QAction *a = step == QLatin1String("in")    ? named(w, "actionZoomIn")
                         : step == QLatin1String("out") ? named(w, "actionZoomOut")
                                                        : named(w, "actionZoomReset");
            a->trigger();
            if (ScintillaNext *editor = w->currentEditor())
                ctx.reveal(editor);
            ctx.ok({{QStringLiteral("level"), w->zoomLevel}});
        });

    add(QStringLiteral("panels.list"),
        QStringLiteral("List the side and bottom panels (workspaces, AI sessions, search results, ...)"), false, {},
        [w = w](const CommandContext &ctx) {
            QJsonArray list;
            for (QDockWidget *dock : panelsOf(w)) {
                if (dock->objectName().isEmpty())
                    continue;
                QJsonObject o{{QStringLiteral("panel"), dock->objectName()},
                              {QStringLiteral("title"), dock->windowTitle()},
                              {QStringLiteral("shown"), panelShown(dock)},
                              {QStringLiteral("open"), !dock->isHidden()}};
                if (dock->isFloating())
                    o.insert(QStringLiteral("floating"), true);
                if (!dock->isFloating() && !dock->isHidden()) {
                    o.insert(QStringLiteral("width"), dock->width());
                    o.insert(QStringLiteral("height"), dock->height());
                }
                list.append(o);
            }
            ctx.ok({{QStringLiteral("panels"), list}});
        });

    add(QStringLiteral("panels.show"), QStringLiteral("Show a panel in front, or hide it"), true,
        {required(QStringLiteral("panel"), ArgType::String, QStringLiteral("panel id from panels.list")),
         optional(QStringLiteral("visible"), ArgType::Bool, QStringLiteral("false hides it; default true"))},
        [w = w](const CommandContext &ctx) {
            QDockWidget *dock = panelArg(ctx, w);
            if (!dock)
                return;
            if (ctx.flag(QStringLiteral("visible"), true)) {
                dock->show();
                ctx.reveal(dock);
            } else {
                dock->hide();
            }
            ctx.ok({{QStringLiteral("shown"), panelShown(dock)}});
        });

    add(QStringLiteral("panels.resize"), QStringLiteral("Resize a docked panel (its separator moves)"), true,
        {required(QStringLiteral("panel"), ArgType::String, QStringLiteral("panel id from panels.list")),
         optional(QStringLiteral("width"), ArgType::Int, QStringLiteral("width in pixels (left or right panel)")),
         optional(QStringLiteral("height"), ArgType::Int, QStringLiteral("height in pixels (top or bottom panel)"))},
        [w = w](const CommandContext &ctx) {
            QDockWidget *dock = panelArg(ctx, w);
            if (!dock)
                return;
            if (!ctx.has(QStringLiteral("width")) && !ctx.has(QStringLiteral("height"))) {
                ctx.fail(400, QStringLiteral("give \"width\" and/or \"height\""));
                return;
            }
            if (dock->isFloating()) {
                dock->resize(int(ctx.integer(QStringLiteral("width"), dock->width())),
                             int(ctx.integer(QStringLiteral("height"), dock->height())));
            } else {
                if (ctx.has(QStringLiteral("width")))
                    w->resizeDocks({dock}, {int(ctx.integer(QStringLiteral("width")))}, Qt::Horizontal);
                if (ctx.has(QStringLiteral("height")))
                    w->resizeDocks({dock}, {int(ctx.integer(QStringLiteral("height")))}, Qt::Vertical);
            }
            ctx.reveal(dock);
            ctx.ok({{QStringLiteral("width"), dock->width()}, {QStringLiteral("height"), dock->height()}});
        });
}

void Registrar::menu()
{
    // Menu actions that open a dialog: the command doing their job, or "-"
    // when only the user can answer it.
    const QList<QPair<QString, QString>> marks{
        {QStringLiteral("actionOpen"), QStringLiteral("files.open")},
        {QStringLiteral("actionQuickFileOpen"), QStringLiteral("files.open")},
        {QStringLiteral("actionOpenFolderasWorkspace"), QStringLiteral("workspace.open")},
        {QStringLiteral("actionOpenFolderAsWorkspaceNew"), QStringLiteral("workspace.open")},
        {QStringLiteral("actionOpenRemoteFolderSsh"), QStringLiteral("workspace.open")},
        {QStringLiteral("actionOpenRemoteTerminal"), QStringLiteral("terminal.open")},
        {QStringLiteral("actionReload"), QStringLiteral("files.reload")},
        {QStringLiteral("actionSaveAs"), QStringLiteral("files.save")},
        {QStringLiteral("actionSaveCopyAs"), QStringLiteral("files.saveCopy")},
        {QStringLiteral("actionExportHtml"), QStringLiteral("files.export")},
        {QStringLiteral("actionExportRtf"), QStringLiteral("files.export")},
        {QStringLiteral("actionRename"), QStringLiteral("files.rename")},
        {QStringLiteral("actionClose"), QStringLiteral("tabs.close")},
        {QStringLiteral("actionCloseAll"), QStringLiteral("tabs.close")},
        {QStringLiteral("actionCloseAllExceptActive"), QStringLiteral("tabs.close")},
        {QStringLiteral("actionCloseAllToLeft"), QStringLiteral("tabs.close")},
        {QStringLiteral("actionCloseAllToRight"), QStringLiteral("tabs.close")},
        {QStringLiteral("actionMoveToTrash"), QStringLiteral("files.trash")},
        {QStringLiteral("actionExit"), QStringLiteral("app.quit")},
        {QStringLiteral("actionFind"), QStringLiteral("editor.find")},
        {QStringLiteral("actionReplace"), QStringLiteral("editor.replaceAll")},
        {QStringLiteral("actionQuickFind"), QStringLiteral("editor.find")},
        {QStringLiteral("actionFindInFiles"), QStringLiteral("search.inFiles")},
        {QStringLiteral("actionGoToLine"), QStringLiteral("editor.select")},
        {QStringLiteral("actionPreferences"), QStringLiteral("settings.set")},
        {QStringLiteral("actionRunMacroMultipleTimes"), QStringLiteral("macro.run")},
        {QStringLiteral("actionSaveCurrentRecordedMacro"), QStringLiteral("macro.save")},
        {QStringLiteral("actionEditMacros"), QStringLiteral("macro.list")},
        {QStringLiteral("actionEditTasks"), QStringLiteral("tasks.set")},
        {QStringLiteral("actionPm2Processes"), QStringLiteral("pm2.list")},
        {QStringLiteral("actionScheduleTasks"), QStringLiteral("schedule.list")},
        {QStringLiteral("actionAiAgents"), QStringLiteral("ai.agents")},
        {QStringLiteral("actionEditMiniApps"), QStringLiteral("miniapps.list")},
        {QStringLiteral("actionQuickBrowser"), QStringLiteral("browser.open")},
        {QStringLiteral("actionPrint"), QStringLiteral("files.print")},
        {QStringLiteral("actionSearchAndBookmark"), QStringLiteral("editor.mark")},
        {QStringLiteral("actionColumnMode"), QStringLiteral("editor.columnInsert")},
        {QStringLiteral("actionConnectExternalAgent"), QStringLiteral("-")},
        {QStringLiteral("actionCheckForUpdates"), QStringLiteral("app.updates")},
        {QStringLiteral("actionAboutQt"), QStringLiteral("app.debugInfo")},
        {QStringLiteral("actionAboutNotepadNext"), QStringLiteral("app.debugInfo")},
        {QStringLiteral("actionDebugInfo"), QStringLiteral("app.debugInfo")},
        {QStringLiteral("actionThis_is_not_currently_implemented"), QStringLiteral("-")},
    };
    for (const auto &[id, command] : marks) {
        QAction *action = w->findChild<QAction *>(id);
        Q_ASSERT_X(action, "Registrar::menu", "a marked menu action was renamed");
        if (action)
            setCommandFor(action, command);
    }
    // Debug builds only (Help > Debug).
    if (QAction *sshLog = w->findChild<QAction *>(QStringLiteral("actionSshDebugLog")))
        setCommandFor(sshLog, QStringLiteral("ssh.debugLog"));

    add(QStringLiteral("menu.list"),
        QStringLiteral("List the menu bar's actions (for operations no other command covers)"), false,
        {optional(QStringLiteral("menu"), ArgType::String,
                  QStringLiteral("only actions under this top menu title, e.g. Edit"))},
        [w = w](const CommandContext &ctx) {
            const QString only = ctx.str(QStringLiteral("menu"));
            QJsonArray list;
            for (const MenuEntry &e : menuEntries(w)) {
                if (only.isEmpty() || (!e.path.isEmpty() && e.path.first().compare(only, Qt::CaseInsensitive) == 0))
                    list.append(menuJson(e));
            }
            ctx.ok({{QStringLiteral("actions"), list}});
        });

    add(QStringLiteral("menu.run"),
        QStringLiteral("Run a menu action by id, as clicking it would (actions that open a dialog are refused)"),
        true, {required(QStringLiteral("id"), ArgType::String, QStringLiteral("action id from menu.list"))},
        [w = w, feedback = api->feedback()](const CommandContext &ctx) {
            const QString id = ctx.str(QStringLiteral("id"));
            QAction *action = nullptr;
            QStringList path;
            for (const MenuEntry &e : menuEntries(w)) {
                if (e.action->objectName() == id) {
                    action = e.action;
                    path = e.path;
                    break;
                }
            }
            if (!action) {
                ctx.fail(404, QStringLiteral("no menu action '%1'; menu.list lists them").arg(id));
                return;
            }
            const QString command = commandFor(action);
            if (command == QLatin1String("-")) {
                ctx.fail(409, QStringLiteral("'%1' opens a dialog only the user can answer").arg(plainText(action->text())));
                return;
            }
            if (!command.isEmpty()) {
                ctx.fail(409, QStringLiteral("'%1' opens a dialog; use the command %2").arg(plainText(action->text()), command),
                         {{QStringLiteral("useCommand"), command}});
                return;
            }
            if (!action->isEnabled()) {
                ctx.fail(409, QStringLiteral("'%1' is disabled right now").arg(plainText(action->text())));
                return;
            }
            // A dialog the action opens anyway (a save prompt) is cancelled
            // at once: the agent cannot answer it, a command can.
            QWidget *const modalBefore = QApplication::activeModalWidget();
            auto dialog = std::make_shared<QString>();
            QTimer trap;
            trap.setInterval(5);
            QObject::connect(&trap, &QTimer::timeout, &trap, [modalBefore, dialog] {
                QWidget *modal = QApplication::activeModalWidget();
                if (modal && modal != modalBefore) {
                    *dialog = modal->windowTitle();
                    if (auto *d = qobject_cast<QDialog *>(modal))
                        d->reject();
                    else
                        modal->close();
                }
                if (QWidget *popup = QApplication::activePopupWidget()) {
                    *dialog = QStringLiteral("menu");
                    popup->close();
                }
            });
            trap.start();
            feedback->flashAction(action);
            action->trigger();
            trap.stop();
            if (!dialog->isEmpty()) {
                ctx.fail(409, QStringLiteral("'%1' opened a dialog (%2), which was cancelled; use a command for this")
                                  .arg(plainText(action->text()), *dialog));
                return;
            }
            QJsonObject result{{QStringLiteral("id"), id}, {QStringLiteral("menu"), path.join(QStringLiteral(" ▸ "))}};
            if (action->isCheckable())
                result.insert(QStringLiteral("checked"), action->isChecked());
            ctx.ok(result);
        });
}

} // namespace control::commands
