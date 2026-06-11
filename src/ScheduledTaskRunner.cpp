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

#include "ScheduledTaskRunner.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

#include "AcpAgentManager.h"
#include "AcpConnection.h"
#include "AcpSessionModel.h"
#include "ApplicationSettings.h"
#include "CronExpression.h"
#include "GoalAgent.h"
#include "ScheduledTaskRegistry.h"
#include "docks/AiAgentDock.h"
#include "remote/ExecutionContext.h"
#include "remote/ExecutionContextRegistry.h"
#include "remote/RemoteExecutionContext.h"
#include "remote/SshProfile.h"

namespace {
Q_LOGGING_CATEGORY(lcScheduledTask, "notepadai.scheduledtask")
} // namespace

ScheduledTaskRunner::ScheduledTaskRunner(ScheduledTaskRegistry *registry,
                                         AcpAgentManager *manager,
                                         ApplicationSettings *settings,
                                         remote::ExecutionContextRegistry *contextRegistry,
                                         QObject *parent)
    : QObject(parent)
    , m_registry(registry)
    , m_manager(manager)
    , m_settings(settings)
    , m_contextRegistry(contextRegistry)
{
    m_timer.setInterval(60000); // 60 seconds
    connect(&m_timer, &QTimer::timeout, this, &ScheduledTaskRunner::onTick);
    connect(m_registry, &ScheduledTaskRegistry::changed, this, &ScheduledTaskRunner::onRegistryChanged);
}

void ScheduledTaskRunner::start()
{
    recalculateNextFireTimes();
    m_timer.start();
}

void ScheduledTaskRunner::stop()
{
    m_timer.stop();
}

void ScheduledTaskRunner::manualTrigger(const QString &taskId)
{
    fireTask(taskId);
}

void ScheduledTaskRunner::onTick()
{
    const QDateTime now = QDateTime::currentDateTime();
    const auto tasks = m_registry->tasks();

    for (const auto &task : tasks) {
        if (!task.enabled) {
            continue;
        }

        auto it = m_nextFireTimes.constFind(task.id);
        if (it == m_nextFireTimes.constEnd()) {
            continue;
        }

        if (now >= it.value()) {
            fireTask(task.id);

            // Recalculate next fire time for this task
            auto parsed = CronExpression::parse(task.cron);
            if (parsed && parsed->isValid()) {
                const QDateTime next = parsed->nextFireTime(now);
                if (next.isValid()) {
                    m_nextFireTimes[task.id] = next;
                } else {
                    m_nextFireTimes.remove(task.id);
                }
            }
        }
    }
}

void ScheduledTaskRunner::onRegistryChanged()
{
    recalculateNextFireTimes();
}

void ScheduledTaskRunner::recalculateNextFireTimes()
{
    m_nextFireTimes.clear();
    const QDateTime now = QDateTime::currentDateTime();
    const auto tasks = m_registry->tasks();

    for (const auto &task : tasks) {
        if (!task.enabled) {
            continue;
        }
        auto parsed = CronExpression::parse(task.cron);
        if (!parsed || !parsed->isValid()) {
            continue;
        }
        const QDateTime next = parsed->nextFireTime(now);
        if (next.isValid()) {
            m_nextFireTimes.insert(task.id, next);
        }
    }
}

void ScheduledTaskRunner::fireTask(const QString &taskId)
{
    const ScheduledTaskDefinition task = m_registry->task(taskId);
    if (task.id.isEmpty()) {
        return;
    }

    // Check skipIfRunning (cron-timer guard)
    auto it = m_activeSessions.find(taskId);
    if (it != m_activeSessions.end() && !it.value().isNull()) {
        if (it.value()->isBusy() && task.skipIfRunning) {
            return;
        }
        if (!it.value()->isBusy()) {
            it.value()->close();
        }
    }

    // Resolve execution context from the task's cwd (capture-at-spawn).
    // If cwd is an ssh:// URI → remote context; otherwise local (nullptr).
    remote::ExecutionContext *context = nullptr;
    QString effectiveCwd = task.cwd;

    if (remote::isSshUri(task.cwd)) {
        const remote::SshUri parsed = remote::parseSshUri(task.cwd);
        if (!parsed.valid) {
            qCWarning(lcScheduledTask)
                << "Skipping scheduled task" << task.name
                << ": invalid SSH URI" << task.cwd;
            return;
        }
        if (!m_contextRegistry) {
            qCWarning(lcScheduledTask)
                << "Skipping scheduled task" << task.name
                << ": no execution context registry available";
            return;
        }
        auto *remoteCtx = m_contextRegistry->remoteContext(parsed.profileId);
        if (!remoteCtx
            || remoteCtx->state() != remote::ExecutionContext::State::Connected) {
            qCWarning(lcScheduledTask)
                << "Skipping scheduled task" << task.name
                << ": remote workspace not connected";
            return;
        }
        context = remoteCtx;
        effectiveCwd = parsed.remotePath;
    } else {
        // Local path — check existence as before
        if (!task.cwd.isEmpty() && !QFileInfo::exists(task.cwd)) {
            return;
        }
    }

    // Open agent with the resolved context (remote or nullptr for local)
    AiAgentDock *dock = m_manager->openAgent(task.agentId, effectiveCwd,
                                             false, context);
    if (!dock) {
        return;
    }

    m_activeSessions[taskId] = dock;
    emit taskFired(dock);

    // Wait for initialized signal, then send prompt
    AcpConnection *conn = dock->connection();
    if (!conn) {
        return;
    }

    const QString prompt = task.prompt;
    const bool hasGoal = task.hasGoalConfig;
    const ScheduledTaskGoalConfig goalCfg = task.goalConfig;
    const int timeoutMin = task.timeoutMinutes;

    connect(conn, &AcpConnection::initialized, dock, [conn, prompt, hasGoal, goalCfg, dock, timeoutMin, this]() {
        if (hasGoal) {
            GoalAgent *goal = new GoalAgent(m_manager, m_settings, dock);
            goal->setTargetSession(conn, dock->model());
            dock->attachGoalAgent(goal);
            GoalAgent::StartRequest req;
            req.targetSessionId = dock->sessionId();
            req.successCriteriaList = goalCfg.criteriaList;
            req.agentId = goalCfg.agentId;
            req.maxIterations = goalCfg.maxIterations;
            req.promptTemplateId = goalCfg.promptTemplateId;
            req.originalUserMessage = prompt;
            goal->start(req);
        }

        dock->model()->appendUserMessage(prompt, {});
        conn->sendPrompt(prompt, {});

        if (timeoutMin > 0) {
            QTimer *timeout = new QTimer(dock);
            timeout->setSingleShot(true);
            timeout->setInterval(timeoutMin * 60000);
            connect(timeout, &QTimer::timeout, dock, [dock]() {
                dock->close();
            });
            timeout->start();
        }
    }, Qt::SingleShotConnection);

    // Update lastRunTime
    ScheduledTaskDefinition updated = task;
    updated.lastRunTime = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_registry->updateTask(updated);
}
