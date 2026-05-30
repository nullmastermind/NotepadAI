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

#ifndef GIT_WATCHER_H
#define GIT_WATCHER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>

class QFileSystemWatcher;
class QTimer;

#ifdef Q_OS_WIN
class RecursiveTreeWatcher;
#endif

// Watches a repo's .git/ control files plus the working tree (recursively).
// Coalesces flurries of events into one of four debounced signals.
class GitWatcher : public QObject
{
    Q_OBJECT
public:
    explicit GitWatcher(QObject *parent = nullptr);
    ~GitWatcher() override;

    void setRepo(const QString &toplevel);
    void clear();

    // Forward the set of gitignored top-level paths to the recursive tree
    // watcher so working-tree churn confined to ignored dirs (build output,
    // node_modules, …) is dropped before it can trigger a status refresh.
    // No-op on non-Windows, where the working tree isn't watched recursively.
    void setIgnoredPrefixes(const QStringList &prefixes);

signals:
    void headChanged();
    void indexChanged();
    void refsChanged();
    void workingTreeChanged();
    void operationStateFileChanged();

private slots:
    void onFileChanged(const QString &path);
    void onDirChanged(const QString &path);
    void onDebounce();

private:
    QFileSystemWatcher *m_fs = nullptr;
    QTimer *m_debounce = nullptr;
    QString m_repoRoot;
    QString m_gitDir;
    int m_pending = 0;

#ifdef Q_OS_WIN
    RecursiveTreeWatcher *m_treeWatcher = nullptr;
#endif

    enum Pending : std::uint8_t { PHead = 1, PIndex = 2, PRefs = 4, PTree = 8, POpState = 16 };

    void rewatch();
    QStringList currentWatchedFiles() const;
    QStringList currentWatchedDirs() const;
};

#endif // GIT_WATCHER_H
