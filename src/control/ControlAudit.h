/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_AUDIT_H
#define CONTROL_AUDIT_H

#include <QStringList>

class QMainWindow;
class QWidget;

namespace control {

// The controllability rule, checked: every action reachable from the window
// (menus filled as when opened, toolbars, shortcut actions) and every
// interactive widget under it must have a stable, unique Control API id.
// Returns one line per problem.
QStringList auditProblems(QMainWindow *window);
// Prints the problems to stdout and returns their count.
int runAudit(QMainWindow *window);

// Buttons, inputs, combos, spin boxes, sliders, item views, tab bars; not the
// parts of other controls (a combo's line edit, a scroll bar, a header).
bool isInteractiveWidget(const QWidget *widget);

} // namespace control

#endif // CONTROL_AUDIT_H
