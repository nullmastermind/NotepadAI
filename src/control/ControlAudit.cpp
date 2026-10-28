/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlAudit.h"

#include "ControlCommands.h"

#include <QAction>
#include <QHash>
#include <QMainWindow>
#include <QRegularExpression>
#include <QTextStream>

namespace control {

QStringList auditProblems(QMainWindow *window, const CommandRegistry &commands)
{
    QStringList problems;
    static const QRegularExpression kName(QStringLiteral("^[a-z][a-zA-Z0-9]*(\\.[a-z][a-zA-Z0-9]*)+$"));
    for (const Command &c : commands.all()) {
        if (!kName.match(c.name).hasMatch())
            problems.append(QStringLiteral("command '%1': name must be domain.verb in camelCase").arg(c.name));
        if (c.summary.trimmed().isEmpty())
            problems.append(QStringLiteral("command '%1': no summary").arg(c.name));
        if (!c.handler)
            problems.append(QStringLiteral("command '%1': no handler").arg(c.name));
        QStringList seen;
        for (const Arg &a : c.args) {
            if (a.description.trimmed().isEmpty())
                problems.append(QStringLiteral("command '%1': argument '%2' has no description").arg(c.name, a.name));
            if (a.type == ArgType::Enum && a.values.isEmpty())
                problems.append(QStringLiteral("command '%1': enum argument '%2' lists no values").arg(c.name, a.name));
            if (seen.contains(a.name))
                problems.append(QStringLiteral("command '%1': argument '%2' declared twice").arg(c.name, a.name));
            seen.append(a.name);
        }
    }

    if (window) {
        QHash<QString, int> names;
        const QList<QAction *> actions = window->findChildren<QAction *>();
        for (QAction *a : actions) {
            const QString name = a->objectName();
            // _q_*: Qt's own (a line edit's clear button), never menu actions.
            if (name.isEmpty() || a->isSeparator() || name.startsWith(QLatin1String("_q_")))
                continue;
            ++names[name];
            // "-": a dialog only the user can answer, no command does its job.
            const QString target = commandFor(a);
            if (!target.isEmpty() && target != QLatin1String("-")
                && !commands.find(target.section(QLatin1Char(' '), 0, 0)))
                problems.append(QStringLiteral("menu action '%1' names command '%2', which does not exist")
                                    .arg(name, target));
        }
        for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
            if (it.value() > 1)
                problems.append(QStringLiteral("menu action id '%1' is used by %2 actions").arg(it.key()).arg(it.value()));
        }
    }
    return problems;
}

int runAudit(QMainWindow *window, const CommandRegistry &commands)
{
    const QStringList problems = auditProblems(window, commands);
    QTextStream out(stdout);
    for (const QString &p : problems)
        out << p << '\n';
    out << "control-api-audit: " << commands.all().size() << " command(s), " << problems.size() << " problem(s)\n";
    out.flush();
    return int(problems.size());
}

} // namespace control
