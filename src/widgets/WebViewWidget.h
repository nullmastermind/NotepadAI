/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

// Abstract webview widget. Platform implementations live in
// WebViewWidget_win.cpp (WebView2) and WebViewWidget_mac.mm (WKWebView).
// On Linux, no webview is embedded — xdg-open is used instead.
class WebViewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WebViewWidget(const QString &appId, const QUrl &url, QWidget *parent = nullptr);
    ~WebViewWidget() override = default;

    virtual void initialize() = 0;
    virtual void navigate(const QUrl &url) = 0;
    virtual void reload() = 0;
    virtual void stop() = 0;
    virtual void goBack() = 0;
    virtual void goForward() = 0;
    virtual void destroy() = 0;
    virtual QString debugInfo() const { return QString(); }

    virtual void executeScript(const QString &js, std::function<void(const QString &)> callback = nullptr) = 0;
    virtual QString nativePostMessage() const = 0;

    // Factory: creates the platform-appropriate subclass.
    // Returns nullptr on Linux (caller should use xdg-open fallback).
    // If userDataFolder is non-empty, it overrides the default MiniApps/<appId> path.
    static WebViewWidget *create(const QString &appId, const QUrl &url, int debugPort = 0,
                                 QWidget *parent = nullptr, const QString &userDataFolder = QString(),
                                 int proxyType = 0, const QString &proxyHost = QString(),
                                 int proxyPort = 0, const QString &proxyBypassList = QString(),
                                 bool allowCrossOrigin = false);

    // CDP URL accessors
    QString cdpHttpUrl() const { return m_cdpHttpUrl; }
    QString cdpDisplayText() const { return m_cdpDisplayText; }

    QString currentUrl() const { return m_urlEdit ? m_urlEdit->text() : QString(); }

signals:
    void navigationCompleted(bool success, const QString &error);
    void processFailed(const QString &description);
    void loadingStateChanged(bool loading);
    void cdpReady(const QString &httpUrl, const QString &wsUrl);
    void titleChanged(const QString &title);
    void urlChanged(const QString &url);
    void copilotCommandRequested(const QString &command);
    void copilotResult(bool success, const QString &data);

public:
    void showCdpUrl(const QString &httpUrl);
    void hideCdpUrl();
    void updateUrlBar(const QString &url);

    void executeCopilotCommand(const QString &command, const QString &providerUrl,
                               const QString &model, const QString &apiKey);
    void handleCopilotMessage(const QString &json);
    void handleNativeFetch(const QString &json);
    void copilotLog(const QString &msg);

protected:
    QVBoxLayout *mainLayout() const { return m_mainLayout; }
    QString appId() const { return m_appId; }
    QUrl initialUrl() const { return m_url; }

    void setLoading(bool loading);

private:
    void setupToolbar();
    void showCopilotInputDialog();
    void showCopilotResultDialog(bool success, const QString &data);

    QString m_appId;
    QUrl m_url;
    QVBoxLayout *m_mainLayout = nullptr;
    QHBoxLayout *m_toolbarLayout = nullptr;
    QToolButton *m_backBtn = nullptr;
    QToolButton *m_forwardBtn = nullptr;
    QToolButton *m_reloadBtn = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QToolButton *m_stopBtn = nullptr;
    QToolButton *m_cdpBtn = nullptr;
    QString m_cdpHttpUrl;
    QString m_cdpDisplayText;

    // AI copilot
    QToolButton *m_aiBtn = nullptr;
    QTimer *m_aiBlinkTimer = nullptr;
    bool m_copilotExecuting = false;

    // Cross-page retry state
    QTimer *m_copilotRetryTimer = nullptr;
    QString m_copilotLastCmd;
    QString m_copilotProviderUrl;
    QString m_copilotModel;
    QString m_copilotApiKey;
    int m_copilotNavRetries = 0;
    static constexpr int kMaxNavRetries = 20;

    // Debug log popup
    QDialog *m_copilotLogDlg = nullptr;
    QPlainTextEdit *m_copilotLogText = nullptr;

    // Native fetch proxy for CSP bypass
    QNetworkAccessManager *m_fetchNam = nullptr;
};
