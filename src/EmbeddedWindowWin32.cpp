/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "EmbeddedWindowWin32.h"

#ifdef Q_OS_WIN
#include <Windows.h>

namespace {
constexpr wchar_t EmbedPropertyName[] = L"NotepadAI.EmbeddedWindow.Token";

bool setLong(HWND hwnd, int index, LONG_PTR value)
{
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(hwnd, index, value);
    return previous != 0 || GetLastError() == ERROR_SUCCESS;
}

bool setParent(HWND hwnd, HWND parent)
{
    SetLastError(ERROR_SUCCESS);
    const HWND previous = SetParent(hwnd, parent);
    return previous != nullptr || GetLastError() == ERROR_SUCCESS;
}

bool sameIdentity(const EmbeddedWindowWin32::NativeWindowState &state)
{
    HWND hwnd = reinterpret_cast<HWND>(state.handle);
    DWORD pid = 0;
    const DWORD tid = IsWindow(hwnd) ? GetWindowThreadProcessId(hwnd, &pid) : 0;
    return tid == state.threadId && pid == state.processId;
}

WINDOWPLACEMENT placementFrom(const EmbeddedWindowWin32::NativeWindowState &s)
{
    WINDOWPLACEMENT p{sizeof(WINDOWPLACEMENT)};
    p.flags = s.placementFlags;
    p.showCmd = s.placementShowCmd;
    p.ptMinPosition = {s.placementMinX, s.placementMinY};
    p.ptMaxPosition = {s.placementMaxX, s.placementMaxY};
    p.rcNormalPosition = {s.placementNormalLeft, s.placementNormalTop,
                          s.placementNormalRight, s.placementNormalBottom};
    return p;
}
} // namespace
#endif

namespace EmbeddedWindowWin32 {

bool markWindow(quintptr handle, quintptr token)
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(handle);
    return IsWindow(hwnd) && !GetPropW(hwnd, EmbedPropertyName)
        && SetPropW(hwnd, EmbedPropertyName, reinterpret_cast<HANDLE>(token));
#else
    Q_UNUSED(handle) Q_UNUSED(token)
    return false;
#endif
}

bool isDirectChildOfHost(quintptr targetHandle, quintptr hostHandle)
{
#ifdef Q_OS_WIN
    HWND target = reinterpret_cast<HWND>(targetHandle);
    HWND host = reinterpret_cast<HWND>(hostHandle);
    return IsWindow(target) && IsWindow(host) && GetParent(target) == host;
#else
    Q_UNUSED(targetHandle) Q_UNUSED(hostHandle)
    return false;
#endif
}

bool hostRetainsForeignChild(quintptr hostHandle)
{
#ifdef Q_OS_WIN
    HWND host = reinterpret_cast<HWND>(hostHandle);
    if (!IsWindow(host))
        return false;
    struct Context {
        DWORD ownProcessId;
        bool found;
    } context{GetCurrentProcessId(), false};
    EnumChildWindows(host, [](HWND child, LPARAM data) -> BOOL {
        auto *context = reinterpret_cast<Context *>(data);
        DWORD childProcessId = 0;
        if (IsWindow(child) && GetWindowThreadProcessId(child, &childProcessId)
            && childProcessId != context->ownProcessId) {
            context->found = true;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
    return context.found;
#else
    Q_UNUSED(hostHandle)
    return false;
#endif
}

bool sameWindow(const NativeWindowState &state, quintptr token)
{
#ifdef Q_OS_WIN
    return sameIdentity(state) && GetPropW(reinterpret_cast<HWND>(state.handle), EmbedPropertyName)
        == reinterpret_cast<HANDLE>(token);
#else
    Q_UNUSED(state) Q_UNUSED(token)
    return false;
#endif
}

bool prepareEmbed(const NativeWindowState &s, quintptr hostHandle, quintptr token)
{
#ifdef Q_OS_WIN
    if (!sameWindow(s, token))
        return false;
    HWND hwnd = reinterpret_cast<HWND>(s.handle);
    const LONG_PTR style = (s.originalStyle | WS_CHILD | WS_CLIPSIBLINGS)
        & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX
            | WS_MAXIMIZEBOX | WS_SYSMENU);
    const LONG_PTR exStyle = s.originalExStyle & ~(WS_EX_APPWINDOW | WS_EX_TOPMOST);
    return setLong(hwnd, GWL_STYLE, style) && sameWindow(s, token)
        && setLong(hwnd, GWL_EXSTYLE, exStyle) && sameWindow(s, token)
        && setParent(hwnd, reinterpret_cast<HWND>(hostHandle))
        && GetParent(hwnd) == reinterpret_cast<HWND>(hostHandle) && sameWindow(s, token)
        && SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER
                            | SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
#else
    Q_UNUSED(s) Q_UNUSED(hostHandle) Q_UNUSED(token)
    return false;
#endif
}

bool restoreExact(const NativeWindowState &s, quintptr token)
{
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(s.handle);
    // Token ownership (a per-embed SetPropW value) is re-verified immediately
    // before EVERY mutation. IsWindow + pid/tid cannot detect HWND recycling
    // within the same process/thread, but a recycled window is a NEW window
    // that does NOT carry our property — so a failed sameWindow() means "this
    // is no longer our window; stop touching it" and we bail without mutating.
    if (!sameWindow(s, token))
        return !sameIdentity(s); // gone → released; alive-but-untagged → fail

    // THE release-defining step: reparent the window off our host. Once this
    // succeeds the window can no longer be destroyed with the host and the tab
    // is safe to reap. It runs FIRST and is the SOLE gate on the return value.
    // Everything afterwards (styles, geometry, placement, visibility, untag) is
    // best-effort cosmetic restoration: a cosmetic failure must NOT report the
    // window as "still attached", otherwise the controller vetoes the close and
    // NativeWindowHost::sync() immediately re-parents the window back into the
    // host — the endless in/out tab bounce.
    if (!setParent(hwnd, reinterpret_cast<HWND>(s.originalParent))) {
        qWarning("EmbeddedWindow: restoreExact SetParent failed (err=%lu)", GetLastError());
        return false;
    }

    // Cosmetic best-effort restoration; each is gated on the token so a window
    // recycled after detach is never touched, but none affect the result.
    if (sameWindow(s, token))
        setLong(hwnd, GWL_STYLE, s.originalStyle);
    if (sameWindow(s, token))
        setLong(hwnd, GWL_EXSTYLE, s.originalExStyle);
    if (!(s.originalStyle & WS_CHILD) && sameWindow(s, token))
        setLong(hwnd, GWLP_HWNDPARENT, s.originalOwner);
    RECT rect{s.rectLeft, s.rectTop, s.rectRight, s.rectBottom};
    if ((s.originalStyle & WS_CHILD) && s.originalParent)
        MapWindowPoints(HWND_DESKTOP, reinterpret_cast<HWND>(s.originalParent),
                        reinterpret_cast<POINT *>(&rect), 2);
    if (sameWindow(s, token)) {
        const HWND z = (s.originalExStyle & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(hwnd, z, rect.left, rect.top, rect.right - rect.left,
                     rect.bottom - rect.top, SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    if (s.placementValid && !(s.originalStyle & WS_CHILD) && sameWindow(s, token)) {
        WINDOWPLACEMENT placement = placementFrom(s);
        SetWindowPlacement(hwnd, &placement);
    }
    if (sameWindow(s, token)) {
        const int show = s.placementValid ? static_cast<int>(s.placementShowCmd) : SW_SHOWNOACTIVATE;
        ShowWindowAsync(hwnd, s.originallyVisible ? (show == SW_HIDE ? SW_SHOWNOACTIVATE : show) : SW_HIDE);
    }
    // Untag is best-effort: if the window was recycled the new owner keeps its
    // property; our host is already detached so no re-grab can occur.
    if (sameWindow(s, token))
        RemovePropW(hwnd, EmbedPropertyName);
    return true;
#else
    Q_UNUSED(s) Q_UNUSED(token)
    return true;
#endif
}

bool failSafeDetach(const NativeWindowState &s, quintptr token)
{
#ifdef Q_OS_WIN
    if (!sameIdentity(s))
        return true;
    HWND hwnd = reinterpret_cast<HWND>(s.handle);
    // Never mutate a window that no longer carries our token. Token loss means
    // either external removal or same-process/thread HWND recycling; neither is
    // safe to distinguish using PID/TID alone. The controller separately reads
    // the host hierarchy to decide whether the tab must be retained.
    if (!sameWindow(s, token))
        return false;

    // The reparent is the only release-defining operation. Do it before all
    // cosmetic cleanup so a style/owner/placement failure cannot cause the
    // controller to veto after the target is already outside our host.
    if (!setParent(hwnd, nullptr) || GetParent(hwnd)) {
        qWarning("EmbeddedWindow: failSafeDetach SetParent failed (err=%lu)", GetLastError());
        return false;
    }

    // Best-effort cosmetic cleanup, always token-gated. None of these failures
    // can turn an already-detached window back into an attached one.
    if (sameWindow(s, token))
        setLong(hwnd, GWL_STYLE, (s.originalStyle & ~WS_CHILD) | WS_POPUP);
    if (sameWindow(s, token))
        setLong(hwnd, GWL_EXSTYLE, s.originalExStyle);
    if (sameWindow(s, token))
        setLong(hwnd, GWLP_HWNDPARENT, s.originalOwner);
    if (sameWindow(s, token)) {
        UINT flags = SWP_NOACTIVATE | SWP_FRAMECHANGED
            | (s.originallyVisible ? SWP_SHOWWINDOW : SWP_NOZORDER);
        SetWindowPos(hwnd, HWND_TOP, s.rectLeft, s.rectTop,
                     s.rectRight - s.rectLeft, s.rectBottom - s.rectTop, flags);
    }
    if (!s.originallyVisible && sameWindow(s, token))
        ShowWindowAsync(hwnd, SW_HIDE);
    if (sameWindow(s, token))
        RemovePropW(hwnd, EmbedPropertyName);
    return true;
#else
    Q_UNUSED(s) Q_UNUSED(token)
    return true;
#endif
}

SyncResult syncGeometry(quintptr targetHandle, quintptr hostHandle, quintptr token)
{
#ifdef Q_OS_WIN
    HWND target = reinterpret_cast<HWND>(targetHandle);
    HWND host = reinterpret_cast<HWND>(hostHandle);
    const HANDLE mark = reinterpret_cast<HANDLE>(token);
    const auto ownsTarget = [target, mark]() {
        return IsWindow(target) && GetPropW(target, EmbedPropertyName) == mark;
    };
    if (!ownsTarget())
        return SyncResult::WindowLost;
    if (GetParent(target) != host) {
        if (!ownsTarget())
            return SyncResult::WindowLost;
        if (!setParent(target, host))
            return SyncResult::TransientError;
    }
    RECT rect{};
    if (!GetClientRect(host, &rect))
        return SyncResult::TransientError;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (!ownsTarget())
        return SyncResult::WindowLost;
    if (!SetWindowPos(target, nullptr, 0, 0, width, height,
                      SWP_NOACTIVATE | SWP_NOZORDER | SWP_ASYNCWINDOWPOS | SWP_SHOWWINDOW))
        return SyncResult::TransientError;
    if (!ownsTarget())
        return SyncResult::WindowLost;
    RedrawWindow(target, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    if (!ownsTarget())
        return SyncResult::WindowLost;
    PostMessageW(target, WM_SIZE, SIZE_RESTORED,
                 MAKELPARAM(static_cast<WORD>(width), static_cast<WORD>(height)));
    return ownsTarget() ? SyncResult::Ok : SyncResult::WindowLost;
#else
    Q_UNUSED(targetHandle) Q_UNUSED(hostHandle) Q_UNUSED(token)
    return SyncResult::WindowLost;
#endif
}

bool attachForeignInput(quintptr targetHandle, quintptr token, quint32 *foreignThreadIdOut,
                        quint32 *foreignProcessIdOut)
{
#ifdef Q_OS_WIN
    if (!foreignThreadIdOut || !foreignProcessIdOut)
        return false;
    *foreignThreadIdOut = 0;
    *foreignProcessIdOut = 0;
    HWND target = reinterpret_cast<HWND>(targetHandle);
    if (!IsWindow(target) || GetPropW(target, EmbedPropertyName) != reinterpret_cast<HANDLE>(token))
        return false;
    DWORD foreignProcessId = 0;
    const DWORD foreignThreadId = GetWindowThreadProcessId(target, &foreignProcessId);
    const DWORD currentThreadId = GetCurrentThreadId();
    if (!foreignThreadId || !foreignProcessId)
        return false;
    if (foreignThreadId == currentThreadId)
        return true;
    if (!AttachThreadInput(currentThreadId, foreignThreadId, TRUE)) {
        qWarning("EmbeddedWindow: AttachThreadInput failed (err=%lu)", GetLastError());
        return false;
    }
    *foreignThreadIdOut = static_cast<quint32>(foreignThreadId);
    *foreignProcessIdOut = static_cast<quint32>(foreignProcessId);
    return true;
#else
    Q_UNUSED(targetHandle) Q_UNUSED(token) Q_UNUSED(foreignThreadIdOut) Q_UNUSED(foreignProcessIdOut)
    return false;
#endif
}

bool focusForeign(quintptr targetHandle, quintptr token)
{
#ifdef Q_OS_WIN
    HWND target = reinterpret_cast<HWND>(targetHandle);
    if (!IsWindow(target) || GetPropW(target, EmbedPropertyName) != reinterpret_cast<HANDLE>(token))
        return false;
    // With the input queues merged by attachForeignInput(), SetFocus routes
    // keystrokes to the foreign child; the child is now parented under our
    // top-level window, so there is no separate top-level to SetActiveWindow.
    SetLastError(ERROR_SUCCESS);
    SetFocus(target);
    // Self-drawn apps (Godot, Chromium/Edge, ...) gate keyboard/IME input on
    // their OWN activation flag, which they update only from WM_NCACTIVATE /
    // WM_ACTIVATE. A WS_CHILD window never receives those (only top-levels do),
    // so after reparenting they believe they are inactive and silently drop
    // every keystroke even though GetFocus() already points at them (proven by
    // the diagnostic log: Godot/Edge keep GetActiveWindow on our Qt window).
    // Synthesize activation to re-enable their input path. PostMessage (async) so
    // a hung foreign message loop can never block our GUI thread — matches
    // syncGeometry. lParam (the "other" window) is left 0; these apps ignore it.
    if (!PostMessageW(target, WM_NCACTIVATE, TRUE, 0))
        qWarning("EmbeddedWindow: WM_NCACTIVATE post failed (err=%lu)", GetLastError());
    if (!PostMessageW(target, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0), 0))
        qWarning("EmbeddedWindow: WM_ACTIVATE post failed (err=%lu)", GetLastError());
    if (GetFocus() == target)
        return true;
    const DWORD error = GetLastError();
    qWarning("EmbeddedWindow: SetFocus on foreign child failed (err=%lu)", error);
    return false;
#else
    Q_UNUSED(targetHandle) Q_UNUSED(token)
    return false;
#endif
}

bool detachForeignInput(quint32 foreignThreadId, quint32 foreignProcessId)
{
#ifdef Q_OS_WIN
    if (!foreignThreadId)
        return true;
    const DWORD threadId = static_cast<DWORD>(foreignThreadId);
    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, threadId);
    if (!thread) {
        const DWORD error = GetLastError();
        if (error == ERROR_INVALID_PARAMETER)
            return true; // Thread exited; Windows automatically detached its queue.
        qWarning("EmbeddedWindow: OpenThread before input detach failed (err=%lu)", error);
        return false;
    }
    const DWORD currentProcessId = GetProcessIdOfThread(thread);
    CloseHandle(thread);
    if (!currentProcessId)
        return true; // Thread exited during validation.
    if (currentProcessId != static_cast<DWORD>(foreignProcessId))
        return true; // Thread id was reused; never mutate the replacement thread.
    if (AttachThreadInput(GetCurrentThreadId(), threadId, FALSE))
        return true;
    const DWORD error = GetLastError();
    if (error == ERROR_INVALID_PARAMETER)
        return true; // Thread exited between validation and detach.
    qWarning("EmbeddedWindow: AttachThreadInput detach failed (err=%lu)", error);
    return false;
#else
    Q_UNUSED(foreignThreadId) Q_UNUSED(foreignProcessId)
    return true;
#endif
}

bool isChildMouseActivationMessage(const void *message)
{
#ifdef Q_OS_WIN
    if (!message)
        return false;
    const auto *native = static_cast<const MSG *>(message);
    if (native->message != WM_PARENTNOTIFY)
        return false;
    switch (LOWORD(native->wParam)) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
        return true;
    default:
        return false;
    }
#else
    Q_UNUSED(message)
    return false;
#endif
}

} // namespace EmbeddedWindowWin32
