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
#include <QPointer>
#include <QTimer>

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

    // Check skipIfRunning (cron-timer guard) — keyed by session id so a
    // sibling slot on the same project dock is not mistaken for this task.
    auto it = m_activeSessions.find(taskId);
    if (it != m_activeSessions.end()) {
        const QString &previousId = it.value();
        if (m_manager->modelFor(previousId) == nullptr) {
            m_activeSessions.erase(it);
        } else if (m_manager->sessionIsBusy(previousId) && task.skipIfRunning) {
            return;
        } else if (!m_manager->sessionIsBusy(previousId)) {
            m_manager->closeSession(previousId);
            m_activeSessions.remove(taskId);
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

    // Open agent with the resolved context (remote or nullptr for local).
    // Background spawn: join the project group without stealing current.
    AiAgentDock *dock = m_manager->openAgent(task.agentId, effectiveCwd,
                                             false, context, false);
    if (!dock) {
        return;
    }

    const QString sessionId = dock->newestSessionId();
    m_activeSessions[taskId] = sessionId;
    emit taskFired(dock);

    AcpConnection *conn = m_manager->connectionFor(sessionId);
    AcpSessionModel *model = m_manager->modelFor(sessionId);
    if (!conn || !model) {
        return;
    }

    const QString prompt = task.prompt;
    const bool hasGoal = task.hasGoalConfig;
    const ScheduledTaskGoalConfig goalCfg = task.goalConfig;
    const int timeoutMin = task.timeoutMinutes;

    connect(conn, &AcpConnection::initialized, this,
            [this, sessionId, prompt, hasGoal, goalCfg, timeoutMin,
             dockGuard = QPointer<AiAgentDock>(dock)]() {
        AcpConnection *liveConn = m_manager->connectionFor(sessionId);
        AcpSessionModel *liveModel = m_manager->modelFor(sessionId);
        if (!liveConn || !liveModel)
            return;

        if (hasGoal && dockGuard) {
            GoalAgent *goal = new GoalAgent(m_manager, m_settings, dockGuard.data());
            goal->setTargetSession(liveConn, liveModel);
            dockGuard->attachGoalAgent(goal, sessionId);
            GoalAgent::StartRequest req;
            req.targetSessionId = sessionId;
            req.successCriteriaList = goalCfg.criteriaList;
            req.agentId = goalCfg.agentId;
            req.maxIterations = goalCfg.maxIterations;
            req.promptTemplateId = goalCfg.promptTemplateId;
            req.originalUserMessage = prompt;
            goal->start(req);
        }

        liveModel->appendUserMessage(prompt, {});
        liveConn->sendPrompt(prompt, {});

        if (timeoutMin > 0) {
            constexpr int kMsPerMinute = 60000;
            QTimer *timeout = new QTimer(this);
            timeout->setSingleShot(true);
            timeout->setInterval(timeoutMin * kMsPerMinute);
            connect(timeout, &QTimer::timeout, this,
                    [this, sessionId, timeout, dockGuard]() {
                        if (dockGuard)
                            dockGuard->stopGoalForSession(sessionId);
                        m_manager->closeSession(sessionId);
                        timeout->deleteLater();
                    });
            timeout->start();
        }
    }, Qt::SingleShotConnection);

    // Update lastRunTime
    ScheduledTaskDefinition updated = task;
    updated.lastRunTime = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_registry->updateTask(updated);
}
