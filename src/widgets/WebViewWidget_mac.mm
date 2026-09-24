/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "WebViewWidget.h"

#include <QResizeEvent>
#include <QUrl>
#include <QWindow>
#include <QUuid>

#import <WebKit/WebKit.h>
#import <Foundation/Foundation.h>

// Forward declare the C++ class so the delegate can reference it.
class WebViewWidgetMac;

// Navigation delegate bridging WKWebView events to Qt signals.
@interface MiniAppNavDelegate : NSObject <WKNavigationDelegate>
@property (assign) WebViewWidgetMac *owner;
@end

// Script message handler for copilot result callback.
@interface CopilotMessageHandler : NSObject <WKScriptMessageHandler>
@property (assign) WebViewWidgetMac *owner;
@end

// macOS WebView implementation using WKWebView embedded via QWindow::fromWinId.
class WebViewWidgetMac : public WebViewWidget
{
    Q_OBJECT

public:
    WebViewWidgetMac(const QString &appId, const QUrl &url, QWidget *parent, const QString &userDataFolder)
        : WebViewWidget(appId, url, parent)
    {
        @autoreleasepool {
            WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];

            // Quick Browser passes a userDataFolder. WKWebView cannot choose an
            // arbitrary profile directory, so use the app's persistent default
            // store there instead of a fresh UUID-backed store per tab.
            if (!userDataFolder.isEmpty()) {
                config.websiteDataStore = [WKWebsiteDataStore defaultDataStore];
            } else if (@available(macOS 14.0, *)) {
                QUuid ns = QUuid::fromString(QStringLiteral("{6ba7b810-9dad-11d1-80b4-00c04fd430c8}"));
                QUuid storeId = QUuid::createUuidV5(ns, appId);
                NSUUID *nsUuid = [[NSUUID alloc] initWithUUIDString:storeId.toString(QUuid::WithoutBraces).toNSString()];
                config.websiteDataStore = [WKWebsiteDataStore dataStoreForIdentifier:nsUuid];
            }

            // Inject pristine fetch reference before any page script runs
            NSString *pristineFetchJs = @"Object.defineProperty(window,'__nai_fetch',{"
                "value:window.fetch.bind(window),"
                "writable:false,configurable:false,enumerable:false});";
            WKUserScript *fetchScript = [[WKUserScript alloc]
                initWithSource:pristineFetchJs
                injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                forMainFrameOnly:YES];
            [config.userContentController addUserScript:fetchScript];

            // NOTE: We deliberately do NOT pre-create a Trusted Types 'default' policy at
            // document start. 'default' is a per-realm singleton — claiming it first makes
            // the page's own createPolicy('default') throw "already exists" (e.g.
            // OWA/Outlook, Teams), which aborts their boot and leaves the page unreachable.
            // Page-agent instead creates its policy lazily at run time (see
            // executeCopilotCommand in WebViewWidget.cpp): it tries 'default', and on
            // failure falls back to the named 'nai-page-agent' policy, degrading silently
            // if even that is CSP-blocked.

            // Register message handler for copilot results
            m_msgHandler = [[CopilotMessageHandler alloc] init];
            m_msgHandler.owner = this;
            [config.userContentController addScriptMessageHandler:m_msgHandler name:@"pageAgent"];

            NSRect frame = NSMakeRect(0, 0, 400, 300);
            m_webView = [[WKWebView alloc] initWithFrame:frame configuration:config];
            m_webView.customUserAgent = @"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.4 Safari/605.1.15";

            m_navDelegate = [[MiniAppNavDelegate alloc] init];
            m_navDelegate.owner = this;
            m_webView.navigationDelegate = m_navDelegate;

            QWindow *foreignWindow = QWindow::fromWinId(reinterpret_cast<WId>(m_webView));
            m_viewportHost = new QWidget(this);
            styleViewportHost(m_viewportHost);
            mainLayout()->addWidget(m_viewportHost, 1);
            m_container = QWidget::createWindowContainer(foreignWindow, m_viewportHost);

            setLoading(true);
            NSURL *nsUrl = [NSURL URLWithString:initialUrl().toString().toNSString()];
            [m_webView loadRequest:[NSURLRequest requestWithURL:nsUrl]];
        }
    }

    ~WebViewWidgetMac() override { destroy(); }

    void initialize() override {}

    void navigate(const QUrl &url) override
    {
        @autoreleasepool {
            setLoading(true);
            NSURL *nsUrl = [NSURL URLWithString:url.toString().toNSString()];
            [m_webView loadRequest:[NSURLRequest requestWithURL:nsUrl]];
        }
    }

    void reload() override { setLoading(true); [m_webView reload]; }
    void stop() override { [m_webView stopLoading]; setLoading(false); }
    void goBack() override { if (m_webView) [m_webView goBack]; }
    void goForward() override { if (m_webView) [m_webView goForward]; }

    void executeScript(const QString &js, std::function<void(const QString &)> callback) override
    {
        if (!m_webView) {
            if (callback) callback(QString());
            return;
        }
        @autoreleasepool {
            NSString *nsJs = js.toNSString();
            if (callback) {
                auto cb = std::move(callback);
                [m_webView evaluateJavaScript:nsJs completionHandler:^(id result, NSError *error) {
                    Q_UNUSED(error)
                    QString str;
                    if (result)
                        str = QString::fromNSString([NSString stringWithFormat:@"%@", result]);
                    cb(str);
                }];
            } else {
                [m_webView evaluateJavaScript:nsJs completionHandler:nil];
            }
        }
    }

    QString nativePostMessage() const override
    {
        return QStringLiteral("(function(msg){ window.webkit.messageHandlers.pageAgent.postMessage(msg); })");
    }

    void destroy() override
    {
        if (m_webView) {
            [m_webView stopLoading];
            [m_webView.configuration.userContentController removeScriptMessageHandlerForName:@"pageAgent"];
            m_webView.navigationDelegate = nil;
            m_webView = nil;
        }
        if (m_navDelegate) {
            m_navDelegate.owner = nullptr;
            m_navDelegate = nil;
        }
        if (m_msgHandler) {
            m_msgHandler.owner = nullptr;
            m_msgHandler = nil;
        }
    }

    void onNavFinished(bool success, const QString &error)
    {
        setLoading(false);
        if (m_webView) {
            NSURL *currentUrl = m_webView.URL;
            if (currentUrl)
                updateUrlBar(QString::fromNSString(currentUrl.absoluteString));
        }
        emit navigationCompleted(success, error);
    }

protected:
    void applyViewport() override
    {
        if (!m_viewportHost || !m_container)
            return;
        styleViewportHost(m_viewportHost);
        m_container->setGeometry(viewportBounds(m_viewportHost->width(), m_viewportHost->height()));
    }

    void resizeEvent(QResizeEvent *event) override
    {
        WebViewWidget::resizeEvent(event);
        applyViewport();
    }

    void focusInEvent(QFocusEvent *event) override
    {
        WebViewWidget::focusInEvent(event);
        if (m_webView) {
            [[m_webView window] makeFirstResponder:m_webView];
        }
    }

private:
    WKWebView *m_webView = nil;
    MiniAppNavDelegate *m_navDelegate = nil;
    CopilotMessageHandler *m_msgHandler = nil;
    QWidget *m_viewportHost = nullptr;
    QWidget *m_container = nullptr;
};

// --- ObjC delegate implementation ---

@implementation MiniAppNavDelegate

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation
{
    Q_UNUSED(webView) Q_UNUSED(navigation)
    if (_owner) _owner->onNavFinished(true, QString());
}

- (void)webView:(WKWebView *)webView didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error
{
    Q_UNUSED(webView) Q_UNUSED(navigation)
    if (_owner) _owner->onNavFinished(false, QString::fromNSString(error.localizedDescription));
}

- (void)webView:(WKWebView *)webView didFailProvisionalNavigation:(WKNavigation *)navigation withError:(NSError *)error
{
    Q_UNUSED(webView) Q_UNUSED(navigation)
    if (_owner) _owner->onNavFinished(false, QString::fromNSString(error.localizedDescription));
}

@end

// --- Copilot message handler implementation ---

@implementation CopilotMessageHandler

- (void)userContentController:(WKUserContentController *)userContentController
      didReceiveScriptMessage:(WKScriptMessage *)message
{
    Q_UNUSED(userContentController)
    if (_owner && message.body && [message.body isKindOfClass:[NSString class]]) {
        _owner->handleCopilotMessage(QString::fromNSString((NSString *)message.body));
    }
}

@end

// Factory: macOS implementation
WebViewWidget *WebViewWidget::create(const QString &appId, const QUrl &url, int /*debugPort*/,
                                     QWidget *parent, const QString &userDataFolder,
                                     int /*proxyType*/, const QString &/*proxyHost*/,
                                     int /*proxyPort*/, const QString &/*proxyBypassList*/,
                                     bool /*allowCrossOrigin*/)
{
    return new WebViewWidgetMac(appId, url, parent, userDataFolder);
}

#include "WebViewWidget_mac.moc"
