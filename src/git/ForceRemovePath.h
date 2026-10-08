/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef FORCE_REMOVE_PATH_H
#define FORCE_REMOVE_PATH_H

#include <QString>
#include <QStringList>

namespace ForceRemovePath {

// Delete a local file or directory, including Windows reserved device names
// (NUL, CON, PRN, …) that git clean / QFile::remove cannot unlink.
// Returns true iff the path no longer exists. Existence is checked with the
// Win32 `\\?\` prefix on Windows — QFile::exists("…\\nul") hits the NUL device.
bool forceRemove(const QString &absPath);

// After `git clean` failed, try forceRemove on each remaining relative path
// under repoRoot. Returns true iff none of the requested paths still exist.
// Rejects empty lists, `..`, and paths that escape repoRoot.
bool recoverFailedUntrackedDeletes(const QString &repoRoot, const QStringList &relPaths);

} // namespace ForceRemovePath

#endif // FORCE_REMOVE_PATH_H
