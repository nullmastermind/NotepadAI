/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// terminal.*, tasks.* (docs/adr/0007).

#include "Registrar.h"

#include "NotepadNextApplication.h"
#include "TerminalCwdResolver.h"
#include "TerminalManager.h"
#include "TerminalTaskRegistry.h"
#include "dialogs/MainWindow.h"
#include "docks/FolderAsWorkspaceDock.h"
#include "docks/TerminalDock.h"
#include "remote/ExecutionContext.h"
#include "remote/ExecutionContextRegistry.h"
#include "remote/RemoteExecutionContext.h"
#include "remote/SshProfile.h"
#include "widgets/TerminalWidget.h"

#include "DockWidget.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QKeySequence>

#include <algorithm>

namespace control::commands {

namespace {

QString taskStateName(TerminalDock::TaskState state)
{
    switch (state) {
    case TerminalDock::TaskState::Running:
        return QStringLiteral("running");
    case TerminalDock::TaskState::Succeeded:
        return QStringLiteral("succeeded");
    case TerminalDock::TaskState::Failed:
        return QStringLiteral("failed");
    default:
        return QString();
    }
}

QJsonObject terminalJson(MainWindow *w, TerminalDock *term)
{
    ads::CDockWidget *dock = term->hostDockWidget();
    QJsonObject o = dock ? Registrar::tabJson(w, dock) : QJsonObject();
    o.insert(QStringLiteral("cwd"), Registrar::displayPath(term->currentCwd()));
    if (term->isRemoteSession())
        o.insert(QStringLiteral("ssh"), true);
    TerminalWidget *t = term->terminalWidget();
    const bool running = t && t->isProcessRunning();
    o.insert(QStringLiteral("running"), running);
    if (running)
        o.insert(QStringLiteral("busy"), term->isBusy());
    if (t && !t->title().isEmpty())
        o.insert(QStringLiteral("shellTitle"), t->title());
    if (!term->customName().isEmpty())
        o.insert(QStringLiteral("name"), term->customName());
    if (term->isTask()) {
        o.insert(QStringLiteral("task"), term->taskCommand());
        if (const QString state = taskStateName(term->taskState()); !state.isEmpty())
            o.insert(QStringLiteral("taskState"), state);
        if (term->taskExitCode() >= 0)
            o.insert(QStringLiteral("exitCode"), term->taskExitCode());
    }
    return o;
}

// The execution context and resolved cwd for a new terminal: `cwd` when given
// (absolute, or relative to the workspace), else the workspace root.
bool spawnPlace(const CommandContext &ctx, MainWindow *w, remote::ExecutionContext **context, QString *cwd)
{
    FolderAsWorkspaceDock *dock = nullptr;
    if (ctx.has(QStringLiteral("workspace"))) {
        dock = Registrar::workspaceArg(ctx, w);
        if (!dock)
            return false;
    } else {
        dock = Registrar::activeWorkspace(w);
    }
    const QString root = dock ? dock->rootPath() : QString();
    *context = nullptr;
    if (remote::isSshUri(root)) {
        const remote::SshUri uri = remote::parseSshUri(root);
        remote::ExecutionContextRegistry *registry = Registrar::application(w)->getExecutionContextRegistry();
        *context = registry ? registry->remoteContext(uri.profileId) : nullptr;
        if (!*context) {
            ctx.fail(409, QStringLiteral("the SSH workspace %1 is not connected").arg(root));
            return false;
        }
        const QString base = TerminalCwdResolver::resolveWorkspaceForContext(*context, root);
        if (base.isEmpty()) {
            ctx.fail(409, QStringLiteral("the SSH workspace %1 is not connected").arg(root));
            return false;
        }
        const QString want = ctx.str(QStringLiteral("cwd"));
        *cwd = want.isEmpty() ? base
                              : (want.startsWith(QLatin1Char('/')) ? want : QDir::cleanPath(base + QLatin1Char('/') + want));
        return true;
    }
    const QString want = ctx.str(QStringLiteral("cwd"));
    if (want.isEmpty()) {
        *cwd = root.isEmpty() ? QDir::homePath() : TerminalCwdResolver::resolveWorkspace(root);
    } else {
        const QString p = QDir::fromNativeSeparators(want);
        *cwd = QDir::cleanPath(QDir::isRelativePath(p) && !root.isEmpty() ? root + QLatin1Char('/') + p : p);
    }
    if (!QFileInfo(*cwd).isDir()) {
        ctx.fail(404, QStringLiteral("no folder %1").arg(*cwd));
        return false;
    }
    return true;
}

void typeText(TerminalWidget *t, const QString &text)
{
    QInputMethodEvent ev;
    ev.setCommitString(text);
    QApplication::sendEvent(t, &ev);
}

} // namespace

void Registrar::terminal()
{
    const Arg tab = tabArg(QStringLiteral("terminal tab id; default: the terminal in front, else the newest"));

    add(QStringLiteral("terminal.list"), QStringLiteral("List the open terminals with their folder, task and state"),
        false, {}, [w = w](const CommandContext &ctx) {
            QJsonArray list;
            for (TerminalDock *term : w->terminalManager->terminalDocks())
                list.append(terminalJson(w, term));
            ctx.ok({{QStringLiteral("terminals"), list}});
        });

    add(QStringLiteral("terminal.open"),
        QStringLiteral("Open a terminal tab (over SSH in an SSH workspace), optionally typing a command into it"), true,
        {optional(QStringLiteral("cwd"), ArgType::String,
                  QStringLiteral("folder; relative to the workspace root; default the workspace root (else home)")),
         Registrar::workspaceArgSpec(),
         optional(QStringLiteral("command"), ArgType::String,
                  QStringLiteral("typed at the prompt and run (Enter); for a command whose exit code you want, use "
                                 "terminal.run"))},
        [w = w](const CommandContext &ctx) {
            remote::ExecutionContext *context = nullptr;
            QString cwd;
            if (!spawnPlace(ctx, w, &context, &cwd))
                return;
            TerminalDock *term = nullptr;
            QString error;
            const QString command = ctx.str(QStringLiteral("command"));
            if (!command.isEmpty())
                term = w->terminalManager->openTerminalWithInput(context, cwd, command + QLatin1Char('\r'));
            else if (context)
                term = w->terminalManager->openRemoteTerminal(context, cwd, QString());
            else
                term = w->terminalManager->openTerminal(cwd, &error);
            if (!term) {
                ctx.fail(422, error.isEmpty() ? QStringLiteral("the terminal could not start") : error);
                return;
            }
            ctx.reveal(term);
            ctx.ok(terminalJson(w, term));
        });

    add(QStringLiteral("terminal.run"),
        QStringLiteral("Run a command in a new task terminal that reports its exit code (exitCode / taskState in "
                       "terminal.state)"),
        true,
        {required(QStringLiteral("command"), ArgType::String, QStringLiteral("shell command line")),
         optional(QStringLiteral("name"), ArgType::String, QStringLiteral("tab name; default the command")),
         optional(QStringLiteral("cwd"), ArgType::String,
                  QStringLiteral("folder; relative to the workspace root; default the workspace root")),
         Registrar::workspaceArgSpec()},
        [w = w](const CommandContext &ctx) {
            remote::ExecutionContext *context = nullptr;
            QString cwd;
            if (!spawnPlace(ctx, w, &context, &cwd))
                return;
            TerminalTask task;
            task.command = ctx.str(QStringLiteral("command"));
            task.name = ctx.str(QStringLiteral("name"), task.command);
            TerminalDock *term = nullptr;
            QString error;
            if (context) {
                const QList<TerminalDock *> before = w->terminalManager->terminalDocks();
                w->terminalManager->openRemoteTask(context, cwd, task);
                const QList<TerminalDock *> after = w->terminalManager->terminalDocks();
                for (TerminalDock *t : after) {
                    if (!before.contains(t))
                        term = t;
                }
            } else {
                term = w->terminalManager->openTask(cwd, task, &error);
            }
            if (!term) {
                ctx.fail(422, error.isEmpty() ? QStringLiteral("the task could not start") : error);
                return;
            }
            ctx.reveal(term);
            ctx.ok(terminalJson(w, term));
        });

    add(QStringLiteral("terminal.state"),
        QStringLiteral("Read a terminal's folder, whether a command runs, and a task's state and exit code"), false, {tab},
        [w = w](const CommandContext &ctx) {
            if (TerminalDock *term = terminalArg(ctx, w))
                ctx.ok(terminalJson(w, term));
        });

    add(QStringLiteral("terminal.read"),
        QStringLiteral("Read a terminal's text: the screen, or the last lines including scrollback"), false,
        {tab,
         optional(QStringLiteral("lines"), ArgType::Int,
                  QStringLiteral("the last this many rows up to the last one with text (scrollback + screen); "
                                 "default: the visible screen")),
         optional(QStringLiteral("from"), ArgType::Int,
                  QStringLiteral("instead: rows from this absolute row (0 = oldest scrollback row)")),
         optional(QStringLiteral("count"), ArgType::Int, QStringLiteral("with `from`: how many rows; default 200"))},
        [w = w](const CommandContext &ctx) {
            TerminalDock *term = terminalArg(ctx, w);
            if (!term)
                return;
            TerminalWidget *t = term->terminalWidget();
            if (!t) {
                ctx.fail(409, QStringLiteral("the terminal is restarting"));
                return;
            }
            int total = 0;
            int history = 0;
            t->textRows(0, 0, &total, &history);
            qint64 from = history;
            qint64 count = total - history;
            if (ctx.has(QStringLiteral("from"))) {
                from = std::clamp<qint64>(ctx.integer(QStringLiteral("from")), 0, total);
                count = std::clamp<qint64>(ctx.integer(QStringLiteral("count"), 200), 0, 10000);
            } else if (ctx.has(QStringLiteral("lines"))) {
                // Count back from the last row with text (or the cursor's), not
                // from the blank bottom of a screen the output has not reached.
                const QStringList screen = t->textRows(history, total - history);
                qint64 end = history + t->cursorCell().y() + 1;
                for (qsizetype i = screen.size() - 1; i >= 0; --i) {
                    if (!screen.at(i).trimmed().isEmpty()) {
                        end = std::max<qint64>(end, history + i + 1);
                        break;
                    }
                }
                end = std::min<qint64>(end, total);
                count = std::clamp<qint64>(ctx.integer(QStringLiteral("lines")), 0, 10000);
                from = std::max<qint64>(0, end - count);
                count = end - from;
            }
            QStringList rows = t->textRows(int(from), int(count));
            // Blank rows below the prompt carry nothing.
            while (!rows.isEmpty() && rows.constLast().trimmed().isEmpty())
                rows.removeLast();
            QJsonObject o = terminalJson(w, term);
            o.insert(QStringLiteral("text"), rows.join(QLatin1Char('\n')));
            o.insert(QStringLiteral("from"), from);
            o.insert(QStringLiteral("totalRows"), total);
            o.insert(QStringLiteral("screenStart"), history);
            const QPoint cursor = t->cursorCell();
            o.insert(QStringLiteral("cursor"),
                     QJsonObject{{QStringLiteral("row"), history + cursor.y()}, {QStringLiteral("column"), cursor.x() + 1}});
            ctx.ok(o);
        });

    add(QStringLiteral("terminal.send"),
        QStringLiteral("Type text into a terminal as the keyboard would (\\n is Enter)"), true,
        {tab, required(QStringLiteral("text"), ArgType::String, QStringLiteral("text to type")),
         optional(QStringLiteral("enter"), ArgType::Bool, QStringLiteral("press Enter after it; default false")),
         optional(QStringLiteral("paste"), ArgType::Bool,
                  QStringLiteral("paste it instead (bracketed paste, control characters removed); for long text"))},
        [w = w](const CommandContext &ctx) {
            TerminalDock *term = terminalArg(ctx, w);
            if (!term)
                return;
            TerminalWidget *t = term->terminalWidget();
            if (!t || t->hasExited()) {
                ctx.fail(409, QStringLiteral("the terminal's process has exited; restart it with terminal.restart"));
                return;
            }
            ctx.reveal(term);
            t->setFocus(Qt::OtherFocusReason);
            QString text = ctx.str(QStringLiteral("text"));
            if (ctx.flag(QStringLiteral("paste"))) {
                t->pasteText(text);
                if (ctx.flag(QStringLiteral("enter")))
                    typeText(t, QStringLiteral("\r"));
            } else {
                text.replace(QLatin1String("\r\n"), QLatin1String("\r"));
                text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
                if (ctx.flag(QStringLiteral("enter")))
                    text += QLatin1Char('\r');
                typeText(t, text);
            }
            ctx.ok(terminalJson(w, term));
        });

    add(QStringLiteral("terminal.keys"),
        QStringLiteral("Press keys in a terminal: \"Ctrl+C\", \"Up\", \"Tab\", \"Enter\", \"Escape\", \"Ctrl+D\"..."), true,
        {tab, required(QStringLiteral("keys"), ArgType::Array,
                       QStringLiteral("key names in order, Qt style (\"Ctrl+C\", \"Shift+Tab\", \"PgUp\", \"F5\")"))},
        [w = w](const CommandContext &ctx) {
            TerminalDock *term = terminalArg(ctx, w);
            if (!term)
                return;
            TerminalWidget *t = term->terminalWidget();
            if (!t || t->hasExited()) {
                ctx.fail(409, QStringLiteral("the terminal's process has exited; restart it with terminal.restart"));
                return;
            }
            QList<QKeyCombination> combos;
            for (const QString &name : ctx.strings(QStringLiteral("keys"))) {
                const QKeySequence seq = QKeySequence::fromString(name, QKeySequence::PortableText);
                if (seq.isEmpty() || seq[0].key() == Qt::Key_unknown) {
                    ctx.fail(400, QStringLiteral("unknown key '%1'").arg(name));
                    return;
                }
                for (int i = 0; i < seq.count(); ++i)
                    combos.append(seq[i]);
            }
            ctx.reveal(term);
            t->setFocus(Qt::OtherFocusReason);
            for (const QKeyCombination combo : std::as_const(combos)) {
                const Qt::Key key = combo.key();
                const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
                QString text;
                if (key == Qt::Key_Return || key == Qt::Key_Enter)
                    text = QStringLiteral("\r");
                else if (key == Qt::Key_Tab)
                    text = QStringLiteral("\t");
                else if (key == Qt::Key_Escape)
                    text = QStringLiteral("\x1b");
                else if (key >= Qt::Key_Space && key <= Qt::Key_AsciiTilde && !(mods & ~Qt::ShiftModifier))
                    text = (mods & Qt::ShiftModifier) ? QChar(key) : QChar(key).toLower();
                else if ((mods & Qt::ControlModifier) && key >= Qt::Key_A && key <= Qt::Key_Z)
                    text = QChar(char(key - Qt::Key_A + 1));
                QKeyEvent press(QEvent::KeyPress, key, mods, text);
                QApplication::sendEvent(t, &press);
                QKeyEvent release(QEvent::KeyRelease, key, mods, text);
                QApplication::sendEvent(t, &release);
            }
            ctx.ok(terminalJson(w, term));
        });

    add(QStringLiteral("terminal.close"),
        QStringLiteral("Close a terminal; one running a command needs `force` (which stops it)"), true,
        {tab, optional(QStringLiteral("force"), ArgType::Bool, QStringLiteral("stop a running command and close"))},
        [w = w](const CommandContext &ctx) {
            TerminalDock *term = terminalArg(ctx, w);
            if (!term)
                return;
            const int id = tabIdOf(term->hostDockWidget());
            QString error;
            if (!w->terminalManager->closeTerminal(term, ctx.flag(QStringLiteral("force")), &error)) {
                ctx.fail(409, error + QStringLiteral(" Pass force: true to stop it."),
                         {{QStringLiteral("needs"), QStringLiteral("force")}});
                return;
            }
            ctx.ok({{QStringLiteral("closed"), id}});
        });

    add(QStringLiteral("terminal.restart"),
        QStringLiteral("Restart a terminal: stop what runs and start its shell (or task) again"), true,
        {tab, optional(QStringLiteral("force"), ArgType::Bool, QStringLiteral("stop a running command; needed when busy"))},
        [w = w](const CommandContext &ctx) {
            TerminalDock *term = terminalArg(ctx, w);
            if (!term)
                return;
            if (term->isBusy() && !term->isTask() && !ctx.flag(QStringLiteral("force"))) {
                ctx.fail(409, QStringLiteral("a command is running; pass force: true to stop it"),
                         {{QStringLiteral("needs"), QStringLiteral("force")}});
                return;
            }
            term->restartTask();
            ctx.reveal(term);
            ctx.ok(terminalJson(w, term));
        });

    add(QStringLiteral("terminal.clear"), QStringLiteral("Clear a terminal's scrollback and screen"), true, {tab},
        [w = w](const CommandContext &ctx) {
            TerminalDock *term = terminalArg(ctx, w);
            if (!term)
                return;
            term->clearTerminal();
            ctx.reveal(term);
            ctx.ok(terminalJson(w, term));
        });

    add(QStringLiteral("terminal.rename"), QStringLiteral("Name a terminal's tab (empty restores the automatic title)"), true,
        {tab, required(QStringLiteral("name"), ArgType::String, QStringLiteral("the tab name"))},
        [w = w](const CommandContext &ctx) {
            TerminalDock *term = terminalArg(ctx, w);
            if (!term)
                return;
            term->setCustomName(ctx.str(QStringLiteral("name")));
            ctx.reveal(term);
            ctx.ok(terminalJson(w, term));
        });

    // --- tasks.*: the workspace's saved commands (Terminal > Tasks) --------------
    auto tasksRoot = [](const CommandContext &ctx, MainWindow *w) -> QString {
        FolderAsWorkspaceDock *dock = workspaceArg(ctx, w);
        if (!dock)
            return QString();
        if (remote::isSshUri(dock->rootPath())) {
            ctx.fail(409, QStringLiteral("tasks are kept for local workspaces only"));
            return QString();
        }
        return TerminalTaskRegistry::normalizeWorkspacePath(dock->rootPath());
    };

    add(QStringLiteral("tasks.list"), QStringLiteral("List a workspace's saved tasks (Terminal > Tasks)"), false, {Registrar::workspaceArgSpec()},
        [w = w, tasksRoot](const CommandContext &ctx) {
            const QString root = tasksRoot(ctx, w);
            if (root.isEmpty())
                return;
            QJsonArray list;
            for (const TerminalTask &task : w->terminalManager->tasksForWorkspace(root)) {
                // Never the env: it may hold secrets.
                QJsonObject o{{QStringLiteral("name"), task.name}, {QStringLiteral("command"), task.command}};
                if (!task.cwd.isEmpty())
                    o.insert(QStringLiteral("cwd"), task.cwd);
                if (!task.env.trimmed().isEmpty())
                    o.insert(QStringLiteral("hasEnv"), true);
                list.append(o);
            }
            ctx.ok({{QStringLiteral("tasks"), list}});
        });

    add(QStringLiteral("tasks.run"), QStringLiteral("Run a saved task in its own terminal tab"), true,
        {required(QStringLiteral("name"), ArgType::String, QStringLiteral("task name from tasks.list")), Registrar::workspaceArgSpec()},
        [w = w, tasksRoot](const CommandContext &ctx) {
            const QString root = tasksRoot(ctx, w);
            if (root.isEmpty())
                return;
            const QString name = ctx.str(QStringLiteral("name"));
            const QList<TerminalTask> tasks = w->terminalManager->tasksForWorkspace(root);
            const auto it = std::find_if(tasks.cbegin(), tasks.cend(), [&](const TerminalTask &t) { return t.name == name; });
            if (it == tasks.cend()) {
                ctx.fail(404, QStringLiteral("no task '%1'; tasks.list lists them").arg(name));
                return;
            }
            const QString cwd = TerminalCwdResolver::resolveWorkspace(root);
            QString error;
            TerminalDock *term = w->terminalManager->openTask(cwd, *it, &error);
            if (!term) {
                ctx.fail(422, error.isEmpty() ? QStringLiteral("the task could not start") : error);
                return;
            }
            ctx.reveal(term);
            ctx.ok(terminalJson(w, term));
        });

    Arg tasksArg = required(QStringLiteral("tasks"), ArgType::Array,
                            QStringLiteral("[{name, command, cwd?, env?}] (env: KEY=VALUE lines); replaces the list"));
    tasksArg.secret = true; // env may hold secrets
    add(QStringLiteral("tasks.set"), QStringLiteral("Replace a workspace's saved tasks"), true, {tasksArg, Registrar::workspaceArgSpec()},
        [w = w, tasksRoot](const CommandContext &ctx) {
            const QString root = tasksRoot(ctx, w);
            if (root.isEmpty())
                return;
            QList<TerminalTask> tasks;
            const QJsonArray tasksJson = ctx.array(QStringLiteral("tasks"));
            for (const auto &v : tasksJson) {
                const QJsonObject o = v.toObject();
                TerminalTask task;
                task.name = o.value(QStringLiteral("name")).toString().trimmed();
                task.command = o.value(QStringLiteral("command")).toString();
                task.cwd = o.value(QStringLiteral("cwd")).toString();
                task.env = o.value(QStringLiteral("env")).toString();
                if (task.name.isEmpty() || task.command.trimmed().isEmpty()) {
                    ctx.fail(400, QStringLiteral("each task needs a name and a command"));
                    return;
                }
                tasks.append(task);
            }
            w->terminalManager->setTasks(root, tasks);
            ctx.ok({{QStringLiteral("count"), int(tasks.size())}});
        });
}

} // namespace control::commands
