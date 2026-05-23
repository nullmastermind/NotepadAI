/*
 * This file is part of Notepad Next.
 * Copyright 2022 Justin Dailey
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


#ifndef FOLDERASWORKSPACEDOCK_H
#define FOLDERASWORKSPACEDOCK_H

#include <QDockWidget>
#include <QMultiHash>
#include <QPersistentModelIndex>
#include <QSet>
#include <QString>
#include <QStringList>

#include "GitStatusEntry.h"

namespace Ui {
class FolderAsWorkspaceDock;
}

class QFileSystemModel;
class QTimer;
class GitTabWidget;
class GitDiffViewController;
class ScintillaNext;

// Snapshot of per-workspace UI state. Carried verbatim between disk (QSettings
// nested array under FolderAsWorkspace/WorkspaceStates) and the dock.
struct WorkspaceStateSnapshot
{
    QString     rootPath;            // cleanPath'd absolute path — primary key
    int         activeTabIndex = 0;  // 0 = Files, 1 = Git
    QString     currentItemPath;     // selected/current row, may be empty
    QStringList expandedFolders;     // absolute cleaned paths of expanded dirs
    qint64      lastUsedEpochMs = 0; // for LRU cap of the on-disk memo
};

class FolderAsWorkspaceDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit FolderAsWorkspaceDock(QWidget *parent = nullptr);
    FolderAsWorkspaceDock(const QString &initialPath, QWidget *parent);
    ~FolderAsWorkspaceDock();

    void setRootPath(const QString dir);
    QString rootPath() const;

    // Returns the lazily-created Git tab, or nullptr if the user has never
    // opened the Git tab in this dock yet.
    GitTabWidget *gitTabWidget() const { return gitTab; }

    // Forwards to the per-dock GitDiffViewController, creating it on first use.
    // Host (MainWindow) calls this in response to gitDiffRequested.
    void showGitDiffPreview(const GitStatusEntry &entry);

    // Switch the tab widget to the Git page and lazy-construct it if needed.
    // Used when a workspace is opened via a path that implies "show me Git first"
    // (e.g. clicking a submodule in another workspace's Git status tree).
    void showGitTab();

    // Apply a saved snapshot just BEFORE setRootPath() fires the model load.
    // Caller (MainWindow) must call this between dock construction and setRootPath,
    // so the directoryLoaded handler has the pending-expansion map populated
    // before the very first directoryLoaded signal arrives. activeTabIndex==1
    // (Git) queues a Qt::QueuedConnection showGitTab so the GitController spawn
    // doesn't block window->show().
    void applySavedTreeState(const WorkspaceStateSnapshot &snapshot);

    // Snapshot the current dock state for persistence. Cheap — no QSettings IO.
    WorkspaceStateSnapshot captureState() const;

signals:
    void fileDoubleClicked(const QString &filePath);
    // Forwarded from GitTabWidget once the Git tab is created.
    void gitDiffRequested(const GitStatusEntry &entry);
    void gitOpenSubmoduleRequested(const QString &absPath);
    // Forwarded from the per-dock GitDiffViewController — host raises this
    // editor as the active tab so the user lands on the rendered diff.
    void gitDiffPreviewRendered(ScintillaNext *editor);
    void gitDiffPreviewFailed(const QString &relPath, const QString &message);

    // Emitted from closeEvent BEFORE the dock destructs (WA_DeleteOnClose).
    // MainWindow snapshots state into FolderAsWorkspace/WorkspaceStates here
    // so that closing one workspace mid-session preserves its memo for the
    // next time the user reopens that path.
    void aboutToBeClosed(const QString &rootPath, FolderAsWorkspaceDock *self);
    // Emitted when user-driven UI state changes (tree expand/collapse, tab
    // switch, current item change). MainWindow uses this to set its
    // workspace-state dirty bit for the 60s autosave path.
    void stateDirty();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onTabChanged(int index);
    void onDirectoryLoaded(const QString &loadedPath);
    void onTreeExpanded(const QModelIndex &index);
    void onTreeCollapsed(const QModelIndex &index);

private:
    Ui::FolderAsWorkspaceDock *ui;

    QFileSystemModel *model;
    GitTabWidget *gitTab = nullptr;
    GitDiffViewController *gitDiffViewController = nullptr;

    QTimer *tooltipTimer;
    QPersistentModelIndex pendingTooltipIndex;

    // Restoration state — see openspec discussion. parentDir → child path map
    // drained on each QFileSystemModel::directoryLoaded fire. Per-dock; cleared
    // on dock destruction (no global cache).
    QMultiHash<QString, QString> m_pendingExpansion;
    // Paths the user explicitly collapsed during the restore window. A pending
    // entry whose path OR any of its ancestors is here will be vetoed. User
    // intent always wins over the saved snapshot.
    QSet<QString> m_userVetoed;
    // Sync-emit guard. QTreeView::setExpanded fires expanded()/collapsed()
    // synchronously via direct-connect; the slots check this flag to skip
    // marking dirty / vetoing for our own programmatic toggles.
    bool m_programmaticToggle = false;

    void ensureGitTab();
    GitDiffViewController *ensureGitDiffViewController();

    // True if any cleaned-path ancestor of `child` (up to and including `key`'s
    // parent chain stop) is in m_userVetoed. O(depth), depth typically < 10.
    bool ancestorVetoed(const QString &cleanedChild) const;
};

#endif // FOLDERASWORKSPACEDOCK_H
