/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "WatchedPathDelete.h"

#include <QDir>
#include <QFile>
#include <QFileSystemModel>

namespace WatchedPathDelete {

bool remove(QFileSystemModel *model, const QString &path, bool isDir)
{
    if (model) {
        const QModelIndex idx = model->index(path);
        // QFileSystemModel::remove() unwatches the path on Windows before
        // unlinking. Skipping that is what wakes the watcher thread with
        // ERROR_ACCESS_DENIED.
        if (idx.isValid())
            return model->remove(idx);
    }
    if (isDir)
        return QDir(path).removeRecursively();
    return QFile::remove(path);
}

} // namespace WatchedPathDelete
