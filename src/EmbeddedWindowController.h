/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "EmbeddedWindowWin32.h"

class QTimer;

// Pure lifecycle state machine for embedding a foreign OS window into an editor
// tab, with ZERO dependency on Win32, Qt Advanced Docking System, or the native
// host widget. Every side effect goes through the protected platform hooks so
// the whole close/veto/retry machine is unit-testable offline: a test subclass
// overrides the hooks with in-memory fakes. EmbeddedWindowManager is the real
// subclass that maps the hooks onto Win32 + ADS + NativeWindowHost.
//
// Core safety invariant (proven by the state machine, enforced by the hooks):
// while the foreign window is still attached, closeEmbeddedTab NEVER tears the
// tab down. It asks the platform to release (restore/detach); only on success
// does it force-close the tab. On failure it VETOes — keeps everything in place,
// marks the embed close-pending, and lets poll()/the watchdog retry. When the
// registry finally drains it emits allEmbedsReleased() so a deferred app-close
// or restart can complete itself.
class EmbeddedWindowController : public QObject
{
    Q_OBJECT

public:
    explicit EmbeddedWindowController(QObject *parent = nullptr);
    ~EmbeddedWindowController() override;

    // Begin embedding `handle`. Runs the full capture → tab → mark → prepare →
    // attach sequence through the platform hooks; unwinds cleanly on any failure.
    void embedWindow(quintptr handle, const QString &title);

    // Attempt to release every embed. Any refused detach stays registered so
    // hasPendingEmbeds() remains true and teardown can be safely vetoed.
    void shutdown();

    // Cancel retry requests created by a vetoed app-close/restart. Does not
    // mutate HWNDs or tabs; dead-window reaping remains active.
    void cancelPendingClose();

    // True while any embed is still registered (attached or awaiting detach).
    bool hasPendingEmbeds() const { return !m_embeds.isEmpty(); }
    int embedCount() const { return m_embeds.size(); }

    // Run one retry/reap pass. Called by the watchdog timer in production and
    // driven directly by tests. Retries close-pending embeds and reaps windows
    // destroyed by their owning process.
    void poll();

signals:
    // Emitted exactly when the registry transitions to empty. Drives deferred
    // app-close / restart completion in MainWindow.
    void allEmbedsReleased();

protected:
    enum class ReleaseMode { RestoreExact, FailSafeDetach };

    struct Embed {
        quintptr token = 0;
        EmbeddedWindowWin32::NativeWindowState state;
        bool marked = false;
        bool closePending = false;
    };

    // THE single source of truth for tearing an embed down. Returns false only
    // when the caller must veto (window still attached / identity ambiguous).
    bool closeEmbeddedTab(Embed *embed, ReleaseMode mode);

    // Token-addressed entry points used by platform signal wiring (dock closed,
    // destroyed, child-lost). Safe against reuse: unknown tokens are no-ops.
    bool closeByToken(quintptr token, ReleaseMode mode);
    Embed *findEmbed(quintptr token) const;

    // ---- Platform hooks (no-op-free contract) ---------------------------
    // Capture the pre-embed native state. False if not embeddable.
    virtual bool platCaptureState(quintptr handle, EmbeddedWindowWin32::NativeWindowState *out) = 0;
    // Create the ADS tab + native host for `token`. Returns the host's native
    // handle (non-zero) on success, wiring close/destroyed/child-lost back to
    // closeByToken. Returns 0 on failure after releasing its own resources.
    virtual quintptr platCreateTab(quintptr token, const QString &title) = 0;
    // Tag the foreign window with `token`. False if refused/already tagged.
    virtual bool platMarkWindow(quintptr handle, quintptr token) = 0;
    // Reparent + restyle the foreign window into the host, then attach/sync.
    virtual bool platPrepareAndAttach(quintptr token, const EmbeddedWindowWin32::NativeWindowState &state,
                                      quintptr hostHandle) = 0;
    // Read-only ownership guard: the expected target still carries our token
    // (also validates handle + pid + tid). This gates every HWND mutation.
    virtual bool platTargetOwnsToken(
        const EmbeddedWindowWin32::NativeWindowState &state, quintptr token) = 0;
    // Read-only target-specific hierarchy guard: the expected HWND is currently
    // a direct child of this embed's native host, independent of token state.
    virtual bool platExpectedTargetAttached(
        const EmbeddedWindowWin32::NativeWindowState &state, quintptr token) = 0;
    // Read-only final destruction guard: the host still parents any live foreign
    // descendant (including an unrelated/replacement child). PID/TID liveness is
    // NOT sufficient proof that a foreign child remains under the host.
    virtual bool platHostRetainsForeignChild(quintptr token) = 0;
    // Exact restore / fail-safe desktop detach. Each gates every HWND mutation
    // on the token still being present.
    virtual bool platRestoreExact(const EmbeddedWindowWin32::NativeWindowState &state, quintptr token) = 0;
    virtual bool platFailSafeDetach(const EmbeddedWindowWin32::NativeWindowState &state, quintptr token) = 0;
    // Stop the host tracking the foreign child (host no longer syncs geometry).
    virtual void platDetachHost(quintptr token) = 0;
    // Clear the host's child-lost callback so no queued call can re-enter.
    virtual void platClearHostCallback(quintptr token) = 0;
    // Force-close and delete the ADS dock/host for `token`. Only ever called
    // after a successful release (no foreign child remains attached).
    virtual void platForceCloseTab(quintptr token) = 0;
    // Close the pristine scratch editor after a fully successful embed.
    virtual void platCloseScratchTab() = 0;
    // Show the user the "Windows refused to embed" feedback.
    virtual void platNotifyEmbedRefused() = 0;
    // Show the user the "cannot detach; tab kept open" feedback.
    virtual void platNotifyCloseVetoed() = 0;

    // Start/stop the retry watchdog. Base impl toggles a coarse timer; override
    // is unnecessary for tests (they call poll() directly, watchdog stays idle).
    virtual void updateWatchdog();

private:
    bool releaseWindow(Embed *embed, ReleaseMode mode);

    QList<Embed *> m_embeds;
    QTimer *m_watchdog = nullptr;
    bool m_shuttingDown = false;
    quintptr m_nextToken = 1;
};
