/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_COMMANDS_H
#define CONTROL_COMMANDS_H

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>

class QAction;
class QObject;
class QWidget;

namespace control {

namespace http { struct Request; }

// What an argument holds. Int/Number accept JSON numbers; Enum a string from
// Arg::values; Any anything.
enum class ArgType : std::uint8_t { String, Int, Number, Bool, Array, Object, Enum, Any };

struct Arg
{
    QString name;
    ArgType type = ArgType::String;
    bool required = false;
    QString description;
    QStringList values; // Enum
    bool secret = false; // masked in the activity log
};

class CommandContext;
using CommandHandler = std::function<void(const CommandContext &ctx)>;

// One operation an external agent can run (docs/adr/0007). `changes`: it
// alters something (logged, shown); else a silent read that also answers GET.
struct Command
{
    QString name;    // "files.open"
    QString summary; // one sentence, imperative: "Open a file in a tab"
    bool changes = true;
    QList<Arg> args;
    CommandHandler handler;
};

// The commands, registered once at startup; lookups are one hash find.
class CommandRegistry
{
public:
    void add(Command command);
    const Command *find(const QString &name) const;
    const QList<Command> &all() const { return m_commands; }

    // Checks `args` against the command (unknown, missing, wrong type, value
    // not allowed) and converts numbers given as strings by GET. Returns the
    // error, empty when valid.
    static QString validate(const Command &command, QJsonObject &args);
    // Arguments of a GET: each declared argument read from the query string.
    static QJsonObject queryArgs(const Command &command, const http::Request &request);
    static QJsonObject describe(const Command &command);
    // Markdown reference of every command, grouped by domain (the skill's tail).
    QByteArray reference() const;

private:
    QList<Command> m_commands;
    QHash<QString, qsizetype> m_index;
};

// The outcome of one command, answered exactly once (copies share it), now
// or later (async work captures the context by value).
class CommandContext
{
public:
    struct Hooks
    {
        // Sends the HTTP response.
        std::function<void(int status, const QJsonObject &body)> respond;
        // Called once with the outcome of a changing command (log, events).
        std::function<void(bool ok, const QString &error)> finished;
        // Shows a widget to the user and flashes it.
        std::function<void(QWidget *)> reveal;
    };

    CommandContext(const Command *command, QJsonObject args, Hooks hooks);

    const Command &command() const { return *m_state->command; }
    const QJsonObject &args() const { return m_state->args; }
    bool has(const QString &name) const { return m_state->args.contains(name); }
    QString str(const QString &name, const QString &fallback = QString()) const;
    qint64 integer(const QString &name, qint64 fallback = 0) const;
    double number(const QString &name, double fallback = 0) const;
    bool flag(const QString &name, bool fallback = false) const;
    QJsonArray array(const QString &name) const;
    QJsonObject object(const QString &name) const;
    QStringList strings(const QString &name) const;

    // {"ok": true, ...result}.
    void ok(const QJsonObject &result = {}) const;
    // {"ok": false, "error": message, ...extra}. 404: no such thing; 409: not
    // now, or an answer is missing (name the argument); 422: it failed.
    void fail(int status, const QString &message, const QJsonObject &extra = {}) const;
    bool answered() const { return m_state->answered; }
    // Unanswered and no copy kept for later: the handler forgot to answer.
    bool abandoned() const { return !m_state->answered && m_state.use_count() == 1; }

    // Brings `widget` to the front (its tab current, its panel shown) and
    // flashes it. Commands call it for what they changed.
    void reveal(QWidget *widget) const;

private:
    struct State
    {
        const Command *command = nullptr;
        QJsonObject args;
        Hooks hooks;
        bool answered = false;
    };
    std::shared_ptr<State> m_state;
};

// Text of a JSON value for messages: a string as itself, else compact JSON.
QString argText(const QJsonValue &value);

// The tab id of an ADS dock widget (docs/adr/0007): assigned on first use,
// kept for the dock's lifetime; 0 for null.
int tabIdOf(QObject *dockWidget);

// A menu action that opens a dialog names the command doing its job (with an
// example of its arguments); menu.run refuses it and points there instead.
void setCommandFor(QAction *action, const QString &command);
QString commandFor(const QAction *action);

} // namespace control

#endif // CONTROL_COMMANDS_H
