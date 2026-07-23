/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

// Pure Win32 layer for the embedded-window feature. Deliberately free of Qt
// widgets and Qt Advanced Docking System types so it can be unit-reasoned in
// isolation and so neither NativeWindowHost nor EmbeddedWindowManager pull in
// <Windows.h>. Native handles cross this boundary as opaque quintptr values;
// window attributes are captured into a POD NativeWindowState of fixed-width
// integers (no WINDOWPLACEMENT in the header). On non-Windows every entry point
// is a no-op returning a safe default so the rest of the app still builds.
namespace EmbeddedWindowWin32 {

// A top-level, user-visible, foreign-process window the user may embed.
struct Candidate {
    quintptr handle = 0; // native HWND, opaque here
    QString title;
};

// Snapshot of a foreign window's pre-embed state. Everything needed to restore
// it exactly, or at minimum detach it safely, without ever touching a recycled
// handle (identity is proven by handle + processId + threadId + token).
struct NativeWindowState {
    quintptr handle = 0;
    quint32 processId = 0;
    quint32 threadId = 0;
    quintptr originalParent = 0;
    quintptr originalOwner = 0;
    qint64 originalStyle = 0;
    qint64 originalExStyle = 0;
    qint32 rectLeft = 0;
    qint32 rectTop = 0;
    qint32 rectRight = 0;
    qint32 rectBottom = 0;
    quint32 placementFlags = 0;
    quint32 placementShowCmd = 0;
    qint32 placementMinX = 0;
    qint32 placementMinY = 0;
    qint32 placementMaxX = 0;
    qint32 placementMaxY = 0;
    qint32 placementNormalLeft = 0;
    qint32 placementNormalTop = 0;
    qint32 placementNormalRight = 0;
    qint32 placementNormalBottom = 0;
    bool placementValid = false;
    bool originallyVisible = false;
};

// Result of re-syncing the foreign child's geometry to its host client area.
enum class SyncResult {
    Ok,             // reparented (if asked) and resized successfully
    TransientError, // a Win32 call failed but the window still looks alive
    WindowLost,     // the window is gone or the token no longer matches
};

// Alt-tab style set: top-level, visible, titled windows owned by other
// processes. Cold path (menu open); returns [] on non-Windows.
QList<Candidate> enumerateEmbeddableWindows();

// Validate `handle` is embeddable and snapshot its full pre-embed state.
// Returns false (and leaves *out untouched) if the window is not embeddable or
// its state could not be captured.
bool captureState(quintptr handle, NativeWindowState *out);

// Tag the window with our per-embed token via SetPropW. False if the window is
// already tagged or the property could not be set.
bool markWindow(quintptr handle, quintptr token);

// Read-only target-specific hierarchy test. True iff `targetHandle` currently
// exists and is a direct child of `hostHandle`, independent of token ownership.
// Used to retain a host when a token was lost but its target is still attached.
bool isDirectChildOfHost(quintptr targetHandle, quintptr hostHandle);

// Read-only hierarchy test. True iff `hostHandle` currently has at least one
// live descendant window owned by another process. This is the only proof that
// destroying the host could still destroy/reparent a foreign child. It does not
// inspect or mutate the embed token and returns false on non-Windows.
bool hostRetainsForeignChild(quintptr hostHandle);

// True only while the handle still resolves to the *same* native window our
// token+pid+tid describe — guards every mutation against handle recycling.
bool sameWindow(const NativeWindowState &state, quintptr token);

// Reparent the foreign window into the host HWND with child styles. Returns
// false without partial rollback expectations if Windows refuses any step.
bool prepareEmbed(const NativeWindowState &state, quintptr hostHandle, quintptr token);

// Exact restore: original styles, owner, placement, visibility, and untag.
// Returns false if any step failed while the window was still alive (caller
// decides whether to escalate to failSafeDetach).
bool restoreExact(const NativeWindowState &state, quintptr token);

// Guaranteed best-effort: strip WS_CHILD, reparent to the desktop so an
// imminent host destruction cannot take the foreign window down, then untag.
// Returns true if the window ended detached from any host (or was already gone).
bool failSafeDetach(const NativeWindowState &state, quintptr token);

// Resize the foreign child to the host's client rect (optionally reparenting
// first) and nudge it to repaint. Never blocks on the foreign process.
SyncResult syncGeometry(quintptr targetHandle, quintptr hostHandle, quintptr token);

} // namespace EmbeddedWindowWin32
