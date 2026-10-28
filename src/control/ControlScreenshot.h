/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_SCREENSHOT_H
#define CONTROL_SCREENSHOT_H

#include <QImage>

class QWidget;

namespace control {

// What `widget` shows, at device pixels. QWidget::grab() paints Qt widgets
// only, so a native child that renders itself (WebView2) comes out blank; on
// Windows those are drawn in from the window manager (PrintWindow).
QImage grabWidget(QWidget *widget);

} // namespace control

#endif // CONTROL_SCREENSHOT_H
