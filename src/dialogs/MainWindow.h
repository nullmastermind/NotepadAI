/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
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


#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QActionGroup>
#include <QHash>
#include <QPointer>

#include "DockedEditor.h"

#include "MacroManager.h"
#include "ScintillaNext.h"
#include "NppImporter.h"
#include "SearchResultsCollector.h"
#include "FileIndexCache.h"

#include <memory>

class FileWatcher;

namespace Ui {
class MainWindow;
}

class NotepadNextApplication;
class Macro;
class Settings;
class QuickFindWidget;
class ZoomEventWatcher;
class Converter;
class DefaultDirectoryManager;
class TabsQuickActionsBar;
class TerminalManager;
class AiAgentDock;
class FindInFolderDock;
class FolderAsWorkspaceDock;
class ConflictListDock;
class ConflictMergeViewerDock;
class GitOperationManager;
class MiniAppManager;
class MiniAppRegistry;
class WorkspaceFileEnumerator;
class QuickFileOpenDialog;
namespace remote { class ExecutionContext; }
class SshDebugDialog;
struct ConflictEntry;
struct GitStatusEntry;
struct WorkspaceStateSnapshot;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(NotepadNextApplication *app);
    ~MainWindow() override;

    bool isAnyUnsaved() const;

    void setupLanguageMenu();
    ScintillaNext *currentEditor() const;
    int editorCount() const;
    QVector<ScintillaNext *> editors() const;
    DockedEditor *getDockedEditor() const { return dockedEditor; }

public slots:
    // replaceInitialEditor: when true, an unedited pristine "New X" buffer that
    // is currently the sole editor is closed right after the new tab opens, so
    // the user doesn't accumulate empty scratch tabs. Pass true only from the
    // explicit "open a new tab" surfaces (New action, + button, title-bar
    // double-click); leave false for split/respawn/startup/drop paths.
    void newFile(bool replaceInitialEditor = false);

    void openFileDialog();
    void openFile(const QString &filePath);
    void previewFile(const QString &filePath);

    void openFolderAsWorkspaceDialog();
    void setFolderAsWorkspacePath(const QString &dir);

    void reloadFile();

    void closeCurrentFile();
    void closeFile(ScintillaNext *editor);
    void closeAllFiles();
    void closeAllExceptActive();
    void closeAllToLeft();
    void closeAllToRight();

    bool saveCurrentFile();
    bool saveFile(ScintillaNext *editor);

    bool saveCurrentFileAsDialog();
    bool saveCurrentFileAs(const QString &fileName);
    bool saveFileAs(ScintillaNext *editor, const QString &fileName);

    bool saveCopyAsDialog();
    bool saveCopyAs(const QString &fileName);
    bool saveAll();
    bool saveAllEditors(const QVector<ScintillaNext *> &editors);

    void exportAsFormat(Converter *converter, const QString &filter);
    void copyAsFormat(Converter *converter, const QString &mimeType);

    void renameFile();

    // Rename a file/folder from the workspace file tree. Renames on disk
    // (metadata-only; case-only renames on Windows go via a unique temp with
    // rollback) and, only on success, rebases every open editor under the old
    // path via ScintillaNext::updatePathAfterMove so tabs follow the move
    // without a buffer rewrite. Returns true on success.
    bool renameWorkspaceEntry(const QString &oldPath, const QString &newPath, bool isDir);

    // Disk-level rename for renameWorkspaceEntry. Plain QDir::rename, except a
    // case-only rename (caseOnly==true) on a case-insensitive FS goes via a
    // guaranteed-unique temp name with rollback to the original on partial
    // failure so the entry is never stranded. Returns true on success.
    bool renameOnDisk(const QString &oldClean, const QString &newClean, bool caseOnly);

    void moveCurrentFileToTrash();
    void moveFileToTrash(ScintillaNext *editor);

    void print();

    void convertEOLs(int eolMode);

    void showFindReplaceDialog(int index);

    void updateFileStatusBasedUi(ScintillaNext *editor);
    void updateEOLBasedUi(ScintillaNext *editor);
    void updateDocumentBasedUi(Scintilla::Update updated);
    void updateSelectionBasedUi(ScintillaNext *editor);
    void updateContentBasedUi(ScintillaNext *editor);
    void updateSaveStatusBasedUi(ScintillaNext *editor);
    void updateEditorPositionBasedUi();
    void updateLanguageBasedUi(ScintillaNext *editor);
    void updateGui(ScintillaNext *editor);

    // Recompute the preview toolbar/menu action's enabled state, dynamic tooltip
    // ("Preview Markdown"/"Preview HTML"/"Preview CSV"/…), and checked state for
    // the given editor, resolving the type through PreviewTabManager's registry.
    void updatePreviewActionForEditor(ScintillaNext *editor);
    // Re-tint the eye icon from the live palette (ButtonText). Called on theme
    // change — Qt's icon engine resolves currentColor to opaque black once and
    // does not follow theme switches.
    void retintPreviewActionIcon();

    void detectLanguage(ScintillaNext *editor);

    void setLanguage(ScintillaNext *editor, const QString &languageName);

    void bringWindowToForeground();
    void focusIn();

    void addEditor(ScintillaNext *editor);

    void checkForUpdates(bool silent = false);

    void restoreWindowState();
    void restoreWindowGeometry();
    void restoreOpenWorkspaces();
    void raiseSavedActiveWorkspace();

    // Workspace-state persistence (per-dock inner tab + expanded folders +
    // current item). Dirty bit flipped from FolderAsWorkspaceDock::stateDirty;
    // 60s autosave path checks and runs saveWorkspaceStatesOnly().
    bool isWorkspaceStateDirty() const { return m_workspaceStateDirty; }
    void clearWorkspaceStateDirty() { m_workspaceStateDirty = false; }
    void saveWorkspaceStatesOnly();

    void switchToEditor(const ScintillaNext *editor);

    AiAgentDock *activeAiDock() const;

    // Resolve the active ExecutionContext: the remote context for the active
    // SSH workspace if one is connected, else the shared local context.
    // Null-safe — always returns the local context as a fallback (never null
    // once the app's ExecutionContextRegistry exists).
    remote::ExecutionContext *activeExecutionContext() const;

signals:
    void editorActivated(ScintillaNext *editor);
    void aboutToClose();
    void fileDialogAccepted(const QString &filePath);
    void activeWorkspaceChanged(FolderAsWorkspaceDock *newDock, FolderAsWorkspaceDock *oldDock);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void tabBarRightClicked(ScintillaNext *editor);
    void languageMenuTriggered();
    void checkForUpdatesFinished(const QString &url);
    void activateEditor(ScintillaNext *editor);

    // Centralized "the editor area just became empty" handler. Wired to
    // DockedEditor::lastTabClosed, which fires after the LAST tab of ANY kind
    // (editor, preview, browser, mini-app, …) is removed. Decides between
    // exiting and spawning a fresh "New X" buffer. No-ops while m_isClosing.
    void handleEditorAreaEmptied();

private:
    Ui::MainWindow *ui = Q_NULLPTR;
    NotepadNextApplication *app = Q_NULLPTR;
    DockedEditor *dockedEditor = Q_NULLPTR;

    // Set once in closeEvent() so the centralized empty-area handler
    // (handleEditorAreaEmptied) never resurrects a "New X" buffer while the
    // window is tearing down its tabs during application exit.
    bool m_isClosing = false;

    QScopedPointer<SearchResultsCollector> searchResults;

    template <typename Method>
    void connectEditorAction(QAction* action, Method method) {
        connect(action, &QAction::triggered, this, [this, method]() {
            if (auto* editor = currentEditor()) {
                (editor->*method)();
            }
        });
    }
    template <typename Method, typename Arg>
    void connectEditorAction(QAction* action, Method method, Arg value) {
        connect(action, &QAction::triggered, this, [this, method, value]() {
            if (auto* editor = currentEditor()) {
                (editor->*method)(value);
            }
        });
    }
    void applyStyleSheet();
    void applyCustomShortcuts();
    void initUpdateCheck();
    ScintillaNext *getInitialEditor();
    void openFileList(const QStringList &fileNames);
    bool checkEditorsBeforeClose(const QVector<ScintillaNext *> &editors);
    bool checkFileForModification(ScintillaNext *editor);
    void showSaveErrorMessage(ScintillaNext *editor, QFileDevice::FileError error);
    void showEditorZoomLevelIndicator();
    void attachAiAgentDock(AiAgentDock *dock, bool raise = true);
    void registerWorkspaceDock(FolderAsWorkspaceDock *dock);
    void openFolderAsWorkspacePath(const QString &dir, bool showGitTab = false);
    void wireWorkspaceGitSignals(FolderAsWorkspaceDock *dock);
    FolderAsWorkspaceDock *activeWorkspaceDock() const;
    QString currentWorkspaceRoot() const;

    // Resolve which open workspace dock should host a "Show in Workspace"
    // reveal of the given file. Walks all open workspace docks (findChildren)
    // with boundary-safe, normalized containment and picks the LONGEST matching
    // root (most specific, for nested workspaces). Prefers the active dock if it
    // contains the file; never resolves for an unsaved file (nothing to locate
    // in a tree). Returns nullptr → action disabled. Used by both the tab-menu
    // gating and the triggered handler so the gate and the action agree.
    FolderAsWorkspaceDock *resolveShowInWorkspaceDock(const QString &filePath, bool isFile) const;

    // Centralized active-workspace setter: assigns m_activeWorkspace, syncs
    // CrashContext, and emits activeWorkspaceChanged(new, old). All five sites
    // that previously assigned m_activeWorkspace directly route through this
    // so generators / runners can cancel-on-switch.
    void setActiveWorkspace(FolderAsWorkspaceDock *dock);

    // Workspace state on-disk memo, loaded once from QSettings at the start of
    // restoreOpenWorkspaces and consulted per-dock from openFolderAsWorkspacePath.
    void loadAllWorkspaceStates() const;
    // Merge live snapshots with the on-disk memo, cap to MAX_MEMOED, write back.
    // Called both from full saveSettings (aboutToClose) and from
    // saveWorkspaceStatesOnly (60s autosave when dirty).
    void persistWorkspaceStatesMerged(const QVector<WorkspaceStateSnapshot> &live) const;
    // Persist a single snapshot (mid-session dock close path).
    void persistOneWorkspaceState(const WorkspaceStateSnapshot &snapshot) const;

    enum class UserSaveAction { SaveAll, DiscardAll, Cancel };
    UserSaveAction promptForSave(const QVector<ScintillaNext *> &editors);

    void saveSettings() const;
    void restoreSettings();

    ISearchResultsHandler *determineSearchResultsHandler();

    QActionGroup *languageActionGroup;

    TabsQuickActionsBar *tabsQuickActionsBar = Q_NULLPTR;

    //NppImporter *npp;

    MacroManager macroManager;
    DefaultDirectoryManager *defaultDirectoryManager;

    ZoomEventWatcher *zoomEventWatcher;
    int zoomLevel = 0;
    int contextMenuPos = 0;
    QMenu *buildMenu(const QStringList &actionNames);

    TerminalManager *terminalManager = Q_NULLPTR;
    FileWatcher *fileWatcher = Q_NULLPTR;
    MiniAppManager *m_miniAppManager = nullptr;
    MiniAppRegistry *m_miniAppRegistry = nullptr;
    FindInFolderDock *findInFolderDock = nullptr;

    QAction *m_actionPreview = nullptr;
    QAction *m_actionQuickFileOpen = nullptr;

    // --- Quick File Open (Ctrl+P) per-workspace index cache (RCU-lite) ---
    // Keyed by QDir::cleanPath(workspaceRoot). Written/read ONLY on the UI
    // thread; the enumerator publishes snapshots via a queued signal so the
    // slot (onFileIndexReady) is the sole writer — no lock needed. Snapshots
    // are immutable (shared_ptr<const>) so the open dialog reads lock-free.
    QHash<QString, std::shared_ptr<const FileIndexCache>> m_fileIndexCache;
    WorkspaceFileEnumerator *m_fileIndexEnumerator = nullptr;
    QPointer<QuickFileOpenDialog> m_quickFileOpenDialog;
    QString m_quickFileOpenRootKey;
    void onFileIndexReady(const QString &rootKey,
                          std::shared_ptr<const FileIndexCache> snapshot);
    QStringList workspaceMruFiles(const QString &workspaceRoot) const;

    QPointer<FolderAsWorkspaceDock> m_activeWorkspace;
    QPointer<AiAgentDock> m_activeAiDock;

    // On-disk memo of per-workspace UI state keyed by cleanPath rootPath.
    // Loaded once in restoreOpenWorkspaces; consulted in openFolderAsWorkspacePath.
    // mutable so const helpers can lazy-fill it.
    mutable QHash<QString, WorkspaceStateSnapshot> m_workspaceStateMemo;
    bool m_workspaceStateDirty = false;

    GitOperationManager *m_gitOpMgr = nullptr;
    QPointer<ConflictListDock> m_conflictListDock;
    void setupGitOperationMenu();

    // SSH (Phase 1). setupSshMenu wires the File-menu SSH actions; the last
    // successfully connected remote context drives "Open Remote Terminal" until
    // the Phase-2 remote workspace tree resolves it via activeExecutionContext.
    void setupSshMenu();
    QPointer<remote::ExecutionContext> m_lastConnectedRemote;
    // "Open Remote Folder via SSH…" flow (D10 Batch H): profile pick → staged
    // connect → folder picker → open the remote workspace dock.
    void openRemoteFolderViaSshFlow();
    // Wire a workspace dock to a RemoteExecutionContext's stateChanged signal so
    // the dock's inline banner tracks the connection lifecycle (D10 Batch H).
    // Idempotent — safe to call multiple times for the same dock.
    void wireSshDockToContext(FolderAsWorkspaceDock *dock, remote::ExecutionContext *ctx);
    // Reconnect handler for the dock's Reconnect button: retrieves or creates the
    // context, re-triggers connectToHost, and (re-)wires the dock.
    void reconnectSshWorkspace(FolderAsWorkspaceDock *dock);
    void showConflictListDock(const QString &repoPath);
    void openConflictMergeViewer(const ConflictEntry &entry);

#ifndef NDEBUG
    // SSH transport debug log dialog (debug builds only). Rebound whenever the
    // active SSH workspace changes via activeWorkspaceChanged.
    QPointer<SshDebugDialog> m_sshDebugDialog;
    void rebindSshDebugDialog();
#endif
};

#endif // MAINWINDOW_H
