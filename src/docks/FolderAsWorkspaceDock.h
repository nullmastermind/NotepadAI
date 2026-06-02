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
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

#include "GitStatusEntry.h"
#include "PathStatusIndex.h"

namespace Ui {
class FolderAsWorkspaceDock;
}

class FolderAsWorkspaceFsModel;
class FolderAsWorkspaceProxyModel;
class QFrame;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QTimer;
class GitTabWidget;
class GitDiffViewController;
class GitCommitView;
class GitOperationManager;
class ScintillaNext;
class SubmoduleStatusFetcher;
namespace remote { class IWorkspaceFsModel; class RemoteFsBackend; class RemoteDirectoryWatcher; }

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
    ~FolderAsWorkspaceDock() override;

    void setRootPath(const QString &dir);
    QString rootPath() const;

    // Switch this dock's file tree from the default local QFileSystemModel-backed
    // model to an SFTP-backed RemoteFileSystemModel driven by `backend` (D2).
    // Must be called BEFORE setRootPath()/applySavedTreeState() (i.e. right after
    // construction, mirroring the local default-ctor + setRootPath sequence). The
    // dock keeps talking to the model only through the IWorkspaceFsModel seam, so
    // every path-space code path (reveal, restore, expansion cascade) is
    // unchanged. Git decorations are local-only and are skipped while remote
    // (the remote git path is wired in a later batch). `backend` is owned by the
    // workspace's RemoteExecutionContext and must outlive this dock.
    void useRemoteBackend(remote::RemoteFsBackend *backend);
    // Null for local workspaces; non-null for SSH workspaces after useRemoteBackend.
    remote::RemoteFsBackend *remoteBackend() const { return m_remoteBackend; }

    // --- SSH connection-state surface (D10 / Batch H) -----------------------
    // The banner is an inline, palette-driven, keyboard-accessible row at the top
    // of the dock that surfaces the live SSH connection lifecycle without ever
    // blocking the UI or popping a modal. MainWindow drives these as the
    // workspace's RemoteExecutionContext changes state. None of this touches the
    // tree model — it only shows/hides the banner above it. A purely-local dock
    // never calls these, so the banner stays hidden (zero layout cost).

    // Show "Connecting to user@host…" with an indeterminate spinner. Used both on
    // the initial open and on startup auto-reconnect, before the tree is live.
    void showConnectingState(const QString &userHost);
    // Connection is up: hide the banner so only the tree shows. Caller has already
    // (or is about to) populate the tree via useRemoteBackend.
    void showConnectedState();
    // Background reconnect / connect failed: inline reason + a Reconnect button.
    // No modal. Clicking Reconnect emits reconnectRequested().
    void showFailedState(const QString &reason);
    // Mid-session connection drop: "Connection lost — Reconnect" banner with a
    // Reconnect button. Distinct copy from the startup-failed case.
    void showConnectionLostState(const QString &reason);
    // FIX-2 back-pressure: "Waiting for an available channel…" indicator while a
    // channel open is queued behind the per-profile budget. Cleared by
    // clearChannelQueuedState() once a slot frees (channelReady).
    void showChannelQueuedState();
    void clearChannelQueuedState();

    // Set the SSH workspace URI identity (D5a). When set, rootPath() returns this
    // URI for deduplication/persistence/context-resolution. The model still
    // receives the POSIX path via setRootPath(). Call BEFORE setRootPath().
    void setSshWorkspaceUri(const QString &uri) { m_sshWorkspaceUri = uri; }
    QString sshWorkspaceUri() const { return m_sshWorkspaceUri; }

    void setGitOperationManager(GitOperationManager *mgr);

    // Returns the lazily-created Git tab, or nullptr if the user has never
    // opened the Git tab in this dock yet.
    GitTabWidget *gitTabWidget() const { return gitTab; }

    // Forwards to the per-dock GitDiffViewController, creating it on first use.
    // Host (MainWindow) calls this in response to gitDiffRequested.
    void showGitDiffPreview(const GitStatusEntry &entry);

    // Open the commit detail tab via the per-dock GitCommitView controller,
    // creating it on first use. Host (MainWindow) calls this in response to
    // gitOpenCommitDetailRequested.
    void showGitCommitDetail(const QByteArray &sha);

    // Switch the tab widget to the Git page and lazy-construct it if needed.
    // Used when a workspace is opened via a path that implies "show me Git first"
    // (e.g. clicking a submodule in another workspace's Git status tree).
    void showGitTab();

    // Reveal an absolute path in the file tree (JetBrains "Select in Project
    // Tree"): switch the inner tab to Files, expand the ancestor chain from the
    // workspace root down to the file's parent, then select + scroll to the
    // leaf. Reuses the async-restore plumbing (m_pendingExpansion +
    // pendingCurrentItem + onDirectoryLoaded) so it works even while the model
    // is still lazily loading directories. Caller is responsible for raising /
    // showing the dock first; this method focuses the tree so the selection is
    // active. No-op if the path is empty or not under this dock's root.
    void revealAndSelectPath(const QString &absolutePath);

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
    void fileClicked(const QString &filePath);
    void fileDoubleClicked(const QString &filePath);
    // Forwarded from GitTabWidget once the Git tab is created.
    void gitDiffRequested(const GitStatusEntry &entry);
    void gitOpenSubmoduleRequested(const QString &absPath);
    void gitOpenCommitDetailRequested(const QByteArray &sha);
    void gitChangesContextMenuRequested(QMenu *menu, const GitStatusEntry &entry);
    void gitMergeRequested(FolderAsWorkspaceDock *dock);
    void gitRebaseRequested(FolderAsWorkspaceDock *dock);
    void gitInteractiveRebaseRequested(FolderAsWorkspaceDock *dock);
    // Forwarded from the per-dock GitDiffViewController — host raises this
    // editor as the active tab so the user lands on the rendered diff.
    void gitDiffPreviewRendered(ScintillaNext *editor);
    void gitDiffPreviewFailed(const QString &relPath, const QString &message);
    // From GitCommitView — host docks the new editor or focuses an existing one.
    void gitCommitEditorCreated(ScintillaNext *editor);
    void gitCommitEditorRendered(ScintillaNext *editor);
    void gitCommitEditorFocus(ScintillaNext *editor);
    void gitFileAtShaEditorCreated(ScintillaNext *editor);
    void gitFileAtShaEditorFocus(ScintillaNext *editor);
    void gitCommitDetailFailed(const QByteArray &sha, const QString &message);

    // Emitted from closeEvent BEFORE the dock destructs (WA_DeleteOnClose).
    // MainWindow snapshots state into FolderAsWorkspace/WorkspaceStates here
    // so that closing one workspace mid-session preserves its memo for the
    // next time the user reopens that path.
    void aboutToBeClosed(const QString &rootPath, FolderAsWorkspaceDock *self);
    // Emitted when user-driven UI state changes (tree expand/collapse, tab
    // switch, current item change). MainWindow uses this to set its
    // workspace-state dirty bit for the 60s autosave path.
    void stateDirty();
    void treeContextMenuRequested(QMenu *menu, const QString &path, bool isDir);

    // SSH connection lifecycle — MainWindow subscribes to drive registry->connect.
    void reconnectRequested(FolderAsWorkspaceDock *dock);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onTabChanged(int index);
    void onDirectoryLoaded(const QString &loadedPath);
    void onTreeExpanded(const QModelIndex &index);
    void onTreeCollapsed(const QModelIndex &index);
    void onStatusUpdated();
    void onSubmoduleEntriesReady(const GitStatusEntries &entries);
    // Relay from GitTabWidget. If a git-diff fetch is still in flight, the
    // editor open is queued so it fires AFTER the diff tab activation —
    // double-click should always end with the editor tab active, but the
    // async git diff process can otherwise win the race and steal focus.
    void onGitTabFileActivated(const QString &absPath);
    void onGitDiffPreviewRendered(ScintillaNext *editor);
    void onGitDiffPreviewFailed(const QString &relPath, const QString &message);

private:
    Ui::FolderAsWorkspaceDock *ui;

    // Local QFileSystemModel-backed model. Non-null for a local workspace; set to
    // nullptr by useRemoteBackend() (which deletes it) so the git-decoration code
    // — which is QFileSystemModel-specific — safely no-ops on a remote workspace.
    FolderAsWorkspaceFsModel *model;
    // The active file-tree model behind the path-based seam (the local `model`
    // above, or a RemoteFileSystemModel after useRemoteBackend). Every path-space
    // touch-point routes through this so the dock is backend-agnostic. Always the
    // proxy's source.
    remote::IWorkspaceFsModel *fsModel = nullptr;
    // Synthetic-root proxy that presents the workspace dir as the single top
    // node. The view binds to this; `fsModel` is its source. Per-dock, parented
    // to the dock. See FolderAsWorkspaceProxyModel.h for the invariants.
    FolderAsWorkspaceProxyModel *proxy;

    // Remote (SFTP) poll-watcher (D3). Null for a local workspace — local change
    // notification stays on QFileSystemModel's own QFileSystemWatcher, which
    // surfaces through the SAME proxy rowsInserted/rowsRemoved seam, so the dock
    // is backend-agnostic. Created + armed by useRemoteBackend(); fed the
    // visible/expanded-dir set + focus/visibility/minimize state by the dock.
    // Parented to the dock; explicitly stopped in the destructor before the
    // tree/proxy are torn down. Its directoryChanged drives the remote model's
    // onDirectoryChanged re-list/diff.
    remote::RemoteDirectoryWatcher *m_remoteWatcher = nullptr;
    // Non-owning pointer to the active remote backend (set in useRemoteBackend,
    // null for local workspaces). Used by context menu operations to reach the
    // remote rename/mkdir/unlink API without crossing the SSH seam directly.
    remote::RemoteFsBackend *m_remoteBackend = nullptr;
    // The top-level window we installed the focus/minimize event filter on (for
    // the remote watcher's interval gating). QPointer so a destroyed window
    // auto-nulls; used to remove the filter on teardown / re-hook.
    QPointer<QWidget> m_watchedWindow;
    GitTabWidget *gitTab = nullptr;
    GitDiffViewController *gitDiffViewController = nullptr;
    GitCommitView         *gitCommitView = nullptr;
    GitOperationManager   *m_gitOpMgr = nullptr;

    QTimer *tooltipTimer;
    QPersistentModelIndex pendingTooltipIndex;

    // Per-workspace git decoration index. Owned by-value; held alive for the
    // dock's lifetime. Pointer handed to `model` via setStatusIndex; cleared
    // in destructor before the index dies.
    PathStatusIndex m_pathStatus;
    // Async fetcher for per-file `git status` inside each detected submodule.
    // Lazily created on first need; nullptr means no submodule status has
    // been requested for this dock yet.
    SubmoduleStatusFetcher *m_subFetcher = nullptr;
    // Cached parent-repo status snapshot held between the first repaint pass
    // (parent entries only) and the second pass (parent + submodule entries
    // merged) once the fetcher returns. Empty when no fetch is in flight.
    GitStatusEntries m_pendingParentEntries;
    QString m_pendingRepoTop;
    // Set true once setRootPath has scheduled the queued ensureGitTab so we
    // don't double-schedule when subsequent setRootPath calls land on the
    // same root or while construction is pending.
    bool m_gitTabScheduled = false;

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

    // Root-node (PR) default-expand / restore state. The root is now a real top
    // node, but its parent dir is outside the tree so the m_pendingExpansion
    // cascade can't drive it — it is expanded directly on the root's first
    // directoryLoaded. m_rootShouldExpand defaults true (first-run / no saved
    // state, incl. the 2-arg ctor path that never calls applySavedTreeState);
    // applySavedTreeState lowers it to false when a saved snapshot omitted the
    // root path (the user had collapsed it last session). m_rootExpandApplied
    // makes the directoryLoaded handling a one-shot.
    bool m_rootShouldExpand = true;
    bool m_rootExpandApplied = false;

    // True between showGitDiffPreview() and the matching gitDiffPreviewRendered/
    // Failed signal — the git diff process is async, so a double-click that
    // requests both a diff (click) and an editor (dblclick) would race and
    // sometimes leave the diff tab active. While this is set, a pending editor
    // open is queued in m_pendingEditorOpenPath and fired once the diff
    // settles, so the editor tab is always the last one activated.
    bool m_diffRenderPending = false;
    QString m_pendingEditorOpenPath;

    // --- SSH connection banner (D10 / Batch H) ------------------------------
    // Inline banner at the top of the Files tab, above the tree. Hidden for local
    // workspaces. Palette-driven, keyboard-accessible (Reconnect button is
    // focusable). Created lazily on first showConnectingState/showFailedState call.
    QFrame *m_connectionBanner = nullptr;
    QLabel *m_bannerLabel = nullptr;
    QProgressBar *m_bannerSpinner = nullptr;
    QPushButton *m_bannerReconnectBtn = nullptr;
    // Thin "Waiting for an available channel…" label below the main banner.
    QLabel *m_channelQueuedLabel = nullptr;
    void ensureConnectionBanner();

    // SSH workspace identity (D5a). When non-empty, rootPath() returns this URI
    // instead of the model's root path, so deduplication, persistence, and
    // activeExecutionContext resolution all key off the ssh:// URI. The model
    // itself receives only the POSIX path for SFTP operations.
    QString m_sshWorkspaceUri;

    // Convert a POSIX path from the remote model to an ssh:// URI when this dock
    // is an SSH workspace, so open/preview consumers receive the correct identity.
    // For local workspaces (m_sshWorkspaceUri empty) returns posixPath unchanged.
    // Index-based overload for click handlers; path-based overload for git-tab
    // paths that arrive as pre-built absolute strings (onGitTabFileActivated).
    QString resolvedFilePath(const QModelIndex &proxyIdx) const;
    QString resolvedPath(const QString &posixPath) const;

    void ensureGitTab();
    GitDiffViewController *ensureGitDiffViewController();
    GitCommitView         *ensureGitCommitView();
    // Lazy-create the per-dock SubmoduleStatusFetcher; ensures connection to
    // onSubmoduleEntriesReady is wired exactly once.
    SubmoduleStatusFetcher *ensureSubmoduleFetcher();
    // Apply a fully merged GitStatusEntries vector (parent + sub-status if
    // available) to m_pathStatus, computing the delta against the previous
    // snapshot and asking the model to repaint only the affected rows.
    void applyMergedEntries(const GitStatusEntries &merged, const QString &repoTop);

    // Wire the new FolderAsWorkspaceFsModel to the app's theme signal +
    // ApplicationSettings/FileTreeGitColors setting + this dock's
    // PathStatusIndex. Idempotent at construction time only — called once
    // from each ctor. Safe to call when qApp is not the NotepadNextApplication
    // (tests instantiate the dock standalone): the theme connect is skipped.
    void wireFileTreeGitDecorations();
    void wireTreeContextMenu();
    // Connect the active model's directoryLoaded(QString) signal to
    // onDirectoryLoaded by signature (string-based), so it resolves against
    // whichever concrete model (local QFileSystemModel or RemoteFileSystemModel)
    // backs fsModel. Called once per model from each ctor and from
    // useRemoteBackend after the model swap.
    void connectModelSignals();

    // --- remote poll-watch wiring (D3) ---------------------------------------
    // Construct + arm the RemoteDirectoryWatcher over `backend`, feeding it the
    // visible-dir set (collectExpandedDirs) and the current focus/visibility/
    // minimize state. Called once from useRemoteBackend after the model swap.
    void setupRemoteWatcher(remote::RemoteFsBackend *backend);
    // Cleaned absolute paths of every directory currently EXPANDED in the tree
    // (the workspace root included when expanded). Walks the proxy tree skipping
    // unexpanded subtrees, so the result is non-recursive by construction — a
    // collapsed dir contributes neither itself nor its descendants. O(visible
    // expanded rows). This is the watcher's VisibleDirsFn source.
    QStringList collectExpandedDirs() const;
    // Install (idempotently) the focus/minimize event filter on this dock's
    // top-level window and sync the watcher's initial window state. Called from
    // showEvent and setupRemoteWatcher (whichever happens first once both a
    // watcher and a window exist). No-op without a remote watcher.
    void hookWindowStateForWatcher();

    // Schedule a deferred ensureGitTab() if the workspace has a directory
    // and the file-tree decoration setting is enabled. No-op if gitTab is
    // already constructed or a schedule is already in flight.
    void maybeScheduleGitTabForDecoration();

    // True if any cleaned-path ancestor of `child` (up to and including `key`'s
    // parent chain stop) is in m_userVetoed. O(depth), depth typically < 10.
    bool ancestorVetoed(const QString &cleanedChild) const;
};

#endif // FOLDERASWORKSPACEDOCK_H
