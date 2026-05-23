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
class FolderAsWorkspaceDock;
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
    void newFile();

    void openFileDialog();
    void openFile(const QString &filePath);

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

    void detectLanguage(ScintillaNext *editor);

    void setLanguage(ScintillaNext *editor, const QString &languageName);

    void bringWindowToForeground();
    void focusIn();

    void addEditor(ScintillaNext *editor);

    void checkForUpdates(bool silent = false);

    void restoreWindowState();
    void restoreOpenWorkspaces();
    void raiseSavedActiveWorkspace();

    // Workspace-state persistence (per-dock inner tab + expanded folders +
    // current item). Dirty bit flipped from FolderAsWorkspaceDock::stateDirty;
    // 60s autosave path checks and runs saveWorkspaceStatesOnly().
    bool isWorkspaceStateDirty() const { return m_workspaceStateDirty; }
    void clearWorkspaceStateDirty() { m_workspaceStateDirty = false; }
    void saveWorkspaceStatesOnly();

    void switchToEditor(const ScintillaNext *editor);

signals:
    void editorActivated(ScintillaNext *editor);
    void aboutToClose();
    void fileDialogAccepted(const QString &filePath);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void tabBarRightClicked(ScintillaNext *editor);
    void languageMenuTriggered();
    void checkForUpdatesFinished(QString url);
    void activateEditor(ScintillaNext *editor);

private:
    Ui::MainWindow *ui = Q_NULLPTR;
    NotepadNextApplication *app = Q_NULLPTR;
    DockedEditor *dockedEditor = Q_NULLPTR;

    QScopedPointer<SearchResultsCollector> searchResults;

    void applyStyleSheet();
    void applyCustomShortcuts();
    void initUpdateCheck();
    ScintillaNext *getInitialEditor();
    void openFileList(const QStringList &fileNames);
    bool checkEditorsBeforeClose(const QVector<ScintillaNext *> &editors);
    bool checkFileForModification(ScintillaNext *editor);
    void showSaveErrorMessage(ScintillaNext *editor, QFileDevice::FileError error);
    void showEditorZoomLevelIndicator();
    void attachAiAgentDock(class AiAgentDock *dock);
    void registerWorkspaceDock(FolderAsWorkspaceDock *dock);
    void openFolderAsWorkspacePath(const QString &dir, bool showGitTab = false);
    void wireWorkspaceGitSignals(FolderAsWorkspaceDock *dock);
    FolderAsWorkspaceDock *activeWorkspaceDock() const;
    QString currentWorkspaceRoot() const;

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
    QMenu *buildMenu(QStringList actionNames);

    TerminalManager *terminalManager = Q_NULLPTR;

    QPointer<FolderAsWorkspaceDock> m_activeWorkspace;

    // On-disk memo of per-workspace UI state keyed by cleanPath rootPath.
    // Loaded once in restoreOpenWorkspaces; consulted in openFolderAsWorkspacePath.
    // mutable so const helpers can lazy-fill it.
    mutable QHash<QString, WorkspaceStateSnapshot> m_workspaceStateMemo;
    bool m_workspaceStateDirty = false;
};

#endif // MAINWINDOW_H
