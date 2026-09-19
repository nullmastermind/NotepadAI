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

#ifndef ACP_TRANSCRIPT_TRUNCATION_H
#define ACP_TRANSCRIPT_TRUNCATION_H

#include <QVector>

#include <cstdint>

#include "AcpSessionModel.h"

// View-side transcript cap. The model/history is never mutated — AcpSessionView
// uses this to decide which timeline entries to instantiate as widgets.
//
// 256 is a TOTAL render cap: hide the oldest hideable agent events until
// visibleCount == 256 (or until nothing hideable remains). Never a per-turn
// "collapse everything" — that dropped a 400-event transcript down to a
// handful of summaries.
//
// Never hidden:
//   - user / goal (user+fromGoalAgent) / system messages
//   - the last agent event of every turn (the summary), even of old turns,
//     even if those pins alone exceed 256
//
// Each hidden run emits a gap sitting where the events were (between that
// turn's user/goal and its last agent) — never one blob at the head.
//
// Complexity: O(n) time, one pass to mark last-agents + one pass to hide +
// one pass to emit gaps.
namespace AcpTranscriptTruncation {

constexpr int kMaxVisibleEvents = 256;
// Live path only: don't start collapsing at 257 (avoids a sudden layout jump
// right as a turn crosses the cap). Hydrate uses kMaxVisibleEvents as-is.
constexpr int kLiveHysteresis = 32;

// Never-hidden roles. System is pinned in place but does NOT start a turn —
// only user/goal messages do (otherwise a mid-turn system notice would steal
// the previous tool-call as that turn's "summary").
inline bool isPinnedRole(const QString &role)
{
    return role == QLatin1String("user") || role == QLatin1String("system");
}

bool isPinnedEntry(const AcpTimelineEntry &entry, const QVector<AcpMessage> &messages);
bool isTurnAnchor(const AcpTimelineEntry &entry, const QVector<AcpMessage> &messages);

struct Plan
{
    // 1 = render, 0 = hide. Size equals timeline.size().
    QVector<std::uint8_t> visible;
    struct Gap
    {
        int beforeIndex = 0; // first visible timeline index after the run; == n if tail
        int hiddenCount = 0;
    };
    QVector<Gap> gaps;
    int visibleCount = 0;
};

Plan compute(const QVector<AcpTimelineEntry> &timeline,
             const QVector<AcpMessage> &messages,
             int maxVisible = kMaxVisibleEvents);

} // namespace AcpTranscriptTruncation

#endif // ACP_TRANSCRIPT_TRUNCATION_H
