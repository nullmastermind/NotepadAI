/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_EFFECTS_H
#define CONTROL_EFFECTS_H

#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

#include <functional>

namespace control {

// What is open: every window and dock (ids::rootTable) that counts as present.
// Diffing two snapshots gives a command's effects.
class ControlSnapshot
{
public:
    // Whether a root counts as open; default: visible. Docks need the ADS
    // notion of "closed" (a dock behind another tab is hidden but open).
    using Presence = std::function<bool(const QWidget *)>;

    static ControlSnapshot take(const Presence &presence = {});

    // {"opened":[{id,kind,title}], "closed":[...], "focus":id, "modal":id, "popup":id}
    QJsonObject diff(const ControlSnapshot &after) const;

private:
    struct Entry
    {
        QPointer<QWidget> widget;
        QString id;
        QString kind;
        QString title;
    };
    QList<Entry> m_entries;
};

// Runs `act` from a 0-ms timer on `context`'s thread and calls `done` once:
// finished=true when `act` returned, false when a modal dialog or popup menu
// `act` opened is waiting for input first (from inside its exec()). A nested
// loop that only waits (processEvents, a QEventLoop) does not settle it.
void runUntilSettled(QObject *context, std::function<void()> act, std::function<void(bool finished)> done);

// Kind of a root for effects: "window", "dialog", "dock", "menu".
QString rootKind(const QWidget *root);
// Window title, dock title, menu title.
QString rootTitle(const QWidget *root);

} // namespace control

#endif // CONTROL_EFFECTS_H
