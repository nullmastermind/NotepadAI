/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "EmbeddedWindowManager.h"

#include "DockedEditor.h"
#include "DockWidget.h"
#include "NativeWindowHost.h"

#include <QIcon>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QWidget>

#include <utility>

struct EmbeddedWindowManager::Tab
{
    QPointer<NativeWindowHost> host;
    QPointer<ads::CDockWidget> dock;
};

EmbeddedWindowManager::EmbeddedWindowManager(DockedEditor *dockedEditor, QObject *parent)
    : EmbeddedWindowController(parent)
    , m_dockedEditor(dockedEditor)
{
    connect(m_dockedEditor, &DockedEditor::previewTabActivated, this, [this](QWidget *widget) {
        m_focusRouter.request(widget);
    });
}

EmbeddedWindowManager::~EmbeddedWindowManager()
{
    shutdown();
    // MainWindow::closeEvent guarantees teardown is vetoed while any embed is
    // pending. This assert catches any future exit path that bypasses it.
    Q_ASSERT(!hasPendingEmbeds());
    for (Tab *tab : std::as_const(m_tabs)) {
        if (tab->host) {
            m_focusRouter.unregisterHost(tab->host.data());
            tab->host->setFocusRequestCallback(NativeWindowHost::FocusRequestCallback());
            tab->host->setChildLostCallback(NativeWindowHost::ChildLostCallback());
        }
        delete tab; // metadata only; ADS owns dock/host widgets
    }
}

QList<EmbeddedWindowManager::WindowInfo> EmbeddedWindowManager::enumerateEmbeddableWindows() const
{
    QList<WindowInfo> result;
    for (const auto &candidate : EmbeddedWindowWin32::enumerateEmbeddableWindows())
        result.append({candidate.handle, candidate.title});
    return result;
}

bool EmbeddedWindowManager::platCaptureState(
    quintptr handle, EmbeddedWindowWin32::NativeWindowState *out)
{
    return m_dockedEditor && EmbeddedWindowWin32::captureState(handle, out);
}

quintptr EmbeddedWindowManager::platCreateTab(quintptr token, const QString &title)
{
    auto *host = new NativeWindowHost;
    ads::CDockWidget *dock = m_dockedEditor->addPreviewTab(host, title, QIcon());
    if (!dock) {
        delete host;
        return 0;
    }
    const quintptr hostHandle = host->nativeHandle();
    if (!hostHandle) {
        // No foreign child or token exists yet; this empty ADS dock owns host
        // and can be force-closed safely before controller registration.
        dock->closeDockWidget();
        return 0;
    }

    auto *tab = new Tab{host, dock};
    m_tabs.insert(token, tab);
    const QPointer<EmbeddedWindowManager> managerGuard(this);
    const QPointer<NativeWindowHost> hostGuard(host);
    m_focusRouter.registerHost(host, [hostGuard]() {
        if (hostGuard)
            hostGuard->restoreForeignFocus();
    });
    host->setFocusRequestCallback([managerGuard, hostGuard]() {
        if (managerGuard && hostGuard)
            managerGuard->m_focusRouter.request(hostGuard.data());
    });
    host->setChildLostCallback([managerGuard, token]() {
        if (!managerGuard)
            return;
        QMetaObject::invokeMethod(managerGuard.data(), [managerGuard, token]() {
            if (managerGuard)
                managerGuard->closeByToken(token, ReleaseMode::FailSafeDetach);
        }, Qt::QueuedConnection);
    });

    dock->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    dock->setFeature(ads::CDockWidget::DockWidgetPinnable, false);
    dock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    dock->setFeature(ads::CDockWidget::CustomCloseHandling, true);
    connect(dock, &ads::CDockWidget::closeRequested, this, [this, token]() {
        // A user close must complete synchronously when exact restoration is
        // refused: try exact first, then the token-gated desktop detach in the
        // same operation instead of leaving completion to the watchdog.
        if (!closeByToken(token, ReleaseMode::FailSafeDetach))
            platNotifyCloseVetoed();
    });
    connect(dock, &ads::CDockWidget::closed, this, [this, token]() {
        closeByToken(token, ReleaseMode::FailSafeDetach);
    });
    connect(dock, &QObject::destroyed, this, [this, token]() {
        closeByToken(token, ReleaseMode::FailSafeDetach);
    });
    return hostHandle;
}

bool EmbeddedWindowManager::platMarkWindow(quintptr handle, quintptr token)
{
    return EmbeddedWindowWin32::markWindow(handle, token);
}

bool EmbeddedWindowManager::platPrepareAndAttach(
    quintptr token, const EmbeddedWindowWin32::NativeWindowState &state,
    quintptr hostHandle)
{
    Tab *tab = m_tabs.value(token);
    return tab && tab->host
        && EmbeddedWindowWin32::prepareEmbed(state, hostHandle, token)
        && tab->host->attach(state.handle, token);
}

bool EmbeddedWindowManager::platTargetOwnsToken(
    const EmbeddedWindowWin32::NativeWindowState &state, quintptr token)
{
    return EmbeddedWindowWin32::sameWindow(state, token);
}

bool EmbeddedWindowManager::platExpectedTargetAttached(
    const EmbeddedWindowWin32::NativeWindowState &state, quintptr token)
{
    Tab *tab = m_tabs.value(token);
    return tab && tab->host
        && EmbeddedWindowWin32::isDirectChildOfHost(
            state.handle, tab->host->nativeHandle());
}

bool EmbeddedWindowManager::platHostRetainsForeignChild(quintptr token)
{
    Tab *tab = m_tabs.value(token);
    return tab && tab->host
        && EmbeddedWindowWin32::hostRetainsForeignChild(tab->host->nativeHandle());
}

bool EmbeddedWindowManager::platRestoreExact(
    const EmbeddedWindowWin32::NativeWindowState &state, quintptr token)
{
    return EmbeddedWindowWin32::restoreExact(state, token);
}

bool EmbeddedWindowManager::platFailSafeDetach(
    const EmbeddedWindowWin32::NativeWindowState &state, quintptr token)
{
    return EmbeddedWindowWin32::failSafeDetach(state, token);
}

void EmbeddedWindowManager::platDetachHost(quintptr token)
{
    if (Tab *tab = m_tabs.value(token); tab && tab->host)
        tab->host->detach();
}

void EmbeddedWindowManager::platClearHostCallback(quintptr token)
{
    if (Tab *tab = m_tabs.value(token); tab && tab->host)
        tab->host->setChildLostCallback(NativeWindowHost::ChildLostCallback());
}

void EmbeddedWindowManager::platForceCloseTab(quintptr token)
{
    Tab *tab = m_tabs.take(token);
    if (!tab)
        return;
    const QPointer<ads::CDockWidget> dock = tab->dock;
    if (tab->host) {
        m_focusRouter.unregisterHost(tab->host.data());
        tab->host->setFocusRequestCallback(NativeWindowHost::FocusRequestCallback());
    }
    delete tab;
    if (dock && !dock->isClosed())
        dock->closeDockWidget();
}

void EmbeddedWindowManager::platCloseScratchTab()
{
    if (ScintillaNext *initial = m_dockedEditor->initialEditor())
        initial->close();
}

void EmbeddedWindowManager::platNotifyEmbedRefused()
{
    QMessageBox::warning(qobject_cast<QWidget *>(parent()), tr("Embed Window"),
                         tr("Windows refused to embed this window."));
}

void EmbeddedWindowManager::platNotifyCloseVetoed()
{
    QMessageBox::warning(qobject_cast<QWidget *>(parent()), tr("Close Embedded Window"),
                         tr("Windows refused to detach the embedded window. The tab was kept open."));
}
