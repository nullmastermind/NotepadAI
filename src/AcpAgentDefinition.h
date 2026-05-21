/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#ifndef ACP_AGENT_DEFINITION_H
#define ACP_AGENT_DEFINITION_H

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QStringList>

// Plain data describing one configured ACP agent (built-in or user-defined).
// Intentionally not a QObject so it can be copied and stored by value.
struct AcpAgentDefinition
{
    QString id;
    QString name;
    QString command;
    QStringList args;
    QMap<QString, QString> env;
    QString icon;
    bool builtin = false;
};

inline QJsonObject acpAgentDefinitionToJson(const AcpAgentDefinition &def)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), def.id);
    obj.insert(QStringLiteral("name"), def.name);
    obj.insert(QStringLiteral("command"), def.command);

    QJsonArray argsArray;
    for (const QString &arg : def.args) {
        argsArray.append(arg);
    }
    obj.insert(QStringLiteral("args"), argsArray);

    QJsonObject envObj;
    for (auto it = def.env.constBegin(); it != def.env.constEnd(); ++it) {
        envObj.insert(it.key(), it.value());
    }
    obj.insert(QStringLiteral("env"), envObj);

    obj.insert(QStringLiteral("icon"), def.icon);
    obj.insert(QStringLiteral("builtin"), def.builtin);
    return obj;
}

inline AcpAgentDefinition acpAgentDefinitionFromJson(const QJsonObject &obj)
{
    AcpAgentDefinition def;
    def.id = obj.value(QStringLiteral("id")).toString();
    def.name = obj.value(QStringLiteral("name")).toString();
    def.command = obj.value(QStringLiteral("command")).toString();

    const QJsonArray argsArray = obj.value(QStringLiteral("args")).toArray();
    def.args.reserve(argsArray.size());
    for (const auto &v : argsArray) {
        def.args.append(v.toString());
    }

    const QJsonObject envObj = obj.value(QStringLiteral("env")).toObject();
    for (auto it = envObj.constBegin(); it != envObj.constEnd(); ++it) {
        def.env.insert(it.key(), it.value().toString());
    }

    def.icon = obj.value(QStringLiteral("icon")).toString();
    def.builtin = obj.value(QStringLiteral("builtin")).toBool(false);
    return def;
}

#endif // ACP_AGENT_DEFINITION_H
