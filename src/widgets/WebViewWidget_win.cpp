/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "WebViewWidget.h"

#include "FaviconIcon.h"
#include "BrowserProxyArgs.h"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSet>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabBar>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QWindow>

#include <atomic>
#include <memory>

#include <windows.h>
#include <aclapi.h>
#include <WebView2.h>

// Load CreateCoreWebView2EnvironmentWithOptions dynamically so we don't
// depend on WebView2LoaderStatic.lib (MSVC-only). The function lives in
// WebView2Loader.dll which ships with the WebView2 Runtime.
using CreateEnvironmentFn = HRESULT(STDAPICALLTYPE *)(
    PCWSTR, PCWSTR, IUnknown *,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);

static void ensureDirectoryWritable(const QString &path)
{
    QDir().mkpath(path);

    // Grant Everyone full control with inheritance so WebView2 can freely
    // create EBWebView/ and its subtree. This is the user's own AppData.
    EXPLICIT_ACCESS_W ea = {};
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
    ea.Trustee.ptstrName = const_cast<LPWSTR>(L"Everyone");

    PACL acl = nullptr;
    if (SetEntriesInAclW(1, &ea, nullptr, &acl) == ERROR_SUCCESS) {
        std::wstring wpath = path.toStdWString();
        SetNamedSecurityInfoW(
            wpath.data(),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr, nullptr, acl, nullptr);
        LocalFree(acl);
    }
}

static void nukeDirectory(const QString &path)
{
    QDir dir(path);
    if (dir.exists())
        dir.removeRecursively();
}

static CreateEnvironmentFn resolveCreateEnvironment()
{
    static CreateEnvironmentFn fn = []() -> CreateEnvironmentFn {
        HMODULE mod = LoadLibraryW(L"WebView2Loader.dll");
        if (!mod) return nullptr;
        return reinterpret_cast<CreateEnvironmentFn>(
            GetProcAddress(mod, "CreateCoreWebView2EnvironmentWithOptions"));
    }();
    return fn;
}

// Owns in-flight WebView2 parent widgets after their browser tab is gone.
// QWidget::setParent recreates a native HWND; QWindow::setParent does not, so
// the controller can still Close against the same parent it was created on.
QWidget *detachedHostAnchor()
{
    static QWidget *anchor = nullptr;
    if (!anchor) {
        anchor = new QWidget;
        anchor->setAttribute(Qt::WA_DontShowOnScreen);
        anchor->setAttribute(Qt::WA_NativeWindow);
        anchor->winId();
        anchor->hide();
    }
    return anchor;
}

void salvageHost(QWidget *host)
{
    if (!host)
        return;
    host->hide();
    host->winId();
    QWidget *anchor = detachedHostAnchor();
    if (QWindow *window = host->windowHandle())
        window->setParent(anchor->windowHandle());
    host->QObject::setParent(anchor);
}

void closeBorrowedController(ICoreWebView2Controller *controller)
{
    if (!controller)
        return;
    // Invoke's pointer is borrowed for the call. Own a ref across Close.
    controller->AddRef();
    controller->Close();
    controller->Release();
}

void injectPageScripts(ICoreWebView2 *webView)
{
    if (!webView)
        return;
    static const wchar_t kFetchJs[] =
        L"Object.defineProperty(window,'__nai_fetch',{"
        L"value:window.fetch.bind(window),"
        L"writable:false,configurable:false,enumerable:false});";
    webView->AddScriptToExecuteOnDocumentCreated(kFetchJs, nullptr);
}

// WebView2 Windows implementation using the COM SDK directly.
// Async initialization: HWND → Environment → Controller → WebView → Navigate.
class WebViewWidgetWin : public WebViewWidget
{

public:
    WebViewWidgetWin(const QString &appId, const QUrl &url, int debugPort, QWidget *parent,
                     const QString &userDataFolder, int proxyType, const QString &proxyHost,
                     int proxyPort, const QString &proxyBypassList, bool allowCrossOrigin)
        : WebViewWidget(appId, url, parent)
        , m_debugPort(debugPort)
        , m_customUserDataFolder(userDataFolder)
        , m_proxyType(proxyType)
        , m_proxyHost(proxyHost)
        , m_proxyPort(proxyPort)
        , m_proxyBypassList(proxyBypassList)
        , m_allowCrossOrigin(allowCrossOrigin)
    {
        m_tabBar = new QTabBar(this);
        m_tabBar->setDocumentMode(true);
        m_tabBar->setExpanding(false);
        m_tabBar->setDrawBase(false);
        m_tabBar->setElideMode(Qt::ElideRight);
        m_tabBar->setTabsClosable(true);
        m_tabBar->setUsesScrollButtons(true);
        m_tabBar->hide();
        m_tabBar->installEventFilter(this);
        connect(m_tabBar, &QTabBar::currentChanged, this, [this](int index) {
            if (!m_switching && index >= 0)
                activatePage(index);
        });
        connect(m_tabBar, &QTabBar::tabCloseRequested, this, [this](int index) {
            closePage(index);
        });
        mainLayout()->insertWidget(0, m_tabBar);

        m_stack = new QStackedWidget(this);
        mainLayout()->addWidget(m_stack, 1);

        m_hostWidget = new QWidget(m_stack);
        m_hostWidget->setAttribute(Qt::WA_NativeWindow);
        m_hostWidget->setAttribute(Qt::WA_DontCreateNativeAncestors, false);
        m_hostWidget->setFocusPolicy(Qt::ClickFocus);
        styleViewportHost(m_hostWidget);
        m_stack->addWidget(m_hostWidget);
    }

    ~WebViewWidgetWin() override
    {
        destroy();
    }

    void initialize() override
    {
        if (m_initStarted) return;
        m_initStarted = true;
        m_hwnd = reinterpret_cast<HWND>(m_hostWidget->winId());
        m_dbgInitCalled = true;
        initWebView2();
    }

    QString debugInfo() const override
    {
        QString info;
        info += QStringLiteral("--- WebView2 Win Debug ---\n");
        info += QStringLiteral("initCalled: %1\n").arg(m_dbgInitCalled ? "true" : "false");
        info += QStringLiteral("HWND: 0x%1\n").arg(reinterpret_cast<quintptr>(m_hwnd), 0, 16);
        info += QStringLiteral("hostWidget visible: %1\n").arg(m_hostWidget ? (m_hostWidget->isVisible() ? "true" : "false") : "null");
        info += QStringLiteral("hostWidget size: %1x%2\n").arg(m_hostWidget ? m_hostWidget->width() : 0).arg(m_hostWidget ? m_hostWidget->height() : 0);
        info += QStringLiteral("loaderResolved: %1\n").arg(m_dbgLoaderResolved ? "true" : "false");
        info += QStringLiteral("createEnvHR: 0x%1\n").arg(static_cast<unsigned>(m_dbgCreateEnvHr), 8, 16, QLatin1Char('0'));
        info += QStringLiteral("envCreated: %1\n").arg(m_environment ? "true" : "false");
        info += QStringLiteral("envCallbackHR: 0x%1\n").arg(static_cast<unsigned>(m_dbgEnvCallbackHr), 8, 16, QLatin1Char('0'));
        info += QStringLiteral("controllerCreated: %1\n").arg(m_controller ? "true" : "false");
        info += QStringLiteral("ctrlCallbackHR: 0x%1\n").arg(static_cast<unsigned>(m_dbgCtrlCallbackHr), 8, 16, QLatin1Char('0'));
        info += QStringLiteral("webViewReady: %1\n").arg(m_webView ? "true" : "false");
        info += QStringLiteral("navigateCalled: %1\n").arg(m_dbgNavigateCalled ? "true" : "false");
        info += QStringLiteral("navCompleted: %1\n").arg(m_dbgNavCompleted ? "true" : "false");
        info += QStringLiteral("userDataFolder: %1\n").arg(m_dbgUserDataFolder);
        return info;
    }

    void navigate(const QUrl &url) override
    {
        if (!m_webView) return;
        setLoading(true);
        m_webView->Navigate(url.toString().toStdWString().c_str());
    }

    void reload() override
    {
        if (!m_webView) return;
        setLoading(true);
        m_webView->Reload();
    }

    void stop() override
    {
        if (!m_webView) return;
        m_webView->Stop();
        setLoading(false);
    }

    void goBack() override
    {
        if (!m_webView) return;
        m_webView->GoBack();
    }

    void goForward() override
    {
        if (!m_webView) return;
        m_webView->GoForward();
    }

    void executeScript(const QString &js, std::function<void(const QString &)> callback) override
    {
        if (!m_webView) {
            if (callback) callback(QString());
            return;
        }
        struct ScriptHandler : ICoreWebView2ExecuteScriptCompletedHandler {
            std::function<void(const QString &)> cb;
            ULONG refCount = 1;
            ScriptHandler(std::function<void(const QString &)> c) : cb(std::move(c)) {}
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
                if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2ExecuteScriptCompletedHandler)) {
                    *ppv = this; AddRef(); return S_OK;
                }
                *ppv = nullptr; return E_NOINTERFACE;
            }
            ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
            ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
            HRESULT STDMETHODCALLTYPE Invoke(HRESULT, LPCWSTR result) override {
                if (cb) cb(result ? QString::fromWCharArray(result) : QString());
                return S_OK;
            }
        };
        m_webView->ExecuteScript(js.toStdWString().c_str(),
                                 callback ? new ScriptHandler(std::move(callback)) : nullptr);
    }

    QString nativePostMessage() const override
    {
        return QStringLiteral("window.chrome.webview.postMessage");
    }

    void ensureCspBypassed() override
    {
        if (m_cspBypassed || !m_webView) return;
        m_cspBypassed = true;
        m_webView->CallDevToolsProtocolMethod(L"Page.setBypassCSP",
            L"{\"enabled\":true}", nullptr);
    }

    void destroy() override
    {
        if (!m_alive->exchange(false, std::memory_order_acq_rel))
            return;

        // Move in-flight parent widgets off this tree before any callback can
        // run. Their HWND stays the one WebView2 was created against.
        for (auto it = m_deferredHosts.cbegin(); it != m_deferredHosts.cend(); ++it)
            salvageHost(it.value());
        m_deferredHosts.clear();
        if (m_pages.isEmpty()) {
            if (m_initialControllerPending)
                salvageHost(m_hostWidget);
        } else {
            for (Page &page : m_pages) {
                if (!page.controller && m_inflightPages.contains(page.id))
                    salvageHost(page.host);
            }
        }

        if (m_pages.isEmpty()) {
            if (m_controller) {
                m_controller->Close();
                m_controller->Release();
            }
            if (m_webView)
                m_webView->Release();
            m_controller = nullptr;
            m_webView = nullptr;
        } else {
            for (Page &page : m_pages) {
                if (page.controller) {
                    page.controller->Close();
                    page.controller->Release();
                    page.controller = nullptr;
                }
                if (page.webView) {
                    page.webView->Release();
                    page.webView = nullptr;
                }
            }
            m_pages.clear();
            m_controller = nullptr;
            m_webView = nullptr;
        }
        m_inflightPages.clear();
        m_initialControllerPending = false;

        abandonPendingWindows();

        if (m_cdpPollTimer) {
            m_cdpPollTimer->stop();
            m_cdpPollTimer->deleteLater();
            m_cdpPollTimer = nullptr;
        }
        if (m_cdpNam) {
            m_cdpNam->deleteLater();
            m_cdpNam = nullptr;
        }

        if (m_environment) {
            m_environment->Release();
            m_environment = nullptr;
        }
        hideCdpUrl();
    }

    void notifyFocusLost(QWidget *newFocusWidget) override
    {
        if (!m_controller) return;
        HWND focused = GetFocus();
        if (!focused) return;
        if (focused == m_hwnd || IsChild(m_hwnd, focused)) {
            HWND target = newFocusWidget
                ? GetAncestor(reinterpret_cast<HWND>(newFocusWidget->effectiveWinId()), GA_ROOT)
                : GetAncestor(m_hwnd, GA_ROOT);
            if (target)
                SetFocus(target);
        }
    }

protected:
    void applyControllerBounds(ICoreWebView2Controller *controller, QWidget *host)
    {
        if (!controller || !host)
            return;
        const QRect r = viewportBounds(host->width(), host->height());
        RECT bounds;
        bounds.left = r.x();
        bounds.top = r.y();
        bounds.right = r.x() + r.width();
        bounds.bottom = r.y() + r.height();
        controller->put_Bounds(bounds);
    }

    void applyTouchEmulation(ICoreWebView2 *webView)
    {
        if (!webView)
            return;
        const bool touch = touchViewport();
        const bool modeChanged = (webView != m_touchView || touch != m_touchOn);
        if (modeChanged) {
            m_touchView = webView;
            m_touchOn = touch;
            const wchar_t *mouseParams = touch
                ? L"{\"enabled\":true,\"configuration\":\"mobile\"}"
                : L"{\"enabled\":false}";
            const wchar_t *touchParams = touch ? L"{\"enabled\":true}" : L"{\"enabled\":false}";
            webView->CallDevToolsProtocolMethod(
                L"Emulation.setEmitTouchEventsForMouse", mouseParams, nullptr);
            webView->CallDevToolsProtocolMethod(
                L"Emulation.setTouchEmulationEnabled", touchParams, nullptr);
            QString uaJson;
            if (!touch) {
                uaJson = QStringLiteral("{\"userAgent\":\"\"}");
            } else if (viewportMode() == ViewportMode::Mobile) {
                uaJson = QStringLiteral(
                    "{\"userAgent\":\"Mozilla/5.0 (iPhone; CPU iPhone OS 17_6 like Mac OS X) "
                    "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.6 Mobile/15E148 Safari/604.1\","
                    "\"platform\":\"iPhone\"}");
            } else {
                uaJson = QStringLiteral(
                    "{\"userAgent\":\"Mozilla/5.0 (iPad; CPU OS 17_6 like Mac OS X) "
                    "AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.6 Mobile/15E148 Safari/604.1\","
                    "\"platform\":\"iPad\"}");
            }
            webView->CallDevToolsProtocolMethod(
                L"Emulation.setUserAgentOverride", uaJson.toStdWString().c_str(), nullptr);
        }

        const QRect r = m_hostWidget
            ? viewportBounds(m_hostWidget->width(), m_hostWidget->height())
            : QRect();
        const int w = qMax(1, r.width());
        const int h = qMax(1, r.height());
        const int dpr = (viewportMode() == ViewportMode::Mobile) ? 3 : 2;
        if (!touch) {
            if (m_metricsOn) {
                m_metricsOn = false;
                webView->CallDevToolsProtocolMethod(
                    L"Emulation.clearDeviceMetricsOverride", L"{}", nullptr);
            }
            return;
        }
        if (m_metricsOn && webView == m_metricsView && w == m_metricsW && h == m_metricsH && dpr == m_metricsDpr)
            return;
        m_metricsOn = true;
        m_metricsView = webView;
        m_metricsW = w;
        m_metricsH = h;
        m_metricsDpr = dpr;
        const QString metrics = QStringLiteral(
            "{\"width\":%1,\"height\":%2,\"deviceScaleFactor\":%3,\"mobile\":true,"
            "\"screenWidth\":%1,\"screenHeight\":%2,"
            "\"screenOrientation\":{\"type\":\"portraitPrimary\",\"angle\":0}}")
                                    .arg(w)
                                    .arg(h)
                                    .arg(dpr);
        webView->CallDevToolsProtocolMethod(
            L"Emulation.setDeviceMetricsOverride", metrics.toStdWString().c_str(), nullptr);
    }

    void applyViewport() override
    {
        styleViewportHost(m_hostWidget);
        applyControllerBounds(m_controller, m_hostWidget);
        if (!m_webView)
            return;
        if (m_webView != m_touchView || touchViewport() != m_touchOn) {
            applyTouchEmulation(m_webView);
            return;
        }
        if (!m_emulationTimer) {
            m_emulationTimer = new QTimer(this);
            m_emulationTimer->setSingleShot(true);
            m_emulationTimer->setInterval(50);
            connect(m_emulationTimer, &QTimer::timeout, this, [this]() {
                applyTouchEmulation(m_webView);
            });
        }
        m_emulationTimer->start();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        applyViewport();
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_tabBar && event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::MiddleButton) {
                const int idx = m_tabBar->tabAt(me->position().toPoint());
                if (idx >= 0)
                    closePage(idx);
                return true;
            }
        }
        return WebViewWidget::eventFilter(watched, event);
    }

private:
    void initWebView2()
    {
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (m_customUserDataFolder.isEmpty()) {
            m_dbgUserDataFolder = QDir::toNativeSeparators(
                appData + QStringLiteral("/MiniApps/") + appId());
        } else {
            m_dbgUserDataFolder = QDir::toNativeSeparators(m_customUserDataFolder);
        }
        ensureDirectoryWritable(m_dbgUserDataFolder);

        auto createEnv = resolveCreateEnvironment();
        m_dbgLoaderResolved = (createEnv != nullptr);
        if (!createEnv) {
            emit navigationCompleted(false, tr("WebView2Loader.dll not found. Please install Microsoft Edge WebView2 Runtime."));
            return;
        }

        initWebView2Core();
    }

    void initWebView2Core()
    {
        auto createEnv = resolveCreateEnvironment();
        if (!createEnv) return;

        const QString argsStr = buildBrowserArgs(m_debugPort, m_proxyType, m_proxyHost, m_proxyPort, m_proxyBypassList, m_allowCrossOrigin);

        // Set browser arguments via environment variable. WebView2Loader.dll reads
        // this synchronously during CreateCoreWebView2EnvironmentWithOptions before
        // returning, so save/restore is safe on the single-threaded GUI thread.
        const QByteArray envName = "WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS";
        const QByteArray prevEnv = qgetenv(envName.constData());
        const bool hadEnv = !prevEnv.isNull();

        if (!argsStr.isEmpty()) {
            QByteArray combined = prevEnv;
            if (!combined.isEmpty())
                combined += ' ';
            combined += argsStr.toUtf8();
            qputenv(envName.constData(), combined);
        }

        HRESULT hr = createEnv(
            nullptr,
            m_dbgUserDataFolder.toStdWString().c_str(),
            nullptr,
            new EnvironmentCompletedHandler(this));

        if (!argsStr.isEmpty()) {
            if (hadEnv)
                qputenv(envName.constData(), prevEnv);
            else
                qunsetenv(envName.constData());
        }

        m_dbgCreateEnvHr = hr;
        if (FAILED(hr)) {
            if (!m_retried) {
                m_retried = true;
                nukeDirectory(m_dbgUserDataFolder);
                ensureDirectoryWritable(m_dbgUserDataFolder);
                initWebView2Core();
                return;
            }
            emit navigationCompleted(false, tr("Failed to create WebView2 environment (0x%1)")
                                                .arg(static_cast<unsigned>(hr), 8, 16, QLatin1Char('0')));
        }
    }

    void onEnvironmentCreated(HRESULT hr, ICoreWebView2Environment *env)
    {
        m_dbgEnvCallbackHr = hr;
        if (FAILED(hr) || !env) {
            if (!m_retried) {
                m_retried = true;
                nukeDirectory(m_dbgUserDataFolder);
                ensureDirectoryWritable(m_dbgUserDataFolder);
                initWebView2Core();
                return;
            }
            emit navigationCompleted(false, tr("WebView2 Runtime not available (0x%1)")
                                                .arg(static_cast<unsigned>(hr), 8, 16, QLatin1Char('0')));
            return;
        }
        m_environment = env;
        m_environment->AddRef();

        // Start CDP discovery polling if debug port is configured
        if (m_debugPort > 0)
            startCdpDiscovery();

        auto *handler = new ControllerCompletedHandler(this, 0, m_hostWidget);
        m_initialControllerPending = true;
        const HRESULT createHr = env->CreateCoreWebView2Controller(m_hwnd, handler);
        if (FAILED(createHr)) {
            m_initialControllerPending = false;
            handler->Release();
        }
    }

    void onControllerCreated(HRESULT hr, ICoreWebView2Controller *controller)
    {
        m_initialControllerPending = false;
        m_dbgCtrlCallbackHr = hr;
        if (FAILED(hr) || !controller) {
            if (!m_retried) {
                m_retried = true;
                if (m_environment) { m_environment->Release(); m_environment = nullptr; }
                nukeDirectory(m_dbgUserDataFolder);
                ensureDirectoryWritable(m_dbgUserDataFolder);
                initWebView2Core();
                return;
            }
            emit navigationCompleted(false, tr("Failed to create WebView2 controller (0x%1)")
                                                .arg(static_cast<unsigned>(hr), 8, 16, QLatin1Char('0')));
            return;
        }
        m_controller = controller;
        m_controller->AddRef();

        m_controller->get_CoreWebView2(&m_webView);
        if (!m_webView) {
            emit navigationCompleted(false, tr("Failed to get WebView2 core"));
            return;
        }

        // Configure settings
        ICoreWebView2Settings *settings = nullptr;
        m_webView->get_Settings(&settings);
        if (settings) {
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(TRUE);
            settings->put_AreDevToolsEnabled(TRUE);
            settings->Release();
        }

        // Set initial bounds
        applyControllerBounds(m_controller, m_hostWidget);
        applyTouchEmulation(m_webView);

        // Subscribe to NavigationCompleted
        EventRegistrationToken navToken;
        m_webView->add_NavigationCompleted(
            new NavigationCompletedHandler(this), &navToken);

        // Subscribe to ProcessFailed
        EventRegistrationToken procToken;
        m_webView->add_ProcessFailed(
            new ProcessFailedHandler(this), &procToken);

        // Subscribe to DocumentTitleChanged
        EventRegistrationToken titleToken;
        m_webView->add_DocumentTitleChanged(
            new DocumentTitleChangedHandler(this), &titleToken);

        ICoreWebView2_15 *webView15 = nullptr;
        if (SUCCEEDED(m_webView->QueryInterface(IID_ICoreWebView2_15,
                                                reinterpret_cast<void **>(&webView15)))
            && webView15) {
            EventRegistrationToken favToken;
            webView15->add_FaviconChanged(new FaviconChangedHandler(this), &favToken);
            webView15->Release();
        }

        // Subscribe to NewWindowRequested on every page, including ones opened
        // from target=_blank, so those can open further tabs.
        EventRegistrationToken newWinToken;
        m_webView->add_NewWindowRequested(
            new NewWindowRequestedHandler(this), &newWinToken);

        EventRegistrationToken closeToken;
        m_webView->add_WindowCloseRequested(
            new WindowCloseRequestedHandler(this), &closeToken);

        registerInitialPage();

        // Subscribe to SourceChanged to update the URL bar
        EventRegistrationToken srcToken;
        m_webView->add_SourceChanged(
            new SourceChangedHandler(this), &srcToken);

        // Inject page scripts before any page script runs
        injectPageScripts(m_webView);

        // NOTE: We deliberately do NOT pre-create a Trusted Types 'default' policy at
        // document creation. 'default' is a per-realm singleton — claiming it first makes
        // the page's own createPolicy('default') throw "already exists" (e.g. OWA/Outlook,
        // Teams), which aborts their boot and leaves the page unreachable. Page-agent
        // instead creates its policy lazily at run time (see executeCopilotCommand in
        // WebViewWidget.cpp): it tries 'default', and on failure falls back to the named
        // 'nai-page-agent' policy, degrading silently if even that is CSP-blocked.

        // Bypass ALL CSP enforcement via DevTools Protocol. Uses a completion
        // callback to guarantee the bypass is active before Navigate() fires.
        // Ungated — page-agent needs this regardless of allowCrossOrigin.
        struct CspBypassHandler : ICoreWebView2CallDevToolsProtocolMethodCompletedHandler {
            WebViewWidgetWin *owner;
            std::shared_ptr<std::atomic<bool>> alive;
            ULONG refCount = 1;
            CspBypassHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
                if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2CallDevToolsProtocolMethodCompletedHandler)) {
                    *ppv = this; AddRef(); return S_OK;
                }
                *ppv = nullptr; return E_NOINTERFACE;
            }
            ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
            ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
            HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, LPCWSTR) override {
                if (!alive->load(std::memory_order_acquire)) return S_OK;
                if (SUCCEEDED(hr))
                    owner->m_cspBypassed = true;
                else
                    qWarning("WebViewWidgetWin: Page.setBypassCSP failed (0x%08X)", static_cast<unsigned>(hr));
                owner->setLoading(true);
                owner->m_dbgNavigateCalled = true;
                owner->m_webView->Navigate(owner->initialUrl().toString().toStdWString().c_str());
                return S_OK;
            }
        };
        m_webView->CallDevToolsProtocolMethod(L"Page.setBypassCSP",
            L"{\"enabled\":true}", new CspBypassHandler(this));

        // Subscribe to WebMessage for copilot result callback
        EventRegistrationToken msgToken;
        m_webView->add_WebMessageReceived(
            new WebMessageReceivedHandler(this), &msgToken);

        // Sync Qt focus state when WebView2 grabs native focus. Without this,
        // Qt doesn't know focus left the previously focused widget, so clicking
        // back on that widget won't fire focusChanged (Qt thinks it already has
        // focus there).
        EventRegistrationToken gotFocusToken;
        m_controller->add_GotFocus(new GotFocusHandler(this), &gotFocusToken);

        // Navigate is triggered from CspBypassHandler::Invoke after CDP completes.
    }

    // --- Lightweight COM callback implementations (prevent DLL ref-counting) ---

    // EnvironmentCompletedHandler: called when CreateCoreWebView2EnvironmentWithOptions finishes.
    struct EnvironmentCompletedHandler : ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        EnvironmentCompletedHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment *env) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            owner->onEnvironmentCreated(hr, env);
            return S_OK;
        }
    };

    // ControllerCompletedHandler: called when CreateCoreWebView2Controller finishes.
    struct ControllerCompletedHandler : ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        quint64 pageId = 0;
        QPointer<QWidget> host;
        ControllerCompletedHandler(WebViewWidgetWin *o, quint64 id = 0, QWidget *h = nullptr)
            : owner(o), alive(o->m_alive), pageId(id), host(h) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller *ctrl) override {
            if (!alive->load(std::memory_order_acquire)) {
                closeBorrowedController(ctrl);
                if (host)
                    host->deleteLater();
                return S_OK;
            }
            if (pageId == 0)
                owner->onControllerCreated(hr, ctrl);
            else
                owner->onPopupControllerCreated(pageId, hr, ctrl);
            return S_OK;
        }
    };

    // NavigationCompletedHandler: fires when a navigation finishes.
    struct NavigationCompletedHandler : ICoreWebView2NavigationCompletedEventHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        NavigationCompletedHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2NavigationCompletedEventHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, ICoreWebView2NavigationCompletedEventArgs *args) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            BOOL success = FALSE;
            if (args) args->get_IsSuccess(&success);
            owner->m_dbgNavCompleted = true;
            if (owner->isActiveWebView(sender)) {
                owner->setLoading(false);
                emit owner->navigationCompleted(success, success ? QString() : QStringLiteral("Navigation failed"));
            }
            return S_OK;
        }
    };

    // ProcessFailedHandler: fires when the renderer crashes.
    struct ProcessFailedHandler : ICoreWebView2ProcessFailedEventHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        ProcessFailedHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2ProcessFailedEventHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *args) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
            if (args) args->get_ProcessFailedKind(&kind);
            QString desc;
            switch (kind) {
            case COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED:
                desc = QStringLiteral("Browser process exited"); break;
            case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED:
                desc = QStringLiteral("Renderer process exited"); break;
            case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE:
                desc = QStringLiteral("Renderer process unresponsive"); break;
            default:
                desc = QStringLiteral("Process failed"); break;
            }
            emit owner->processFailed(desc);
            owner->m_cspBypassed = false;
            return S_OK;
        }
    };

    // DocumentTitleChangedHandler: fires when the page title changes.
    struct DocumentTitleChangedHandler : ICoreWebView2DocumentTitleChangedEventHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        DocumentTitleChangedHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2DocumentTitleChangedEventHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, IUnknown *) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            LPWSTR title = nullptr;
            if (sender && SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                QString qtTitle = QString::fromWCharArray(title);
                CoTaskMemFree(title);
                owner->onPageTitle(sender, qtTitle);
            }
            return S_OK;
        }
    };

    static QByteArray bytesFromIStream(IStream *stream)
    {
        if (!stream)
            return {};
        QByteArray data;
        char buf[4096];
        for (;;) {
            ULONG n = 0;
            const HRESULT hr = stream->Read(buf, sizeof(buf), &n);
            if (n > 0)
                data.append(buf, static_cast<int>(n));
            if (FAILED(hr) || n == 0)
                break;
        }
        return data;
    }

    struct GetFaviconCompletedHandler : ICoreWebView2GetFaviconCompletedHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        ICoreWebView2 *page;
        GetFaviconCompletedHandler(WebViewWidgetWin *o, ICoreWebView2 *wv)
            : owner(o), alive(o->m_alive), page(wv) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown)
                || IsEqualIID(riid, IID_ICoreWebView2GetFaviconCompletedHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, IStream *result) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            if (!owner->isActiveWebView(page)) return S_OK;
            if (FAILED(errorCode) || !result) {
                emit owner->faviconChanged(QIcon());
                return S_OK;
            }
            emit owner->faviconChanged(faviconIconFromData(bytesFromIStream(result)));
            return S_OK;
        }
    };

    struct FaviconChangedHandler : ICoreWebView2FaviconChangedEventHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        FaviconChangedHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown)
                || IsEqualIID(riid, IID_ICoreWebView2FaviconChangedEventHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, IUnknown *) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            ICoreWebView2_15 *webView15 = nullptr;
            if (!sender
                || FAILED(sender->QueryInterface(IID_ICoreWebView2_15,
                                                 reinterpret_cast<void **>(&webView15)))
                || !webView15) {
                return S_OK;
            }
            webView15->GetFavicon(COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG,
                                  new GetFaviconCompletedHandler(owner, sender));
            webView15->Release();
            return S_OK;
        }
    };

    // NewWindowRequestedHandler: intercepts window.open / target="_blank" and navigates in-place.
    struct NewWindowRequestedHandler : ICoreWebView2NewWindowRequestedEventHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        NewWindowRequestedHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2NewWindowRequestedEventHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            owner->beginNewWindow(args);
            return S_OK;
        }
    };

    struct WindowCloseRequestedHandler : ICoreWebView2WindowCloseRequestedEventHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        WindowCloseRequestedHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2WindowCloseRequestedEventHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, IUnknown *) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            owner->closePageFor(sender);
            return S_OK;
        }
    };

    // SourceChangedHandler: fires when the URL changes (navigation, redirect, fragment change).
    struct SourceChangedHandler : ICoreWebView2SourceChangedEventHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        SourceChangedHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2SourceChangedEventHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, ICoreWebView2SourceChangedEventArgs *) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            LPWSTR uri = nullptr;
            if (sender && SUCCEEDED(sender->get_Source(&uri)) && uri) {
                QString url = QString::fromWCharArray(uri);
                CoTaskMemFree(uri);
                owner->onPageUrl(sender, url);
            }
            return S_OK;
        }
    };

    // WebMessageReceivedHandler: fires when page calls window.chrome.webview.postMessage().
    struct WebMessageReceivedHandler : ICoreWebView2WebMessageReceivedEventHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        WebMessageReceivedHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2WebMessageReceivedEventHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            LPWSTR msg = nullptr;
            if (args && SUCCEEDED(args->TryGetWebMessageAsString(&msg)) && msg) {
                owner->handleCopilotMessage(QString::fromWCharArray(msg));
                CoTaskMemFree(msg);
            }
            return S_OK;
        }
    };

    // GotFocusHandler: fires when WebView2 acquires native focus. Sets Qt focus
    // to the host widget so Qt's focus tracking stays in sync with Win32.
    struct GotFocusHandler : ICoreWebView2FocusChangedEventHandler {
        WebViewWidgetWin *owner;
        std::shared_ptr<std::atomic<bool>> alive;
        ULONG refCount = 1;
        GotFocusHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2FocusChangedEventHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2Controller *, IUnknown *) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            if (owner->m_hostWidget)
                owner->m_hostWidget->setFocus(Qt::OtherFocusReason);
            return S_OK;
        }
    };

    // --- CDP Discovery ---

    void startCdpDiscovery()
    {
        m_cdpNam = new QNetworkAccessManager(this);
        m_cdpPollCount = 0;
        m_cdpPollTimer = new QTimer(this);
        m_cdpPollTimer->setInterval(100);
        connect(m_cdpPollTimer, &QTimer::timeout, this, [this]() { onCdpPollTick(); });
        m_cdpPollTimer->start();
    }

    void onCdpPollTick()
    {
        if (++m_cdpPollCount > 50) {
            // Timeout: 50 * 100ms = 5 seconds
            m_cdpPollTimer->stop();
            qWarning("WebViewWidgetWin: CDP discovery timed out on port %d", m_debugPort);
            return;
        }

        const QString url = QStringLiteral("http://127.0.0.1:%1/json/version").arg(m_debugPort);
        QNetworkRequest req{QUrl(url)};
        req.setTransferTimeout(500);
        QNetworkReply *reply = m_cdpNam->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (!m_cdpPollTimer || !m_cdpPollTimer->isActive())
                return;

            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 200) {
                m_cdpPollTimer->stop();
                const QByteArray body = reply->readAll();
                const QJsonDocument doc = QJsonDocument::fromJson(body);
                const QString wsUrl = doc.object().value(QStringLiteral("webSocketDebuggerUrl")).toString();
                const QString httpUrl = QStringLiteral("http://127.0.0.1:%1").arg(m_debugPort);
                showCdpUrl(httpUrl);
                emit cdpReady(httpUrl, wsUrl);
            }
            // else: keep polling on next timer tick
        });
    }

    struct Page {
        quint64 id = 0;
        QWidget *host = nullptr;
        HWND hwnd = nullptr;
        ICoreWebView2Controller *controller = nullptr;
        ICoreWebView2 *webView = nullptr;
        QString title;
        QString url;
    };

    struct PendingWindow {
        ICoreWebView2NewWindowRequestedEventArgs *args = nullptr;
        ICoreWebView2Deferral *deferral = nullptr;
        quint64 pageId = 0;
    };

    void registerInitialPage()
    {
        if (!m_pages.isEmpty() || !m_webView || !m_tabBar)
            return;
        Page page;
        page.id = ++m_nextPageId;
        page.host = m_hostWidget;
        page.hwnd = m_hwnd;
        page.controller = m_controller;
        page.webView = m_webView;
        page.url = initialUrl().toString();
        page.title = initialUrl().host();
        m_pages.append(page);
        m_active = 0;
        m_switching = true;
        m_tabBar->addTab(page.title.isEmpty() ? tr("New tab") : page.title);
        m_tabBar->setTabToolTip(0, page.url);
        m_switching = false;
    }

    void beginNewWindow(ICoreWebView2NewWindowRequestedEventArgs *args)
    {
        if (!args)
            return;
        if (!m_environment || !m_stack || !m_tabBar) {
            args->put_Handled(TRUE);
            return;
        }
        ICoreWebView2Deferral *deferral = nullptr;
        if (FAILED(args->GetDeferral(&deferral)) || !deferral) {
            args->put_Handled(TRUE);
            return;
        }
        args->AddRef();

        auto *host = new QWidget(m_stack);
        host->setAttribute(Qt::WA_NativeWindow);
        host->setAttribute(Qt::WA_DontCreateNativeAncestors, false);
        host->setFocusPolicy(Qt::ClickFocus);
        styleViewportHost(host);
        m_stack->addWidget(host);

        QString title = tr("New tab");
        QString url;
        LPWSTR uri = nullptr;
        if (SUCCEEDED(args->get_Uri(&uri)) && uri) {
            url = QString::fromWCharArray(uri);
            CoTaskMemFree(uri);
            const QUrl parsed(url);
            if (!parsed.host().isEmpty())
                title = parsed.host();
            else if (!url.isEmpty())
                title = url;
        }

        Page page;
        page.id = ++m_nextPageId;
        page.host = host;
        page.hwnd = reinterpret_cast<HWND>(host->winId());
        page.title = title;
        page.url = url;
        m_pages.append(page);
        const int index = m_pages.size() - 1;

        m_switching = true;
        m_tabBar->addTab(title);
        m_tabBar->setTabToolTip(index, url);
        m_switching = false;
        if (m_pages.size() > 1)
            m_tabBar->show();

        m_pending.append(PendingWindow{args, deferral, page.id});
        qInfo("WebView: opening tab %s", qUtf8Printable(url.isEmpty() ? title : url));
        auto *handler = new ControllerCompletedHandler(this, page.id, host);
        m_inflightPages.insert(page.id);
        const HRESULT createHr = m_environment->CreateCoreWebView2Controller(page.hwnd, handler);
        if (FAILED(createHr)) {
            m_inflightPages.remove(page.id);
            handler->Release();
            qWarning("WebView: CreateCoreWebView2Controller failed (0x%08X)",
                     static_cast<unsigned>(createHr));
            releasePending(takePending(page.id));
            dropFailedPage(pageIndexById(page.id));
        } else {
            handler->Release();
        }
    }

    void onPopupControllerCreated(quint64 pageId, HRESULT hr, ICoreWebView2Controller *controller)
    {
        m_inflightPages.remove(pageId);
        const int index = pageIndexById(pageId);
        if (FAILED(hr) || !controller || index < 0) {
            if (FAILED(hr) || !controller)
                qWarning("WebView: new tab controller failed (0x%08X)", static_cast<unsigned>(hr));
            closeBorrowedController(controller);
            releasePending(takePending(pageId));
            if (index >= 0)
                dropFailedPage(index);
            else if (QWidget *host = m_deferredHosts.take(pageId))
                host->deleteLater();
            return;
        }

        Page &page = m_pages[index];
        if (!page.host) {
            closeBorrowedController(controller);
            releasePending(takePending(pageId));
            dropFailedPage(index);
            return;
        }
        page.controller = controller;
        page.controller->AddRef();
        page.controller->get_CoreWebView2(&page.webView);
        if (!page.webView) {
            releasePending(takePending(pageId));
            dropFailedPage(index);
            return;
        }

        ICoreWebView2Settings *settings = nullptr;
        page.webView->get_Settings(&settings);
        if (settings) {
            settings->put_IsStatusBarEnabled(FALSE);
            settings->put_AreDefaultContextMenusEnabled(TRUE);
            settings->put_AreDevToolsEnabled(TRUE);
            settings->Release();
        }

        applyControllerBounds(page.controller, page.host);
        applyTouchEmulation(page.webView);
        wirePageEvents(page.webView, page.controller);

        injectPageScripts(page.webView);

        // Hand the still-unnavigated view to WebView2 only after this returns.
        // Completing the deferral is what lets the opener navigate it; doing that
        // before put_NewWindow is what creates the floating popup.
        struct PopupCspHandler : ICoreWebView2CallDevToolsProtocolMethodCompletedHandler {
            WebViewWidgetWin *owner;
            std::shared_ptr<std::atomic<bool>> alive;
            quint64 pageId;
            ULONG refCount = 1;
            PopupCspHandler(WebViewWidgetWin *o, quint64 id)
                : owner(o), alive(o->m_alive), pageId(id) {}
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
                if (IsEqualIID(riid, IID_IUnknown)
                    || IsEqualIID(riid, IID_ICoreWebView2CallDevToolsProtocolMethodCompletedHandler)) {
                    *ppv = this; AddRef(); return S_OK;
                }
                *ppv = nullptr; return E_NOINTERFACE;
            }
            ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
            ULONG STDMETHODCALLTYPE Release() override {
                if (--refCount == 0) { delete this; return 0; }
                return refCount;
            }
            HRESULT STDMETHODCALLTYPE Invoke(HRESULT, LPCWSTR) override {
                if (!alive->load(std::memory_order_acquire)) return S_OK;
                owner->finishPopup(pageId);
                return S_OK;
            }
        };
        auto *cspHandler = new PopupCspHandler(this, pageId);
        const HRESULT cspHr = page.webView->CallDevToolsProtocolMethod(
            L"Page.setBypassCSP", L"{\"enabled\":true}", cspHandler);
        if (FAILED(cspHr)) {
            cspHandler->Release();
            finishPopup(pageId);
        } else {
            cspHandler->Release();
        }
    }

    void finishPopup(quint64 pageId)
    {
        const int index = pageIndexById(pageId);
        const bool ok = index >= 0 && m_pages[index].webView;
        PendingWindow pending = takePending(pageId);
        if (pending.args) {
            if (ok)
                pending.args->put_NewWindow(m_pages[index].webView);
            pending.args->put_Handled(TRUE);
            pending.args->Release();
        }
        if (pending.deferral) {
            pending.deferral->Complete();
            pending.deferral->Release();
        }
        if (ok)
            activatePage(index);
        else if (index >= 0)
            dropFailedPage(index);
    }

    void wirePageEvents(ICoreWebView2 *webView, ICoreWebView2Controller *controller)
    {
        if (!webView)
            return;
        EventRegistrationToken token{};
        // add_* AddRefs. Drop our initial ref so the handler dies with the webview.
        auto subscribe = [](HRESULT hr, IUnknown *handler) {
            Q_UNUSED(hr);
            if (handler)
                handler->Release();
        };
        auto *nav = new NavigationCompletedHandler(this);
        subscribe(webView->add_NavigationCompleted(nav, &token), nav);
        auto *proc = new ProcessFailedHandler(this);
        subscribe(webView->add_ProcessFailed(proc, &token), proc);
        auto *title = new DocumentTitleChangedHandler(this);
        subscribe(webView->add_DocumentTitleChanged(title, &token), title);
        auto *win = new NewWindowRequestedHandler(this);
        subscribe(webView->add_NewWindowRequested(win, &token), win);
        auto *closed = new WindowCloseRequestedHandler(this);
        subscribe(webView->add_WindowCloseRequested(closed, &token), closed);
        auto *src = new SourceChangedHandler(this);
        subscribe(webView->add_SourceChanged(src, &token), src);
        auto *msg = new WebMessageReceivedHandler(this);
        subscribe(webView->add_WebMessageReceived(msg, &token), msg);
        ICoreWebView2_15 *webView15 = nullptr;
        if (SUCCEEDED(webView->QueryInterface(IID_ICoreWebView2_15,
                                              reinterpret_cast<void **>(&webView15)))
            && webView15) {
            auto *fav = new FaviconChangedHandler(this);
            subscribe(webView15->add_FaviconChanged(fav, &token), fav);
            webView15->Release();
        }
        if (controller) {
            auto *focus = new GotFocusHandler(this);
            subscribe(controller->add_GotFocus(focus, &token), focus);
        }
    }

    void activatePage(int index)
    {
        if (index < 0 || index >= m_pages.size())
            return;
        m_active = index;
        Page &page = m_pages[index];
        m_webView = page.webView;
        m_controller = page.controller;
        m_hostWidget = page.host;
        m_hwnd = page.hwnd;
        if (m_stack && page.host)
            m_stack->setCurrentWidget(page.host);
        for (int i = 0; i < m_pages.size(); ++i) {
            if (m_pages[i].hwnd)
                ShowWindow(m_pages[i].hwnd, i == index ? SW_SHOW : SW_HIDE);
        }
        applyControllerBounds(m_controller, page.host);
        applyTouchEmulation(m_webView);
        if (m_tabBar && m_tabBar->currentIndex() != index) {
            m_switching = true;
            m_tabBar->setCurrentIndex(index);
            m_switching = false;
        }
        updateUrlBar(page.url);
        if (!page.title.isEmpty())
            emit titleChanged(page.title);
    }

    void closePage(int index)
    {
        if (index < 0 || index >= m_pages.size())
            return;
        if (m_pages.size() == 1) {
            emit closeRequested();
            return;
        }
        const quint64 id = m_pages[index].id;
        const bool awaitingController = !m_pages[index].controller && m_inflightPages.contains(id);
        Page page = m_pages.takeAt(index);
        m_switching = true;
        if (m_tabBar && index < m_tabBar->count())
            m_tabBar->removeTab(index);
        m_switching = false;
        releasePending(takePending(id));
        if (page.controller) {
            page.controller->Close();
            page.controller->Release();
        }
        if (page.webView)
            page.webView->Release();
        if (awaitingController && m_inflightPages.contains(id) && page.host) {
            page.host->hide();
            m_deferredHosts.insert(id, page.host);
        } else if (page.host) {
            page.host->deleteLater();
        }

        if (m_pages.size() < 2 && m_tabBar)
            m_tabBar->hide();
        int next = m_active;
        if (index < m_active)
            --next;
        if (next >= m_pages.size())
            next = m_pages.size() - 1;
        activatePage(next);
    }

    void closePageFor(ICoreWebView2 *webView)
    {
        const int index = pageIndexOf(webView);
        if (index >= 0)
            closePage(index);
    }

    void dropFailedPage(int index)
    {
        if (index < 0 || index >= m_pages.size())
            return;
        Page page = m_pages.takeAt(index);
        m_switching = true;
        if (m_tabBar && index < m_tabBar->count())
            m_tabBar->removeTab(index);
        m_switching = false;
        if (page.controller) {
            page.controller->Close();
            page.controller->Release();
        }
        if (page.webView)
            page.webView->Release();
        if (page.host)
            page.host->deleteLater();
        if (m_pages.size() < 2 && m_tabBar)
            m_tabBar->hide();
        if (index < m_active)
            --m_active;
        if (m_active >= m_pages.size())
            m_active = qMax(0, m_pages.size() - 1);
        if (!m_pages.isEmpty() && m_stack && m_pages[m_active].host
            && m_stack->currentWidget() != m_pages[m_active].host)
            activatePage(m_active);
    }

    PendingWindow takePending(quint64 pageId)
    {
        for (int i = 0; i < m_pending.size(); ++i) {
            if (m_pending[i].pageId == pageId)
                return m_pending.takeAt(i);
        }
        return {};
    }

    void releasePending(PendingWindow pending)
    {
        if (pending.args) {
            pending.args->put_Handled(TRUE);
            pending.args->Release();
        }
        if (pending.deferral) {
            pending.deferral->Complete();
            pending.deferral->Release();
        }
    }

    int pageIndexById(quint64 pageId) const
    {
        for (int i = 0; i < m_pages.size(); ++i) {
            if (m_pages[i].id == pageId)
                return i;
        }
        return -1;
    }

    void abandonPendingWindows()
    {
        for (PendingWindow &pending : m_pending) {
            if (pending.args) {
                pending.args->put_Handled(TRUE);
                pending.args->Release();
            }
            if (pending.deferral) {
                pending.deferral->Complete();
                pending.deferral->Release();
            }
        }
        m_pending.clear();
    }

    int pageIndexOf(ICoreWebView2 *webView) const
    {
        for (int i = 0; i < m_pages.size(); ++i) {
            if (m_pages[i].webView == webView)
                return i;
        }
        return -1;
    }

    bool isActiveWebView(ICoreWebView2 *webView) const
    {
        return webView && webView == m_webView;
    }

    void onPageTitle(ICoreWebView2 *sender, const QString &title)
    {
        const int index = pageIndexOf(sender);
        if (index >= 0) {
            m_pages[index].title = title;
            if (m_tabBar && index < m_tabBar->count())
                m_tabBar->setTabText(index, title.isEmpty() ? tr("New tab") : title);
        }
        if (sender == m_webView)
            emit titleChanged(title);
    }

    void onPageUrl(ICoreWebView2 *sender, const QString &url)
    {
        const int index = pageIndexOf(sender);
        if (index >= 0) {
            m_pages[index].url = url;
            if (m_tabBar && index < m_tabBar->count())
                m_tabBar->setTabToolTip(index, url);
        }
        if (sender == m_webView)
            updateUrlBar(url);
    }

    QWidget *m_hostWidget = nullptr;
    HWND m_hwnd = nullptr;
    bool m_initStarted = false;
    bool m_retried = false;
    ICoreWebView2Environment *m_environment = nullptr;
    ICoreWebView2Controller *m_controller = nullptr;
    ICoreWebView2 *m_webView = nullptr;
    ICoreWebView2 *m_touchView = nullptr;
    bool m_touchOn = false;
    ICoreWebView2 *m_metricsView = nullptr;
    bool m_metricsOn = false;
    int m_metricsW = 0;
    int m_metricsH = 0;
    int m_metricsDpr = 0;
    QTimer *m_emulationTimer = nullptr;
    QTabBar *m_tabBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QVector<Page> m_pages;
    QVector<PendingWindow> m_pending;
    QHash<quint64, QWidget *> m_deferredHosts;
    QSet<quint64> m_inflightPages;
    bool m_initialControllerPending = false;
    int m_active = 0;
    quint64 m_nextPageId = 0;
    bool m_switching = false;

    // Shared flag for COM callback safety: handlers hold a copy and check
    // before dereferencing `owner`. Set to false in destroy() before releasing
    // COM objects so late-firing callbacks see the widget as gone.
    std::shared_ptr<std::atomic<bool>> m_alive = std::make_shared<std::atomic<bool>>(true);

    // Debug state
    bool m_dbgInitCalled = false;
    bool m_dbgLoaderResolved = false;
    bool m_dbgNavigateCalled = false;
    bool m_dbgNavCompleted = false;
    HRESULT m_dbgCreateEnvHr = S_OK;
    HRESULT m_dbgEnvCallbackHr = S_OK;
    HRESULT m_dbgCtrlCallbackHr = S_OK;
    QString m_dbgUserDataFolder;

    // CDP debug port
    int m_debugPort = 0;
    QString m_customUserDataFolder;
    int m_proxyType = 0;
    QString m_proxyHost;
    int m_proxyPort = 0;
    QString m_proxyBypassList;
    bool m_allowCrossOrigin = false;
    bool m_cspBypassed = false;
    QNetworkAccessManager *m_cdpNam = nullptr;
    QTimer *m_cdpPollTimer = nullptr;
    int m_cdpPollCount = 0;
};

// Factory: Windows implementation
WebViewWidget *WebViewWidget::create(const QString &appId, const QUrl &url, int debugPort,
                                     QWidget *parent, const QString &userDataFolder,
                                     int proxyType, const QString &proxyHost,
                                     int proxyPort, const QString &proxyBypassList,
                                     bool allowCrossOrigin)
{
    return new WebViewWidgetWin(appId, url, debugPort, parent, userDataFolder,
                                proxyType, proxyHost, proxyPort, proxyBypassList,
                                allowCrossOrigin);
}
