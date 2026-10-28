/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// pm2.* (docs/adr/0007). Every command runs through the PM2 Processes window
// (pm2's helper lives exactly as long as it is open, ADR 0005), selecting the
// rows it acts on, so the user sees each change in the table.

#include "Registrar.h"

#include "dialogs/MainWindow.h"
#include "docks/TerminalDock.h"
#include "pm2/Pm2ProcessesDialog.h"
#include "remote/SshProfile.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <functional>

namespace control::commands {

namespace {

QString statusName(const Pm2ProcessInfo &p)
{
    switch (p.status) {
    case Pm2Status::Online:
        return QStringLiteral("online");
    case Pm2Status::Launching:
        return QStringLiteral("launching");
    case Pm2Status::Stopping:
        return QStringLiteral("stopping");
    case Pm2Status::Stopped:
        return QStringLiteral("stopped");
    case Pm2Status::Errored:
        return QStringLiteral("errored");
    default:
        return p.statusText;
    }
}

QJsonObject processJson(Pm2ProcessesDialog *dlg, const Pm2ProcessInfo &p)
{
    QJsonObject o{{QStringLiteral("id"), p.pmId},
                  {QStringLiteral("name"), p.name},
                  {QStringLiteral("status"), statusName(p)},
                  {QStringLiteral("restarts"), p.restarts},
                  {QStringLiteral("cwd"), QDir::fromNativeSeparators(p.cwd)}};
    if (pm2IsRunning(p.status)) {
        o.insert(QStringLiteral("pid"), p.pid);
        o.insert(QStringLiteral("cpu"), p.cpuPercent);
        o.insert(QStringLiteral("memoryMB"), double(p.memoryBytes) / (1024.0 * 1024.0));
        if (p.startedAtMs > 0)
            o.insert(QStringLiteral("uptimeSec"), (QDateTime::currentMSecsSinceEpoch() - p.startedAtMs) / 1000);
    }
    // Processes NotepadAI created carry their definition (never the env: it may hold secrets).
    if (const std::optional<Pm2Definition> def = dlg->definitionOf(p.pmId)) {
        o.insert(QStringLiteral("command"), def->command);
        if (!def->shell.isEmpty())
            o.insert(QStringLiteral("shell"), def->shell);
        if (!def->envText.trimmed().isEmpty())
            o.insert(QStringLiteral("hasEnv"), true);
        o.insert(QStringLiteral("autorestart"), def->options.autorestart);
        o.insert(QStringLiteral("watch"), def->options.watch);
        if (!def->options.maxMemoryRestart.isEmpty())
            o.insert(QStringLiteral("maxMemory"), def->options.maxMemoryRestart);
        if (def->options.restartDelayMs > 0)
            o.insert(QStringLiteral("restartDelayMs"), def->options.restartDelayMs);
        o.insert(QStringLiteral("editable"), true);
    } else {
        o.insert(QStringLiteral("script"), QDir::fromNativeSeparators(p.script));
    }
    return o;
}

QJsonObject listJson(Pm2ProcessesDialog *dlg)
{
    QJsonArray list;
    for (const Pm2ProcessInfo &p : dlg->processes())
        list.append(processJson(dlg, p));
    QJsonObject o{{QStringLiteral("processes"), list}};
    if (!dlg->savedNotRunning().isEmpty())
        o.insert(QStringLiteral("savedNotRunning"), QJsonArray::fromStringList(dlg->savedNotRunning()));
    return o;
}

// Opens the PM2 Processes window and runs `fn` once pm2 answers (the first
// time, pm2 may be fetched through npx: that can take a while).
void withPm2(const CommandContext &ctx, MainWindow *w, const std::function<void(Pm2ProcessesDialog *)> &fn)
{
    if (remote::isSshUri(Registrar::workspaceRoot(w))) {
        ctx.fail(409, QStringLiteral("PM2 is not available in SSH workspaces (its processes are on this machine); "
                                     "activate a local workspace first"));
        return;
    }
    Pm2ProcessesDialog *dlg = Registrar::openPm2Window(w);
    if (!dlg) {
        ctx.fail(422, QStringLiteral("the PM2 window did not open"));
        return;
    }
    ctx.reveal(dlg);
    if (dlg->isReady()) {
        fn(dlg);
        return;
    }
    if (dlg->hasFailed()) {
        ctx.fail(422, dlg->statusText());
        return;
    }
    auto *wait = new QObject(dlg);
    QObject::connect(dlg, &Pm2ProcessesDialog::statusChanged, wait, [ctx, dlg, wait, fn] {
        if (dlg->isReady()) {
            wait->deleteLater();
            fn(dlg);
        } else if (dlg->hasFailed()) {
            wait->deleteLater();
            ctx.fail(422, dlg->statusText());
        }
    });
    QTimer::singleShot(180000, wait, [ctx, wait] {
        wait->deleteLater();
        ctx.fail(504, QStringLiteral("pm2 did not answer within 3 minutes"));
    });
}

// The processes `name` / `id` / `names` name; fails the command when one is unknown.
bool processIds(const CommandContext &ctx, Pm2ProcessesDialog *dlg, QList<int> *ids, bool allowAll = false)
{
    QStringList names = ctx.strings(QStringLiteral("names"));
    if (ctx.has(QStringLiteral("name")))
        names.append(ctx.str(QStringLiteral("name")));
    const QVector<Pm2ProcessInfo> &all = dlg->processes();
    if (ctx.has(QStringLiteral("id"))) {
        const int id = int(ctx.integer(QStringLiteral("id")));
        if (std::none_of(all.cbegin(), all.cend(), [id](const Pm2ProcessInfo &p) { return p.pmId == id; })) {
            ctx.fail(404, QStringLiteral("pm2 has no process %1; pm2.list lists them").arg(id));
            return false;
        }
        ids->append(id);
    }
    for (const QString &name : std::as_const(names)) {
        const auto it = std::find_if(all.cbegin(), all.cend(), [&](const Pm2ProcessInfo &p) { return p.name == name; });
        if (it == all.cend()) {
            ctx.fail(404, QStringLiteral("pm2 has no process '%1'; pm2.list lists them").arg(name));
            return false;
        }
        ids->append(it->pmId);
    }
    if (ids->isEmpty() && allowAll && ctx.flag(QStringLiteral("all"))) {
        for (const Pm2ProcessInfo &p : all)
            ids->append(p.pmId);
    }
    if (ids->isEmpty()) {
        ctx.fail(400, allowAll ? QStringLiteral("name the processes: name, id, names, or all: true")
                               : QStringLiteral("name the process: name or id"));
        return false;
    }
    return true;
}

QList<Arg> targetArgs(bool many)
{
    QList<Arg> args{Registrar::optional(QStringLiteral("name"), ArgType::String, QStringLiteral("process name")),
                    Registrar::optional(QStringLiteral("id"), ArgType::Int, QStringLiteral("pm2 id"))};
    if (many) {
        args.append(Registrar::optional(QStringLiteral("names"), ArgType::Array, QStringLiteral("several process names")));
        args.append(Registrar::optional(QStringLiteral("all"), ArgType::Bool, QStringLiteral("every process")));
    }
    return args;
}

// Answers with the list once pm2 has applied the command (the next list it pushes).
Pm2ProcessesDialog::Done answerWithList(const CommandContext &ctx, Pm2ProcessesDialog *dlg)
{
    return [ctx, dlg = QPointer<Pm2ProcessesDialog>(dlg)](bool ok, const QString &error) {
        if (!ok) {
            ctx.fail(422, error.isEmpty() ? QStringLiteral("pm2 refused") : error);
            return;
        }
        if (!dlg) {
            ctx.ok();
            return;
        }
        auto *wait = new QObject(dlg);
        auto answer = [ctx, dlg, wait] {
            wait->deleteLater();
            ctx.ok(dlg ? listJson(dlg) : QJsonObject());
        };
        QObject::connect(dlg, &Pm2ProcessesDialog::statusChanged, wait, answer, Qt::SingleShotConnection);
        QTimer::singleShot(3000, wait, answer);
    };
}

QList<Arg> definitionArgs(bool creating)
{
    QList<Arg> args;
    if (creating) {
        args.append(Registrar::required(QStringLiteral("name"), ArgType::String, QStringLiteral("process name (unique in pm2)")));
        args.append(Registrar::required(QStringLiteral("command"), ArgType::String, QStringLiteral("command line to run")));
    } else {
        args.append(Registrar::optional(QStringLiteral("rename"), ArgType::String, QStringLiteral("new process name")));
        args.append(Registrar::optional(QStringLiteral("command"), ArgType::String, QStringLiteral("new command line")));
    }
    args.append(Registrar::optional(QStringLiteral("cwd"), ArgType::String,
                                    creating ? QStringLiteral("working directory; default the active workspace (else home)")
                                             : QStringLiteral("working directory")));
    args.append(Registrar::oneOf(QStringLiteral("shell"),
                                 {QStringLiteral("system"), QStringLiteral("cmd"), QStringLiteral("powershell"), QStringLiteral("pwsh"),
                                  QStringLiteral("bash"), QStringLiteral("sh")},
                                 QStringLiteral("shell that runs the command; default system (cmd.exe / /bin/sh)")));
    Arg env = Registrar::optional(QStringLiteral("env"), ArgType::String, QStringLiteral("KEY=value lines"));
    env.secret = true;
    args.append(env);
    args.append(Registrar::optional(QStringLiteral("autorestart"), ArgType::Bool,
                                    QStringLiteral("restart when it exits; default true")));
    args.append(Registrar::optional(QStringLiteral("watch"), ArgType::Bool,
                                    QStringLiteral("restart when files in its folder change; default false")));
    args.append(Registrar::optional(QStringLiteral("maxMemory"), ArgType::String,
                                    QStringLiteral("restart above this memory, e.g. \"512M\"; \"\" = no limit")));
    args.append(Registrar::optional(QStringLiteral("restartDelayMs"), ArgType::Int,
                                    QStringLiteral("wait this long before a restart; default 0")));
    return args;
}

void applyDefinitionArgs(const CommandContext &ctx, Pm2Definition *def)
{
    if (ctx.has(QStringLiteral("command")))
        def->command = ctx.str(QStringLiteral("command")).trimmed();
    if (ctx.has(QStringLiteral("cwd")))
        def->cwd = QDir::toNativeSeparators(QDir::cleanPath(ctx.str(QStringLiteral("cwd")).trimmed()));
    if (ctx.has(QStringLiteral("shell")))
        def->shell = ctx.str(QStringLiteral("shell")) == QLatin1String("system") ? QString() : ctx.str(QStringLiteral("shell"));
    if (ctx.has(QStringLiteral("env")))
        def->envText = ctx.str(QStringLiteral("env"));
    if (ctx.has(QStringLiteral("autorestart")))
        def->options.autorestart = ctx.flag(QStringLiteral("autorestart"));
    if (ctx.has(QStringLiteral("watch")))
        def->options.watch = ctx.flag(QStringLiteral("watch"));
    if (ctx.has(QStringLiteral("maxMemory")))
        def->options.maxMemoryRestart = ctx.str(QStringLiteral("maxMemory")).trimmed().toUpper();
    if (ctx.has(QStringLiteral("restartDelayMs")))
        def->options.restartDelayMs = int(ctx.integer(QStringLiteral("restartDelayMs")));
}

} // namespace

void Registrar::pm2()
{
    add(QStringLiteral("pm2.list"),
        QStringLiteral("List pm2's processes (opens the PM2 Processes window: pm2 is reached only while it is open)"),
        false, {}, [w = w](const CommandContext &ctx) {
            withPm2(ctx, w, [ctx](Pm2ProcessesDialog *dlg) { ctx.ok(listJson(dlg)); });
        });

    struct Verb
    {
        const char *name;
        const char *summary;
        void (Pm2ProcessesDialog::*run)(const QList<int> &, Pm2ProcessesDialog::Done);
    };
    const Verb verbs[] = {
        {"pm2.start", "Start stopped pm2 processes", &Pm2ProcessesDialog::startIds},
        {"pm2.stop", "Stop pm2 processes", &Pm2ProcessesDialog::stopIds},
        {"pm2.restart", "Restart pm2 processes", &Pm2ProcessesDialog::restartIds},
        {"pm2.delete", "Stop pm2 processes and remove them from pm2 (and their NotepadAI entrypoints)",
         &Pm2ProcessesDialog::deleteIds},
    };
    for (const Verb &verb : verbs) {
        add(QString::fromLatin1(verb.name), QString::fromLatin1(verb.summary), true, targetArgs(true),
            [w = w, run = verb.run](const CommandContext &ctx) {
                withPm2(ctx, w, [ctx, run](Pm2ProcessesDialog *dlg) {
                    QList<int> ids;
                    if (!processIds(ctx, dlg, &ids, true))
                        return;
                    dlg->selectIds(ids);
                    (dlg->*run)(ids, answerWithList(ctx, dlg));
                });
            });
    }

    add(QStringLiteral("pm2.create"),
        QStringLiteral("Create and start a pm2 process that runs a command (like the window's New button)"), true,
        definitionArgs(true), [w = w](const CommandContext &ctx) {
            withPm2(ctx, w, [ctx, w](Pm2ProcessesDialog *dlg) {
                Pm2Definition def;
                def.name = ctx.str(QStringLiteral("name")).trimmed();
                const QString root = w->currentWorkspaceRoot();
                def.cwd = QDir::toNativeSeparators(root.isEmpty() || remote::isSshUri(root) ? QDir::homePath() : root);
                applyDefinitionArgs(ctx, &def);
                if (const QString problem = dlg->create(def, answerWithList(ctx, dlg)); !problem.isEmpty())
                    ctx.fail(400, problem);
            });
        });

    QList<Arg> editArgs = targetArgs(false);
    editArgs.append(definitionArgs(false));
    add(QStringLiteral("pm2.edit"),
        QStringLiteral("Change a process NotepadAI created (unnamed fields keep their value); name, folder or pm2 "
                       "options recreate it"),
        true, editArgs, [w = w](const CommandContext &ctx) {
            withPm2(ctx, w, [ctx](Pm2ProcessesDialog *dlg) {
                QList<int> ids;
                if (!processIds(ctx, dlg, &ids))
                    return;
                std::optional<Pm2Definition> def = dlg->definitionOf(ids.front());
                if (!def) {
                    ctx.fail(409, QStringLiteral("that process was created outside NotepadAI; only its own can be edited"));
                    return;
                }
                if (ctx.has(QStringLiteral("rename")))
                    def->name = ctx.str(QStringLiteral("rename")).trimmed();
                applyDefinitionArgs(ctx, &*def);
                dlg->selectIds(ids);
                if (const QString problem = dlg->edit(ids.front(), *def, answerWithList(ctx, dlg)); !problem.isEmpty())
                    ctx.fail(400, problem);
            });
        });

    add(QStringLiteral("pm2.logs"), QStringLiteral("Follow a process's logs in a terminal tab (`pm2 logs`)"), true,
        targetArgs(false), [w = w](const CommandContext &ctx) {
            withPm2(ctx, w, [ctx, w](Pm2ProcessesDialog *dlg) {
                QList<int> ids;
                if (!processIds(ctx, dlg, &ids))
                    return;
                dlg->selectIds(ids);
                TerminalDock *term = dlg->showLogsFor(ids.front());
                if (!term) {
                    ctx.fail(422, QStringLiteral("could not open the logs (pm2's install folder is unknown)"));
                    return;
                }
                ctx.reveal(term);
                ads::CDockWidget *dock = term->hostDockWidget();
                ctx.ok(dock ? tabJson(w, dock) : QJsonObject());
            });
        });

    add(QStringLiteral("pm2.resurrect"), QStringLiteral("Start the processes pm2 saved that are not running"), true, {},
        [w = w](const CommandContext &ctx) {
            withPm2(ctx, w, [ctx](Pm2ProcessesDialog *dlg) { dlg->resurrectSaved(answerWithList(ctx, dlg)); });
        });

    add(QStringLiteral("pm2.discardSaved"),
        QStringLiteral("Forget saved processes that are not running (pm2's saved list becomes what runs now)"), true, {},
        [w = w](const CommandContext &ctx) {
            withPm2(ctx, w, [ctx](Pm2ProcessesDialog *dlg) { dlg->discardSaved(answerWithList(ctx, dlg)); });
        });

    add(QStringLiteral("pm2.close"), QStringLiteral("Close the PM2 Processes window (processes keep running)"), true, {},
        [w = w](const CommandContext &ctx) {
            if (Pm2ProcessesDialog *dlg = w->m_pm2Dialog)
                dlg->close();
            ctx.ok();
        });
}

} // namespace control::commands
