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

#ifndef SCHEDULED_TASK_RUNNER_H
#define SCHEDULED_TASK_RUNNER_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>

class AcpAgentManager;
class AiAgentDock;
class ApplicationSettings;
class ScheduledTaskRegistry;
namespace remote { class ExecutionContextRegistry; }

// Periodically checks scheduled tasks and fires them when their cron
// expression matches. Owned by NotepadNextApplication.
class ScheduledTaskRunner : public QObject
{
    Q_OBJECT

public:
    explicit ScheduledTaskRunner(ScheduledTaskRegistry *registry,
                                 AcpAgentManager *manager,
                                 ApplicationSettings *settings,
                                 remote::ExecutionContextRegistry *contextRegistry,
                                 QObject *parent = nullptr);

    void start();
    void stop();
    void manualTrigger(const QString &taskId);
    ScheduledTaskRegistry *registry() const { return m_registry; }

signals:
    void taskFired(AiAgentDock *dock);

private slots:
    void onTick();
    void onRegistryChanged();

private:
    void recalculateNextFireTimes();
    void fireTask(const QString &taskId);

    ScheduledTaskRegistry *m_registry;
    AcpAgentManager *m_manager;
    ApplicationSettings *m_settings;
    remote::ExecutionContextRegistry *m_contextRegistry;
    QTimer m_timer;

    QHash<QString, QString> m_activeSessions;
    QHash<QString, QDateTime> m_nextFireTimes;
};

#endif // SCHEDULED_TASK_RUNNER_H
