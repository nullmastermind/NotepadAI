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
#include <QDir>
#include <QEventLoop>
#include <QFont>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUuid>
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

static QString fetchCdpPageId(const QString &cdpHttpUrl)
{
    QUrl listUrl(cdpHttpUrl + QStringLiteral("/json/list"));
    QNetworkAccessManager nam;
    QNetworkRequest req(listUrl);
    req.setTransferTimeout(500);
    QNetworkReply *reply = nam.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return {};
    }
    const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
    reply->deleteLater();
    for (const QJsonValue &v : arr) {
        const QJsonObject obj = v.toObject();
        if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("page"))
            return obj.value(QStringLiteral("id")).toString();
    }
    return {};
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
    sweepStaleQuickBrowserData();

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

    // Warning at 4th instance (3 already running, counting quick browser tabs)
    const int totalWebViews = m_instances.size() + m_quickBrowserTabs.size();
    if (totalWebViews >= 3) {
        QMessageBox::StandardButton btn = QMessageBox::warning(
            nullptr,
            tr("Mini Apps"),
            tr("Each Mini App uses ~100MB RAM. You have %1 WebView2 instances running. Continue?")
                .arg(totalWebViews),
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
                    const QString cdpUrl = instance->cdpHttpUrl();
                    QString currentPage;
                    if (auto *wv = instance->webViewWidget()) {
                        const QString url = wv->currentUrl();
                        if (!url.isEmpty())
                            currentPage = QStringLiteral(" Currently on: %1.").arg(url);
                    }
                    const QString pageId = fetchCdpPageId(cdpUrl);
                    QString pageConstraint;
                    if (!pageId.isEmpty())
                        pageConstraint = QStringLiteral(" Use only target/page ID %1 — do not create or switch to other pages.").arg(pageId);
                    else
                        pageConstraint = QStringLiteral(" Do not create or switch to other pages.");
                    const QString msg = QStringLiteral(
                        "--connect %1 (via CDP).%2%3\n\n")
                        .arg(cdpUrl, currentPage, pageConstraint);
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
    // Destroy quick browser webviews
    for (const QuickBrowserTab &tab : m_quickBrowserTabs) {
        if (tab.webView)
            tab.webView->destroy();
    }
    m_quickBrowserTabs.clear();

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

void MiniAppManager::sweepStaleQuickBrowserData()
{
    const QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/QuickBrowser");
    QDir baseDir(basePath);
    if (!baseDir.exists())
        return;

    const QStringList entries = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        QDir subDir(baseDir.filePath(entry));
        if (!subDir.removeRecursively()) {
            qWarning("MiniAppManager: failed to sweep stale QuickBrowser data: %s",
                     qUtf8Printable(subDir.path()));
        }
    }
}

void MiniAppManager::launchQuickBrowser(const QUrl &url, bool enableCdp,
                                        int proxyType, const QString &proxyHost,
                                        int proxyPort, const QString &proxyBypassList)
{
#ifdef Q_OS_LINUX
    QDesktopServices::openUrl(url);
    return;
#endif

    // RAM warning at 4th total WebView2 instance
    const int totalWebViews = m_instances.size() + m_quickBrowserTabs.size();
    if (totalWebViews >= 3) {
        QMessageBox::StandardButton btn = QMessageBox::warning(
            nullptr,
            tr("Quick Browser"),
            tr("Each browser tab uses ~100MB RAM. You have %1 WebView2 instances running. Continue?")
                .arg(totalWebViews),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (btn != QMessageBox::Yes)
            return;
    }

    const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString appId = QStringLiteral("qb-") + uuid;
    const QString userDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/QuickBrowser/") + uuid;

    int debugPort = 0;
    if (enableCdp) {
        QSet<int> reserved;
        for (const MiniAppDefinition &def : m_registry->globalApps()) {
            if (def.debugPort > 0)
                reserved.insert(def.debugPort);
        }
        for (const MiniAppInstance *inst : m_instances) {
            if (inst->definition().debugPort > 0)
                reserved.insert(inst->definition().debugPort);
        }
        QTcpSocket sock;
        for (int port = 9222; port <= 9322; ++port) {
            if (reserved.contains(port))
                continue;
            if (sock.bind(QHostAddress::LocalHost, port)) {
                debugPort = port;
                sock.close();
                break;
            }
        }
    }

    auto *webView = WebViewWidget::create(appId, url, debugPort, nullptr, userDataPath,
                                          proxyType, proxyHost, proxyPort, proxyBypassList);
    if (!webView)
        return;

    ads::CDockWidget *dw = m_dockedEditor->addPreviewTab(
        webView, url.host().isEmpty() ? url.toString() : url.host(), tintedGlobeIcon());

    QuickBrowserTab tab;
    tab.webView = webView;
    tab.dockWidget = dw;
    tab.userDataPath = userDataPath;
    m_quickBrowserTabs.append(tab);

    // Wire tab close → cleanup
    connect(dw, &ads::CDockWidget::closed, this, [this, webView, dw, userDataPath]() {
        webView->destroy();

        // Remove from tracked list
        for (int i = 0; i < m_quickBrowserTabs.size(); ++i) {
            if (m_quickBrowserTabs[i].dockWidget == dw) {
                m_quickBrowserTabs.removeAt(i);
                break;
            }
        }

        // Retry-delete user data folder
        auto *timer = new QTimer(this);
        timer->setInterval(1000);
        int *attempts = new int(0);
        connect(timer, &QTimer::timeout, this, [timer, attempts, userDataPath]() {
            ++(*attempts);
            QDir dir(userDataPath);
            if (!dir.exists() || dir.removeRecursively()) {
                timer->stop();
                delete attempts;
                timer->deleteLater();
                return;
            }
            if (*attempts >= 5) {
                qWarning("MiniAppManager: failed to delete QuickBrowser data after 5 attempts: %s",
                         qUtf8Printable(userDataPath));
                timer->stop();
                delete attempts;
                timer->deleteLater();
            }
        });
        timer->start();
    });

    // Wire title changes → tab title update
    connect(webView, &WebViewWidget::titleChanged, dw, [dw](const QString &title) {
        if (!title.isEmpty())
            dw->setWindowTitle(title);
    });

    dw->tabWidget()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dw->tabWidget(), &QWidget::customContextMenuRequested, this, [this, webView, dw](const QPoint &pos) {
        QMenu menu;
        auto *mainWin = qobject_cast<MainWindow *>(parent());
        AiAgentDock *aiDock = mainWin ? mainWin->activeAiDock() : nullptr;
        if (aiDock && !webView->cdpHttpUrl().isEmpty()) {
            menu.addAction(tr("Send to AI"), this, [webView, aiDock]() {
                const QString cdpUrl = webView->cdpHttpUrl();
                QString currentPage;
                const QString url = webView->currentUrl();
                if (!url.isEmpty())
                    currentPage = QStringLiteral(" Currently on: %1.").arg(url);
                const QString pageId = fetchCdpPageId(cdpUrl);
                QString pageConstraint;
                if (!pageId.isEmpty())
                    pageConstraint = QStringLiteral(" Use only target/page ID %1 — do not create or switch to other pages.").arg(pageId);
                else
                    pageConstraint = QStringLiteral(" Do not create or switch to other pages.");
                const QString msg = QStringLiteral(
                    "--connect %1 (via CDP).%2%3\n\n")
                    .arg(cdpUrl, currentPage, pageConstraint);
                aiDock->insertTextToInput(msg);
                aiDock->setVisible(true);
                aiDock->raise();
            });
        }
        QAction *cdpAction = menu.addAction(tr("Copy CDP URL"), this, [webView]() {
            QApplication::clipboard()->setText(webView->cdpHttpUrl());
        });
        cdpAction->setEnabled(!webView->cdpHttpUrl().isEmpty());
        menu.addSeparator();
        menu.addAction(tr("Close"), dw, &ads::CDockWidget::closeDockWidget);
        menu.exec(dw->tabWidget()->mapToGlobal(pos));
    });

    webView->initialize();
}

void MiniAppManager::retintAllIcons()
{
    QIcon icon = tintedGlobeIcon();
    for (MiniAppInstance *inst : m_instances) {
        if (inst->dockWidget())
            inst->dockWidget()->tabWidget()->setIcon(icon);
    }
    for (const QuickBrowserTab &tab : m_quickBrowserTabs) {
        if (tab.dockWidget)
            tab.dockWidget->tabWidget()->setIcon(icon);
    }
}

QIcon MiniAppManager::tintedGlobeIcon() const
{
    return tintIcon(m_iconPath, m_app->palette().color(QPalette::ButtonText));
}
