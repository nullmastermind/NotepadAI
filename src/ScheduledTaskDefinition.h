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

#ifndef SCHEDULED_TASK_DEFINITION_H
#define SCHEDULED_TASK_DEFINITION_H

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

// Optional goal-agent configuration for a scheduled task.
struct ScheduledTaskGoalConfig
{
    QStringList criteriaList;
    QString agentId;
    int maxIterations = 100;
    QString promptTemplateId;
};

// Plain data describing one scheduled AI task. Intentionally not a QObject
// so it can be copied and stored by value.
struct ScheduledTaskDefinition
{
    QString id;
    QString name;
    QString cron;
    QString agentId;
    QString cwd;
    QString prompt;
    bool enabled = true;
    bool skipIfRunning = true;
    bool hasGoalConfig = false;
    ScheduledTaskGoalConfig goalConfig;
    int timeoutMinutes = 0;
    QString lastRunTime;
    QString createdAt;
};

inline QJsonObject scheduledTaskGoalConfigToJson(const ScheduledTaskGoalConfig &cfg)
{
    QJsonObject obj;
    QJsonArray arr;
    for (const QString &c : cfg.criteriaList) {
        arr.append(c);
    }
    obj.insert(QStringLiteral("criteriaList"), arr);
    obj.insert(QStringLiteral("agentId"), cfg.agentId);
    obj.insert(QStringLiteral("maxIterations"), cfg.maxIterations);
    obj.insert(QStringLiteral("promptTemplateId"), cfg.promptTemplateId);
    return obj;
}

inline ScheduledTaskGoalConfig scheduledTaskGoalConfigFromJson(const QJsonObject &obj)
{
    ScheduledTaskGoalConfig cfg;
    const QJsonArray arr = obj.value(QStringLiteral("criteriaList")).toArray();
    cfg.criteriaList.reserve(arr.size());
    for (const auto &v : arr) {
        cfg.criteriaList.append(v.toString());
    }
    cfg.agentId = obj.value(QStringLiteral("agentId")).toString();
    cfg.maxIterations = obj.value(QStringLiteral("maxIterations")).toInt(100);
    cfg.promptTemplateId = obj.value(QStringLiteral("promptTemplateId")).toString();
    return cfg;
}

inline QJsonObject scheduledTaskDefinitionToJson(const ScheduledTaskDefinition &def)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), def.id);
    obj.insert(QStringLiteral("name"), def.name);
    obj.insert(QStringLiteral("cron"), def.cron);
    obj.insert(QStringLiteral("agentId"), def.agentId);
    obj.insert(QStringLiteral("cwd"), def.cwd);
    obj.insert(QStringLiteral("prompt"), def.prompt);
    obj.insert(QStringLiteral("enabled"), def.enabled);
    obj.insert(QStringLiteral("skipIfRunning"), def.skipIfRunning);
    if (def.hasGoalConfig) {
        obj.insert(QStringLiteral("goalConfig"), scheduledTaskGoalConfigToJson(def.goalConfig));
    } else {
        obj.insert(QStringLiteral("goalConfig"), QJsonValue::Null);
    }
    obj.insert(QStringLiteral("timeoutMinutes"), def.timeoutMinutes);
    obj.insert(QStringLiteral("lastRunTime"), def.lastRunTime);
    obj.insert(QStringLiteral("createdAt"), def.createdAt);
    return obj;
}

inline ScheduledTaskDefinition scheduledTaskDefinitionFromJson(const QJsonObject &obj)
{
    ScheduledTaskDefinition def;
    def.id = obj.value(QStringLiteral("id")).toString();
    def.name = obj.value(QStringLiteral("name")).toString();
    def.cron = obj.value(QStringLiteral("cron")).toString();
    def.agentId = obj.value(QStringLiteral("agentId")).toString();
    def.cwd = obj.value(QStringLiteral("cwd")).toString();
    def.prompt = obj.value(QStringLiteral("prompt")).toString();
    def.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
    def.skipIfRunning = obj.value(QStringLiteral("skipIfRunning")).toBool(true);
    const QJsonValue goalVal = obj.value(QStringLiteral("goalConfig"));
    if (goalVal.isObject()) {
        def.hasGoalConfig = true;
        def.goalConfig = scheduledTaskGoalConfigFromJson(goalVal.toObject());
    } else {
        def.hasGoalConfig = false;
    }
    def.timeoutMinutes = obj.value(QStringLiteral("timeoutMinutes")).toInt(0);
    def.lastRunTime = obj.value(QStringLiteral("lastRunTime")).toString();
    def.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    return def;
}

#endif // SCHEDULED_TASK_DEFINITION_H
