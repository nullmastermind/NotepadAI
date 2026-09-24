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

#ifndef GIT_REFRESH_COALESCER_H
#define GIT_REFRESH_COALESCER_H

#include <QtGlobal>

#include <cstdint>

// Collapses a burst of working-tree dirty signals into one full git refresh.
// Caller passes monotonic milliseconds. No allocation.
//
// Quiet is trailing: each dirty restarts the wait, measured from the last
// event, so edits landing a few hundred milliseconds apart do not each start
// a status refresh. A burst that never pauses still fires once kMaxWaitMs
// after it began, then the ceiling restarts.
class GitRefreshCoalescer
{
public:
    static constexpr qint64 kQuietMs = 1000;
    static constexpr qint64 kMaxWaitMs = 3000;

    enum class Decision : std::uint8_t { Hold, Fire };

    Decision onDirty(qint64 nowMs, bool refreshInFlight)
    {
        m_dirty = true;
        m_lastDirtyMs = nowMs;
        if (!m_hasBurst) {
            m_burstStartMs = nowMs;
            m_hasBurst = true;
        }
        if (!refreshInFlight && m_hasBurst && nowMs >= m_burstStartMs
            && nowMs - m_burstStartMs >= kMaxWaitMs)
            return fire();
        return Decision::Hold;
    }

    Decision onQuietElapsed(qint64 nowMs, bool refreshInFlight)
    {
        if (!m_dirty || refreshInFlight)
            return Decision::Hold;
        if (nowMs < m_lastDirtyMs + kQuietMs)
            return Decision::Hold;
        return fire();
    }

    Decision onRefreshDrained(qint64 nowMs)
    {
        if (m_immediate)
            return fire();
        if (!m_dirty)
            return Decision::Hold;
        if (m_hasBurst && nowMs >= m_burstStartMs
            && nowMs - m_burstStartMs >= kMaxWaitMs)
            return fire();
        if (nowMs >= m_lastDirtyMs + kQuietMs)
            return fire();
        return Decision::Hold;
    }

    void requestImmediateFollowUp() { m_immediate = true; }

    void acknowledgeRefresh()
    {
        m_dirty = false;
        m_hasBurst = false;
        m_immediate = false;
    }

    bool pending() const { return m_dirty || m_immediate; }

private:
    Decision fire()
    {
        acknowledgeRefresh();
        return Decision::Fire;
    }

    qint64 m_lastDirtyMs = 0;
    qint64 m_burstStartMs = 0;
    bool m_dirty = false;
    bool m_hasBurst = false;
    bool m_immediate = false;
};

#endif // GIT_REFRESH_COALESCER_H
