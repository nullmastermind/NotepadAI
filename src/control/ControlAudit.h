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

namespace control {

class CommandRegistry;

// The controllability rule, checked (docs/adr/0007): every command and
// argument is described, enum arguments list their values, every named menu
// action has a unique id (menu.run), and a menu action marked as opening a
// dialog names a command that exists. Returns one line per problem.
QStringList auditProblems(QMainWindow *window, const CommandRegistry &commands);
// Prints the problems to stdout and returns their count.
int runAudit(QMainWindow *window, const CommandRegistry &commands);

} // namespace control

#endif // CONTROL_AUDIT_H
