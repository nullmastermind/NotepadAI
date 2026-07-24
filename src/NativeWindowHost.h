/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QWidget>

#include <functional>

class NativeWindowHost final : public QWidget
{
public:
    using ChildLostCallback = std::function<void()>;
    using FocusRequestCallback = std::function<void()>;

    explicit NativeWindowHost(QWidget *parent = nullptr);
    ~NativeWindowHost() override;

    // Forces the host HWND to exist, then attaches and synchronizes the foreign
    // child. The manager owns the identity token; this class only observes it.
    bool attach(quintptr targetHandle, quintptr token);
    void detach();
    void restoreForeignFocus();
    quintptr nativeHandle() const;
    void setChildLostCallback(ChildLostCallback callback);
    void setFocusRequestCallback(FocusRequestCallback callback);

protected:
    bool event(QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void reportChildLost();
    bool releaseInput();
    bool sync();

    quintptr m_targetHandle = 0;
    quintptr m_token = 0;
    quint32 m_foreignThreadId = 0;
    quint32 m_foreignProcessId = 0;
    ChildLostCallback m_childLost;
    FocusRequestCallback m_focusRequest;
    bool m_lossReported = false;
};
