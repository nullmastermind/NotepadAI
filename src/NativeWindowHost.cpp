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

bool NativeWindowHost::attach(quintptr targetHandle, quintptr token)
{
    m_targetHandle = targetHandle;
    m_token = token;
    m_lossReported = false;
    return sync();
}

void NativeWindowHost::detach()
{
    m_targetHandle = 0;
    m_token = 0;
    m_lossReported = false;
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

bool NativeWindowHost::event(QEvent *event)
{
    const QEvent::Type type = event->type();
    const bool result = QWidget::event(event);
    if (type == QEvent::Resize || type == QEvent::Show || type == QEvent::WinIdChange)
        sync();
    return result;
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
    m_targetHandle = 0;
    m_token = 0;
    if (m_childLost)
        m_childLost();
}
