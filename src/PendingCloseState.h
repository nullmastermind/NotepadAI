/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

// Tiny, allocation-free state machine shared by MainWindow and unit tests.
// It guarantees one outstanding close intent, at most one queued retry, and
// exactly-once acceptance (notably exactly one restart process launch).
class PendingCloseState
{
public:
    enum class Intent { None, Exit, Restart };

    void begin(Intent intent)
    {
        // Restart is stronger than Exit; an ordinary close never downgrades it.
        if (m_intent != Intent::Restart || intent == Intent::Restart)
            m_intent = intent;
    }

    void cancel()
    {
        m_intent = Intent::None;
        m_retryQueued = false;
        m_feedbackShown = false;
    }

    Intent intent() const { return m_intent; }
    bool active() const { return m_intent != Intent::None; }
    bool feedbackShown() const { return m_feedbackShown; }
    void markFeedbackShown() { m_feedbackShown = true; }

    // Called when the embed registry transitions to empty. Returns true exactly
    // once until retryDequeued() is called, preventing close() queue storms.
    bool requestRetry()
    {
        if (!active() || m_retryQueued)
            return false;
        m_retryQueued = true;
        return true;
    }

    void retryDequeued() { m_retryQueued = false; }

    // Called only after QCloseEvent remains accepted. Clears state and returns
    // the accepted intent so MainWindow can launch a restart exactly once.
    Intent accept()
    {
        const Intent accepted = m_intent;
        cancel();
        return accepted;
    }

private:
    Intent m_intent = Intent::None;
    bool m_retryQueued = false;
    bool m_feedbackShown = false;
};
