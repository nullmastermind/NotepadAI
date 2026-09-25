/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PROJECT_GOAL_PRESETS_H
#define PROJECT_GOAL_PRESETS_H

#include <QList>
#include <QString>
#include <QStringList>

// Project-scoped criteria presets under <project>/.agents/.goals.
//
// One criterion:  <name>.md
// Two or more:    <name>/1.md, <name>/2.md, ...
//
// List and read never create directories and never rewrite files.
// Save creates .agents/.goals when it writes. A failed write of the first
// numbered file leaves an existing <name>.md in place.
namespace ProjectGoalPresets {

struct Listed {
    QString name;
    int count = 0;
};

// False for empty, ".", "..", Windows device names, trailing space or dot,
// control characters, or \ / : * ? " < > |. Longer than 100 is rejected.
// The name must already be trimmed.
bool isValidName(const QString &name);

// True when the session is local and workingDirectory is an existing directory.
bool projectScopeAvailable(const QString &workingDirectory, bool sessionIsRemote);

// Writes a dense layout. One criterion becomes <name>.md and collapses a
// same-named directory when that directory has no other files. Two or more
// become <name>/1.md .. <name>/N.md and remove the loose <name>.md only after
// every numbered write commits. Caps: 50 criteria, 4000 characters each.
bool save(const QString &projectRoot, const QString &name,
          const QStringList &criteria, QString *error);

// Directory form wins when both <name>.md and <name>/ exist. Gaps are kept.
// Does not rewrite the disk. False when the preset is missing or empty.
bool read(const QString &projectRoot, const QString &name,
          QStringList *criteria, QString *error);

// One listing of .agents/.goals, then one listing per preset directory.
// Oversized files are skipped by size, without a full read. Missing folder
// is an empty list, not an error.
QList<Listed> list(const QString &projectRoot);

// Deletes the winning form. A directory preset removes recognized N.md files
// and the directory only when nothing else remains.
bool remove(const QString &projectRoot, const QString &name, QString *error);

} // namespace ProjectGoalPresets

#endif
