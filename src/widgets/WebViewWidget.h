/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QRect>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

class QButtonGroup;

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

    // Called when Qt focus moves to a widget outside this WebView. Platform
    // implementations use this to release native keyboard focus back to the
    // target widget's top-level window (e.g. WebView2 on Windows).
    virtual void notifyFocusLost(QWidget *newFocusWidget) { Q_UNUSED(newFocusWidget); }

    virtual void executeScript(const QString &js, std::function<void(const QString &)> callback = nullptr) = 0;
    virtual QString nativePostMessage() const = 0;
    virtual void ensureCspBypassed() {}

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

    enum class ViewportMode : quint8 {
        Fit = 0,
        Mobile,
        Tablet,
        Pc
    };

    ViewportMode viewportMode() const { return m_viewportMode; }
    void setViewportMode(ViewportMode mode);

    static QRect viewportBoundsFor(ViewportMode mode, int availW, int availH)
    {
        int target = 0;
        switch (mode) {
        case ViewportMode::Mobile: target = 390; break;
        case ViewportMode::Tablet: target = 768; break;
        case ViewportMode::Pc:     target = 1280; break;
        case ViewportMode::Fit:    break;
        }
        const int w = (target <= 0) ? qMax(0, availW) : qMin(qMax(0, availW), target);
        const int x = (availW - w) / 2;
        return {x, 0, w, qMax(0, availH)};
    }

signals:
    void navigationCompleted(bool success, const QString &error);
    void processFailed(const QString &description);
    void loadingStateChanged(bool loading);
    void cdpReady(const QString &httpUrl, const QString &wsUrl);
    void titleChanged(const QString &title);
    void faviconChanged(const QIcon &icon);
    void urlChanged(const QString &url);
    void copilotCommandRequested(const QString &command);
    void copilotResult(bool success, const QString &data);
    // The page called window.close() on the last in-widget tab.
    void closeRequested();

public:
    void showCdpUrl(const QString &httpUrl);
    void hideCdpUrl();
    void updateUrlBar(const QString &url);

    void executeCopilotCommand(const QString &command, const QString &providerUrl,
                               const QString &model, const QString &apiKey,
                               bool anthropicMessages = false);
    void handleCopilotMessage(const QString &json);
    void handleNativeFetch(const QString &json);
    void copilotLog(const QString &msg);

protected:
    QVBoxLayout *mainLayout() const { return m_mainLayout; }
    QString appId() const { return m_appId; }
    QUrl initialUrl() const { return m_url; }

    void setLoading(bool loading);
    void changeEvent(QEvent *event) override;
    QRect viewportBounds(int availW, int availH) const
    {
        return viewportBoundsFor(m_viewportMode, availW, availH);
    }
    bool touchViewport() const
    {
        return m_viewportMode == ViewportMode::Mobile || m_viewportMode == ViewportMode::Tablet;
    }
    virtual void applyViewport() {}
    void styleViewportHost(QWidget *host) const;

private:
    void setupToolbar();
    void rebuildToolbarIcons();
    void showCopilotInputDialog();
    void showCopilotResultDialog(bool success, const QString &data);

private:
    QString m_appId;
    QUrl m_url;
    QVBoxLayout *m_mainLayout = nullptr;
    QHBoxLayout *m_toolbarLayout = nullptr;
    QToolButton *m_backBtn = nullptr;
    QToolButton *m_forwardBtn = nullptr;
    QToolButton *m_reloadBtn = nullptr;
    QToolButton *m_goBtn = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QToolButton *m_stopBtn = nullptr;
    QToolButton *m_cdpBtn = nullptr;
    QString m_cdpHttpUrl;
    QString m_cdpDisplayText;
    ViewportMode m_viewportMode = ViewportMode::Fit;
    QButtonGroup *m_viewportGroup = nullptr;

    // AI copilot
    QToolButton *m_aiBtn = nullptr;
    QToolButton *m_aiStopBtn = nullptr;
    QTimer *m_aiBlinkTimer = nullptr;
    bool m_aiBlinkOn = false;
    bool m_copilotExecuting = false;

    // Cross-page retry state
    QTimer *m_copilotRetryTimer = nullptr;
    QString m_copilotLastCmd;
    QString m_copilotProviderUrl;
    QString m_copilotModel;
    QString m_copilotApiKey;
    bool m_copilotAnthropic = false;
    int m_copilotNavRetries = 0;
    static constexpr int kMaxNavRetries = 20;

    // Debug log popup
    QDialog *m_copilotLogDlg = nullptr;
    QPlainTextEdit *m_copilotLogText = nullptr;

    // Native fetch proxy for CSP bypass
    QNetworkAccessManager *m_fetchNam = nullptr;
};
