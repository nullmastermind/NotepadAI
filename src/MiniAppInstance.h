/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "MiniAppDefinition.h"

#include <DockWidget.h>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QUrl>

#include <cstdint>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpSocket;
class WebViewWidget;

class MiniAppInstance : public QObject
{
    Q_OBJECT

public:
    enum State : std::uint8_t {
        Idle,
        Spawning,
        Polling,
        Ready,
        Initializing,
        Running,
        Failed,
        Crashed
    };
    Q_ENUM(State)

    explicit MiniAppInstance(const MiniAppDefinition &def, QObject *parent = nullptr);
    ~MiniAppInstance() override;

    State state() const { return m_state; }
    QString appId() const { return m_def.id; }
    QString appName() const { return m_def.name; }
    QString lastError() const { return m_lastError; }
    const MiniAppDefinition &definition() const { return m_def; }
    QString debugInfo() const;

    QString cdpHttpUrl() const { return m_cdpHttpUrl; }
    QString cdpWsUrl() const { return m_cdpWsUrl; }

    WebViewWidget *webViewWidget() const { return m_webView; }
    void setDockWidget(ads::CDockWidget *dw) { m_dockWidget = dw; }
    ads::CDockWidget *dockWidget() const { return m_dockWidget; }

    void start();
    void destroy();
    void retry();

    void setAllowCrossOrigin(bool allow) { m_allowCrossOrigin = allow; }

signals:
    void stateChanged(MiniAppInstance::State newState);
    void titleChanged(const QString &title);
    void cdpUrlChanged(const QString &httpUrl);
    void finished();

private slots:
    void onProcessStarted();
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onHealthPoll();
    void onWebViewNavigationCompleted(bool success, const QString &error);
    void onWebViewProcessFailed(const QString &description);

private:
    void setState(State s);
    void spawnProcess();
    void startHealthPolling();
    void createWebView();
    QString buildTitle() const;

    MiniAppDefinition m_def;
    State m_state = Idle;
    QProcess *m_process = nullptr;
    QTimer *m_pollTimer = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    qint64 m_pollStartTime = 0;
    WebViewWidget *m_webView = nullptr;
    QPointer<ads::CDockWidget> m_dockWidget;
    QString m_lastError;
    QString m_cdpHttpUrl;
    QString m_cdpWsUrl;
    bool m_allowCrossOrigin = false;
};

