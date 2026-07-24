/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "NativeWindowHost.h"

#include "EmbeddedWindowWin32.h"

#include <QEvent>

#include <utility>

NativeWindowHost::NativeWindowHost(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);
    setAutoFillBackground(true);
    setFocusPolicy(Qt::StrongFocus);
}

NativeWindowHost::~NativeWindowHost()
{
    releaseInput();
}

bool NativeWindowHost::attach(quintptr targetHandle, quintptr token)
{
    // A host is normally single-use, but make accidental reuse safe: never
    // overwrite an outstanding attachment and lose the only detach identity.
    if (!releaseInput())
        return false;
    m_targetHandle = targetHandle;
    m_token = token;
    m_lossReported = false;
    if (!EmbeddedWindowWin32::attachForeignInput(
            targetHandle, token, &m_foreignThreadId, &m_foreignProcessId)) {
        m_targetHandle = 0;
        m_token = 0;
        return false;
    }
    if (!sync()) {
        releaseInput();
        m_targetHandle = 0;
        m_token = 0;
        return false;
    }
    // Focus is best-effort here: the host may not yet be the foreground/active
    // window at attach time, so SetFocus can legitimately not stick without any
    // error. The reparent + geometry sync above is the real success gate; focus
    // is re-driven on every QEvent::FocusIn once the tab is actually activated.
    EmbeddedWindowWin32::focusForeign(m_targetHandle, m_token);
    return true;
}

void NativeWindowHost::detach()
{
    releaseInput();
    m_targetHandle = 0;
    m_token = 0;
    m_lossReported = false;
}

void NativeWindowHost::restoreForeignFocus()
{
    if (m_targetHandle && !EmbeddedWindowWin32::focusForeign(m_targetHandle, m_token))
        qWarning("EmbeddedWindow: failed to restore focus to embedded child");
}

quintptr NativeWindowHost::nativeHandle() const
{
#ifdef Q_OS_WIN
    // winId() deliberately forces creation; effectiveWinId() can be zero
    // before Qt has materialized a native host.
    return static_cast<quintptr>(const_cast<NativeWindowHost *>(this)->winId());
#else
    return 0;
#endif
}

void NativeWindowHost::setChildLostCallback(ChildLostCallback callback)
{
    m_childLost = std::move(callback);
}

void NativeWindowHost::setFocusRequestCallback(FocusRequestCallback callback)
{
    m_focusRequest = std::move(callback);
}

bool NativeWindowHost::event(QEvent *event)
{
    const QEvent::Type type = event->type();
    const bool result = QWidget::event(event);
    if (type == QEvent::Resize || type == QEvent::Show || type == QEvent::WinIdChange)
        sync();
    // ADS tab activation, Show and FocusIn can arrive in the same event-loop
    // turn. Route both through the manager's coalescing focus router.
    if ((type == QEvent::Show || type == QEvent::FocusIn) && m_targetHandle && m_focusRequest)
        m_focusRequest();
    return result;
}

bool NativeWindowHost::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    // Mouse input is delivered directly to the foreign HWND, bypassing Qt. Its
    // parent still receives WM_PARENTNOTIFY, which is the reliable hook for
    // re-activating an embed after the user interacted with a NotepadAI menu or
    // dialog while the embed tab remained selected.
    if (m_targetHandle && m_focusRequest
        && EmbeddedWindowWin32::isChildMouseActivationMessage(message))
        m_focusRequest();
    return QWidget::nativeEvent(eventType, message, result);
}

bool NativeWindowHost::releaseInput()
{
    if (!m_foreignThreadId)
        return true;
    if (!EmbeddedWindowWin32::detachForeignInput(m_foreignThreadId, m_foreignProcessId))
        return false;
    m_foreignThreadId = 0;
    m_foreignProcessId = 0;
    return true;
}

bool NativeWindowHost::sync()
{
#ifdef Q_OS_WIN
    if (!m_targetHandle || !nativeHandle())
        return false;
    const auto result = EmbeddedWindowWin32::syncGeometry(
        m_targetHandle, nativeHandle(), m_token);
    if (result == EmbeddedWindowWin32::SyncResult::WindowLost) {
        reportChildLost();
        return false;
    }
    return result == EmbeddedWindowWin32::SyncResult::Ok;
#else
    return false;
#endif
}

void NativeWindowHost::reportChildLost()
{
    if (m_lossReported || !m_targetHandle)
        return;
    m_lossReported = true;
    releaseInput();
    m_targetHandle = 0;
    m_token = 0;
    if (m_childLost)
        m_childLost();
}
