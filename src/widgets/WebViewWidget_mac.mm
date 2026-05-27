/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "WebViewWidget.h"

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

// macOS WebView implementation using WKWebView embedded via QWindow::fromWinId.
class WebViewWidgetMac : public WebViewWidget
{
    Q_OBJECT

public:
    WebViewWidgetMac(const QString &appId, const QUrl &url, QWidget *parent)
        : WebViewWidget(appId, url, parent)
    {
        @autoreleasepool {
            WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];

            // Tiered data persistence: macOS 14+ uses per-app isolated store.
            if (@available(macOS 14.0, *)) {
                QUuid ns = QUuid::fromString(QStringLiteral("{6ba7b810-9dad-11d1-80b4-00c04fd430c8}"));
                QUuid storeId = QUuid::createUuidV5(ns, appId);
                NSUUID *nsUuid = [[NSUUID alloc] initWithUUIDString:storeId.toString(QUuid::WithoutBraces).toNSString()];
                config.websiteDataStore = [WKWebsiteDataStore dataStoreForIdentifier:nsUuid];
            }

            NSRect frame = NSMakeRect(0, 0, 400, 300);
            m_webView = [[WKWebView alloc] initWithFrame:frame configuration:config];

            m_navDelegate = [[MiniAppNavDelegate alloc] init];
            m_navDelegate.owner = this;
            m_webView.navigationDelegate = m_navDelegate;

            QWindow *foreignWindow = QWindow::fromWinId(reinterpret_cast<WId>(m_webView));
            m_container = QWidget::createWindowContainer(foreignWindow, this);
            m_container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            mainLayout()->addWidget(m_container, 1);

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

    void destroy() override
    {
        if (m_webView) {
            [m_webView stopLoading];
            m_webView.navigationDelegate = nil;
            m_webView = nil;
        }
        if (m_navDelegate) {
            m_navDelegate.owner = nullptr;
            m_navDelegate = nil;
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

// Factory: macOS implementation
WebViewWidget *WebViewWidget::create(const QString &appId, const QUrl &url, int /*debugPort*/,
                                     QWidget *parent, const QString &/*userDataFolder*/,
                                     int /*proxyType*/, const QString &/*proxyHost*/,
                                     int /*proxyPort*/, const QString &/*proxyBypassList*/)
{
    return new WebViewWidgetMac(appId, url, parent);
}

#include "WebViewWidget_mac.moc"
