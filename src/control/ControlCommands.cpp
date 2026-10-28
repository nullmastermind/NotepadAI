/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlCommands.h"

#include "ControlHttp.h"

#include <QAction>
#include <QJsonDocument>
#include <QMap>
#include <QVariant>

#include <cmath>

namespace control {

namespace {

constexpr char kCommandProperty[] = "nn_controlCommand";
constexpr char kTabIdProperty[] = "nn_controlTabId";

QString typeName(const Arg &arg)
{
    switch (arg.type) {
    case ArgType::String:
        return QStringLiteral("string");
    case ArgType::Int:
        return QStringLiteral("integer");
    case ArgType::Number:
        return QStringLiteral("number");
    case ArgType::Bool:
        return QStringLiteral("boolean");
    case ArgType::Array:
        return QStringLiteral("array");
    case ArgType::Object:
        return QStringLiteral("object");
    case ArgType::Enum:
        return QStringLiteral("enum");
    case ArgType::Any:
        return QStringLiteral("any");
    }
    return QString();
}

QString argList(const Command &command)
{
    if (command.args.isEmpty())
        return QStringLiteral("it takes no arguments");
    QStringList names;
    for (const Arg &a : command.args)
        names.append(a.required ? a.name + QLatin1Char('*') : a.name);
    return QStringLiteral("it takes: %1 (* required; GET /commands/%2 describes them)")
        .arg(names.join(QStringLiteral(", ")), command.name);
}

// A GET query value as the argument's type; Undefined when it does not parse.
QJsonValue fromQuery(const Arg &arg, const QString &text)
{
    switch (arg.type) {
    case ArgType::String:
    case ArgType::Enum:
        return text;
    case ArgType::Int:
    case ArgType::Number: {
        bool ok = false;
        const double v = text.toDouble(&ok);
        return ok ? QJsonValue(v) : QJsonValue(text);
    }
    case ArgType::Bool:
        if (text == QLatin1String("true") || text == QLatin1String("1"))
            return true;
        if (text == QLatin1String("false") || text == QLatin1String("0"))
            return false;
        return text;
    case ArgType::Array:
    case ArgType::Object:
    case ArgType::Any: {
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isArray())
            return doc.array();
        if (doc.isObject())
            return doc.object();
        return text;
    }
    }
    return text;
}

} // namespace

QString argText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isArray())
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    if (value.isObject())
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    return value.toVariant().toString();
}

int tabIdOf(QObject *dockWidget)
{
    static int nextId = 1;
    if (!dockWidget)
        return 0;
    const QVariant v = dockWidget->property(kTabIdProperty);
    if (v.isValid())
        return v.toInt();
    const int id = nextId++;
    dockWidget->setProperty(kTabIdProperty, id);
    return id;
}

void setCommandFor(QAction *action, const QString &command)
{
    action->setProperty(kCommandProperty, command);
}

QString commandFor(const QAction *action)
{
    return action->property(kCommandProperty).toString();
}

// --- CommandRegistry ---------------------------------------------------------

void CommandRegistry::add(Command command)
{
    Q_ASSERT_X(!m_index.contains(command.name), "CommandRegistry::add", "duplicate command name");
    m_index.insert(command.name, m_commands.size());
    m_commands.append(std::move(command));
}

const Command *CommandRegistry::find(const QString &name) const
{
    const auto it = m_index.constFind(name);
    return it == m_index.constEnd() ? nullptr : &m_commands.at(*it);
}

QString CommandRegistry::validate(const Command &command, QJsonObject &args)
{
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        bool known = false;
        for (const Arg &a : command.args)
            known = known || a.name == it.key();
        if (!known)
            return QStringLiteral("%1 has no argument \"%2\"; %3").arg(command.name, it.key(), argList(command));
    }
    for (const Arg &a : command.args) {
        const auto it = args.find(a.name);
        if (it == args.end() || it->isNull()) {
            if (a.required)
                return QStringLiteral("%1 needs \"%2\" (%3); %4")
                    .arg(command.name, a.name, a.description, argList(command));
            if (it != args.end())
                args.erase(it);
            continue;
        }
        const QJsonValue v = *it;
        QString wrong;
        switch (a.type) {
        case ArgType::String:
            if (!v.isString())
                wrong = QStringLiteral("a string");
            break;
        case ArgType::Int:
            if (!v.isDouble() || std::trunc(v.toDouble()) != v.toDouble())
                wrong = QStringLiteral("an integer");
            break;
        case ArgType::Number:
            if (!v.isDouble())
                wrong = QStringLiteral("a number");
            break;
        case ArgType::Bool:
            if (!v.isBool())
                wrong = QStringLiteral("true or false");
            break;
        case ArgType::Array:
            if (!v.isArray())
                wrong = QStringLiteral("an array");
            break;
        case ArgType::Object:
            if (!v.isObject())
                wrong = QStringLiteral("an object");
            break;
        case ArgType::Enum:
            if (!v.isString() || !a.values.contains(v.toString()))
                wrong = QStringLiteral("one of: %1").arg(a.values.join(QStringLiteral(", ")));
            break;
        case ArgType::Any:
            break;
        }
        if (!wrong.isEmpty())
            return QStringLiteral("\"%1\" of %2 must be %3, not %4").arg(a.name, command.name, wrong, argText(v));
    }
    return QString();
}

QJsonObject CommandRegistry::queryArgs(const Command &command, const http::Request &request)
{
    QJsonObject args;
    for (const Arg &a : command.args) {
        if (request.hasQuery(a.name.toUtf8()))
            args.insert(a.name, fromQuery(a, request.queryValue(a.name.toUtf8())));
    }
    return args;
}

QJsonObject CommandRegistry::describe(const Command &command)
{
    QJsonArray args;
    for (const Arg &a : command.args) {
        QJsonObject o{{QStringLiteral("name"), a.name},
                      {QStringLiteral("type"), typeName(a)},
                      {QStringLiteral("required"), a.required},
                      {QStringLiteral("description"), a.description}};
        if (!a.values.isEmpty())
            o.insert(QStringLiteral("values"), QJsonArray::fromStringList(a.values));
        args.append(o);
    }
    return {{QStringLiteral("name"), command.name},
            {QStringLiteral("summary"), command.summary},
            {QStringLiteral("changes"), command.changes},
            {QStringLiteral("args"), args}};
}

QByteArray CommandRegistry::reference() const
{
    // Domain = the name before the last dot ("ai.goal.start" -> "ai.goal").
    QMap<QString, QList<const Command *>> byDomain;
    for (const Command &c : m_commands)
        byDomain[c.name.left(c.name.lastIndexOf(QLatin1Char('.')))].append(&c);
    QString out = QStringLiteral("## Command reference\n\n"
                                 "Generated from the running app. `*` marks a required argument; "
                                 "(read) marks a command that changes nothing and also answers GET.\n");
    for (auto d = byDomain.constBegin(); d != byDomain.constEnd(); ++d) {
        out += QStringLiteral("\n### %1\n\n").arg(d.key());
        for (const Command *c : d.value()) {
            out += QStringLiteral("- `%1`%2: %3").arg(c->name, c->changes ? QString() : QStringLiteral(" (read)"),
                                                     c->summary);
            if (c->args.isEmpty()) {
                out += QLatin1Char('\n');
                continue;
            }
            out += QLatin1Char('\n');
            for (const Arg &a : c->args) {
                QString type = typeName(a);
                if (a.type == ArgType::Enum)
                    type = a.values.join(QLatin1Char('|'));
                out += QStringLiteral("  - `%1%2` (%3): %4\n")
                           .arg(a.name, a.required ? QStringLiteral("*") : QString(), type, a.description);
            }
        }
    }
    return out.toUtf8();
}

// --- CommandContext ----------------------------------------------------------

CommandContext::CommandContext(const Command *command, QJsonObject args, Hooks hooks)
    : m_state(std::make_shared<State>())
{
    m_state->command = command;
    m_state->args = std::move(args);
    m_state->hooks = std::move(hooks);
}

QString CommandContext::str(const QString &name, const QString &fallback) const
{
    const QJsonValue v = m_state->args.value(name);
    return v.isString() ? v.toString() : fallback;
}

qint64 CommandContext::integer(const QString &name, qint64 fallback) const
{
    const QJsonValue v = m_state->args.value(name);
    return v.isDouble() ? qint64(v.toDouble()) : fallback;
}

double CommandContext::number(const QString &name, double fallback) const
{
    const QJsonValue v = m_state->args.value(name);
    return v.isDouble() ? v.toDouble() : fallback;
}

bool CommandContext::flag(const QString &name, bool fallback) const
{
    const QJsonValue v = m_state->args.value(name);
    return v.isBool() ? v.toBool() : fallback;
}

QJsonArray CommandContext::array(const QString &name) const
{
    return m_state->args.value(name).toArray();
}

QJsonObject CommandContext::object(const QString &name) const
{
    return m_state->args.value(name).toObject();
}

QStringList CommandContext::strings(const QString &name) const
{
    QStringList out;
    const QJsonValue v = m_state->args.value(name);
    if (v.isString()) {
        out.append(v.toString());
        return out;
    }
    const QJsonArray a = v.toArray();
    out.reserve(a.size());
    for (const auto &item : a)
        out.append(argText(item));
    return out;
}

void CommandContext::ok(const QJsonObject &result) const
{
    if (m_state->answered)
        return;
    m_state->answered = true;
    QJsonObject body = result;
    body.insert(QStringLiteral("ok"), true);
    if (m_state->hooks.finished)
        m_state->hooks.finished(true, QString());
    if (m_state->hooks.respond)
        m_state->hooks.respond(200, body);
}

void CommandContext::fail(int status, const QString &message, const QJsonObject &extra) const
{
    if (m_state->answered)
        return;
    m_state->answered = true;
    QJsonObject body = extra;
    body.insert(QStringLiteral("ok"), false);
    body.insert(QStringLiteral("error"), message);
    if (m_state->hooks.finished)
        m_state->hooks.finished(false, message);
    if (m_state->hooks.respond)
        m_state->hooks.respond(status, body);
}

void CommandContext::reveal(QWidget *widget) const
{
    if (widget && m_state->hooks.reveal)
        m_state->hooks.reveal(widget);
}

} // namespace control
