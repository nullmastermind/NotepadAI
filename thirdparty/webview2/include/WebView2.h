// Copyright (C) Microsoft Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license found in the
// LICENSE file distributed with the WebView2 SDK NuGet package.
//
// Expanded stub: Full ICoreWebView2 vtable (58 methods, SDK 1.0.2903.40)
// plus handler interfaces needed by WebViewWidget_win.cpp.
// Vtable order is frozen since SDK 0.9.430.

#pragma once

#ifndef __webview2_h__
#define __webview2_h__

#include <objbase.h>
#include <EventToken.h>

// Enums
typedef enum COREWEBVIEW2_PROCESS_FAILED_KIND {
    COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED = 0,
    COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED = 1,
    COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE = 2,
} COREWEBVIEW2_PROCESS_FAILED_KIND;

typedef enum COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION {
    COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_ALWAYS_ALLOW = 0,
    COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION_CANCEL = 1,
} COREWEBVIEW2_SERVER_CERTIFICATE_ERROR_ACTION;

typedef enum COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT {
    COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG = 0,
    COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_JPEG = 1,
} COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT;

typedef enum COREWEBVIEW2_FAVICON_IMAGE_FORMAT {
    COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG = 0,
    COREWEBVIEW2_FAVICON_IMAGE_FORMAT_JPEG = 1,
} COREWEBVIEW2_FAVICON_IMAGE_FORMAT;

typedef enum COREWEBVIEW2_WEB_RESOURCE_CONTEXT {
    COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL = 0,
} COREWEBVIEW2_WEB_RESOURCE_CONTEXT;

// Forward declarations
typedef interface ICoreWebView2 ICoreWebView2;
typedef interface ICoreWebView2Controller ICoreWebView2Controller;
typedef interface ICoreWebView2Environment ICoreWebView2Environment;
typedef interface ICoreWebView2Settings ICoreWebView2Settings;
typedef interface ICoreWebView2NavigationCompletedEventArgs ICoreWebView2NavigationCompletedEventArgs;
typedef interface ICoreWebView2ProcessFailedEventArgs ICoreWebView2ProcessFailedEventArgs;
typedef interface ICoreWebView2ServerCertificateErrorDetectedEventArgs ICoreWebView2ServerCertificateErrorDetectedEventArgs;
typedef interface ICoreWebView2NavigationStartingEventArgs ICoreWebView2NavigationStartingEventArgs;
typedef interface ICoreWebView2ContentLoadingEventArgs ICoreWebView2ContentLoadingEventArgs;
typedef interface ICoreWebView2SourceChangedEventArgs ICoreWebView2SourceChangedEventArgs;
typedef interface ICoreWebView2WebMessageReceivedEventArgs ICoreWebView2WebMessageReceivedEventArgs;
typedef interface ICoreWebView2ScriptDialogOpeningEventArgs ICoreWebView2ScriptDialogOpeningEventArgs;
typedef interface ICoreWebView2PermissionRequestedEventArgs ICoreWebView2PermissionRequestedEventArgs;
typedef interface ICoreWebView2NewWindowRequestedEventArgs ICoreWebView2NewWindowRequestedEventArgs;
typedef interface ICoreWebView2WebResourceRequestedEventArgs ICoreWebView2WebResourceRequestedEventArgs;
typedef interface ICoreWebView2DevToolsProtocolEventReceiver ICoreWebView2DevToolsProtocolEventReceiver;

// Handler forward declarations
typedef interface ICoreWebView2NavigationCompletedEventHandler ICoreWebView2NavigationCompletedEventHandler;
typedef interface ICoreWebView2ProcessFailedEventHandler ICoreWebView2ProcessFailedEventHandler;
typedef interface ICoreWebView2NavigationStartingEventHandler ICoreWebView2NavigationStartingEventHandler;
typedef interface ICoreWebView2ContentLoadingEventHandler ICoreWebView2ContentLoadingEventHandler;
typedef interface ICoreWebView2SourceChangedEventHandler ICoreWebView2SourceChangedEventHandler;
typedef interface ICoreWebView2HistoryChangedEventHandler ICoreWebView2HistoryChangedEventHandler;
typedef interface ICoreWebView2FrameNavigationStartingEventHandler ICoreWebView2FrameNavigationStartingEventHandler;
typedef interface ICoreWebView2FrameNavigationCompletedEventHandler ICoreWebView2FrameNavigationCompletedEventHandler;
typedef interface ICoreWebView2ScriptDialogOpeningEventHandler ICoreWebView2ScriptDialogOpeningEventHandler;
typedef interface ICoreWebView2PermissionRequestedEventHandler ICoreWebView2PermissionRequestedEventHandler;
typedef interface ICoreWebView2ProcessFailedEventHandler ICoreWebView2ProcessFailedEventHandler;
typedef interface ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler;
typedef interface ICoreWebView2ExecuteScriptCompletedHandler ICoreWebView2ExecuteScriptCompletedHandler;
typedef interface ICoreWebView2CapturePreviewCompletedHandler ICoreWebView2CapturePreviewCompletedHandler;
typedef interface ICoreWebView2WebMessageReceivedEventHandler ICoreWebView2WebMessageReceivedEventHandler;
typedef interface ICoreWebView2CallDevToolsProtocolMethodCompletedHandler ICoreWebView2CallDevToolsProtocolMethodCompletedHandler;
typedef interface ICoreWebView2NewWindowRequestedEventHandler ICoreWebView2NewWindowRequestedEventHandler;
typedef interface ICoreWebView2DocumentTitleChangedEventHandler ICoreWebView2DocumentTitleChangedEventHandler;
typedef interface ICoreWebView2FaviconChangedEventHandler ICoreWebView2FaviconChangedEventHandler;
typedef interface ICoreWebView2GetFaviconCompletedHandler ICoreWebView2GetFaviconCompletedHandler;
typedef interface ICoreWebView2_15 ICoreWebView2_15;
typedef interface ICoreWebView2ContainsFullScreenElementChangedEventHandler ICoreWebView2ContainsFullScreenElementChangedEventHandler;
typedef interface ICoreWebView2WebResourceRequestedEventHandler ICoreWebView2WebResourceRequestedEventHandler;
typedef interface ICoreWebView2WindowCloseRequestedEventHandler ICoreWebView2WindowCloseRequestedEventHandler;

// --- Interface definitions ---

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044770")
ICoreWebView2NavigationCompletedEventArgs : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE get_IsSuccess(BOOL *isSuccess) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044771")
ICoreWebView2ProcessFailedEventArgs : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE get_ProcessFailedKind(COREWEBVIEW2_PROCESS_FAILED_KIND *kind) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044772")
ICoreWebView2Settings : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE put_IsScriptEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsScriptEnabled(BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsWebMessageEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsWebMessageEnabled(BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultScriptDialogsEnabled(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDefaultScriptDialogsEnabled(BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsStatusBarEnabled(BOOL isStatusBarEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsStatusBarEnabled(BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDevToolsEnabled(BOOL areDevToolsEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDevToolsEnabled(BOOL *) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AreDefaultContextMenusEnabled(BOOL areDefaultContextMenusEnabled) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AreDefaultContextMenusEnabled(BOOL *) = 0;
};

// ICoreWebView2: Full vtable with all 58 methods in frozen order (SDK 0.9.430+).
// Slots 0-57 must remain in this exact order for vtable correctness.
MIDL_INTERFACE("76eceacb-0462-4d94-ac83-423a6793775e")
ICoreWebView2 : public IUnknown
{
public:
    // 0: get_Settings
    virtual HRESULT STDMETHODCALLTYPE get_Settings(ICoreWebView2Settings **settings) = 0;
    // 1: get_Source
    virtual HRESULT STDMETHODCALLTYPE get_Source(LPWSTR *uri) = 0;
    // 2: Navigate
    virtual HRESULT STDMETHODCALLTYPE Navigate(LPCWSTR uri) = 0;
    // 3: NavigateToString
    virtual HRESULT STDMETHODCALLTYPE NavigateToString(LPCWSTR htmlContent) = 0;
    // 4: add_NavigationStarting
    virtual HRESULT STDMETHODCALLTYPE add_NavigationStarting(
        ICoreWebView2NavigationStartingEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 5: remove_NavigationStarting
    virtual HRESULT STDMETHODCALLTYPE remove_NavigationStarting(EventRegistrationToken token) = 0;
    // 6: add_ContentLoading
    virtual HRESULT STDMETHODCALLTYPE add_ContentLoading(
        ICoreWebView2ContentLoadingEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 7: remove_ContentLoading
    virtual HRESULT STDMETHODCALLTYPE remove_ContentLoading(EventRegistrationToken token) = 0;
    // 8: add_SourceChanged
    virtual HRESULT STDMETHODCALLTYPE add_SourceChanged(
        ICoreWebView2SourceChangedEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 9: remove_SourceChanged
    virtual HRESULT STDMETHODCALLTYPE remove_SourceChanged(EventRegistrationToken token) = 0;
    // 10: add_HistoryChanged
    virtual HRESULT STDMETHODCALLTYPE add_HistoryChanged(
        ICoreWebView2HistoryChangedEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 11: remove_HistoryChanged
    virtual HRESULT STDMETHODCALLTYPE remove_HistoryChanged(EventRegistrationToken token) = 0;
    // 12: add_NavigationCompleted
    virtual HRESULT STDMETHODCALLTYPE add_NavigationCompleted(
        ICoreWebView2NavigationCompletedEventHandler *handler, EventRegistrationToken *token) = 0;
    // 13: remove_NavigationCompleted
    virtual HRESULT STDMETHODCALLTYPE remove_NavigationCompleted(EventRegistrationToken token) = 0;
    // 14: add_FrameNavigationStarting
    virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationStarting(
        ICoreWebView2FrameNavigationStartingEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 15: remove_FrameNavigationStarting
    virtual HRESULT STDMETHODCALLTYPE remove_FrameNavigationStarting(EventRegistrationToken token) = 0;
    // 16: add_FrameNavigationCompleted
    virtual HRESULT STDMETHODCALLTYPE add_FrameNavigationCompleted(
        ICoreWebView2FrameNavigationCompletedEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 17: remove_FrameNavigationCompleted
    virtual HRESULT STDMETHODCALLTYPE remove_FrameNavigationCompleted(EventRegistrationToken token) = 0;
    // 18: add_ScriptDialogOpening
    virtual HRESULT STDMETHODCALLTYPE add_ScriptDialogOpening(
        ICoreWebView2ScriptDialogOpeningEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 19: remove_ScriptDialogOpening
    virtual HRESULT STDMETHODCALLTYPE remove_ScriptDialogOpening(EventRegistrationToken token) = 0;
    // 20: add_PermissionRequested
    virtual HRESULT STDMETHODCALLTYPE add_PermissionRequested(
        ICoreWebView2PermissionRequestedEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 21: remove_PermissionRequested
    virtual HRESULT STDMETHODCALLTYPE remove_PermissionRequested(EventRegistrationToken token) = 0;
    // 22: add_ProcessFailed
    virtual HRESULT STDMETHODCALLTYPE add_ProcessFailed(
        ICoreWebView2ProcessFailedEventHandler *handler, EventRegistrationToken *token) = 0;
    // 23: remove_ProcessFailed
    virtual HRESULT STDMETHODCALLTYPE remove_ProcessFailed(EventRegistrationToken token) = 0;
    // 24: AddScriptToExecuteOnDocumentCreated
    virtual HRESULT STDMETHODCALLTYPE AddScriptToExecuteOnDocumentCreated(
        LPCWSTR javaScript,
        ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler *handler) = 0;
    // 25: RemoveScriptToExecuteOnDocumentCreated
    virtual HRESULT STDMETHODCALLTYPE RemoveScriptToExecuteOnDocumentCreated(LPCWSTR id) = 0;
    // 26: ExecuteScript
    virtual HRESULT STDMETHODCALLTYPE ExecuteScript(
        LPCWSTR javaScript,
        ICoreWebView2ExecuteScriptCompletedHandler *handler) = 0;
    // 27: CapturePreview
    virtual HRESULT STDMETHODCALLTYPE CapturePreview(
        COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT imageFormat,
        IStream *imageStream,
        ICoreWebView2CapturePreviewCompletedHandler *handler) = 0;
    // 28: Reload
    virtual HRESULT STDMETHODCALLTYPE Reload() = 0;
    // 29: PostWebMessageAsJson
    virtual HRESULT STDMETHODCALLTYPE PostWebMessageAsJson(LPCWSTR webMessageAsJson) = 0;
    // 30: PostWebMessageAsString
    virtual HRESULT STDMETHODCALLTYPE PostWebMessageAsString(LPCWSTR webMessageAsString) = 0;
    // 31: add_WebMessageReceived
    virtual HRESULT STDMETHODCALLTYPE add_WebMessageReceived(
        ICoreWebView2WebMessageReceivedEventHandler *handler, EventRegistrationToken *token) = 0;
    // 32: remove_WebMessageReceived
    virtual HRESULT STDMETHODCALLTYPE remove_WebMessageReceived(EventRegistrationToken token) = 0;
    // 33: CallDevToolsProtocolMethod
    virtual HRESULT STDMETHODCALLTYPE CallDevToolsProtocolMethod(
        LPCWSTR methodName, LPCWSTR parametersAsJson,
        ICoreWebView2CallDevToolsProtocolMethodCompletedHandler *handler) = 0;
    // 34: get_BrowserProcessId
    virtual HRESULT STDMETHODCALLTYPE get_BrowserProcessId(UINT32 *value) = 0;
    // 35: get_CanGoBack
    virtual HRESULT STDMETHODCALLTYPE get_CanGoBack(BOOL *canGoBack) = 0;
    // 36: get_CanGoForward
    virtual HRESULT STDMETHODCALLTYPE get_CanGoForward(BOOL *canGoForward) = 0;
    // 37: GoBack
    virtual HRESULT STDMETHODCALLTYPE GoBack() = 0;
    // 38: GoForward
    virtual HRESULT STDMETHODCALLTYPE GoForward() = 0;
    // 39: GetDevToolsProtocolEventReceiver
    virtual HRESULT STDMETHODCALLTYPE GetDevToolsProtocolEventReceiver(
        LPCWSTR eventName, ICoreWebView2DevToolsProtocolEventReceiver **receiver) = 0;
    // 40: Stop
    virtual HRESULT STDMETHODCALLTYPE Stop() = 0;
    // 41: add_NewWindowRequested
    virtual HRESULT STDMETHODCALLTYPE add_NewWindowRequested(
        ICoreWebView2NewWindowRequestedEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 42: remove_NewWindowRequested
    virtual HRESULT STDMETHODCALLTYPE remove_NewWindowRequested(EventRegistrationToken token) = 0;
    // 43: add_DocumentTitleChanged
    virtual HRESULT STDMETHODCALLTYPE add_DocumentTitleChanged(
        ICoreWebView2DocumentTitleChangedEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 44: remove_DocumentTitleChanged
    virtual HRESULT STDMETHODCALLTYPE remove_DocumentTitleChanged(EventRegistrationToken token) = 0;
    // 45: get_DocumentTitle
    virtual HRESULT STDMETHODCALLTYPE get_DocumentTitle(LPWSTR *title) = 0;
    // 46: AddHostObjectToScript
    virtual HRESULT STDMETHODCALLTYPE AddHostObjectToScript(LPCWSTR name, VARIANT *object) = 0;
    // 47: RemoveHostObjectFromScript
    virtual HRESULT STDMETHODCALLTYPE RemoveHostObjectFromScript(LPCWSTR name) = 0;
    // 48: OpenDevToolsWindow
    virtual HRESULT STDMETHODCALLTYPE OpenDevToolsWindow() = 0;
    // 49: add_ContainsFullScreenElementChanged
    virtual HRESULT STDMETHODCALLTYPE add_ContainsFullScreenElementChanged(
        ICoreWebView2ContainsFullScreenElementChangedEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 50: remove_ContainsFullScreenElementChanged
    virtual HRESULT STDMETHODCALLTYPE remove_ContainsFullScreenElementChanged(EventRegistrationToken token) = 0;
    // 51: get_ContainsFullScreenElement
    virtual HRESULT STDMETHODCALLTYPE get_ContainsFullScreenElement(BOOL *containsFullScreenElement) = 0;
    // 52: add_WebResourceRequested
    virtual HRESULT STDMETHODCALLTYPE add_WebResourceRequested(
        ICoreWebView2WebResourceRequestedEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 53: remove_WebResourceRequested
    virtual HRESULT STDMETHODCALLTYPE remove_WebResourceRequested(EventRegistrationToken token) = 0;
    // 54: AddWebResourceRequestedFilter
    virtual HRESULT STDMETHODCALLTYPE AddWebResourceRequestedFilter(
        LPCWSTR uri, COREWEBVIEW2_WEB_RESOURCE_CONTEXT resourceContext) = 0;
    // 55: RemoveWebResourceRequestedFilter
    virtual HRESULT STDMETHODCALLTYPE RemoveWebResourceRequestedFilter(
        LPCWSTR uri, COREWEBVIEW2_WEB_RESOURCE_CONTEXT resourceContext) = 0;
    // 56: add_WindowCloseRequested
    virtual HRESULT STDMETHODCALLTYPE add_WindowCloseRequested(
        ICoreWebView2WindowCloseRequestedEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    // 57: remove_WindowCloseRequested
    virtual HRESULT STDMETHODCALLTYPE remove_WindowCloseRequested(EventRegistrationToken token) = 0;
};


// ICoreWebView2_15: QI from ICoreWebView2. 48 unused slots are ICoreWebView2_2.._14
// so add_FaviconChanged lands at the frozen vtable index.
MIDL_INTERFACE("517B2D1D-7DAE-4A66-A4F4-10352FFB9518")
ICoreWebView2_15 : public ICoreWebView2
{
public:
    virtual HRESULT STDMETHODCALLTYPE _Unused_0() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_1() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_2() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_3() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_4() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_5() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_6() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_7() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_8() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_9() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_10() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_11() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_12() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_13() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_14() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_15() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_16() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_17() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_18() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_19() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_20() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_21() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_22() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_23() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_24() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_25() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_26() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_27() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_28() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_29() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_30() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_31() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_32() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_33() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_34() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_35() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_36() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_37() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_38() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_39() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_40() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_41() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_42() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_43() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_44() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_45() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_46() = 0;
    virtual HRESULT STDMETHODCALLTYPE _Unused_47() = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FaviconChanged(
        ICoreWebView2FaviconChangedEventHandler *eventHandler, EventRegistrationToken *token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FaviconChanged(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_FaviconUri(LPWSTR *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFavicon(
        COREWEBVIEW2_FAVICON_IMAGE_FORMAT format,
        ICoreWebView2GetFaviconCompletedHandler *completedHandler) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044774")
ICoreWebView2Controller : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2 **coreWebView2) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Bounds(RECT bounds) = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

typedef interface ICoreWebView2CreateCoreWebView2ControllerCompletedHandler ICoreWebView2CreateCoreWebView2ControllerCompletedHandler;

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044775")
ICoreWebView2Environment : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(
        HWND parentWindow,
        ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *handler) = 0;
};

// --- Handler (callback) interfaces ---

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044776")
ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment *createdEnvironment) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044777")
ICoreWebView2CreateCoreWebView2ControllerCompletedHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller *createdController) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044778")
ICoreWebView2NavigationCompletedEventHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, ICoreWebView2NavigationCompletedEventArgs *args) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa0044779")
ICoreWebView2ProcessFailedEventHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, ICoreWebView2ProcessFailedEventArgs *args) = 0;
};


MIDL_INTERFACE("2913da94-833d-4de0-8dca-900fc524a1a4")
ICoreWebView2FaviconChangedEventHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, IUnknown *args) = 0;
};

MIDL_INTERFACE("a2508329-7da8-49d7-8c05-fa125e4aee8d")
ICoreWebView2GetFaviconCompletedHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, IStream *result) = 0;
};

MIDL_INTERFACE("0c733a30-2a1c-11ce-ade5-0000aa004477a")
ICoreWebView2DocumentTitleChangedEventHandler : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, IUnknown *args) = 0;
};

// IID constants for handler interfaces (MinGW's MIDL_INTERFACE discards the UUID)
static const IID IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler =
    {0x0c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0x00,0xaa,0x00,0x44,0x76}};
static const IID IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler =
    {0x0c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0x00,0xaa,0x00,0x44,0x77}};
static const IID IID_ICoreWebView2NavigationCompletedEventHandler =
    {0x0c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0x00,0xaa,0x00,0x44,0x78}};
static const IID IID_ICoreWebView2ProcessFailedEventHandler =
    {0x0c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0x00,0xaa,0x00,0x44,0x79}};
static const IID IID_ICoreWebView2DocumentTitleChangedEventHandler =
    {0x0c733a30,0x2a1c,0x11ce,{0xad,0xe5,0x00,0x00,0xaa,0x00,0x44,0x7a}};
static const IID IID_ICoreWebView2_15 =
    {0x517B2D1D,0x7DAE,0x4A66,{0xA4,0xF4,0x10,0x35,0x2F,0xFB,0x95,0x18}};
static const IID IID_ICoreWebView2FaviconChangedEventHandler =
    {0x2913da94,0x833d,0x4de0,{0x8d,0xca,0x90,0x0f,0xc5,0x24,0xa1,0xa4}};
static const IID IID_ICoreWebView2GetFaviconCompletedHandler =
    {0xa2508329,0x7da8,0x49d7,{0x8c,0x05,0xfa,0x12,0x5e,0x4a,0xee,0x8d}};


// Global function: creates a WebView2 environment with options.
STDAPI CreateCoreWebView2EnvironmentWithOptions(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    IUnknown *environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *environmentCreatedHandler);

#endif // __webview2_h__
