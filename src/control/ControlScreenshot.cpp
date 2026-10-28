/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlScreenshot.h"

#include <QPainter>
#include <QPixmap>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#endif

namespace control {

namespace {

#ifdef Q_OS_WIN
// The client area of a native window as the compositor shows it, or null.
QImage printWindow(HWND hwnd)
{
    RECT rc{};
    if (!GetClientRect(hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0)
        return {};
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bitmap);
    const BOOL ok = PrintWindow(hwnd, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
    SelectObject(mem, old);
    QImage image = ok ? QImage::fromHBITMAP(bitmap) : QImage();
    DeleteObject(bitmap);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return image;
}
#endif

} // namespace

QImage grabWidget(QWidget *widget)
{
    QImage image = widget->grab().toImage();
#ifdef Q_OS_WIN
    // Few widgets are native (AA_DontCreateNativeWidgetSiblings): one walk.
    QPainter painter;
    for (QWidget *child : widget->findChildren<QWidget *>()) {
        if (child->isWindow() || !child->testAttribute(Qt::WA_NativeWindow) || !child->isVisible())
            continue;
        const QImage shot = printWindow(reinterpret_cast<HWND>(child->internalWinId()));
        if (shot.isNull())
            continue;
        if (!painter.isActive())
            painter.begin(&image);
        // The painter works in logical pixels (the image carries the ratio).
        painter.drawImage(QRectF(child->mapTo(widget, QPoint(0, 0)), QSizeF(child->size())), shot);
    }
#endif
    return image;
}

} // namespace control
