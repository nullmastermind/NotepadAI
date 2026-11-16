/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef ACP_CATCH_UP_H
#define ACP_CATCH_UP_H

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

// Catch-up for agents that run turns no client sees. Antigravity (`agy acp`)
// streams session/update only while a session/prompt is open; when a
// background command ends after `end_turn`, its completion and the agent's
// reply to it happen with no stream, and only a `session/load` replay shows
// them. This finds what that replay holds beyond what the client already has.
// Pure Qt Core.
namespace AcpCatchUp {

// What the client has of the session's latest turn.
struct Seen
{
    // Main-agent message text since the last prompt, in order.
    QString turnText;
    // Every tool call seen in this connection: id -> last status.
    QHash<QString, QString> toolStatus;
};

// The `update` objects of a session/load replay, in arrival order, minus what
// `seen` already holds:
// - agent message text after the replay's last user message, past the prefix
//   `seen.turnText` covers (a chunk the prefix ends inside is cut);
// - tool calls not in `seen.toolStatus` that come after the last user message
//   (calls before it are older turns), as `tool_call`;
// - known tool calls whose status moved to a final one, as `tool_call_update`
//   (anywhere: agy can replay a running call ahead of its prompt).
// Thoughts, user messages and session metadata are never returned. When the
// replayed text is not an extension of `seen.turnText`, no text is returned
// (a duplicate bubble is worse than a missed one).
QList<QJsonObject> missingUpdates(const QList<QJsonObject> &replay, const Seen &seen);

} // namespace AcpCatchUp

#endif // ACP_CATCH_UP_H
