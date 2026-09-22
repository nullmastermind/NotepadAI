/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef WATCHEDPATHDELETE_H
#define WATCHEDPATHDELETE_H

#include <QString>

class QFileSystemModel;

namespace WatchedPathDelete {

// Delete a local file or directory. When `model` already has an index for
// `path`, deletion goes through QFileSystemModel::remove(), which on Windows
// drops the directory watch before unlinking (QTBUG-65683). Deleting a watched
// directory without that step makes the watcher thread qErrnoWarning
// "FindNextChangeNotification failed … Access is denied".
bool remove(QFileSystemModel *model, const QString &path, bool isDir);

} // namespace WatchedPathDelete

#endif
