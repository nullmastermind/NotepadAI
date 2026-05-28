/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "WebViewWidget.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include <atomic>
#include <memory>

#include <windows.h>
#include <aclapi.h>
#include <WebView2.h>

#include "BrowserProxyArgs.h"

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

// WebView2 Windows implementation using the COM SDK directly.
// Async initialization: HWND → Environment → Controller → WebView → Navigate.
class WebViewWidgetWin : public WebViewWidget
{

public:
    WebViewWidgetWin(const QString &appId, const QUrl &url, int debugPort, QWidget *parent,
                     const QString &userDataFolder, int proxyType, const QString &proxyHost,
                     int proxyPort, const QString &proxyBypassList)
        : WebViewWidget(appId, url, parent)
        , m_debugPort(debugPort)
        , m_customUserDataFolder(userDataFolder)
        , m_proxyType(proxyType)
        , m_proxyHost(proxyHost)
        , m_proxyPort(proxyPort)
        , m_proxyBypassList(proxyBypassList)
    {
        m_hostWidget = new QWidget(this);
        m_hostWidget->setAttribute(Qt::WA_NativeWindow);
        m_hostWidget->setAttribute(Qt::WA_DontCreateNativeAncestors, false);
        mainLayout()->addWidget(m_hostWidget, 1);
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

    void destroy() override
    {
        m_alive->store(false, std::memory_order_release);

        if (m_cdpPollTimer) {
            m_cdpPollTimer->stop();
            m_cdpPollTimer->deleteLater();
            m_cdpPollTimer = nullptr;
        }
        if (m_cdpNam) {
            m_cdpNam->deleteLater();
            m_cdpNam = nullptr;
        }

        if (m_controller) {
            m_controller->Close();
            m_controller->Release();
            m_controller = nullptr;
        }
        if (m_webView) {
            m_webView->Release();
            m_webView = nullptr;
        }
        if (m_environment) {
            m_environment->Release();
            m_environment = nullptr;
        }
        hideCdpUrl();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (m_controller && m_hostWidget) {
            RECT bounds;
            bounds.left = 0;
            bounds.top = 0;
            bounds.right = m_hostWidget->width();
            bounds.bottom = m_hostWidget->height();
            m_controller->put_Bounds(bounds);
        }
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

        const QString argsStr = buildBrowserArgs(m_debugPort, m_proxyType, m_proxyHost, m_proxyPort, m_proxyBypassList);

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

        env->CreateCoreWebView2Controller(
            m_hwnd,
            new ControllerCompletedHandler(this));
    }

    void onControllerCreated(HRESULT hr, ICoreWebView2Controller *controller)
    {
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
        RECT bounds;
        bounds.left = 0;
        bounds.top = 0;
        bounds.right = m_hostWidget->width();
        bounds.bottom = m_hostWidget->height();
        m_controller->put_Bounds(bounds);

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

        // Block new windows — navigate in-place instead
        EventRegistrationToken newWinToken;
        m_webView->add_NewWindowRequested(
            new NewWindowRequestedHandler(this), &newWinToken);

        // Subscribe to SourceChanged to update the URL bar
        EventRegistrationToken srcToken;
        m_webView->add_SourceChanged(
            new SourceChangedHandler(this), &srcToken);

        // Inject pristine fetch reference before any page script runs
        const std::wstring pristineFetchJs = L"Object.defineProperty(window,'__nai_fetch',{"
            L"value:window.fetch.bind(window),"
            L"writable:false,configurable:false,enumerable:false});";
        m_webView->AddScriptToExecuteOnDocumentCreated(pristineFetchJs.c_str(), nullptr);

        // Bypass ALL CSP enforcement via DevTools Protocol. This is intentionally
        // broad: page-agent injects scripts and manipulates DOM on arbitrary sites,
        // so CSP (including Trusted Types) would block core functionality. Security
        // tradeoff is acceptable — these WebViews are automation browsers, not
        // sandboxed content viewers. Must be called before Navigate().
        HRESULT cspHr = m_webView->CallDevToolsProtocolMethod(L"Page.setBypassCSP",
            L"{\"enabled\":true}", nullptr);
        if (FAILED(cspHr))
            qWarning("WebViewWidgetWin: Page.setBypassCSP failed (0x%08X) — Trusted Types may block page-agent",
                     static_cast<unsigned>(cspHr));

        // Subscribe to WebMessage for copilot result callback
        EventRegistrationToken msgToken;
        m_webView->add_WebMessageReceived(
            new WebMessageReceivedHandler(this), &msgToken);

        // Navigate to initial URL
        setLoading(true);
        m_dbgNavigateCalled = true;
        m_webView->Navigate(initialUrl().toString().toStdWString().c_str());
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
        ControllerCompletedHandler(WebViewWidgetWin *o) : owner(o), alive(o->m_alive) {}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
            if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
                *ppv = this; AddRef(); return S_OK;
            }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount; }
        ULONG STDMETHODCALLTYPE Release() override { if (--refCount == 0) { delete this; return 0; } return refCount; }
        HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller *ctrl) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            owner->onControllerCreated(hr, ctrl);
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
        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) override {
            if (!alive->load(std::memory_order_acquire)) return S_OK;
            BOOL success = FALSE;
            if (args) args->get_IsSuccess(&success);
            owner->m_dbgNavCompleted = true;
            owner->setLoading(false);
            emit owner->navigationCompleted(success, success ? QString() : QStringLiteral("Navigation failed"));
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
                emit owner->titleChanged(qtTitle);
            }
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
            LPWSTR uri = nullptr;
            if (args && SUCCEEDED(args->get_Uri(&uri)) && uri) {
                owner->m_webView->Navigate(uri);
                CoTaskMemFree(uri);
            }
            if (args) args->put_Handled(TRUE);
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
                owner->updateUrlBar(url);
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

    QWidget *m_hostWidget = nullptr;
    HWND m_hwnd = nullptr;
    bool m_initStarted = false;
    bool m_retried = false;
    ICoreWebView2Environment *m_environment = nullptr;
    ICoreWebView2Controller *m_controller = nullptr;
    ICoreWebView2 *m_webView = nullptr;

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
    QNetworkAccessManager *m_cdpNam = nullptr;
    QTimer *m_cdpPollTimer = nullptr;
    int m_cdpPollCount = 0;
};

// Factory: Windows implementation
WebViewWidget *WebViewWidget::create(const QString &appId, const QUrl &url, int debugPort,
                                     QWidget *parent, const QString &userDataFolder,
                                     int proxyType, const QString &proxyHost,
                                     int proxyPort, const QString &proxyBypassList)
{
    return new WebViewWidgetWin(appId, url, debugPort, parent, userDataFolder,
                                proxyType, proxyHost, proxyPort, proxyBypassList);
}
