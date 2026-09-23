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

#ifndef GOAL_CONVERSATION_SUMMARY_H
#define GOAL_CONVERSATION_SUMMARY_H

#include "AcpSessionModel.h"

#include <QString>
#include <QVector>

// Target-session XML for the Goal judge / draft enhancer.
// User + assistant text only (thought/system omitted from the XML body).
// Tool calls: toolCallLooksFileMutating — name or title needles, else
// ACP kind edit/delete/move; also bash/powershell/pwsh unless
// rawInput.command contains grep. Drops timeline before the latest
// goal-achieved marker or user `/compact` (the marker itself is kept).
// startIndex is still applied.
class GoalConversationSummary
{
public:
    static QString fromModel(const AcpSessionModel *model, int startIndex = 0);

    // Developer-typed requests after the latest round boundary, plus the
    // resolved original text. Goal-agent prompts are omitted.
    static QString developerRequestsXml(const QVector<AcpMessage> &messages,
                                        const QString &originalText);
    static QString latestDeveloperText(const QVector<AcpMessage> &messages);
    static int latestRoundBoundaryIndex(const QVector<AcpMessage> &messages);
};

#endif // GOAL_CONVERSATION_SUMMARY_H
