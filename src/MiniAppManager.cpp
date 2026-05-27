/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "MiniAppManager.h"
#include "AiAgentDock.h"
#include "DockedEditor.h"
#include "MainWindow.h"
#include "MiniAppInstance.h"
#include "MiniAppRegistry.h"
#include "NotepadNextApplication.h"
#include "WebViewWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QFont>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QUrl>
#include <QVBoxLayout>

#include <DockWidget.h>
#include <DockWidgetTab.h>

static QIcon tintIcon(const QString &svgPath, const QColor &color)
{
    QIcon source(svgPath);
    if (source.isNull()) return source;
    QIcon dst;
    for (int sz : {16, 20, 22, 24, 32, 48}) {
        QPixmap pm = source.pixmap(sz, sz);
        if (pm.isNull()) continue;
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
        p.end();
        dst.addPixmap(pm);
    }
    return dst;
}

MiniAppManager::MiniAppManager(NotepadNextApplication *app,
                               MiniAppRegistry *registry,
                               DockedEditor *dockedEditor,
                               QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_registry(registry)
    , m_dockedEditor(dockedEditor)
    , m_iconPath(QStringLiteral(":/icons/mini-app.svg"))
{
    // Re-tint tab icons on theme change
    connect(app, &NotepadNextApplication::effectiveThemeChanged, this, &MiniAppManager::retintAllIcons);
}

MiniAppManager::~MiniAppManager()
{
    shutdown();
}

void MiniAppManager::launchApp(const MiniAppDefinition &def)
{
    // Dedup: if already running, raise existing tab
    for (MiniAppInstance *inst : m_instances) {
        if (inst->appId() == def.id) {
            if (inst->dockWidget())
                inst->dockWidget()->raise();
            return;
        }
    }

    // Warning at 4th instance (3 already running)
    if (m_instances.size() >= 3) {
        QMessageBox::StandardButton btn = QMessageBox::warning(
            nullptr,
            tr("Mini Apps"),
            tr("Each Mini App uses ~100MB RAM. You have %1 running. Continue?")
                .arg(m_instances.size()),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (btn != QMessageBox::Yes)
            return;
    }

#ifdef Q_OS_LINUX
    // Linux fallback: spawn command (if any) detached, then xdg-open
    if (!def.command.isEmpty()) {
        QProcess::startDetached(
            qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh")),
            {QStringLiteral("-c"), def.command});
    }
    if (!QDesktopServices::openUrl(QUrl(def.url))) {
        QMessageBox::warning(nullptr, tr("Mini Apps"),
            tr("Could not open %1 in your default browser.").arg(def.url));
    }
    return;
#endif

    auto *instance = new MiniAppInstance(def, this);

    connect(instance, &MiniAppInstance::stateChanged, this, [this, instance]() {
        onInstanceStateChanged(instance);
    });
    connect(instance, &MiniAppInstance::finished, this, [this, instance]() {
        onInstanceFinished(instance);
    });
    connect(instance, &MiniAppInstance::titleChanged, this, [instance](const QString &title) {
        if (instance->dockWidget())
            instance->dockWidget()->setWindowTitle(title);
    });

    // Create tab immediately (shows "Starting..." title)
    QWidget *placeholder = new QWidget();
    ads::CDockWidget *dw = m_dockedEditor->addPreviewTab(
        placeholder, def.name + QStringLiteral(" \xe2\x80\x94 Starting..."), tintedGlobeIcon());
    instance->setDockWidget(dw);

    // Wire tab close → destroy instance
    connect(dw, &ads::CDockWidget::closed, this, [this, instance]() {
        instance->destroy();
    });

    // Debug context menu on tab
    dw->tabWidget()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dw->tabWidget(), &QWidget::customContextMenuRequested, this, [this, instance, dw](const QPoint &pos) {
        QMenu menu;
        menu.addAction(tr("Debug Info..."), this, [instance, dw]() {
            QDialog dlg(dw);
            dlg.setWindowTitle(QStringLiteral("Mini App Debug — %1").arg(instance->appName()));
            dlg.resize(500, 400);
            auto *layout = new QVBoxLayout(&dlg);
            auto *text = new QPlainTextEdit(&dlg);
            text->setReadOnly(true);
            text->setPlainText(instance->debugInfo());
            text->setFont(QFont(QStringLiteral("Consolas"), 9));
            layout->addWidget(text);
            dlg.exec();
        });
        if (instance->definition().debugPort > 0) {
            auto *mainWin = qobject_cast<MainWindow *>(parent());
            AiAgentDock *aiDock = mainWin ? mainWin->activeAiDock() : nullptr;
            if (aiDock && !instance->cdpHttpUrl().isEmpty()) {
                menu.addAction(tr("Send to AI"), this, [instance, aiDock]() {
                    const QString msg = QStringLiteral(
                        "Connect to the browser via CDP at %1")
                        .arg(instance->cdpHttpUrl());
                    aiDock->insertTextToInput(msg);
                    aiDock->setVisible(true);
                    aiDock->raise();
                });
            }
            QAction *cdpAction = menu.addAction(tr("Copy CDP URL"), this, [instance]() {
                QApplication::clipboard()->setText(instance->cdpHttpUrl());
            });
            cdpAction->setEnabled(!instance->cdpHttpUrl().isEmpty());
        }
        menu.addSeparator();
        menu.addAction(tr("Retry"), instance, &MiniAppInstance::retry);
        menu.addAction(tr("Close"), dw, &ads::CDockWidget::closeDockWidget);
        menu.exec(dw->tabWidget()->mapToGlobal(pos));
    });

    m_instances.append(instance);
    emit instanceCountChanged(m_instances.size());

    instance->start();
}

void MiniAppManager::onInstanceStateChanged(MiniAppInstance *instance)
{
    if (instance->state() == MiniAppInstance::Initializing) {
        auto *webView = instance->webViewWidget();
        auto *dw = instance->dockWidget();
        if (webView && dw) {
            QWidget *old = dw->widget();
            dw->setWidget(webView);
            if (old && old != webView)
                old->deleteLater();
            webView->initialize();
        }
    }
}

void MiniAppManager::onInstanceFinished(MiniAppInstance *instance)
{
    m_instances.removeOne(instance);
    instance->deleteLater();
    emit instanceCountChanged(m_instances.size());
}

void MiniAppManager::shutdown()
{
    for (MiniAppInstance *inst : m_instances) {
        inst->destroy();
    }
    // Wait briefly for processes to exit
    for (MiniAppInstance *inst : m_instances) {
        Q_UNUSED(inst)
    }
    qDeleteAll(m_instances);
    m_instances.clear();
}

void MiniAppManager::retintAllIcons()
{
    QIcon icon = tintedGlobeIcon();
    for (MiniAppInstance *inst : m_instances) {
        if (inst->dockWidget())
            inst->dockWidget()->tabWidget()->setIcon(icon);
    }
}

QIcon MiniAppManager::tintedGlobeIcon() const
{
    return tintIcon(m_iconPath, m_app->palette().color(QPalette::ButtonText));
}
