/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// ai.*, goal.*, schedule.* (docs/adr/0007). Chat commands go through the
// session's composer and its controls (Send, Cancel, the pickers, the
// permission buttons, the question form), so the chat shows every step.

#include "Registrar.h"

#include "AcpAgentBrand.h"
#include "AcpAgentManager.h"
#include "AcpAgentRegistry.h"
#include "AcpConnection.h"
#include "AcpConversationCopy.h"
#include "AcpSessionModel.h"
#include "AcpSwitchAgent.h"
#include "ApplicationSettings.h"
#include "GoalAgent.h"
#include "GoalAgentSettings.h"
#include "GoalDraftGenerator.h"
#include "GoalHttpJudge.h"
#include "GrillMeForm.h"
#include "NotepadNextApplication.h"
#include "ProjectGoalPresets.h"
#include "ScheduledTaskDefinition.h"
#include "ScheduledTaskRegistry.h"
#include "ScheduledTaskRunner.h"
#include "TerminalCwdResolver.h"
#include "ai/CredentialStore.h"
#include "dialogs/MainWindow.h"
#include "dialogs/SendWithGoalDialog.h"
#include "docks/AiAgentDock.h"
#include "docks/FolderAsWorkspaceDock.h"
#include "remote/ExecutionContextRegistry.h"
#include "remote/RemoteExecutionContext.h"
#include "remote/SshProfile.h"
#include "widgets/AcpSessionView.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <limits>
#include <memory>

namespace control::commands {

namespace {

constexpr int kMaxTextChars = 20000;

QString capped(const QString &text, bool *cut)
{
    *cut = text.size() > kMaxTextChars;
    return *cut ? text.left(kMaxTextChars) : text;
}

QList<AiAgentDock *> aiDocks(MainWindow *w)
{
    return w->findChildren<AiAgentDock *>();
}

AiAgentDock *dockOfSession(MainWindow *w, const QString &sessionId)
{
    for (AiAgentDock *dock : aiDocks(w)) {
        if (dock->viewFor(sessionId))
            return dock;
    }
    return nullptr;
}

QString goalStatusName(GoalAgent::Status s)
{
    switch (s) {
    case GoalAgent::Active:
        return QStringLiteral("active");
    case GoalAgent::Achieved:
        return QStringLiteral("achieved");
    case GoalAgent::Cancelled:
        return QStringLiteral("cancelled");
    case GoalAgent::Failed:
        return QStringLiteral("failed");
    default:
        return QStringLiteral("idle");
    }
}

QJsonArray choicesJson(const AcpSessionView *view, AcpSessionView::Selector selector, QString *current)
{
    QJsonArray list;
    for (const AcpSessionView::Choice &c : view->choices(selector)) {
        list.append(QJsonObject{{QStringLiteral("id"), c.id}, {QStringLiteral("name"), c.name}});
        if (c.current)
            *current = c.id;
    }
    return list;
}

QString questionTypeName(GrillMeForm::QuestionType t)
{
    using T = GrillMeForm::QuestionType;
    switch (t) {
    case T::Multi:
        return QStringLiteral("multi");
    case T::Select:
        return QStringLiteral("select");
    case T::Proposal:
        return QStringLiteral("proposal");
    case T::Confirm:
        return QStringLiteral("confirm");
    case T::Text:
        return QStringLiteral("text");
    case T::Textarea:
        return QStringLiteral("textarea");
    case T::Number:
        return QStringLiteral("number");
    default:
        return QStringLiteral("single");
    }
}

QJsonObject formJson(const GrillMeForm::Spec &spec)
{
    QJsonArray questions;
    for (const GrillMeForm::Question &q : spec.questions) {
        QJsonObject o{{QStringLiteral("id"), q.id},
                      {QStringLiteral("title"), q.title},
                      {QStringLiteral("type"), questionTypeName(q.type)}};
        if (!q.body.isEmpty())
            o.insert(QStringLiteral("body"), q.body);
        if (!q.options.isEmpty()) {
            QJsonArray options;
            for (const GrillMeForm::Option &opt : q.options) {
                QJsonObject oo{{QStringLiteral("value"), opt.value}, {QStringLiteral("label"), opt.label}};
                if (!opt.description.isEmpty())
                    oo.insert(QStringLiteral("description"), opt.description);
                options.append(oo);
            }
            o.insert(QStringLiteral("options"), options);
        }
        if (!q.recommended.isUndefined() && !q.recommended.isNull())
            o.insert(QStringLiteral("recommended"), q.recommended);
        if (!q.recommendation.isEmpty())
            o.insert(QStringLiteral("recommendation"), q.recommendation);
        if (q.required)
            o.insert(QStringLiteral("required"), true);
        if (q.allowOther)
            o.insert(QStringLiteral("allowOther"), true);
        questions.append(o);
    }
    QJsonObject o{{QStringLiteral("questions"), questions}};
    if (!spec.title.isEmpty())
        o.insert(QStringLiteral("title"), spec.title);
    return o;
}

QJsonObject sessionJson(AiAgentDock *dock, const QString &sessionId, bool detail)
{
    AcpSessionView *view = dock->viewFor(sessionId);
    // A prompt still queued (behind a turn, or until the agent connects) is
    // work to come: "working" stays true until it has been answered, so a
    // wait for working == false right after ai.open / ai.send cannot match early.
    const int queued = view ? view->queuedPromptCount() + (view->hasUnsentPrompt() ? 1 : 0) : 0;
    const bool busy = dock->isSessionBusy(sessionId);
    const bool working = busy || queued > 0;
    const bool waiting = dock->isSessionWaiting(sessionId);
    QJsonObject o{{QStringLiteral("session"), sessionId},
                  {QStringLiteral("agent"), dock->agentNameFor(sessionId)},
                  {QStringLiteral("cwd"), Registrar::displayPath(dock->workingDirectory())},
                  {QStringLiteral("current"), dock->sessionId() == sessionId && dock->isVisible()},
                  {QStringLiteral("working"), working},
                  {QStringLiteral("background"), dock->isSessionWorking(sessionId) && !busy},
                  {QStringLiteral("waitingForUser"), waiting},
                  // What a /wait after a prompt waits for: the turn ended, or
                  // it needs an answer (a permission asked mid-turn included).
                  {QStringLiteral("settled"), !working || waiting}};
    if (!view)
        return o;
    if (queued > 0)
        o.insert(QStringLiteral("queued"), queued);
    if (dock->hasAgentExited(sessionId))
        o.insert(QStringLiteral("agentExited"), true);
    if (const AcpSessionModel *m = view->sessionModel())
        o.insert(QStringLiteral("entries"), int(m->timeline().size()));
    if (GoalAgent *goal = dock->goalFor(sessionId))
        o.insert(QStringLiteral("goal"), goalStatusName(goal->status()));
    if (!detail)
        return o;
    QString current;
    const QJsonArray models = choicesJson(view, AcpSessionView::Selector::Model, &current);
    if (!models.isEmpty()) {
        o.insert(QStringLiteral("models"), models);
        o.insert(QStringLiteral("model"), current);
    }
    current.clear();
    const QJsonArray modes = choicesJson(view, AcpSessionView::Selector::Mode, &current);
    if (!modes.isEmpty()) {
        o.insert(QStringLiteral("modes"), modes);
        o.insert(QStringLiteral("mode"), current);
    }
    current.clear();
    const QJsonArray efforts = choicesJson(view, AcpSessionView::Selector::Effort, &current);
    if (!efforts.isEmpty()) {
        o.insert(QStringLiteral("efforts"), efforts);
        o.insert(QStringLiteral("effort"), current);
    }
    o.insert(QStringLiteral("autoApprove"), view->autoApprove());
    if (const AcpProtocol::AcpPermissionRequest *req = view->pendingPermission()) {
        QJsonArray options;
        for (const AcpProtocol::AcpPermissionOption &opt : req->options)
            options.append(QJsonObject{{QStringLiteral("id"), opt.id},
                                       {QStringLiteral("label"), opt.label},
                                       {QStringLiteral("kind"), opt.kind}});
        o.insert(QStringLiteral("permission"), QJsonObject{{QStringLiteral("title"), req->title},
                                                           {QStringLiteral("description"), req->description},
                                                           {QStringLiteral("options"), options}});
    }
    if (const GrillMeForm::Spec *form = view->pendingFormSpec())
        o.insert(QStringLiteral("form"), formJson(*form));
    o.insert(QStringLiteral("draft"), view->peekInputText());
    o.insert(QStringLiteral("newWorktree"), view->newWorktree());
    if (const QString prewalk = view->prewalkTarget(); !prewalk.isEmpty())
        o.insert(QStringLiteral("prewalk"), prewalk);
    if (const QStringList queue = view->queuedPromptTexts(); !queue.isEmpty())
        o.insert(QStringLiteral("queue"), QJsonArray::fromStringList(queue));
    if (const AcpSessionModel *m = view->sessionModel(); m && !m->availableCommands().isEmpty()) {
        QJsonArray commands;
        for (const AcpProtocol::AcpCommandInfo &c : m->availableCommands()) {
            QJsonObject co{{QStringLiteral("name"), c.name}};
            if (!c.description.isEmpty())
                co.insert(QStringLiteral("description"), c.description);
            if (!c.inputHint.isEmpty())
                co.insert(QStringLiteral("input"), c.inputHint);
            commands.append(co);
        }
        o.insert(QStringLiteral("commands"), commands);
    }
    if (const AcpConnection *conn = dock->connectionFor(sessionId)) {
        QJsonArray tasks;
        for (const AcpProtocol::AcpBackgroundTask &t : conn->backgroundTasks())
            tasks.append(QJsonObject{{QStringLiteral("id"), t.taskId},
                                     {QStringLiteral("name"), t.name},
                                     {QStringLiteral("canStop"), t.canStop}});
        if (!tasks.isEmpty())
            o.insert(QStringLiteral("tasks"), tasks);
        QJsonArray jobs;
        for (const AcpProtocol::AcpScheduledJob &j : conn->scheduledJobs()) {
            using K = AcpProtocol::AcpScheduledJob::Kind;
            QJsonObject jo{{QStringLiteral("id"), j.id},
                           {QStringLiteral("kind"), j.kind == K::Cron      ? QStringLiteral("cron")
                                                    : j.kind == K::Monitor ? QStringLiteral("monitor")
                                                                           : QStringLiteral("wake")}};
            if (!j.prompt.isEmpty())
                jo.insert(QStringLiteral("prompt"), j.prompt);
            if (!j.command.isEmpty())
                jo.insert(QStringLiteral("command"), j.command);
            if (!j.cron.isEmpty())
                jo.insert(QStringLiteral("cron"), j.cron);
            if (j.fireAtMs > 0)
                jo.insert(QStringLiteral("at"), QDateTime::fromMSecsSinceEpoch(j.fireAtMs).toString(Qt::ISODate));
            jobs.append(jo);
        }
        if (!jobs.isEmpty())
            o.insert(QStringLiteral("jobs"), jobs);
    }
    return o;
}

// The questions answered: [{id, value, other?, note?, clarify?}].
QVector<GrillMeForm::Answer> answersArg(const QJsonArray &array)
{
    QVector<GrillMeForm::Answer> answers;
    for (const auto &v : array) {
        const QJsonObject o = v.toObject();
        GrillMeForm::Answer a;
        a.questionId = o.value(QStringLiteral("id")).toString();
        a.value = o.value(QStringLiteral("value"));
        a.other = o.value(QStringLiteral("other")).toString();
        a.note = o.value(QStringLiteral("note")).toString();
        a.clarify = o.value(QStringLiteral("clarify")).toBool();
        answers.append(a);
    }
    return answers;
}

GoalAgentSettings goalSettings(ApplicationSettings *settings)
{
    const QString json = settings->get("Ai/GoalAgentSettings", QString());
    return GoalAgentSettings::fromJson(QJsonDocument::fromJson(json.toUtf8()).object());
}

QJsonObject taskJson(const ScheduledTaskDefinition &t)
{
    QJsonObject o{{QStringLiteral("id"), t.id},
                  {QStringLiteral("name"), t.name},
                  {QStringLiteral("cron"), t.cron},
                  {QStringLiteral("agent"), t.agentId},
                  {QStringLiteral("cwd"), QDir::fromNativeSeparators(t.cwd)},
                  {QStringLiteral("prompt"), t.prompt},
                  {QStringLiteral("enabled"), t.enabled},
                  {QStringLiteral("skipIfRunning"), t.skipIfRunning},
                  {QStringLiteral("timeoutMinutes"), t.timeoutMinutes}};
    if (!t.lastRunTime.isEmpty())
        o.insert(QStringLiteral("lastRun"), t.lastRunTime);
    if (t.hasGoalConfig)
        o.insert(QStringLiteral("goal"), scheduledTaskGoalConfigToJson(t.goalConfig));
    return o;
}

Arg secretArg(Arg a)
{
    a.secret = true;
    return a;
}

void saveGoalSettings(ApplicationSettings *settings, const GoalAgentSettings &gs)
{
    settings->setValue(QStringLiteral("Ai/GoalAgentSettings"),
                       QString::fromUtf8(QJsonDocument(gs.toJson()).toJson(QJsonDocument::Compact)));
}

QString brandKeys()
{
    QStringList keys;
    for (const AcpAgentBrandInfo &b : acpAgentBrands())
        keys.append(QString::fromLatin1(b.key));
    return keys.join(QStringLiteral(", "));
}

QJsonObject agentJson(const AcpAgentDefinition &a, const QString &defaultId)
{
    return {{QStringLiteral("id"), a.id},
            {QStringLiteral("name"), a.name},
            {QStringLiteral("command"), a.command},
            {QStringLiteral("args"), QJsonArray::fromStringList(a.args)},
            {QStringLiteral("icon"), a.icon},
            {QStringLiteral("builtin"), a.builtin},
            {QStringLiteral("default"), a.id == defaultId},
            {QStringLiteral("questionForms"), a.interactiveQuestions},
            {QStringLiteral("hasEnv"), !a.env.isEmpty()}};
}

// The definition fields `ctx` names, applied onto `def` with the edit
// dialog's checks. Empty when valid.
QString applyAgentArgs(const CommandContext &ctx, AcpAgentDefinition *def)
{
    if (ctx.has(QStringLiteral("name")))
        def->name = ctx.str(QStringLiteral("name")).trimmed();
    if (ctx.has(QStringLiteral("command")))
        def->command = ctx.str(QStringLiteral("command")).trimmed();
    if (ctx.has(QStringLiteral("args")))
        def->args = ctx.strings(QStringLiteral("args"));
    if (ctx.has(QStringLiteral("env"))) {
        QMap<QString, QString> env;
        const QJsonObject o = ctx.object(QStringLiteral("env"));
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (it.key().trimmed().isEmpty())
                return QStringLiteral("env names cannot be empty");
            if (!it.value().isString())
                return QStringLiteral("env values must be strings (%1 is not)").arg(it.key());
            env.insert(it.key().trimmed(), it.value().toString());
        }
        def->env = env;
    }
    if (ctx.has(QStringLiteral("icon"))) {
        const QString icon = ctx.str(QStringLiteral("icon")).trimmed();
        if (!icon.isEmpty() && !isAcpAgentBrandKey(icon) && !QFileInfo(icon).isFile())
            return QStringLiteral("icon must be empty (automatic), one of %1, or an existing image file").arg(brandKeys());
        def->icon = icon;
    }
    if (ctx.has(QStringLiteral("questionForms")))
        def->interactiveQuestions = ctx.flag(QStringLiteral("questionForms"));
    if (def->name.isEmpty())
        return QStringLiteral("name is empty");
    if (def->command.isEmpty())
        return QStringLiteral("command is empty: give the program that starts the agent");
    return QString();
}

QList<Arg> agentFieldArgs(bool adding)
{
    const auto field = adding ? &Registrar::required : &Registrar::optional;
    return {field(QStringLiteral("name"), ArgType::String, QStringLiteral("display name")),
            field(QStringLiteral("command"), ArgType::String, QStringLiteral("program that starts the ACP agent")),
            Registrar::optional(QStringLiteral("args"), ArgType::Array, QStringLiteral("its arguments, one string each")),
            secretArg(Registrar::optional(QStringLiteral("env"), ArgType::Object,
                                          QStringLiteral("environment variables {NAME: value} (replaces them all); "
                                                         "never shown back"))),
            Registrar::optional(QStringLiteral("icon"), ArgType::String,
                                QStringLiteral("empty (automatic), a bundled mark (claude-code, codex, opencode, "
                                               "oh-my-pi, pi) or an image file")),
            Registrar::optional(QStringLiteral("questionForms"), ArgType::Bool,
                                QStringLiteral("attach the grill_me question-form tool; default true"))};
}

QJsonArray templatesJson(const GoalAgentSettings &s)
{
    QJsonArray templates;
    for (const GoalPromptTemplate &t : s.promptTemplates)
        templates.append(QJsonObject{{QStringLiteral("id"), t.id},
                                     {QStringLiteral("name"), t.name},
                                     {QStringLiteral("builtin"),
                                      t.id == QLatin1String(GoalAgentSettings::kDefaultTemplateId)}});
    return templates;
}

Arg sessionArgSpec()
{
    return Registrar::optional(QStringLiteral("session"), ArgType::String,
                               QStringLiteral("session id from ai.sessions; default: the session in front"));
}

} // namespace

void Registrar::ai()
{
    // The session `session` names, else the one in front of the active AI panel.
    auto sessionArg = [w = w](const CommandContext &ctx, AiAgentDock **dockOut) -> AcpSessionView * {
        QString id = ctx.str(QStringLiteral("session"));
        AiAgentDock *dock = nullptr;
        if (id.isEmpty()) {
            dock = w->activeAiDock();
            if (!dock) {
                const QList<AiAgentDock *> docks = aiDocks(w);
                dock = docks.isEmpty() ? nullptr : docks.constFirst();
            }
            id = dock ? dock->sessionId() : QString();
            if (id.isEmpty()) {
                ctx.fail(404, QStringLiteral("no AI session is open; open one with ai.open"));
                return nullptr;
            }
        } else {
            dock = dockOfSession(w, id);
            if (!dock) {
                ctx.fail(404, QStringLiteral("no AI session %1; ai.sessions lists them").arg(id));
                return nullptr;
            }
        }
        *dockOut = dock;
        return dock->viewFor(id);
    };
    // Brings the session forward: its panel shown and raised, its tab current.
    auto showSession = [](const CommandContext &ctx, AiAgentDock *dock, const QString &id) {
        for (int i = 0; i < dock->slotCount(); ++i) {
            if (dock->sessionIdAt(i) == id && dock->currentIndex() != i)
                dock->switchTo(i);
        }
        dock->setVisible(true);
        dock->raise();
        if (AcpSessionView *view = dock->viewFor(id))
            ctx.reveal(view);
    };
    auto idOf = [](AiAgentDock *dock, AcpSessionView *view) {
        for (int i = 0; i < dock->slotCount(); ++i) {
            if (dock->viewFor(dock->sessionIdAt(i)) == view)
                return dock->sessionIdAt(i);
        }
        return QString();
    };

    add(QStringLiteral("ai.agents"), QStringLiteral("List the configured AI agents (ids for ai.open) and the default"),
        false, {}, [w = w](const CommandContext &ctx) {
            AcpAgentManager *manager = w->app->getAiAgentManager();
            AcpAgentRegistry *registry = manager ? manager->registry() : nullptr;
            if (!registry) {
                ctx.fail(409, QStringLiteral("AI agents are not available"));
                return;
            }
            QJsonArray list;
            const QString defaultId = registry->defaultAgentId();
            for (const AcpAgentDefinition &a : registry->agents())
                list.append(agentJson(a, defaultId));
            ctx.ok({{QStringLiteral("agents"), list},
                    {QStringLiteral("default"), registry->defaultAgentId()},
                    {QStringLiteral("lastUsed"), w->app->getSettings()->lastUsedAiAgentId()}});
        });

    add(QStringLiteral("ai.sessions"), QStringLiteral("List the open AI chat sessions and what each is doing"), false, {},
        [w = w](const CommandContext &ctx) {
            QJsonArray list;
            for (AiAgentDock *dock : aiDocks(w)) {
                for (int i = 0; i < dock->slotCount(); ++i)
                    list.append(sessionJson(dock, dock->sessionIdAt(i), false));
            }
            ctx.ok({{QStringLiteral("sessions"), list}});
        });

    add(QStringLiteral("ai.open"),
        QStringLiteral("Open a chat with an AI agent in a workspace (or folder), optionally sending a first prompt"), true,
        {optional(QStringLiteral("agent"), ArgType::String, QStringLiteral("agent id from ai.agents; default the last used")),
         workspaceArgSpec(),
         optional(QStringLiteral("cwd"), ArgType::String,
                  QStringLiteral("folder instead of the workspace root (local; relative to the workspace)")),
         optional(QStringLiteral("prompt"), ArgType::String, QStringLiteral("first prompt to send"))},
        [w = w, showSession](const CommandContext &ctx) {
            AcpAgentManager *manager = w->app->getAiAgentManager();
            AcpAgentRegistry *registry = manager ? manager->registry() : nullptr;
            if (!registry) {
                ctx.fail(409, QStringLiteral("AI agents are not available"));
                return;
            }
            QString agentId = ctx.str(QStringLiteral("agent"));
            if (agentId.isEmpty()) {
                agentId = w->app->getSettings()->lastUsedAiAgentId();
                if (agentId.isEmpty() || !registry->contains(agentId))
                    agentId = registry->defaultAgentId();
            }
            if (agentId.isEmpty() || !registry->contains(agentId)) {
                ctx.fail(404, QStringLiteral("no agent '%1'; ai.agents lists them").arg(agentId));
                return;
            }
            FolderAsWorkspaceDock *ws = ctx.has(QStringLiteral("workspace")) ? workspaceArg(ctx, w) : w->activeWorkspaceDock();
            if (ctx.has(QStringLiteral("workspace")) && !ws)
                return;
            const QString root = ws ? ws->rootPath() : QString();
            remote::ExecutionContext *context = nullptr;
            QString cwd;
            if (remote::isSshUri(root)) {
                remote::ExecutionContextRegistry *contexts = w->app->getExecutionContextRegistry();
                context = contexts ? contexts->remoteContext(remote::parseSshUri(root).profileId) : nullptr;
                cwd = context ? TerminalCwdResolver::resolveWorkspaceForContext(context, root) : QString();
                if (cwd.isEmpty()) {
                    ctx.fail(409, QStringLiteral("the SSH workspace %1 is not connected").arg(root));
                    return;
                }
                if (const QString sub = ctx.str(QStringLiteral("cwd")); !sub.isEmpty())
                    cwd = sub.startsWith(QLatin1Char('/')) ? sub : QDir::cleanPath(cwd + QLatin1Char('/') + sub);
            } else {
                const QString sub = QDir::fromNativeSeparators(ctx.str(QStringLiteral("cwd")));
                cwd = sub.isEmpty() ? root
                                    : QDir::cleanPath(QDir::isRelativePath(sub) && !root.isEmpty()
                                                          ? root + QLatin1Char('/') + sub
                                                          : sub);
                if (cwd.isEmpty()) {
                    ctx.fail(409, QStringLiteral("no workspace is open; pass `cwd` or open one with workspace.open"),
                             {{QStringLiteral("needs"), QStringLiteral("cwd")}});
                    return;
                }
                if (!QFileInfo(cwd).isDir()) {
                    ctx.fail(404, QStringLiteral("no folder %1").arg(cwd));
                    return;
                }
            }
            AiAgentDock *dock = manager->openAgent(agentId, cwd, true, context);
            if (!dock) {
                ctx.fail(422, QStringLiteral("the agent could not be started"));
                return;
            }
            w->attachAiAgentDock(dock);
            const QString id = dock->newestSessionId();
            showSession(ctx, dock, id);
            if (const QString prompt = ctx.str(QStringLiteral("prompt")); !prompt.isEmpty()) {
                if (AcpSessionView *view = dock->viewFor(id)) {
                    if (const QString error = view->sendFromComposer(prompt); !error.isEmpty()) {
                        ctx.fail(422, error, {{QStringLiteral("session"), id}});
                        return;
                    }
                }
            }
            ctx.ok(sessionJson(dock, id, false));
        });

    add(QStringLiteral("ai.state"),
        QStringLiteral("Read a session's state: working, waiting for an answer (permission / form), models, modes, "
                       "queue"),
        false, {sessionArgSpec()}, [sessionArg, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            ctx.ok(sessionJson(dock, idOf(dock, view), true));
        });

    // The chat's "Debug log" window: the agent process's protocol traffic.
    add(QStringLiteral("ai.debugLog"),
        QStringLiteral("Read a session's debug log, as its Debug log window shows it (agent env values are hidden)"),
        false,
        {sessionArgSpec(), optional(QStringLiteral("last"), ArgType::Int, QStringLiteral("the last this many lines; default 200")),
         optional(QStringLiteral("goalOnly"), ArgType::Bool, QStringLiteral("only the goal agent's lines"))},
        [sessionArg](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            QStringList lines;
            if (!ctx.flag(QStringLiteral("goalOnly")) && view->connection())
                lines = view->connection()->debugLog();
            lines.append(view->goalDebugLog());
            const qsizetype last = std::max<qint64>(1, ctx.integer(QStringLiteral("last"), 200));
            if (lines.size() > last)
                lines = lines.mid(lines.size() - last);
            // The spawn line lists the agent's env overrides with their values.
            static const QRegularExpression envValue(QStringLiteral("([A-Za-z_][A-Za-z0-9_]*)=.*?(?=, [A-Za-z_][A-Za-z0-9_]*=|\\]$)"));
            for (QString &line : lines) {
                const qsizetype at = line.indexOf(QLatin1String("spawn: env-overrides=["));
                if (at < 0)
                    continue;
                const qsizetype open = at + qsizetype(qstrlen("spawn: env-overrides=["));
                QString env = line.mid(open);
                env.replace(envValue, QStringLiteral("\\1=<hidden>"));
                line = line.left(open) + env;
            }
            ctx.ok({{QStringLiteral("lines"), QJsonArray::fromStringList(lines)}});
        });

    add(QStringLiteral("ai.read"), QStringLiteral("Read a session's transcript: prompts, replies and tool calls"), false,
        {sessionArgSpec(), optional(QStringLiteral("last"), ArgType::Int, QStringLiteral("the last this many entries; default 30")),
         optional(QStringLiteral("from"), ArgType::Int, QStringLiteral("instead: entries from this index")),
         optional(QStringLiteral("count"), ArgType::Int, QStringLiteral("with `from`: how many; default 30")),
         optional(QStringLiteral("thoughts"), ArgType::Bool, QStringLiteral("include the agent's thinking; default false"))},
        [sessionArg, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const AcpSessionModel *m = view->sessionModel();
            if (!m) {
                ctx.fail(409, QStringLiteral("the session has no transcript yet"));
                return;
            }
            const auto &timeline = m->timeline();
            const auto &messages = m->messages();
            const qint64 total = timeline.size();
            qint64 from = 0;
            qint64 count = 0;
            if (ctx.has(QStringLiteral("from"))) {
                from = std::clamp<qint64>(ctx.integer(QStringLiteral("from")), 0, total);
                count = std::clamp<qint64>(ctx.integer(QStringLiteral("count"), 30), 0, 2000);
            } else {
                count = std::clamp<qint64>(ctx.integer(QStringLiteral("last"), 30), 0, 2000);
                from = std::max<qint64>(0, total - count);
            }
            count = std::min(count, total - from);
            const bool thoughts = ctx.flag(QStringLiteral("thoughts"));
            QJsonArray entries;
            for (qint64 i = from; i < from + count; ++i) {
                const AcpTimelineEntry &entry = timeline.at(i);
                QJsonObject o{{QStringLiteral("index"), i}};
                if (entry.kind == AcpTimelineEntry::Kind::Message) {
                    if (entry.messageIndex < 0 || entry.messageIndex >= messages.size())
                        continue;
                    const AcpMessage &msg = messages.at(entry.messageIndex);
                    if (msg.role == QLatin1String("thought") && !thoughts)
                        continue;
                    bool cut = false;
                    o.insert(QStringLiteral("type"), msg.role);
                    o.insert(QStringLiteral("text"), capped(AcpConversationCopy::messageText(msg), &cut));
                    if (cut)
                        o.insert(QStringLiteral("truncated"), true);
                    if (msg.fromGoalAgent)
                        o.insert(QStringLiteral("fromGoalAgent"), true);
                    if (msg.fromGoalJudge)
                        o.insert(QStringLiteral("fromGoalJudge"), true);
                } else {
                    const auto it = m->toolCalls().constFind(entry.toolCallId);
                    if (it == m->toolCalls().constEnd() || !it->parentToolCallId.isEmpty())
                        continue; // a subagent's call: part of its Agent call
                    o.insert(QStringLiteral("type"), QStringLiteral("tool"));
                    o.insert(QStringLiteral("title"), it->title);
                    o.insert(QStringLiteral("status"), it->status);
                    if (!it->kind.isEmpty())
                        o.insert(QStringLiteral("kind"), it->kind);
                    if (const int children = int(m->subagentChildren(it->id).size()); children > 0)
                        o.insert(QStringLiteral("subagentCalls"), children);
                }
                entries.append(o);
            }
            QJsonObject out = sessionJson(dock, idOf(dock, view), false);
            out.insert(QStringLiteral("entries"), entries);
            out.insert(QStringLiteral("from"), from);
            out.insert(QStringLiteral("total"), total);
            ctx.ok(out);
        });

    add(QStringLiteral("ai.report"), QStringLiteral("Read the final answer of a session's last finished turn"), false,
        {sessionArgSpec()}, [sessionArg, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const AcpSessionModel *m = view->sessionModel();
            QJsonObject out = sessionJson(dock, idOf(dock, view), false);
            if (m) {
                const QVector<int> reports =
                    AcpConversationCopy::finalReports(m->timeline(), m->messages(), !m->isProcessing());
                if (!reports.isEmpty()) {
                    bool cut = false;
                    out.insert(QStringLiteral("text"),
                               capped(AcpConversationCopy::messageText(m->messages().at(reports.last())), &cut));
                    if (cut)
                        out.insert(QStringLiteral("truncated"), true);
                }
            }
            ctx.ok(out);
        });

    add(QStringLiteral("ai.send"),
        QStringLiteral("Send a prompt from a session's composer (queued behind a running turn; slash commands work)"),
        true,
        {sessionArgSpec(), required(QStringLiteral("text"), ArgType::String, QStringLiteral("the prompt")),
         optional(QStringLiteral("images"), ArgType::Array,
                  QStringLiteral("images to attach: [{path} (png/jpg/gif/webp; relative to the session folder) or "
                                 "{data: base64, mime: \"image/png\"}]")),
         optional(QStringLiteral("newWorktree"), ArgType::Bool,
                  QStringLiteral("set the composer's New worktree toggle first (the conversation works in a new git "
                                 "worktree, then merges); it stays as set"))},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            showSession(ctx, dock, id);
            const QJsonArray images = ctx.array(QStringLiteral("images"));
            for (qsizetype i = 0; i < images.size(); ++i) {
                const QJsonObject image = images.at(i).toObject();
                QString error;
                if (image.contains(QStringLiteral("path"))) {
                    QString path = QDir::fromNativeSeparators(image.value(QStringLiteral("path")).toString());
                    if (QDir::isRelativePath(path))
                        path = QDir(dock->workingDirectory()).absoluteFilePath(path);
                    error = view->attachImageFile(path);
                } else if (image.contains(QStringLiteral("data"))) {
                    const auto decoded = QByteArray::fromBase64Encoding(
                        image.value(QStringLiteral("data")).toString().toLatin1(),
                        QByteArray::AbortOnBase64DecodingErrors);
                    const QString mime = image.value(QStringLiteral("mime")).toString(QStringLiteral("image/png"));
                    error = decoded ? view->attachImage(*decoded, QStringLiteral("image.") + mime.section(QLatin1Char('/'), 1))
                                    : QStringLiteral("not valid base64");
                } else {
                    error = QStringLiteral("give `path` or `data`");
                }
                if (!error.isEmpty()) {
                    view->takeInputImages(); // nothing half-attached is left behind
                    ctx.fail(400, QStringLiteral("image %1: %2 (nothing was sent)").arg(i).arg(error));
                    return;
                }
            }
            if (ctx.has(QStringLiteral("newWorktree")))
                view->setNewWorktree(ctx.flag(QStringLiteral("newWorktree")));
            if (const QString error = view->sendFromComposer(ctx.str(QStringLiteral("text"))); !error.isEmpty()) {
                ctx.fail(409, error);
                return;
            }
            ctx.ok(sessionJson(dock, id, false));
        });

    add(QStringLiteral("ai.draft"),
        QStringLiteral("Put text into a session's composer without sending it (what Send to AI does)"), true,
        {sessionArgSpec(), required(QStringLiteral("text"), ArgType::String, QStringLiteral("text to put in")),
         optional(QStringLiteral("append"), ArgType::Bool,
                  QStringLiteral("insert at the composer's caret (default true); false replaces the composer text"))},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            showSession(ctx, dock, id);
            if (!ctx.flag(QStringLiteral("append"), true))
                view->takeInputText();
            view->insertTextToInput(ctx.str(QStringLiteral("text")));
            ctx.ok({{QStringLiteral("session"), id}, {QStringLiteral("draft"), view->peekInputText()}});
        });

    add(QStringLiteral("ai.improvePrompt"),
        QStringLiteral("Rewrite the composer's prompt with AI (the composer's Improve button) and leave the result "
                       "there"),
        true,
        {sessionArgSpec(), optional(QStringLiteral("text"), ArgType::String,
                            QStringLiteral("put this in the composer first; default: what it holds"))},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            showSession(ctx, dock, id);
            if (ctx.has(QStringLiteral("text"))) {
                view->takeInputText();
                view->insertTextToInput(ctx.str(QStringLiteral("text")));
            }
            if (const QString error = view->improveComposer(); !error.isEmpty()) {
                ctx.fail(409, error);
                return;
            }
            auto *guard = new QObject(view);
            auto done = std::make_shared<bool>(false);
            auto settle = [ctx, guard, done](int status, const QString &textOrError) {
                if (*done)
                    return;
                *done = true;
                guard->deleteLater();
                if (status == 200)
                    ctx.ok({{QStringLiteral("text"), textOrError}});
                else
                    ctx.fail(status, textOrError);
            };
            QObject::connect(view, &AcpSessionView::improveSettled, guard,
                             [settle](bool ok, const QString &t) { settle(ok ? 200 : 422, t); });
            QObject::connect(view, &QObject::destroyed, guard,
                             [settle] { settle(409, QStringLiteral("the session closed")); });
            QTimer::singleShot(180000, guard, [settle] {
                settle(504, QStringLiteral("the improved prompt did not arrive within 3 minutes"));
            });
        });

    add(QStringLiteral("ai.queue"), QStringLiteral("List the prompts queued behind a session's running turn"), false,
        {sessionArgSpec()}, [sessionArg, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            QJsonArray queue;
            const QStringList texts = view->queuedPromptTexts();
            for (qsizetype i = 0; i < texts.size(); ++i)
                queue.append(QJsonObject{{QStringLiteral("index"), i}, {QStringLiteral("text"), texts.at(i)}});
            ctx.ok({{QStringLiteral("session"), idOf(dock, view)}, {QStringLiteral("queue"), queue}});
        });

    add(QStringLiteral("ai.unqueue"),
        QStringLiteral("Remove a queued prompt before it is sent (the queue strip's remove button)"), true,
        {sessionArgSpec(), required(QStringLiteral("index"), ArgType::Int, QStringLiteral("its index in ai.queue"))},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            const qint64 index = ctx.integer(QStringLiteral("index"));
            if (index < 0 || index > std::numeric_limits<int>::max() || !view->removeQueuedPrompt(int(index))) {
                ctx.fail(404, QStringLiteral("no queued prompt %1; ai.queue lists them").arg(index));
                return;
            }
            showSession(ctx, dock, id);
            ctx.ok({{QStringLiteral("session"), id},
                    {QStringLiteral("queue"), QJsonArray::fromStringList(view->queuedPromptTexts())}});
        });

    add(QStringLiteral("ai.stopTask"),
        QStringLiteral("Stop one of a session's background tasks (ai.state lists them under tasks)"), true,
        {sessionArgSpec(), required(QStringLiteral("taskId"), ArgType::String, QStringLiteral("task id from ai.state"))},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            AcpConnection *conn = dock->connectionFor(id);
            const QString taskId = ctx.str(QStringLiteral("taskId"));
            const QList<AcpProtocol::AcpBackgroundTask> tasks =
                conn ? conn->backgroundTasks() : QList<AcpProtocol::AcpBackgroundTask>{};
            const auto it = std::find_if(tasks.cbegin(), tasks.cend(),
                                         [&taskId](const AcpProtocol::AcpBackgroundTask &t) { return t.taskId == taskId; });
            if (it == tasks.cend()) {
                ctx.fail(404, QStringLiteral("no background task %1; ai.state lists them").arg(taskId));
                return;
            }
            if (!it->canStop) {
                ctx.fail(409, QStringLiteral("the agent does not let task %1 be stopped").arg(taskId));
                return;
            }
            showSession(ctx, dock, id);
            conn->stopBackgroundTask(taskId);
            ctx.ok({{QStringLiteral("session"), id}, {QStringLiteral("stopped"), taskId}});
        });

    add(QStringLiteral("ai.retry"),
        QStringLiteral("Restart a session whose agent exited (the chat's Retry); a running agent has nothing to retry"),
        true, {sessionArgSpec()}, [w = w, sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            if (!dock->hasAgentExited(id)) {
                ctx.fail(409, QStringLiteral("the agent is running, so there is nothing to retry; ai.restart starts a "
                                             "fresh conversation"));
                return;
            }
            const QString newId = w->app->getAiAgentManager()->restartSession(id);
            if (newId.isEmpty()) {
                ctx.fail(422, QStringLiteral("the session could not restart"));
                return;
            }
            showSession(ctx, dock, newId);
            ctx.ok(sessionJson(dock, newId, false));
        });

    add(QStringLiteral("ai.cancel"), QStringLiteral("Stop a session's running turn (and a goal running on it)"), true,
        {sessionArgSpec()}, [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            showSession(ctx, dock, id);
            view->cancelFromComposer();
            ctx.ok(sessionJson(dock, id, false));
        });

    add(QStringLiteral("ai.set"), QStringLiteral("Pick a session's model, mode or reasoning effort (ai.state lists them)"),
        true,
        {sessionArgSpec(), optional(QStringLiteral("model"), ArgType::String, QStringLiteral("model id")),
         optional(QStringLiteral("mode"), ArgType::String, QStringLiteral("mode id")),
         optional(QStringLiteral("effort"), ArgType::String, QStringLiteral("effort id")),
         optional(QStringLiteral("prewalk"), ArgType::String,
                  QStringLiteral("Prewalk (Claude Code): a model id to switch to once the agent starts editing files "
                                 "(back when the turn ends), or \"off\""))},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            const struct
            {
                const char *arg;
                AcpSessionView::Selector selector;
            } pickers[] = {{"model", AcpSessionView::Selector::Model},
                           {"mode", AcpSessionView::Selector::Mode},
                           {"effort", AcpSessionView::Selector::Effort}};
            bool any = false;
            for (const auto &p : pickers) {
                const QString arg = QString::fromLatin1(p.arg);
                if (!ctx.has(arg))
                    continue;
                any = true;
                if (!view->choose(p.selector, ctx.str(arg))) {
                    QString current;
                    ctx.fail(400, QStringLiteral("the %1 picker has no '%2' now; ai.state lists its items")
                                      .arg(arg, ctx.str(arg)),
                             {{QStringLiteral("choices"), choicesJson(view, p.selector, &current)}});
                    return;
                }
            }
            if (ctx.has(QStringLiteral("prewalk"))) {
                any = true;
                QString model = ctx.str(QStringLiteral("prewalk"));
                if (model.compare(QLatin1String("off"), Qt::CaseInsensitive) == 0)
                    model.clear();
                if (const QString error = view->pickPrewalk(model); !error.isEmpty()) {
                    ctx.fail(409, error);
                    return;
                }
            }
            if (!any) {
                ctx.fail(400, QStringLiteral("pass model, mode, effort or prewalk"));
                return;
            }
            showSession(ctx, dock, id);
            ctx.ok(sessionJson(dock, id, true));
        });

    add(QStringLiteral("ai.autoApprove"),
        QStringLiteral("Turn auto-approve of agent tool permissions on or off (applies to every session)"), true,
        {required(QStringLiteral("on"), ArgType::Bool, QStringLiteral("true: agents run tools without asking")), sessionArgSpec()},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            view->setAutoApprove(ctx.flag(QStringLiteral("on")));
            showSession(ctx, dock, idOf(dock, view));
            ctx.ok({{QStringLiteral("autoApprove"), view->autoApprove()}});
        });

    add(QStringLiteral("ai.permission"),
        QStringLiteral("Answer the permission request a session is waiting on (ai.state shows its options)"), true,
        {sessionArgSpec(), required(QStringLiteral("option"), ArgType::String,
                            QStringLiteral("option id or kind: allow_once, allow_always, reject_once, ..."))},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const AcpProtocol::AcpPermissionRequest *req = view->pendingPermission();
            if (!req) {
                ctx.fail(409, QStringLiteral("no permission request is waiting"));
                return;
            }
            QJsonArray options;
            for (const AcpProtocol::AcpPermissionOption &opt : req->options)
                options.append(QJsonObject{{QStringLiteral("id"), opt.id}, {QStringLiteral("kind"), opt.kind}});
            const QString id = idOf(dock, view);
            showSession(ctx, dock, id);
            if (!view->answerPermission(ctx.str(QStringLiteral("option")))) {
                ctx.fail(400, QStringLiteral("no option '%1'").arg(ctx.str(QStringLiteral("option"))),
                         {{QStringLiteral("options"), options}});
                return;
            }
            ctx.ok(sessionJson(dock, id, false));
        });

    add(QStringLiteral("ai.answer"),
        QStringLiteral("Answer the question form a session shows (ai.state has its questions) and submit it"), true,
        {sessionArgSpec(),
         required(QStringLiteral("answers"), ArgType::Array,
                  QStringLiteral("[{id, value, other?, note?, clarify?}]: value is an option value (single/select), an "
                                 "array of them (multi), \"accept\"/\"change\" (proposal, a change in `other`), "
                                 "true/false (confirm), text or a number")),
         optional(QStringLiteral("note"), ArgType::String, QStringLiteral("a note sent after the answers"))},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            if (!view->pendingFormSpec()) {
                ctx.fail(409, QStringLiteral("no question form is waiting"));
                return;
            }
            const QString id = idOf(dock, view);
            showSession(ctx, dock, id);
            const QStringList missing =
                view->answerForm(answersArg(ctx.array(QStringLiteral("answers"))), ctx.str(QStringLiteral("note")));
            if (!missing.isEmpty()) {
                ctx.fail(400, QStringLiteral("required questions unanswered: %1 (nothing was sent)")
                                  .arg(missing.join(QStringLiteral(", "))),
                         {{QStringLiteral("missing"), QJsonArray::fromStringList(missing)}});
                return;
            }
            ctx.ok(sessionJson(dock, id, false));
        });

    add(QStringLiteral("ai.dismissForm"), QStringLiteral("Dismiss the question form a session shows"), true,
        {sessionArgSpec(), optional(QStringLiteral("note"), ArgType::String, QStringLiteral("a note for the agent"))},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            showSession(ctx, dock, id);
            if (!view->dismissForm(ctx.str(QStringLiteral("note")))) {
                ctx.fail(409, QStringLiteral("no question form is waiting"));
                return;
            }
            ctx.ok(sessionJson(dock, id, false));
        });

    add(QStringLiteral("ai.focus"), QStringLiteral("Bring a session to the front"), true, {sessionArgSpec()},
        [sessionArg, showSession, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            showSession(ctx, dock, id);
            ctx.ok(sessionJson(dock, id, false));
        });

    add(QStringLiteral("ai.close"), QStringLiteral("Close a session (its agent stops; history is kept)"), true,
        {sessionArgSpec(), optional(QStringLiteral("force"), ArgType::Bool, QStringLiteral("close even while it works"))},
        [w = w, sessionArg, idOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            if (dock->isSessionBusy(id) && !ctx.flag(QStringLiteral("force"))) {
                ctx.fail(409, QStringLiteral("the session is working; pass force: true to stop and close it"),
                         {{QStringLiteral("needs"), QStringLiteral("force")}});
                return;
            }
            dock->stopGoalForSession(id);
            w->app->getAiAgentManager()->closeSession(id);
            ctx.ok({{QStringLiteral("closed"), id}});
        });

    add(QStringLiteral("ai.restart"),
        QStringLiteral("Restart a session's agent with a fresh conversation in the same chat slot"), true,
        {sessionArgSpec(), optional(QStringLiteral("force"), ArgType::Bool, QStringLiteral("restart even while it works"))},
        [w = w, sessionArg, idOf, showSession](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            if (dock->isSessionBusy(id) && !ctx.flag(QStringLiteral("force"))) {
                ctx.fail(409, QStringLiteral("the session is working; pass force: true to restart it"),
                         {{QStringLiteral("needs"), QStringLiteral("force")}});
                return;
            }
            const QString newId = w->app->getAiAgentManager()->restartSession(id);
            if (newId.isEmpty()) {
                ctx.fail(422, QStringLiteral("the session could not restart"));
                return;
            }
            showSession(ctx, dock, newId);
            ctx.ok(sessionJson(dock, newId, false));
        });

    add(QStringLiteral("ai.switchAgent"),
        QStringLiteral("Move an idle local Claude Code session to another Claude Code agent, keeping the conversation"),
        true,
        {sessionArgSpec(), required(QStringLiteral("agent"), ArgType::String, QStringLiteral("target agent id from ai.agents"))},
        [w = w, sessionArg, idOf, showSession](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            AcpSessionView *view = sessionArg(ctx, &dock);
            if (!view)
                return;
            const QString id = idOf(dock, view);
            const AcpSwitchAgent::Block block = AcpSwitchAgent::blockReason(dock->switchState(id));
            if (block != AcpSwitchAgent::Block::None) {
                ctx.fail(409, AcpSwitchAgent::blockText(block));
                return;
            }
            AcpAgentRegistry *registry = w->app->getAiAgentManager()->registry();
            const AcpAgentDefinition target = registry->agent(ctx.str(QStringLiteral("agent")));
            if (target.id.isEmpty() || !AcpSwitchAgent::isClaudeCodeAgent(target)) {
                ctx.fail(400, QStringLiteral("'%1' is not a Claude Code agent; ai.agents lists them")
                                  .arg(ctx.str(QStringLiteral("agent"))));
                return;
            }
            showSession(ctx, dock, id);
            w->app->getAiAgentManager()->switchAgent(id, target.id);
            ctx.ok({{QStringLiteral("session"), id},
                    {QStringLiteral("note"),
                     QStringLiteral("switching; the chat shows a notice when done (ai.state: agent)")}});
        });

    // Agent definitions: what AI > Agents... edits.
    auto registryOf = [w = w](const CommandContext &ctx) -> AcpAgentRegistry * {
        AcpAgentManager *manager = w->app->getAiAgentManager();
        AcpAgentRegistry *registry = manager ? manager->registry() : nullptr;
        if (!registry)
            ctx.fail(409, QStringLiteral("AI agents are not available"));
        return registry;
    };
    auto agentOf = [](const CommandContext &ctx, AcpAgentRegistry *registry, AcpAgentDefinition *out) {
        *out = registry->agent(ctx.str(QStringLiteral("agent")));
        if (out->id.isEmpty())
            ctx.fail(404, QStringLiteral("no agent '%1'; ai.agents lists them").arg(ctx.str(QStringLiteral("agent"))));
        return !out->id.isEmpty();
    };
    const Arg agentId = required(QStringLiteral("agent"), ArgType::String, QStringLiteral("agent id from ai.agents"));

    add(QStringLiteral("ai.addAgent"), QStringLiteral("Add an ACP agent definition (AI > Agents... > Add)"), true,
        agentFieldArgs(true), [registryOf](const CommandContext &ctx) {
            AcpAgentRegistry *registry = registryOf(ctx);
            if (!registry)
                return;
            AcpAgentDefinition def;
            def.id = QStringLiteral("user:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (const QString error = applyAgentArgs(ctx, &def); !error.isEmpty()) {
                ctx.fail(400, error);
                return;
            }
            if (!registry->addAgent(def)) {
                ctx.fail(422, QStringLiteral("the agent could not be added"));
                return;
            }
            ctx.ok(agentJson(registry->agent(def.id), registry->defaultAgentId()));
        });

    QList<Arg> updateArgs{agentId};
    updateArgs.append(agentFieldArgs(false));
    add(QStringLiteral("ai.updateAgent"),
        QStringLiteral("Change an agent definition; unnamed fields keep their value (AI > Agents... > Edit)"), true,
        updateArgs, [registryOf, agentOf](const CommandContext &ctx) {
            AcpAgentRegistry *registry = registryOf(ctx);
            AcpAgentDefinition def;
            if (!registry || !agentOf(ctx, registry, &def))
                return;
            if (const QString error = applyAgentArgs(ctx, &def); !error.isEmpty()) {
                ctx.fail(400, error);
                return;
            }
            if (!registry->updateAgent(def)) {
                ctx.fail(422, QStringLiteral("the agent could not be updated"));
                return;
            }
            ctx.ok(agentJson(registry->agent(def.id), registry->defaultAgentId()));
        });

    add(QStringLiteral("ai.cloneAgent"), QStringLiteral("Copy an agent definition under a new id, right after it"), true,
        {agentId}, [registryOf, agentOf](const CommandContext &ctx) {
            AcpAgentRegistry *registry = registryOf(ctx);
            AcpAgentDefinition def;
            if (!registry || !agentOf(ctx, registry, &def))
                return;
            const QString copy = registry->cloneAgent(def.id);
            if (copy.isEmpty()) {
                ctx.fail(422, QStringLiteral("the agent could not be copied"));
                return;
            }
            ctx.ok(agentJson(registry->agent(copy), registry->defaultAgentId()));
        });

    add(QStringLiteral("ai.removeAgent"), QStringLiteral("Delete an agent definition (open sessions keep running)"), true,
        {agentId, optional(QStringLiteral("force"), ArgType::Bool, QStringLiteral("needed to remove a built-in agent"))},
        [registryOf, agentOf](const CommandContext &ctx) {
            AcpAgentRegistry *registry = registryOf(ctx);
            AcpAgentDefinition def;
            if (!registry || !agentOf(ctx, registry, &def))
                return;
            if (def.builtin && !ctx.flag(QStringLiteral("force"))) {
                ctx.fail(409, QStringLiteral("%1 is a built-in agent; pass force: true to remove it").arg(def.name),
                         {{QStringLiteral("needs"), QStringLiteral("force")}});
                return;
            }
            registry->removeAgent(def.id);
            ctx.ok({{QStringLiteral("removed"), def.id}, {QStringLiteral("default"), registry->defaultAgentId()}});
        });

    add(QStringLiteral("ai.moveAgent"), QStringLiteral("Move an agent to a position in the agent list (and menus)"), true,
        {agentId, required(QStringLiteral("index"), ArgType::Int, QStringLiteral("new 0-based position"))},
        [registryOf, agentOf](const CommandContext &ctx) {
            AcpAgentRegistry *registry = registryOf(ctx);
            AcpAgentDefinition def;
            if (!registry || !agentOf(ctx, registry, &def))
                return;
            const qint64 index = ctx.integer(QStringLiteral("index"));
            const qsizetype count = registry->agents().size();
            if (index < 0 || index >= count) {
                ctx.fail(400, QStringLiteral("index must be 0..%1").arg(count - 1));
                return;
            }
            registry->moveAgent(def.id, int(index));
            QJsonArray order;
            for (const AcpAgentDefinition &a : registry->agents())
                order.append(a.id);
            ctx.ok({{QStringLiteral("order"), order}});
        });

    add(QStringLiteral("ai.setDefaultAgent"),
        QStringLiteral("Make an agent the default (what new chats and scheduled tasks use)"), true, {agentId},
        [registryOf, agentOf](const CommandContext &ctx) {
            AcpAgentRegistry *registry = registryOf(ctx);
            AcpAgentDefinition def;
            if (!registry || !agentOf(ctx, registry, &def))
                return;
            registry->setDefaultAgentId(def.id);
            ctx.ok({{QStringLiteral("default"), registry->defaultAgentId()}});
        });

    add(QStringLiteral("ai.setApiKey"),
        QStringLiteral("Store or clear the AI API key used for commit messages and Improve prompt (Preferences; kept "
                       "in the OS keychain)"),
        true,
        {secretArg(optional(QStringLiteral("key"), ArgType::String, QStringLiteral("the key; never shown back"))),
         optional(QStringLiteral("clear"), ArgType::Bool, QStringLiteral("true: remove the stored key"))},
        [](const CommandContext &ctx) {
            ai::CredentialStore store;
            QString error;
            if (ctx.flag(QStringLiteral("clear"))) {
                if (!store.clearApiKey(&error)) {
                    ctx.fail(422, error.isEmpty() ? QStringLiteral("the key could not be removed") : error);
                    return;
                }
            } else {
                const QString key = ctx.str(QStringLiteral("key")).trimmed();
                if (key.isEmpty()) {
                    ctx.fail(400, QStringLiteral("pass key, or clear: true"));
                    return;
                }
                if (!store.storeApiKey(key, &error)) {
                    ctx.fail(422, error.isEmpty() ? QStringLiteral("the keychain refused the key") : error);
                    return;
                }
            }
            ctx.ok({{QStringLiteral("hasKey"), store.isApiKeyAvailable()}});
        });
}

void Registrar::goal()
{
    auto sessionOf = [w = w](const CommandContext &ctx, AiAgentDock **dockOut, QString *id) -> bool {
        *id = ctx.str(QStringLiteral("session"));
        if (id->isEmpty()) {
            AiAgentDock *dock = w->activeAiDock();
            *id = dock ? dock->sessionId() : QString();
            *dockOut = dock;
        } else {
            *dockOut = dockOfSession(w, *id);
        }
        if (!*dockOut || id->isEmpty()) {
            ctx.fail(404, QStringLiteral("no AI session %1; ai.sessions lists them").arg(*id));
            return false;
        }
        return true;
    };

    add(QStringLiteral("goal.options"),
        QStringLiteral("Read the goal defaults: judge agent, prompt templates, max iterations, native /goal"), false, {},
        [w = w](const CommandContext &ctx) {
            const GoalAgentSettings s = goalSettings(w->app->getSettings());
            const QJsonArray templates = templatesJson(s);
            QJsonArray presets;
            for (const GoalCriteriaPreset &p : s.criteriaPresets)
                presets.append(QJsonObject{{QStringLiteral("name"), p.name},
                                           {QStringLiteral("criteria"), QJsonArray::fromStringList(p.criteria)}});
            ctx.ok({{QStringLiteral("judgeAgent"), s.agentId},
                    {QStringLiteral("template"), s.resolvedPromptTemplateId()},
                    {QStringLiteral("templates"), templates},
                    {QStringLiteral("presets"), presets},
                    {QStringLiteral("maxIterations"), s.defaultMaxIterations},
                    {QStringLiteral("native"), s.useNativeGoal},
                    {QStringLiteral("autoCompact"), s.autoCompact},
                    {QStringLiteral("prefixGoal"), s.prefixGoal},
                    {QStringLiteral("customApiUrl"), s.customApiBaseUrl},
                    {QStringLiteral("customApiModel"), s.customApiModel},
                    {QStringLiteral("customApiHasKey"),
                     !ai::CredentialStore().retrieveSecret(QLatin1String(GoalHttpJudge::kCredentialKey)).isEmpty()},
                    {QStringLiteral("judgeAgents"), [w] {
                         QJsonArray ids;
                         if (AcpAgentManager *m = w->app->getAiAgentManager(); m && m->registry()) {
                             for (const AcpAgentDefinition &a : m->registry()->agents())
                                 ids.append(a.id);
                         }
                         ids.append(QString::fromLatin1(GoalHttpJudge::kAgentId));
                         return ids;
                     }()}});
        });

    add(QStringLiteral("goal.setOptions"),
        QStringLiteral("Change the goal defaults Send with Goal starts from (judge agent, template, iterations, "
                       "native /goal, Custom API)"),
        true,
        {optional(QStringLiteral("judgeAgent"), ArgType::String,
                  QStringLiteral("agent id that judges, or \"custom-api\" (goal.options lists them)")),
         optional(QStringLiteral("template"), ArgType::String, QStringLiteral("judge prompt template id")),
         optional(QStringLiteral("maxIterations"), ArgType::Int, QStringLiteral("per criterion, 1-1000")),
         optional(QStringLiteral("native"), ArgType::Bool, QStringLiteral("send criteria as the agent's own /goal")),
         optional(QStringLiteral("autoCompact"), ArgType::Bool, QStringLiteral("/compact once achieved")),
         optional(QStringLiteral("prefixGoal"), ArgType::Bool, QStringLiteral("prefix goal prompts with /goal")),
         optional(QStringLiteral("customApiUrl"), ArgType::String, QStringLiteral("Custom API judge: http(s) base URL")),
         optional(QStringLiteral("customApiModel"), ArgType::String, QStringLiteral("Custom API judge: model")),
         secretArg(optional(QStringLiteral("customApiKey"), ArgType::String,
                            QStringLiteral("Custom API judge: API key (keychain; \"\" removes it; never shown back)")))},
        [w = w](const CommandContext &ctx) {
            ApplicationSettings *settings = w->app->getSettings();
            GoalAgentSettings s = goalSettings(settings);
            if (ctx.has(QStringLiteral("judgeAgent"))) {
                const QString judge = ctx.str(QStringLiteral("judgeAgent"));
                AcpAgentManager *m = w->app->getAiAgentManager();
                if (!GoalHttpJudge::isCustomApiAgent(judge) && !(m && m->registry() && m->registry()->contains(judge))) {
                    ctx.fail(400, QStringLiteral("no agent '%1'; goal.options lists judgeAgents").arg(judge));
                    return;
                }
                s.agentId = judge;
            }
            if (ctx.has(QStringLiteral("template"))) {
                const QString id = ctx.str(QStringLiteral("template"));
                if (!s.findTemplate(id)) {
                    ctx.fail(400, QStringLiteral("no template '%1'; goal.options lists them").arg(id));
                    return;
                }
                s.promptTemplateId = id;
            }
            if (ctx.has(QStringLiteral("maxIterations"))) {
                const qint64 n = ctx.integer(QStringLiteral("maxIterations"));
                if (n < GoalAgentSettings::kMaxIterationsMin || n > GoalAgentSettings::kMaxIterationsMax) {
                    ctx.fail(400, QStringLiteral("maxIterations must be %1-%2")
                                      .arg(GoalAgentSettings::kMaxIterationsMin)
                                      .arg(GoalAgentSettings::kMaxIterationsMax));
                    return;
                }
                s.defaultMaxIterations = int(n);
            }
            s.useNativeGoal = ctx.flag(QStringLiteral("native"), s.useNativeGoal);
            s.autoCompact = ctx.flag(QStringLiteral("autoCompact"), s.autoCompact);
            s.prefixGoal = ctx.flag(QStringLiteral("prefixGoal"), s.prefixGoal);
            if (ctx.has(QStringLiteral("customApiUrl"))) {
                const QString url = ctx.str(QStringLiteral("customApiUrl")).trimmed();
                if (!url.isEmpty() && !GoalHttpJudge::isUsableEndpointUrl(url)) {
                    ctx.fail(400, QStringLiteral("customApiUrl must be an http(s) URL"));
                    return;
                }
                s.customApiBaseUrl = url;
            }
            if (ctx.has(QStringLiteral("customApiModel")))
                s.customApiModel = ctx.str(QStringLiteral("customApiModel")).trimmed();
            if (ctx.has(QStringLiteral("customApiKey"))) {
                ai::CredentialStore store;
                const QString key = ctx.str(QStringLiteral("customApiKey")).trimmed();
                QString error;
                const bool stored = key.isEmpty()
                                        ? store.clearSecret(QLatin1String(GoalHttpJudge::kCredentialKey), &error)
                                        : store.storeSecret(QLatin1String(GoalHttpJudge::kCredentialKey), key, &error);
                if (!stored) {
                    ctx.fail(422, error.isEmpty() ? QStringLiteral("the keychain refused the key") : error);
                    return;
                }
            }
            saveGoalSettings(settings, s);
            ctx.ok({{QStringLiteral("judgeAgent"), s.agentId},
                    {QStringLiteral("template"), s.resolvedPromptTemplateId()},
                    {QStringLiteral("maxIterations"), s.defaultMaxIterations},
                    {QStringLiteral("native"), s.useNativeGoal},
                    {QStringLiteral("autoCompact"), s.autoCompact},
                    {QStringLiteral("prefixGoal"), s.prefixGoal},
                    {QStringLiteral("customApiUrl"), s.customApiBaseUrl},
                    {QStringLiteral("customApiModel"), s.customApiModel}});
        });

    add(QStringLiteral("goal.template"), QStringLiteral("Read a judge prompt template's text"), false,
        {required(QStringLiteral("template"), ArgType::String, QStringLiteral("template id from goal.options"))},
        [w = w](const CommandContext &ctx) {
            const GoalAgentSettings s = goalSettings(w->app->getSettings());
            const GoalPromptTemplate *t = s.findTemplate(ctx.str(QStringLiteral("template")));
            if (!t) {
                ctx.fail(404, QStringLiteral("no template '%1'; goal.options lists them").arg(ctx.str(QStringLiteral("template"))));
                return;
            }
            ctx.ok({{QStringLiteral("id"), t->id}, {QStringLiteral("name"), t->name}, {QStringLiteral("text"), t->content}});
        });

    add(QStringLiteral("goal.saveTemplate"),
        QStringLiteral("Create a judge prompt template, or rename / rewrite one (the built-in default is read-only)"), true,
        {optional(QStringLiteral("template"), ArgType::String, QStringLiteral("id to change; omit to create one")),
         optional(QStringLiteral("name"), ArgType::String, QStringLiteral("its name (required to create)")),
         optional(QStringLiteral("text"), ArgType::String,
                  QStringLiteral("its text, with {{goal}}, {{conversation}}, {{iteration}}, {{maxIterations}}, "
                                 "{{criterionIndex}}, {{totalCriteria}}; default for a new one: the built-in text"))},
        [w = w](const CommandContext &ctx) {
            ApplicationSettings *settings = w->app->getSettings();
            GoalAgentSettings s = goalSettings(settings);
            const QString id = ctx.str(QStringLiteral("template"));
            const QString name = ctx.str(QStringLiteral("name")).trimmed();
            if (id == QLatin1String(GoalAgentSettings::kDefaultTemplateId)) {
                ctx.fail(409, QStringLiteral("the built-in default template cannot be changed; create a new one"));
                return;
            }
            for (const GoalPromptTemplate &t : s.promptTemplates) {
                if (!name.isEmpty() && t.id != id && t.name.compare(name, Qt::CaseInsensitive) == 0) {
                    ctx.fail(409, QStringLiteral("a template named \"%1\" already exists").arg(t.name));
                    return;
                }
            }
            GoalPromptTemplate *target = nullptr;
            if (id.isEmpty()) {
                if (name.isEmpty()) {
                    ctx.fail(400, QStringLiteral("a new template needs a name"));
                    return;
                }
                GoalPromptTemplate t;
                t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                t.content = GoalAgentSettings::builtinPromptContent();
                s.promptTemplates.append(t);
                target = &s.promptTemplates.last();
            } else {
                for (GoalPromptTemplate &t : s.promptTemplates) {
                    if (t.id == id)
                        target = &t;
                }
                if (!target) {
                    ctx.fail(404, QStringLiteral("no template '%1'; goal.options lists them").arg(id));
                    return;
                }
            }
            if (!name.isEmpty())
                target->name = name;
            if (ctx.has(QStringLiteral("text")))
                target->content = ctx.str(QStringLiteral("text"));
            const QJsonObject result{{QStringLiteral("id"), target->id}, {QStringLiteral("name"), target->name}};
            saveGoalSettings(settings, s);
            ctx.ok(result);
        });

    add(QStringLiteral("goal.deleteTemplate"), QStringLiteral("Delete a judge prompt template"), true,
        {required(QStringLiteral("template"), ArgType::String, QStringLiteral("template id from goal.options"))},
        [w = w](const CommandContext &ctx) {
            ApplicationSettings *settings = w->app->getSettings();
            GoalAgentSettings s = goalSettings(settings);
            const QString id = ctx.str(QStringLiteral("template"));
            if (id == QLatin1String(GoalAgentSettings::kDefaultTemplateId)) {
                ctx.fail(409, QStringLiteral("the built-in default template cannot be deleted"));
                return;
            }
            if (s.promptTemplates.removeIf([&id](const GoalPromptTemplate &t) { return t.id == id; }) == 0) {
                ctx.fail(404, QStringLiteral("no template '%1'; goal.options lists them").arg(id));
                return;
            }
            saveGoalSettings(settings, s);
            ctx.ok({{QStringLiteral("deleted"), id}, {QStringLiteral("templates"), templatesJson(s)}});
        });

    // Criteria presets: global ones in the goal settings, project ones under
    // <workspace>/.agents/.goals (local workspaces only).
    auto projectRootOf = [w = w](const CommandContext &ctx, QString *root) -> bool {
        if (ctx.has(QStringLiteral("workspace"))) {
            FolderAsWorkspaceDock *ws = workspaceArg(ctx, w);
            if (!ws)
                return false;
            *root = ws->rootPath();
        } else {
            *root = workspaceRoot(w);
        }
        return true;
    };

    add(QStringLiteral("goal.presets"), QStringLiteral("List the saved criteria presets, global and the project's"), false,
        {workspaceArgSpec()}, [w = w, projectRootOf](const CommandContext &ctx) {
            QString root;
            if (!projectRootOf(ctx, &root))
                return;
            const GoalAgentSettings s = goalSettings(w->app->getSettings());
            QJsonArray global;
            for (const GoalCriteriaPreset &p : s.criteriaPresets)
                global.append(QJsonObject{{QStringLiteral("name"), p.name},
                                          {QStringLiteral("criteria"), QJsonArray::fromStringList(p.criteria)}});
            QJsonArray project;
            const bool projectOk = ProjectGoalPresets::projectScopeAvailable(root, remote::isSshUri(root));
            if (projectOk) {
                for (const ProjectGoalPresets::Listed &p : ProjectGoalPresets::list(root)) {
                    QStringList criteria;
                    QString error;
                    QJsonObject o{{QStringLiteral("name"), p.name}};
                    if (ProjectGoalPresets::read(root, p.name, &criteria, &error))
                        o.insert(QStringLiteral("criteria"), QJsonArray::fromStringList(criteria));
                    else
                        o.insert(QStringLiteral("error"), error);
                    project.append(o);
                }
            }
            QJsonObject out{{QStringLiteral("global"), global}};
            if (projectOk) {
                out.insert(QStringLiteral("project"), project);
                out.insert(QStringLiteral("projectRoot"), displayPath(root));
            }
            ctx.ok(out);
        });

    add(QStringLiteral("goal.savePreset"),
        QStringLiteral("Save success criteria as a preset (a global one with the same name is replaced)"), true,
        {required(QStringLiteral("name"), ArgType::String, QStringLiteral("preset name")),
         required(QStringLiteral("criteria"), ArgType::Array, QStringLiteral("the criteria, in order")),
         oneOf(QStringLiteral("scope"), {QStringLiteral("global"), QStringLiteral("project")},
               QStringLiteral("global (default) or project (<workspace>/.agents/.goals, local workspaces)")),
         workspaceArgSpec()},
        [w = w, projectRootOf](const CommandContext &ctx) {
            const QString name = ctx.str(QStringLiteral("name")).trimmed();
            QStringList criteria;
            for (const QString &c : ctx.strings(QStringLiteral("criteria"))) {
                if (!c.trimmed().isEmpty())
                    criteria.append(c.trimmed());
            }
            if (name.isEmpty() || criteria.isEmpty()) {
                ctx.fail(400, QStringLiteral("give a name and at least one non-empty criterion"));
                return;
            }
            if (ctx.str(QStringLiteral("scope")) == QLatin1String("project")) {
                QString root;
                if (!projectRootOf(ctx, &root))
                    return;
                if (!ProjectGoalPresets::projectScopeAvailable(root, remote::isSshUri(root))) {
                    ctx.fail(409, QStringLiteral("project presets need an open local workspace"));
                    return;
                }
                QString error;
                if (!ProjectGoalPresets::save(root, name, criteria, &error)) {
                    ctx.fail(422, error);
                    return;
                }
                ctx.ok({{QStringLiteral("name"), name}, {QStringLiteral("scope"), QStringLiteral("project")}});
                return;
            }
            ApplicationSettings *settings = w->app->getSettings();
            GoalAgentSettings s = goalSettings(settings);
            bool replaced = false;
            for (GoalCriteriaPreset &p : s.criteriaPresets) {
                if (p.name.trimmed() == name) {
                    p.criteria = criteria;
                    replaced = true;
                }
            }
            if (!replaced) {
                GoalCriteriaPreset p;
                p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                p.name = name;
                p.criteria = criteria;
                s.criteriaPresets.append(p);
            }
            saveGoalSettings(settings, s);
            ctx.ok({{QStringLiteral("name"), name},
                    {QStringLiteral("scope"), QStringLiteral("global")},
                    {QStringLiteral("replaced"), replaced}});
        });

    add(QStringLiteral("goal.deletePreset"), QStringLiteral("Delete a criteria preset"), true,
        {required(QStringLiteral("name"), ArgType::String, QStringLiteral("preset name")),
         oneOf(QStringLiteral("scope"), {QStringLiteral("global"), QStringLiteral("project")},
               QStringLiteral("global (default) or project")),
         workspaceArgSpec()},
        [w = w, projectRootOf](const CommandContext &ctx) {
            const QString name = ctx.str(QStringLiteral("name")).trimmed();
            if (ctx.str(QStringLiteral("scope")) == QLatin1String("project")) {
                QString root;
                if (!projectRootOf(ctx, &root))
                    return;
                QString error;
                if (!ProjectGoalPresets::projectScopeAvailable(root, remote::isSshUri(root))
                    || !ProjectGoalPresets::remove(root, name, &error)) {
                    ctx.fail(404, error.isEmpty() ? QStringLiteral("no project preset '%1'").arg(name) : error);
                    return;
                }
                ctx.ok({{QStringLiteral("deleted"), name}});
                return;
            }
            ApplicationSettings *settings = w->app->getSettings();
            GoalAgentSettings s = goalSettings(settings);
            if (s.criteriaPresets.removeIf([&name](const GoalCriteriaPreset &p) { return p.name.trimmed() == name; }) == 0) {
                ctx.fail(404, QStringLiteral("no global preset '%1'; goal.presets lists them").arg(name));
                return;
            }
            saveGoalSettings(settings, s);
            ctx.ok({{QStringLiteral("deleted"), name}});
        });

    add(QStringLiteral("goal.start"),
        QStringLiteral("Run a goal on a session: send a prompt (or keep going in the conversation) until each success "
                       "criterion is met"),
        true,
        {sessionArgSpec(),
         required(QStringLiteral("criteria"), ArgType::Array, QStringLiteral("success criteria, checked in order")),
         optional(QStringLiteral("prompt"), ArgType::String,
                  QStringLiteral("first prompt; default: the composer text, else the goal attaches to the conversation")),
         optional(QStringLiteral("native"), ArgType::Bool,
                  QStringLiteral("send each criterion as the agent's own /goal command instead of a judge; default "
                                 "from goal.options")),
         optional(QStringLiteral("judgeAgent"), ArgType::String, QStringLiteral("agent id that judges; default from goal.options")),
         optional(QStringLiteral("maxIterations"), ArgType::Int, QStringLiteral("per criterion; default from goal.options")),
         optional(QStringLiteral("template"), ArgType::String, QStringLiteral("judge prompt template id")),
         optional(QStringLiteral("autoCompact"), ArgType::Bool, QStringLiteral("/compact once achieved")),
         optional(QStringLiteral("prefixGoal"), ArgType::Bool, QStringLiteral("prefix prompts the goal sends with /goal"))},
        [w = w, sessionOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            QString id;
            if (!sessionOf(ctx, &dock, &id))
                return;
            const QStringList criteria = ctx.strings(QStringLiteral("criteria"));
            if (criteria.isEmpty() || std::any_of(criteria.cbegin(), criteria.cend(),
                                                  [](const QString &c) { return c.trimmed().isEmpty(); })) {
                ctx.fail(400, QStringLiteral("give at least one non-empty criterion"));
                return;
            }
            const GoalAgentSettings s = goalSettings(w->app->getSettings());
            SendWithGoalResult res;
            res.successCriteriaList = criteria;
            res.agentId = ctx.str(QStringLiteral("judgeAgent"), s.agentId);
            res.maxIterations = int(std::clamp<qint64>(ctx.integer(QStringLiteral("maxIterations"), s.defaultMaxIterations),
                                                       GoalAgentSettings::kMaxIterationsMin,
                                                       GoalAgentSettings::kMaxIterationsMax));
            res.promptTemplateId = ctx.str(QStringLiteral("template"), s.resolvedPromptTemplateId());
            res.useNativeGoal = ctx.flag(QStringLiteral("native"), s.useNativeGoal);
            res.autoCompact = ctx.flag(QStringLiteral("autoCompact"), s.autoCompact);
            res.prefixGoal = ctx.flag(QStringLiteral("prefixGoal"), s.prefixGoal);
            AcpSessionView *view = dock->viewFor(id);
            if (ctx.has(QStringLiteral("prompt")) && view) {
                view->takeInputText();
                view->insertTextToInput(ctx.str(QStringLiteral("prompt")));
            }
            dock->setVisible(true);
            dock->raise();
            if (view)
                ctx.reveal(view);
            if (const QString error = dock->startGoal(id, res); !error.isEmpty()) {
                ctx.fail(409, error);
                return;
            }
            QJsonObject o{{QStringLiteral("session"), id}, {QStringLiteral("native"), res.useNativeGoal}};
            if (GoalAgent *goal = dock->goalFor(id))
                o.insert(QStringLiteral("goal"), goalStatusName(goal->status()));
            ctx.ok(o);
        });

    add(QStringLiteral("goal.state"), QStringLiteral("Read the goal running on a session: criteria, iterations, verdicts"),
        false, {sessionArgSpec()}, [sessionOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            QString id;
            if (!sessionOf(ctx, &dock, &id))
                return;
            GoalAgent *goal = dock->goalFor(id);
            if (!goal) {
                ctx.ok({{QStringLiteral("session"), id}, {QStringLiteral("goal"), QStringLiteral("none")}});
                return;
            }
            QJsonArray criteria;
            for (const GoalAgent::Criterion &c : goal->criteria()) {
                QJsonObject o{{QStringLiteral("text"), c.text},
                              {QStringLiteral("iteration"), c.iteration},
                              {QStringLiteral("status"),
                               c.status == GoalAgent::Archived         ? QStringLiteral("done")
                               : c.status == GoalAgent::CriterionActive ? QStringLiteral("active")
                                                                        : QStringLiteral("pending")}};
                if (!c.verdict.isEmpty())
                    o.insert(QStringLiteral("verdict"), c.verdict);
                criteria.append(o);
            }
            ctx.ok({{QStringLiteral("session"), id},
                    {QStringLiteral("goal"), goalStatusName(goal->status())},
                    {QStringLiteral("current"), goal->currentCriterionIndex()},
                    {QStringLiteral("maxIterations"), goal->maxIterations()},
                    {QStringLiteral("criteria"), criteria}});
        });

    add(QStringLiteral("goal.stop"), QStringLiteral("Stop the goal running on a session (the turn keeps going)"), true,
        {sessionArgSpec()}, [sessionOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            QString id;
            if (!sessionOf(ctx, &dock, &id))
                return;
            GoalAgent *goal = dock->goalFor(id);
            if (!goal || goal->status() != GoalAgent::Active) {
                ctx.fail(409, QStringLiteral("no goal is running on this session"));
                return;
            }
            dock->stopGoalForSession(id);
            if (AcpSessionView *view = dock->viewFor(id))
                ctx.reveal(view);
            ctx.ok({{QStringLiteral("session"), id}});
        });

    add(QStringLiteral("goal.draftPrompt"),
        QStringLiteral("Write a prompt from success criteria with the judge agent (Improve on an empty composer) and "
                       "put it in the session's composer"),
        true,
        {sessionArgSpec(), required(QStringLiteral("criteria"), ArgType::Array, QStringLiteral("the success criteria")),
         optional(QStringLiteral("judgeAgent"), ArgType::String, QStringLiteral("agent that writes it; default from goal.options")),
         optional(QStringLiteral("template"), ArgType::String, QStringLiteral("prompt template id; default from goal.options"))},
        [w = w, sessionOf](const CommandContext &ctx) {
            AiAgentDock *dock = nullptr;
            QString id;
            if (!sessionOf(ctx, &dock, &id))
                return;
            QStringList criteria;
            for (const QString &c : ctx.strings(QStringLiteral("criteria"))) {
                if (!c.trimmed().isEmpty())
                    criteria.append(c.trimmed());
            }
            if (criteria.isEmpty()) {
                ctx.fail(400, QStringLiteral("give at least one non-empty criterion"));
                return;
            }
            AcpAgentManager *manager = w->app->getAiAgentManager();
            const GoalAgentSettings s = goalSettings(w->app->getSettings());
            GoalDraftGenerator::Request req;
            req.criteria = criteria.join(QLatin1Char('\n'));
            req.agentId = ctx.str(QStringLiteral("judgeAgent"), s.agentId);
            if (req.agentId.isEmpty() && manager && manager->registry())
                req.agentId = manager->registry()->defaultAgentId();
            req.promptTemplateId = ctx.str(QStringLiteral("template"), s.resolvedPromptTemplateId());
            const AcpConnection *conn = dock->connectionFor(id);
            req.workingDirectory = conn ? conn->workingDirectory() : dock->workingDirectory();
            req.targetModel = dock->modelFor(id);
            req.executionContext = conn ? conn->executionContext() : nullptr;
            AcpSessionView *view = dock->viewFor(id);
            dock->setVisible(true);
            dock->raise();
            if (view)
                ctx.reveal(view);

            auto *generator = new GoalDraftGenerator(manager, w->app->getSettings(), dock);
            auto done = std::make_shared<bool>(false);
            QPointer<AcpSessionView> target(view);
            auto settle = [ctx, generator, done, target](int status, const QString &textOrError) {
                if (*done)
                    return;
                *done = true;
                generator->deleteLater();
                if (status != 200) {
                    ctx.fail(status, textOrError);
                    return;
                }
                if (target)
                    target->insertTextToInput(textOrError.trimmed());
                ctx.ok({{QStringLiteral("text"), textOrError.trimmed()}});
            };
            QObject::connect(generator, &GoalDraftGenerator::finished, generator,
                             [settle](const QString &draft) { settle(200, draft); });
            QObject::connect(generator, &GoalDraftGenerator::errorOccurred, generator,
                             [settle](const QString &message) { settle(422, message); });
            QTimer::singleShot(300000, generator, [settle, generator] {
                generator->cancel();
                settle(504, QStringLiteral("no prompt within 5 minutes"));
            });
            if (!generator->start(req))
                settle(422, QStringLiteral("the draft could not start; check the judge agent in goal.options"));
        });
}

void Registrar::schedule()
{
    auto registryOf = [w = w](const CommandContext &ctx) -> ScheduledTaskRegistry * {
        ScheduledTaskRunner *runner = w->app->getScheduledTaskRunner();
        if (!runner || !runner->registry())
            ctx.fail(409, QStringLiteral("scheduled tasks are not available"));
        return runner ? runner->registry() : nullptr;
    };
    auto fill = [](const CommandContext &ctx, ScheduledTaskDefinition *t) -> QString {
        if (ctx.has(QStringLiteral("name")))
            t->name = ctx.str(QStringLiteral("name")).trimmed();
        if (ctx.has(QStringLiteral("cron")))
            t->cron = ctx.str(QStringLiteral("cron")).trimmed();
        if (ctx.has(QStringLiteral("agent")))
            t->agentId = ctx.str(QStringLiteral("agent"));
        if (ctx.has(QStringLiteral("cwd")))
            t->cwd = ctx.str(QStringLiteral("cwd"));
        if (ctx.has(QStringLiteral("prompt")))
            t->prompt = ctx.str(QStringLiteral("prompt"));
        if (ctx.has(QStringLiteral("enabled")))
            t->enabled = ctx.flag(QStringLiteral("enabled"));
        if (ctx.has(QStringLiteral("skipIfRunning")))
            t->skipIfRunning = ctx.flag(QStringLiteral("skipIfRunning"));
        if (ctx.has(QStringLiteral("timeoutMinutes")))
            t->timeoutMinutes = int(std::max<qint64>(0, ctx.integer(QStringLiteral("timeoutMinutes"))));
        if (ctx.has(QStringLiteral("goalCriteria"))) {
            t->goalConfig.criteriaList = ctx.strings(QStringLiteral("goalCriteria"));
            t->hasGoalConfig = !t->goalConfig.criteriaList.isEmpty();
        }
        if (t->name.isEmpty() || t->cron.isEmpty() || t->prompt.trimmed().isEmpty() || t->cwd.isEmpty())
            return QStringLiteral("a task needs name, cron, cwd and prompt");
        return QString();
    };
    const QList<Arg> fields{
        optional(QStringLiteral("name"), ArgType::String, QStringLiteral("task name")),
        optional(QStringLiteral("cron"), ArgType::String, QStringLiteral("5-field cron, e.g. \"0 9 * * 1-5\"")),
        optional(QStringLiteral("agent"), ArgType::String, QStringLiteral("agent id; default the default agent")),
        optional(QStringLiteral("cwd"), ArgType::String, QStringLiteral("folder the agent works in")),
        optional(QStringLiteral("prompt"), ArgType::String, QStringLiteral("prompt sent when it fires")),
        optional(QStringLiteral("enabled"), ArgType::Bool, QStringLiteral("fires on schedule; default true")),
        optional(QStringLiteral("skipIfRunning"), ArgType::Bool,
                 QStringLiteral("skip a run while the previous one works; default true")),
        optional(QStringLiteral("timeoutMinutes"), ArgType::Int, QStringLiteral("close the session after; 0 = never")),
        optional(QStringLiteral("goalCriteria"), ArgType::Array, QStringLiteral("run it as a goal with these criteria"))};

    add(QStringLiteral("schedule.list"), QStringLiteral("List the scheduled AI tasks"), false, {},
        [registryOf](const CommandContext &ctx) {
            ScheduledTaskRegistry *registry = registryOf(ctx);
            if (!registry)
                return;
            QJsonArray list;
            for (const ScheduledTaskDefinition &t : registry->tasks())
                list.append(taskJson(t));
            ctx.ok({{QStringLiteral("tasks"), list}});
        });

    add(QStringLiteral("schedule.add"), QStringLiteral("Add a scheduled AI task (a prompt sent to an agent on a cron)"),
        true, fields, [w = w, registryOf, fill](const CommandContext &ctx) {
            ScheduledTaskRegistry *registry = registryOf(ctx);
            if (!registry)
                return;
            ScheduledTaskDefinition t;
            t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            t.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
            t.cwd = w->currentWorkspaceRoot();
            if (AcpAgentManager *manager = w->app->getAiAgentManager())
                t.agentId = manager->registry()->defaultAgentId();
            if (const QString error = fill(ctx, &t); !error.isEmpty()) {
                ctx.fail(400, error);
                return;
            }
            if (!registry->addTask(t)) {
                ctx.fail(422, QStringLiteral("the task was not saved (check the cron expression)"));
                return;
            }
            ctx.ok(taskJson(t));
        });

    QList<Arg> updateArgs{required(QStringLiteral("id"), ArgType::String, QStringLiteral("task id from schedule.list"))};
    updateArgs.append(fields);
    add(QStringLiteral("schedule.update"), QStringLiteral("Change a scheduled task (unnamed fields keep their value)"),
        true, updateArgs, [registryOf, fill](const CommandContext &ctx) {
            ScheduledTaskRegistry *registry = registryOf(ctx);
            if (!registry)
                return;
            ScheduledTaskDefinition t = registry->task(ctx.str(QStringLiteral("id")));
            if (t.id.isEmpty()) {
                ctx.fail(404, QStringLiteral("no task %1; schedule.list lists them").arg(ctx.str(QStringLiteral("id"))));
                return;
            }
            if (const QString error = fill(ctx, &t); !error.isEmpty()) {
                ctx.fail(400, error);
                return;
            }
            if (!registry->updateTask(t)) {
                ctx.fail(422, QStringLiteral("the task was not saved (check the cron expression)"));
                return;
            }
            ctx.ok(taskJson(t));
        });

    add(QStringLiteral("schedule.remove"), QStringLiteral("Delete a scheduled task"), true,
        {required(QStringLiteral("id"), ArgType::String, QStringLiteral("task id"))},
        [registryOf](const CommandContext &ctx) {
            ScheduledTaskRegistry *registry = registryOf(ctx);
            if (!registry)
                return;
            if (!registry->removeTask(ctx.str(QStringLiteral("id")))) {
                ctx.fail(404, QStringLiteral("no task %1").arg(ctx.str(QStringLiteral("id"))));
                return;
            }
            ctx.ok();
        });

    add(QStringLiteral("schedule.run"), QStringLiteral("Run a scheduled task now (opens its session)"), true,
        {required(QStringLiteral("id"), ArgType::String, QStringLiteral("task id"))},
        [w = w, registryOf](const CommandContext &ctx) {
            ScheduledTaskRegistry *registry = registryOf(ctx);
            if (!registry)
                return;
            const QString id = ctx.str(QStringLiteral("id"));
            if (registry->task(id).id.isEmpty()) {
                ctx.fail(404, QStringLiteral("no task %1").arg(id));
                return;
            }
            w->app->getScheduledTaskRunner()->manualTrigger(id);
            ctx.ok({{QStringLiteral("note"), QStringLiteral("started; ai.sessions shows its session")}});
        });
}

} // namespace control::commands
