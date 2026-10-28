/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_COMMANDS_REGISTRAR_H
#define CONTROL_COMMANDS_REGISTRAR_H

#include "control/ControlCommands.h"

#include <QJsonObject>
#include <QPointer>
#include <QString>

class FolderAsWorkspaceDock;
class MainWindow;
class ScintillaNext;
class TerminalDock;
class AiAgentDock;
class AcpSessionView;
class NotepadNextApplication;
class Pm2ProcessesDialog;
namespace ads { class CDockWidget; }

namespace control {
class ControlApi;
}

namespace control::commands {

// Registers every command of the Control API (docs/adr/0007) against the
// main window. A friend of MainWindow: commands call its managers and
// helpers directly, the same code its menus and buttons run. One member per
// domain, each in its own file (AppCommands.cpp, EditorCommands.cpp, ...).
struct Registrar
{
    Registrar(MainWindow *window, ControlApi *api);
    void registerAll();

    // GET /instance and app.state.
    static QJsonObject appState(MainWindow *w);

    // --- Domains ------------------------------------------------------------
    void app();        // app.*, view.*, panels.*
    void menu();       // menu.*
    void settings();   // settings.*
    void scripting();  // lua.*, macro.*
    void workspace();  // workspace.*, ssh.*
    void tabs();       // tabs.*
    void files();      // files.*
    void editor();     // editor.*
    void search();     // search.*
    void preview();    // preview.*, browser.*, miniapps.*
    void terminal();   // terminal.*, tasks.*
    void pm2();        // pm2.*
    void git();        // git.*
    void ai();         // ai.*
    void goal();       // goal.*
    void schedule();   // schedule.*
    void transfer();   // ssh.* profiles and connection, uploads/downloads, zip, croc
    void csv();        // csv.* (CSV preview grid), hex.*

    void add(const QString &name, const QString &summary, bool changes, QList<Arg> args, CommandHandler handler);

    // --- MainWindow access for helpers outside the class (not friends) --------
    static NotepadNextApplication *application(MainWindow *w);
    static QString workspaceRoot(MainWindow *w);
    static FolderAsWorkspaceDock *activeWorkspace(MainWindow *w);
    // Opens (or raises) Terminal > PM2 Processes.
    static Pm2ProcessesDialog *openPm2Window(MainWindow *w);

    // --- Shared helpers -------------------------------------------------------
    // Tab ids: one id space for every tab (docs/adr/0007), kept on the dock.
    static int tabId(ads::CDockWidget *dock);
    static ads::CDockWidget *tabById(MainWindow *w, int id);
    // The tab holding `widget`, if any.
    static ads::CDockWidget *tabOf(const QWidget *widget);
    // "editor", "diff", "terminal", "browser", "preview".
    static QString tabKind(ads::CDockWidget *dock);
    static QJsonObject tabJson(MainWindow *w, ads::CDockWidget *dock);

    // Resolve an argument or fail the command (and return null). `tab` unset:
    // the current editor / the most recent terminal / the active workspace.
    static ScintillaNext *editorArg(const CommandContext &ctx, MainWindow *w, const QString &arg = QStringLiteral("tab"));
    static TerminalDock *terminalArg(const CommandContext &ctx, MainWindow *w, const QString &arg = QStringLiteral("tab"));
    static FolderAsWorkspaceDock *workspaceArg(const CommandContext &ctx, MainWindow *w,
                                               const QString &arg = QStringLiteral("workspace"));
    static FolderAsWorkspaceDock *workspaceFor(MainWindow *w, const QString &root);
    static QJsonObject workspaceJson(MainWindow *w, FolderAsWorkspaceDock *dock);
    // A path argument as the app stores it: forward slashes, cleaned, absolute
    // (relative paths resolve against the active workspace). ssh:// untouched.
    static QString pathArg(MainWindow *w, const QString &path);
    static QString displayPath(const QString &path);

    static Arg required(const QString &name, ArgType type, const QString &description);
    static Arg optional(const QString &name, ArgType type, const QString &description);
    static Arg oneOf(const QString &name, const QStringList &values, const QString &description, bool required = false);
    // The common `tab` argument (an editor's, a terminal's, any tab's).
    static Arg tabArg(const QString &what);
    static Arg workspaceArgSpec();

    MainWindow *w;
    ControlApi *api;
};

} // namespace control::commands

#endif // CONTROL_COMMANDS_REGISTRAR_H
