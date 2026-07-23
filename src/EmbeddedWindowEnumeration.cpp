/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "EmbeddedWindowWin32.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#include <dwmapi.h>

#include <utility>

namespace {
bool getLong(HWND hwnd, int index, LONG_PTR *value)
{
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR result = GetWindowLongPtrW(hwnd, index);
    if (result == 0 && GetLastError() != ERROR_SUCCESS)
        return false;
    *value = result;
    return true;
}

bool isSystemWindow(HWND hwnd)
{
    if (hwnd == GetShellWindow() || hwnd == GetDesktopWindow())
        return true;
    wchar_t name[64]{};
    if (GetClassNameW(hwnd, name, 64) <= 0)
        return true;
    return lstrcmpW(name, L"Progman") == 0 || lstrcmpW(name, L"WorkerW") == 0
        || lstrcmpW(name, L"Shell_TrayWnd") == 0;
}

bool isEmbeddable(HWND hwnd, DWORD ownPid, LONG_PTR *styleOut = nullptr,
                  LONG_PTR *exStyleOut = nullptr)
{
    if (!IsWindow(hwnd) || isSystemWindow(hwnd))
        return false;
    DWORD cloaked = 0;
    const bool isCloaked = SUCCEEDED(DwmGetWindowAttribute(
        hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked;
    if (!IsWindowVisible(hwnd) || IsHungAppWindow(hwnd)
        || GetAncestor(hwnd, GA_ROOT) != hwnd || GetParent(hwnd)
        || GetWindow(hwnd, GW_OWNER) || isCloaked)
        return false;
    DWORD pid = 0;
    if (!GetWindowThreadProcessId(hwnd, &pid) || !pid || pid == ownPid)
        return false;
    LONG_PTR style = 0;
    LONG_PTR exStyle = 0;
    if (!getLong(hwnd, GWL_STYLE, &style) || !getLong(hwnd, GWL_EXSTYLE, &exStyle)
        || (style & WS_CHILD) || !(style & WS_VISIBLE)
        || (exStyle & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)))
        return false;
    if (styleOut)
        *styleOut = style;
    if (exStyleOut)
        *exStyleOut = exStyle;
    return true;
}
} // namespace
#endif

namespace EmbeddedWindowWin32 {
QList<Candidate> enumerateEmbeddableWindows()
{
    QList<Candidate> windows;
#ifdef Q_OS_WIN
    struct Context { QList<Candidate> *out; DWORD ownPid; };
    Context context{&windows, GetCurrentProcessId()};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        auto *context = reinterpret_cast<Context *>(data);
        if (!isEmbeddable(hwnd, context->ownPid))
            return TRUE;
        const int length = GetWindowTextLengthW(hwnd);
        if (length <= 0)
            return TRUE;
        QString title(length + 1, Qt::Uninitialized);
        const int copied = GetWindowTextW(hwnd, reinterpret_cast<LPWSTR>(title.data()), title.size());
        if (copied > 0) {
            title.truncate(copied);
            context->out->append({reinterpret_cast<quintptr>(hwnd), std::move(title)});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&context));
#endif
    return windows;
}

bool captureState(quintptr handle, NativeWindowState *out)
{
#ifdef Q_OS_WIN
    if (!out)
        return false;
    HWND hwnd = reinterpret_cast<HWND>(handle);
    LONG_PTR style = 0;
    LONG_PTR exStyle = 0;
    if (!isEmbeddable(hwnd, GetCurrentProcessId(), &style, &exStyle))
        return false;
    NativeWindowState state;
    state.handle = handle;
    DWORD processId = 0;
    state.threadId = GetWindowThreadProcessId(hwnd, &processId);
    state.processId = processId;
    RECT rect{};
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    if (!state.threadId || !state.processId || !GetWindowRect(hwnd, &rect)
        || !GetWindowPlacement(hwnd, &placement))
        return false;
    state.originalStyle = style;
    state.originalExStyle = exStyle;
    state.originalParent = reinterpret_cast<quintptr>((style & WS_CHILD) ? GetParent(hwnd) : nullptr);
    state.originalOwner = reinterpret_cast<quintptr>((style & WS_CHILD) ? nullptr : GetWindow(hwnd, GW_OWNER));
    state.rectLeft = rect.left; state.rectTop = rect.top;
    state.rectRight = rect.right; state.rectBottom = rect.bottom;
    state.placementFlags = placement.flags; state.placementShowCmd = placement.showCmd;
    state.placementMinX = placement.ptMinPosition.x; state.placementMinY = placement.ptMinPosition.y;
    state.placementMaxX = placement.ptMaxPosition.x; state.placementMaxY = placement.ptMaxPosition.y;
    state.placementNormalLeft = placement.rcNormalPosition.left;
    state.placementNormalTop = placement.rcNormalPosition.top;
    state.placementNormalRight = placement.rcNormalPosition.right;
    state.placementNormalBottom = placement.rcNormalPosition.bottom;
    state.placementValid = true;
    state.originallyVisible = IsWindowVisible(hwnd);
    *out = state;
    return true;
#else
    Q_UNUSED(handle) Q_UNUSED(out)
    return false;
#endif
}
} // namespace EmbeddedWindowWin32
