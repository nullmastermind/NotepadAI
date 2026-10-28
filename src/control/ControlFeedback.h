/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_FEEDBACK_H
#define CONTROL_FEEDBACK_H

#include <QObject>
#include <QPointer>
#include <QRect>

class QAction;
class QWidget;

namespace control {

// Flashes a highlight over the control an external agent just operated:
// one alien overlay per window, transparent to input, painted only while a
// flash fades (600 ms). Adds no delay to the command.
class ControlFeedback : public QObject
{
    Q_OBJECT

public:
    static constexpr int kFlashMs = 600;

    explicit ControlFeedback(QObject *parent = nullptr);

    // `rect` in `target` coordinates; empty: the whole widget.
    void flash(QWidget *target, const QRect &rect = QRect());
    // Where the user would click: the menu bar title of the action's top menu,
    // its toolbar button, or nothing when it has no on-screen place.
    void flashAction(QAction *action);
};

} // namespace control

#endif // CONTROL_FEEDBACK_H
