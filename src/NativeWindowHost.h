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

    explicit NativeWindowHost(QWidget *parent = nullptr);

    // Forces the host HWND to exist, then attaches and synchronizes the foreign
    // child. The manager owns the identity token; this class only observes it.
    bool attach(quintptr targetHandle, quintptr token);
    void detach();
    quintptr nativeHandle() const;
    void setChildLostCallback(ChildLostCallback callback);

protected:
    bool event(QEvent *event) override;

private:
    void reportChildLost();
    bool sync();

    quintptr m_targetHandle = 0;
    quintptr m_token = 0;
    ChildLostCallback m_childLost;
    bool m_lossReported = false;
};
