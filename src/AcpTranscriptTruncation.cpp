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

#include "AcpTranscriptTruncation.h"

namespace AcpTranscriptTruncation {

bool isPinnedEntry(const AcpTimelineEntry &entry, const QVector<AcpMessage> &messages)
{
    if (entry.kind != AcpTimelineEntry::Kind::Message)
        return false;
    if (entry.messageIndex < 0 || entry.messageIndex >= messages.size())
        return false;
    return isPinnedRole(messages.at(entry.messageIndex).role);
}

bool isTurnAnchor(const AcpTimelineEntry &entry, const QVector<AcpMessage> &messages)
{
    if (entry.kind != AcpTimelineEntry::Kind::Message)
        return false;
    if (entry.messageIndex < 0 || entry.messageIndex >= messages.size())
        return false;
    // User-role covers both the human prompt and fromGoalAgent handoff/compact.
    return messages.at(entry.messageIndex).role == QLatin1String("user");
}

Plan compute(const QVector<AcpTimelineEntry> &timeline,
             const QVector<AcpMessage> &messages,
             int maxVisible)
{
    Plan plan;
    const int n = timeline.size();
    plan.visible.resize(n);
    plan.visible.fill(1);
    plan.visibleCount = n;
    if (n == 0 || maxVisible < 0 || n <= maxVisible)
        return plan;

    auto pinnedAt = [&](int i) -> bool {
        return isPinnedEntry(timeline.at(i), messages);
    };
    auto anchorAt = [&](int i) -> bool {
        return isTurnAnchor(timeline.at(i), messages);
    };

    // A turn starts at every user/goal message. Index 0 is always a start so a
    // leading agent-only prefix (no user yet) is its own turn.
    QVector<int> starts;
    starts.reserve(16);
    starts.append(0);
    for (int i = 1; i < n; ++i) {
        if (anchorAt(i))
            starts.append(i);
    }
    starts.append(n);
    const int turnCount = starts.size() - 1;

    // Last agent of every turn (completed AND live) is a pin — the summary the
    // user scrolls to. Mark them in a dense table so the hide pass is O(n).
    QVector<std::uint8_t> keepLast(n, 0);
    for (int t = 0; t < turnCount; ++t) {
        const int a = starts.at(t);
        const int b = starts.at(t + 1);
        for (int i = b - 1; i >= a; --i) {
            if (!pinnedAt(i)) {
                keepLast[i] = 1;
                break;
            }
        }
    }

    // Hide oldest hideable agents until we are at the cap. Pins (user/goal/
    // system + per-turn last agent) are never touched, so visibleCount may
    // still exceed maxVisible when pins alone overflow.
    int needToHide = n - maxVisible;
    for (int i = 0; i < n && needToHide > 0; ++i) {
        if (pinnedAt(i) || keepLast.at(i) != 0)
            continue;
        plan.visible[i] = 0;
        --plan.visibleCount;
        --needToHide;
    }

    plan.gaps.reserve(turnCount);
    int run = 0;
    for (int i = 0; i < n; ++i) {
        if (plan.visible.at(i) == 0) {
            ++run;
            continue;
        }
        if (run > 0) {
            plan.gaps.append({i, run});
            run = 0;
        }
    }
    if (run > 0)
        plan.gaps.append({n, run});

    return plan;
}

} // namespace AcpTranscriptTruncation
