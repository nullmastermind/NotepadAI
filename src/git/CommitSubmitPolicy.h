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

#ifndef COMMIT_SUBMIT_POLICY_H
#define COMMIT_SUBMIT_POLICY_H

#include <QString>

// Commit button is enabled whenever the repo has something to commit (staged,
// unstaged, or amend), independent of whether a message has been typed.
// An empty message is no longer a disable reason — it routes to the ACP
// agent picker instead (see commitUsesAgentPicker).
inline bool commitSubmitEnabled(bool hasRepo, bool hasConflicts, bool somethingToCommit)
{
    return hasRepo && !hasConflicts && somethingToCommit;
}

// `trimmedMessage` must already be trimmed. Amend with an empty message stays
// on the normal git-amend path (reuse HEAD message).
inline bool commitUsesAgentPicker(const QString &trimmedMessage, bool amend)
{
    return trimmedMessage.isEmpty() && !amend;
}

#endif // COMMIT_SUBMIT_POLICY_H
