/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "MiniAppDefinition.h"

#include <QIcon>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>

class DockedEditor;
class MiniAppInstance;
class MiniAppRegistry;
class NotepadNextApplication;
class QMenu;
class WebViewWidget;

namespace ads { class CDockWidget; }

struct QuickBrowserTab {
    QPointer<WebViewWidget> webView;
    ads::CDockWidget *dockWidget = nullptr;
    QString userDataPath;
};

class MiniAppManager : public QObject
{
    Q_OBJECT

public:
    explicit MiniAppManager(NotepadNextApplication *app,
                            MiniAppRegistry *registry,
                            DockedEditor *dockedEditor,
                            QObject *parent = nullptr);
    ~MiniAppManager() override;

    void launchApp(const MiniAppDefinition &def);
    void launchQuickBrowser(const QUrl &url, bool enableCdp = true,
                            int proxyType = 0, const QString &proxyHost = QString(),
                            int proxyPort = 0, const QString &proxyBypassList = QString(),
                            bool allowCrossOrigin = true);
    void shutdown();

    int runningCount() const { return m_instances.size(); }

signals:
    void instanceCountChanged(int count);

private:
    void onInstanceStateChanged(MiniAppInstance *instance);
    void onInstanceFinished(MiniAppInstance *instance);
    void retintAllIcons();
    QIcon tintedGlobeIcon() const;
    void applyTabFavicon(ads::CDockWidget *dw, const QIcon &icon);
    void sweepStaleQuickBrowserData();
    void setDockTabPinned(ads::CDockWidget *dw, bool pinned);
    void addPinMenuAction(QMenu *menu, ads::CDockWidget *dw);
    void moveDockTabToPinCluster(ads::CDockWidget *dw, bool pinning);
    QString pinKeyFor(ads::CDockWidget *dw) const;
    void persistPinKey(const QString &key, bool pinned);
    void forgetPinKey(ads::CDockWidget *dw);
    void prunePinnedMiniAppKeys();
    void restorePinnedTabs();

    NotepadNextApplication *m_app;
    MiniAppRegistry *m_registry;
    DockedEditor *m_dockedEditor;
    QList<MiniAppInstance *> m_instances;
    QList<QuickBrowserTab> m_quickBrowserTabs;
    QString m_iconPath;
    bool m_restoringPins = false;
    bool m_shuttingDown = false;
};
