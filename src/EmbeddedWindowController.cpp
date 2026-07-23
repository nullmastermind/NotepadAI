/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "EmbeddedWindowController.h"

#include <QTimer>
#include <QtAlgorithms>

EmbeddedWindowController::EmbeddedWindowController(QObject *parent)
    : QObject(parent)
    , m_watchdog(new QTimer(this))
{
    m_watchdog->setInterval(500);
    m_watchdog->setTimerType(Qt::CoarseTimer);
    connect(m_watchdog, &QTimer::timeout, this, &EmbeddedWindowController::poll);
}

EmbeddedWindowController::~EmbeddedWindowController()
{
    qDeleteAll(m_embeds);
}

void EmbeddedWindowController::embedWindow(quintptr handle, const QString &title)
{
    for (const Embed *existing : m_embeds)
        if (existing->state.handle == handle)
            return;

    EmbeddedWindowWin32::NativeWindowState state;
    if (!platCaptureState(handle, &state)) {
        platNotifyEmbedRefused();
        return;
    }

    quintptr token = m_nextToken++;
    if (token == 0)
        token = m_nextToken++;
    const quintptr hostHandle = platCreateTab(token, title);
    if (!hostHandle) {
        platNotifyEmbedRefused();
        return;
    }

    auto *embed = new Embed;
    embed->token = token;
    embed->state = state;
    m_embeds.append(embed); // Registration boundary: source-of-truth owns all failures below.

    if (!platMarkWindow(handle, token)) {
        closeEmbeddedTab(embed, ReleaseMode::RestoreExact);
        platNotifyEmbedRefused();
        return;
    }
    embed->marked = true;
    if (!platPrepareAndAttach(token, state, hostHandle)) {
        closeEmbeddedTab(embed, ReleaseMode::RestoreExact);
        platNotifyEmbedRefused();
        return;
    }

    // Scratch closes only after mark + prepare + host attach/sync all succeeded.
    platCloseScratchTab();
    updateWatchdog();
}

bool EmbeddedWindowController::releaseWindow(Embed *embed, ReleaseMode mode)
{
    if (!embed)
        return false;
    if (!embed->marked)
        return true;
    if (!platTargetOwnsToken(embed->state, embed->token)) {
        // Token mismatch forbids touching the target. A hard veto is necessary
        // only while the expected target or another foreign child is proven to
        // remain under our native host. Otherwise the host is empty (target
        // gone, detached, or HWND recycled) and can be reaped mutation-free.
        if (platExpectedTargetAttached(embed->state, embed->token)
            || platHostRetainsForeignChild(embed->token))
            return false;
        platDetachHost(embed->token);
        return true;
    }

    bool released = platRestoreExact(embed->state, embed->token);
    if (!released && mode == ReleaseMode::FailSafeDetach)
        released = platFailSafeDetach(embed->state, embed->token);
    if (released)
        platDetachHost(embed->token);
    return released;
}

bool EmbeddedWindowController::closeEmbeddedTab(Embed *embed, ReleaseMode mode)
{
    if (!embed || !m_embeds.contains(embed))
        return true;
    const bool released = releaseWindow(embed, mode);
    const bool hostStillHasForeignChild = platHostRetainsForeignChild(embed->token);
    if (!released || hostStillHasForeignChild) {
        // Hard veto: no takeWidget/reparent/force-close/delete while the native
        // host still retains any foreign child. The second hierarchy read also
        // protects an unrelated child that remains after our target restored.
        qWarning("EmbeddedWindow: close vetoed (mode=%d released=%d hostHasForeignChild=%d)",
                 static_cast<int>(mode), released, hostStillHasForeignChild);
        embed->closePending = true;
        updateWatchdog();
        return false;
    }

    const quintptr token = embed->token;
    platClearHostCallback(token);
    m_embeds.removeOne(embed);
    delete embed;
    updateWatchdog();
    platForceCloseTab(token); // Proven release success is the sole force-close predecessor.
    if (m_embeds.isEmpty())
        emit allEmbedsReleased();
    return true;
}

bool EmbeddedWindowController::closeByToken(quintptr token, ReleaseMode mode)
{
    Embed *embed = findEmbed(token);
    const bool closed = closeEmbeddedTab(embed, mode);
    if (!closed && mode == ReleaseMode::RestoreExact)
        platNotifyCloseVetoed();
    return closed;
}

EmbeddedWindowController::Embed *EmbeddedWindowController::findEmbed(quintptr token) const
{
    for (Embed *embed : m_embeds)
        if (embed->token == token)
            return embed;
    return nullptr;
}

void EmbeddedWindowController::cancelPendingClose()
{
    for (Embed *embed : m_embeds)
        embed->closePending = false;
    m_shuttingDown = false;
    updateWatchdog();
}

void EmbeddedWindowController::poll()
{
    const QList<Embed *> snapshot = m_embeds;
    for (Embed *embed : snapshot) {
        if (embed->closePending || !platTargetOwnsToken(embed->state, embed->token))
            closeEmbeddedTab(embed, ReleaseMode::FailSafeDetach);
    }
    updateWatchdog();
}

void EmbeddedWindowController::shutdown()
{
    if (m_shuttingDown)
        return;
    m_shuttingDown = true;
    const QList<Embed *> snapshot = m_embeds;
    for (Embed *embed : snapshot)
        closeEmbeddedTab(embed, ReleaseMode::FailSafeDetach);
    if (!m_embeds.isEmpty()) {
        m_shuttingDown = false;
        updateWatchdog();
    } else {
        m_watchdog->stop();
    }
}

void EmbeddedWindowController::updateWatchdog()
{
    if (m_embeds.isEmpty())
        m_watchdog->stop();
    else if (!m_watchdog->isActive())
        m_watchdog->start();
}
