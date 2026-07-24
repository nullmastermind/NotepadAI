/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "EmbeddedWindowController.h"
#include "EmbeddedWindowFocusRouter.h"

#include <QHash>
#include <QList>
#include <QString>

class DockedEditor;

// Windows/ADS adapter for the pure EmbeddedWindowController state machine.
// No lifecycle policy lives here: it only maps abstract operations to the
// Win32 layer, NativeWindowHost, and CDockWidget. The controller is the sole
// source of truth for release/veto/retry/force-close ordering.
class EmbeddedWindowManager final : public EmbeddedWindowController
{
    Q_OBJECT

public:
    struct WindowInfo {
        quintptr handle = 0;
        QString title;
    };

    explicit EmbeddedWindowManager(DockedEditor *dockedEditor, QObject *parent = nullptr);
    ~EmbeddedWindowManager() override;

    QList<WindowInfo> enumerateEmbeddableWindows() const;

protected:
    bool platCaptureState(quintptr handle, EmbeddedWindowWin32::NativeWindowState *out) override;
    quintptr platCreateTab(quintptr token, const QString &title) override;
    bool platMarkWindow(quintptr handle, quintptr token) override;
    bool platPrepareAndAttach(quintptr token, const EmbeddedWindowWin32::NativeWindowState &state,
                              quintptr hostHandle) override;
    bool platTargetOwnsToken(
        const EmbeddedWindowWin32::NativeWindowState &state, quintptr token) override;
    bool platExpectedTargetAttached(
        const EmbeddedWindowWin32::NativeWindowState &state, quintptr token) override;
    bool platHostRetainsForeignChild(quintptr token) override;
    bool platRestoreExact(const EmbeddedWindowWin32::NativeWindowState &state, quintptr token) override;
    bool platFailSafeDetach(const EmbeddedWindowWin32::NativeWindowState &state, quintptr token) override;
    void platDetachHost(quintptr token) override;
    void platClearHostCallback(quintptr token) override;
    void platForceCloseTab(quintptr token) override;
    void platCloseScratchTab() override;
    void platNotifyEmbedRefused() override;
    void platNotifyCloseVetoed() override;

private:
    struct Tab;

    DockedEditor *m_dockedEditor = nullptr;
    EmbeddedWindowFocusRouter m_focusRouter;
    QHash<quintptr, Tab *> m_tabs;
};
