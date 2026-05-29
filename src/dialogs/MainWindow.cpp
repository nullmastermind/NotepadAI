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


#include "MainWindow.h"
#include "BookMarkDecorator.h"
#include "DefaultDirectoryManager.h"
#include "MarkerAppDecorator.h"
#include "ScintillaSorter.h"
#include "URLFinder.h"
#include "SessionManager.h"
#include "UndoAction.h"
#include "ui_MainWindow.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QStringList>
#include <QClipboard>
#include <QStandardPaths>
#include <QWindow>
#include <QPushButton>
#include <QTimer>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QDirIterator>
#include <QProcess>
#include <QScreen>
#include <QFontDatabase>
#include <QDateTime>
#include <QKeyEvent>

#ifdef Q_OS_WIN
#include <QSimpleUpdater.h>
#include <Windows.h>
#endif

#include "DockAreaWidget.h"

#include "NotepadNextApplication.h"
#include "ApplicationSettings.h"
#include "StylesheetResource.h"

#include "ScintillaNext.h"

#include "RecentFilesListManager.h"
#include "RecentFilesListMenuBuilder.h"
#include "EditorManager.h"

#include "LuaConsoleDock.h"
#include "LanguageInspectorDock.h"
#include "EditorInspectorDock.h"
#include "FolderAsWorkspaceDock.h"
#include "GitStatusEntry.h"
#include "SearchResultsDock.h"
#include "DebugLogDock.h"
#include "FileListDock.h"
#include "TerminalDock.h"
#include "DockMiddleClickCloser.h"
#include "PreviewTabManager.h"

#include "TerminalManager.h"
#include "TerminalCwdResolver.h"
#include "TerminalTaskRegistry.h"
#include "EditTasksDialog.h"
#include "MiniAppManager.h"
#include "MiniAppRegistry.h"
#include "EditMiniAppsDialog.h"

#include "FindReplaceDialog.h"
#include "MacroRunDialog.h"
#include "MacroSaveDialog.h"
#include "PreferencesDialog.h"
#include "AcpAgentSettingsDialog.h"
#include "AcpAgentRegistry.h"
#include "AcpAgentManager.h"
#include "AiAgentDock.h"
#include "ScheduledTaskDialog.h"
#include "ScheduledTaskRegistry.h"
#include "ScheduledTaskRunner.h"
#include "ColumnEditorDialog.h"
#include "ConflictListDock.h"
#include "ConflictMergeViewerDock.h"
#include "ConflictEntry.h"
#include "GitOperationManager.h"
#include "GitController.h"
#include "GitProcessRunner.h"
#include "GitTabWidget.h"
#include "BranchPickerPopup.h"
#include "InteractiveRebaseDialog.h"

#include "TabsQuickActionsBar.h"

#include "QuickFindWidget.h"
#include "QuickFileOpenDialog.h"
#include "WorkspaceFileEnumerator.h"

#include "EditorPrintPreviewRenderer.h"
#include "MacroEditorDialog.h"

#include "ZoomEventWatcher.h"
#include "FileDialogHelpers.h"
#include "FileWatcher.h"

#include "HtmlConverter.h"
#include "RtfConverter.h"
#include "PandocExporter.h"

#include "FadingIndicator.h"

#include "ActionUtils.h"

#include "CrashContext.h"
#include "CrashHandler.h"
#include "ProfileScope.h"

#include <QApplication>
#include <QEvent>
#include <QActionEvent>
#include <QFont>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include "DockWidget.h"

#include <algorithm>

namespace {

// Walks up the parent chain looking for an ADS dock (or any QWidget with a
// non-empty objectName) so the crash report can name *where* the user was
// focused. Pure read-only; safe to call from focusChanged signals.
QString resolveDockObjectName(QWidget *w)
{
    while (w) {
        if (auto *dock = qobject_cast<ads::CDockWidget *>(w)) {
            return dock->objectName();
        }
        const QString name = w->objectName();
        if (!name.isEmpty() && w->isWindow() == false) {
            // Heuristic: only return names of dock-ish widgets, not random children.
            if (name.contains(QLatin1String("Dock"), Qt::CaseInsensitive)) {
                return name;
            }
        }
        w = w->parentWidget();
    }
    return {};
}

// Connects a QAction's triggered() to CrashContext::setLastAction so we know
// what the user just clicked when a later crash happens. Deduplicates via a
// property to survive being called multiple times (filter + findChildren sweep).
void wireActionForCrashContext(QAction *action)
{
    if (!action || action->property("crashContextWired").toBool()) {
        return;
    }
    action->setProperty("crashContextWired", true);

    QPointer<QAction> guard(action);
    QObject::connect(action, &QAction::triggered, action, [guard]() {
        if (!guard) return;
        // Prefer objectName ("actionOpen") over text (which is translated and
        // may contain '&' mnemonics) so the report is stable across locales.
        const QString name = guard->objectName().isEmpty()
                                 ? guard->text()
                                 : guard->objectName();
        CrashContext::setLastAction(name);
    });
}

// QApplication-wide event filter that catches QActionEvent::ActionAdded so
// dynamically-created actions (recent files menu, language menu) also report
// themselves to CrashContext. Static lifetime via Q_GLOBAL_STATIC would be
// fine but we just leak one instance for the lifetime of the app.
class ActionAddedFilter : public QObject
{
public:
    explicit ActionAddedFilter(QObject *parent = nullptr) : QObject(parent) {}

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override
    {
        if (ev->type() == QEvent::ActionAdded) {
            auto *ae = static_cast<QActionEvent *>(ev);
            wireActionForCrashContext(ae->action());
        }
        return QObject::eventFilter(obj, ev);
    }
};

bool isEditorFocused()
{
    QWidget *w = QApplication::focusWidget();
    while (w) {
        if (qobject_cast<ScintillaNext *>(w))
            return true;
        w = w->parentWidget();
    }
    return false;
}

bool forwardClipboardToFocusWidget(const char *slot)
{
    QWidget *w = QApplication::focusWidget();
    if (!w) return false;
    return QMetaObject::invokeMethod(w, slot);
}

} // namespace


MainWindow::MainWindow(NotepadNextApplication *app) :
    ui(new Ui::MainWindow),
    app(app),
    zoomEventWatcher(new ZoomEventWatcher(this))
{
    PROFILE_SCOPE("MainWindow::ctor");
    qInfo(Q_FUNC_INFO);

    setAttribute(Qt::WA_DeleteOnClose);

    {
        PROFILE_SCOPE("MainWindow::ctor.setupUi");
        ui->setupUi(this);
    }

    // CrashContext wiring: catch every QAction added to this app from now on
    // (including dynamically-built recent-files / language entries) so the
    // crash report knows what the user just clicked. The follow-up
    // findChildren() sweep at the end of the constructor catches everything
    // setupUi created before this filter was installed.
    qApp->installEventFilter(new ActionAddedFilter(qApp));

    // Track which dock has focus so the report points to the right surface
    // (e.g. "AiAgentDock" vs "FolderAsWorkspaceDock") when a faulting action
    // wasn't a QAction trigger.
    connect(qApp, &QApplication::focusChanged, this,
            [](QWidget * /*old*/, QWidget *now) {
                const QString dockName = resolveDockObjectName(now);
                if (!dockName.isEmpty()) {
                    CrashContext::setActiveDockId(dockName);
                }
            });

    applyCustomShortcuts();

    qInfo("setupUi Completed");

    defaultDirectoryManager = new DefaultDirectoryManager(this, app->getSettings());

    connect(this, &MainWindow::aboutToClose, this, &MainWindow::saveSettings);

    // Create and set up the connections to the docked editor
    dockedEditor = new DockedEditor(this);
    connect(dockedEditor, &DockedEditor::editorCloseRequested, this, &MainWindow::closeFile);
    connect(dockedEditor, &DockedEditor::editorActivated, this, &MainWindow::activateEditor);
    connect(dockedEditor, &DockedEditor::previewTabActivated, this, [this](QWidget *) {
        if (m_actionMarkdownPreview)
            m_actionMarkdownPreview->setChecked(true);
    });
    connect(dockedEditor, &DockedEditor::contextMenuRequestedForEditor, this, &MainWindow::tabBarRightClicked);
    connect(dockedEditor, &DockedEditor::titleBarDoubleClicked, this, &MainWindow::newFile);

    fileWatcher = new FileWatcher(this);
    connect(fileWatcher, &FileWatcher::fileModifiedExternally, this, [this](ScintillaNext *editor) {
        if (!editor->modify()) {
            editor->reload();
        } else if (editor == currentEditor()) {
            checkFileForModification(editor);
        }
        updateGui(editor);
    });
    connect(fileWatcher, &FileWatcher::fileDeletedExternally, this, [this](ScintillaNext *editor) {
        updateGui(editor);
    });
    connect(fileWatcher, &FileWatcher::fileRestoredExternally, this, [this](ScintillaNext *editor) {
        if (editor == currentEditor())
            checkFileForModification(editor);
        updateGui(editor);
    });

    // Set up the menus
    {
    PROFILE_SCOPE("MainWindow::ctor.actionWiring");
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newFile);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(ui->actionReload, &QAction::triggered, this, &MainWindow::reloadFile);
    connect(ui->actionClose, &QAction::triggered, this, &MainWindow::closeCurrentFile);
    connect(ui->actionCloseAll, &QAction::triggered, this, &MainWindow::closeAllFiles);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);

    // Split editor actions
    connect(ui->actionSplitHorizontal, &QAction::triggered, this, [this]() {
        newFile();
        ScintillaNext *newEditor = currentEditor();
        if (newEditor) {
            dockedEditor->splitToRight(newEditor);
        }
    });
    connect(ui->actionSplitVertical, &QAction::triggered, this, [this]() {
        newFile();
        ScintillaNext *newEditor = currentEditor();
        if (newEditor) {
            dockedEditor->splitToBottom(newEditor);
        }
    });

#ifdef Q_OS_WIN
    ui->actionExit->setShortcut(QKeySequence("Alt+F4"));
#else
    ui->actionExit->setShortcut(QKeySequence::Quit);
#endif

    connect(ui->actionOpenFolderasWorkspace, &QAction::triggered, this, &MainWindow::openFolderAsWorkspaceDialog);
    connect(ui->actionOpenFolderAsWorkspaceNew, &QAction::triggered, this, &MainWindow::openFolderAsWorkspaceDialog);

    connect(ui->menuFile, &QMenu::aboutToShow, this, [=]() {
        RecentFilesListManager *recents = app->getRecentWorkspacesListManager();
        const bool hasRecents = recents->count() > 0;

        ui->actionOpenFolderasWorkspace->setVisible(!hasRecents);
        ui->menuOpenFolderAsWorkspace->menuAction()->setVisible(hasRecents);

        if (!hasRecents) return;

        // Static head of the submenu = "Open New Folder..." + separator (2 actions).
        // Strip any previously-built recent entries before rebuilding.
        while (ui->menuOpenFolderAsWorkspace->actions().size() > 2) {
            delete ui->menuOpenFolderAsWorkspace->actions().takeLast();
        }

        int i = 0;
        for (const QString &path : recents->fileList()) {
            ++i;
            const QString native = QDir::toNativeSeparators(path);
            QAction *action = new QAction(
                QString("%1%2: %3").arg(i < 10 ? "&" : "").arg(i).arg(native),
                ui->menuOpenFolderAsWorkspace);
            action->setData(path);
            connect(action, &QAction::triggered, this, [this, path]() {
                openFolderAsWorkspacePath(path);
            });
            ui->menuOpenFolderAsWorkspace->addAction(action);
        }
    });

    connect(ui->actionCloseAllExceptActive, &QAction::triggered, this, &MainWindow::closeAllExceptActive);
    connect(ui->actionCloseAllToLeft, &QAction::triggered, this, &MainWindow::closeAllToLeft);
    connect(ui->actionCloseAllToRight, &QAction::triggered, this, &MainWindow::closeAllToRight);

    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::saveCurrentFile);
    connect(ui->actionSaveAs, &QAction::triggered, this, &MainWindow::saveCurrentFileAsDialog);
    connect(ui->actionSaveCopyAs, &QAction::triggered, this, &MainWindow::saveCopyAsDialog);
    connect(ui->actionSaveAll, &QAction::triggered, this, &MainWindow::saveAll);
    connect(ui->actionRename, &QAction::triggered, this, &MainWindow::renameFile);

    connect(ui->actionExportHtml, &QAction::triggered, this, [this]() {
        HtmlConverter html(currentEditor());
        exportAsFormat(&html, QStringLiteral("HTML files (*.html)"));
    });

    connect(ui->actionExportRtf, &QAction::triggered, this, [this]() {
        RtfConverter rtf(currentEditor());
        exportAsFormat(&rtf, QStringLiteral("RTF Files (*.rtf)"));
    });

    connect(ui->actionPrint, &QAction::triggered, this, &MainWindow::print);

    {
        m_actionQuickFileOpen = new QAction(tr("Quick File Open..."), this);
        m_actionQuickFileOpen->setObjectName(QStringLiteral("actionQuickFileOpen"));
        m_actionQuickFileOpen->setShortcuts({
            QKeySequence(Qt::CTRL | Qt::Key_P),
            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
        });
        m_actionQuickFileOpen->setEnabled(false);
        addAction(m_actionQuickFileOpen);

        // Lazily-built background enumerator. Created once; its queued signal is
        // connected exactly once so onFileIndexReady stays the sole UI-thread
        // writer of m_fileIndexCache (RCU-lite, no lock).
        qRegisterMetaType<std::shared_ptr<const FileIndexCache>>();
        m_fileIndexEnumerator = new WorkspaceFileEnumerator(this);
        connect(m_fileIndexEnumerator, &WorkspaceFileEnumerator::indexReady,
                this, &MainWindow::onFileIndexReady, Qt::QueuedConnection);

        connect(m_actionQuickFileOpen, &QAction::triggered, this, [this]() {
            const QString rootKey = QDir::cleanPath(currentWorkspaceRoot());
            if (rootKey.isEmpty()) return;
            m_actionQuickFileOpen->setEnabled(false);
            auto *dlg = new QuickFileOpenDialog(rootKey, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->move(mapToGlobal(QPoint((width() - dlg->minimumWidth()) / 2, height() / 5)));

            // Track the live dialog so onFileIndexReady can hand it a refreshed
            // snapshot if revalidation finishes while it is open.
            m_quickFileOpenDialog = dlg;
            m_quickFileOpenRootKey = rootKey;

            // Serve the cached snapshot immediately if warm; otherwise the
            // dialog shows its own "Indexing…" placeholder until indexReady.
            if (auto it = m_fileIndexCache.constFind(rootKey); it != m_fileIndexCache.cend())
                dlg->adoptSnapshot(*it);

            // MRU files (workspace-relative) drive the empty-query ordering.
            dlg->setMruFiles(workspaceMruFiles(rootKey));

            connect(dlg, &QDialog::finished, this, [this, dlg](int result) {
                if (result == QDialog::Accepted) {
                    const QString path = dlg->selectedFilePath();
                    if (!path.isEmpty())
                        previewFile(path);
                }
                m_actionQuickFileOpen->setEnabled(!currentWorkspaceRoot().isEmpty());
            });
            dlg->show();

            // Always revalidate in the background so the cache reflects on-disk
            // changes since the last open (enumeration never blocks the UI).
            m_fileIndexEnumerator->enumerate(rootKey);
        });
        connect(this, &MainWindow::activeWorkspaceChanged, m_actionQuickFileOpen, [this]() {
            m_actionQuickFileOpen->setEnabled(!currentWorkspaceRoot().isEmpty());
        });
    }

    connectEditorAction(ui->actionToggleSingleLineComment, &ScintillaNext::toggleCommentSelection);
    connectEditorAction(ui->actionSingleLineComment, &ScintillaNext::commentLineSelection);
    connectEditorAction(ui->actionSingleLineUncomment, &ScintillaNext::uncommentLineSelection);

    connect(ui->actionBase64Encode, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        const QByteArray selection = editor->getSelText();
        editor->replaceSel(selection.toBase64().constData());
    });
    connect(ui->actionURLEncode, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        const QByteArray selection = editor->getSelText();
        editor->replaceSel(selection.toPercentEncoding().constData());
    });
    connect(ui->actionBase64Decode, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        const QByteArray selection = editor->getSelText();
        if (auto result = QByteArray::fromBase64Encoding(selection)) {
            editor->replaceSel((*result).constData());
        }
    });
    connect(ui->actionURLDecode,&QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        const QByteArray selection = editor->getSelText();
        editor->replaceSel(QByteArray::fromPercentEncoding(selection).constData());
    });
    connect(ui->actionCopyURL, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        URLFinder *urlFinder = editor->findChild<URLFinder *>(QString(), Qt::FindDirectChildrenOnly);
        if (urlFinder && urlFinder->isEnabled()) {
            urlFinder->copyURLToClipboard(contextMenuPos);
        }
    });

    connect(ui->actionClearRecentFilesList, &QAction::triggered, app->getRecentFilesListManager(), &RecentFilesListManager::clear);

    connect(ui->actionMoveToTrash, &QAction::triggered, this, &MainWindow::moveCurrentFileToTrash);

    RecentFilesListMenuBuilder *recentFileListMenuBuilder = new RecentFilesListMenuBuilder(app->getRecentFilesListManager());
    connect(ui->menuRecentFiles, &QMenu::aboutToShow, this, [=, this]() {
        // NOTE: its unfortunate that this has to be hard coded, but there's no way
        // to easily determine what should or shouldn't be there
        while (ui->menuRecentFiles->actions().size() > 4) {
            delete ui->menuRecentFiles->actions().takeLast();
        }

        recentFileListMenuBuilder->populateMenu(ui->menuRecentFiles);
    });

    connect(ui->actionRestoreRecentlyClosedFile, &QAction::triggered, this, [=, this]() {
        if (app->getRecentFilesListManager()->count() > 0) {
            openFileList(QStringList() << app->getRecentFilesListManager()->mostRecentFile());
        }
    });

    connect(ui->actionOpenAllRecentFiles, &QAction::triggered, this, [=, this]() {
        openFileList(app->getRecentFilesListManager()->fileList());
    });

    connect(recentFileListMenuBuilder, &RecentFilesListMenuBuilder::fileOpenRequest, this, &MainWindow::openFile);

    QActionGroup *eolActionGroup = new QActionGroup(this);
    eolActionGroup->addAction(ui->actionWindows);
    eolActionGroup->addAction(ui->actionUnix);
    eolActionGroup->addAction(ui->actionMacintosh);

    ui->actionWindows->setData(SC_EOL_CRLF);
    ui->actionUnix->setData(SC_EOL_LF);
    ui->actionMacintosh->setData(SC_EOL_CR);

    auto handleEolTrigger = [this]() {
        // qobject_cast lets us look at which specific action was clicked
        if (auto* action = qobject_cast<QAction*>(sender())) {
            int eolMode = action->data().toInt();
            convertEOLs(eolMode);
        }
    };

    // Connect all three to the same handler
    connect(ui->actionWindows,   &QAction::triggered, this, handleEolTrigger);
    connect(ui->actionUnix,      &QAction::triggered, this, handleEolTrigger);
    connect(ui->actionMacintosh, &QAction::triggered, this, handleEolTrigger);


    connectEditorAction(ui->actionUpperCase, &ScintillaNext::upperCase);
    connectEditorAction(ui->actionLowerCase, &ScintillaNext::lowerCase);

    connectEditorAction(ui->actionDuplicateCurrentLine, &ScintillaNext::lineDuplicate);
    connectEditorAction(ui->actionMoveSelectedLinesUp, &ScintillaNext::moveSelectedLinesUp);
    connectEditorAction(ui->actionMoveSelectedLinesDown, &ScintillaNext::moveSelectedLinesDown);

    connect(ui->actionSplitLines, &QAction::triggered, this, [this]() {
        currentEditor()->targetFromSelection();
        currentEditor()->linesSplit(0);
    });

    connect(ui->actionJoinLines, &QAction::triggered, this, [this]()  {
        currentEditor()->targetFromSelection();
        currentEditor()->linesJoin();
    });

    connect(ui->actionRemoveEmptyLines, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        Finder f(editor);
        const UndoAction ua(editor);

        f.setSearchText(QStringLiteral("\\R\\R+"));
        f.setSearchFlags(SCFIND_REGEXP);
        f.replaceAll(editor->eolString());

        // The regex will not entirely remove a blank first line
        editor->deleteLeadingEmptyLines();

        // Regex will also not delete the final blank line
        editor->deleteTrailingEmptyLines();
    });

    connectEditorAction(ui->actionRemoveDuplicateLines, &ScintillaNext::removeDuplicateLines);
    connectEditorAction(ui->actionRemoveConsecutiveDuplicateLines, &ScintillaNext::removeConsecutiveDuplicateLines);

    connect(ui->actionReverseLineOrder, &QAction::triggered, this, [=, this]() {
        ScintillaSorter scintillaSorter(currentEditor());
        scintillaSorter.sort(ReverseSorter(Sorter::Direction::Ascending));
    });
    connect(ui->actionSortLinesAsc, &QAction::triggered, this, [=, this]() {
        ScintillaSorter scintillaSorter(currentEditor());
        scintillaSorter.sort(CaseSensitiveSorter(Sorter::Direction::Ascending));
    });
    connect(ui->actionSortLinesAscCaseInsensitive, &QAction::triggered, this, [=, this]() {
        ScintillaSorter scintillaSorter(currentEditor());
        scintillaSorter.sort(CaseInsensitiveSorter(Sorter::Direction::Ascending));
    });
    connect(ui->actionSortLinesbyLengthAsc, &QAction::triggered, this, [=, this]() {
        ScintillaSorter scintillaSorter(currentEditor());
        scintillaSorter.sort(LineLengthSorter(Sorter::Direction::Ascending));
    });
    connect(ui->actionSortLinesDesc, &QAction::triggered, this, [=, this]() {
        ScintillaSorter scintillaSorter(currentEditor());
        scintillaSorter.sort(CaseSensitiveSorter(Sorter::Direction::Descending));
    });
    connect(ui->actionSortLinesDescCaseInsensitive, &QAction::triggered, this, [=, this]() {
        ScintillaSorter scintillaSorter(currentEditor());
        scintillaSorter.sort(CaseInsensitiveSorter(Sorter::Direction::Descending));
    });
    connect(ui->actionSortLinesbyLengthDesc, &QAction::triggered, this, [=, this]() {
        ScintillaSorter scintillaSorter(currentEditor());
        scintillaSorter.sort(LineLengthSorter(Sorter::Direction::Descending));
    });

    connect(ui->actionColumnMode, &QAction::triggered, this, [this]() {
        ColumnEditorDialog *columnEditor = findChild<ColumnEditorDialog *>(QString(), Qt::FindDirectChildrenOnly);

        if (columnEditor == Q_NULLPTR) {
            columnEditor = new ColumnEditorDialog(this);
        }

        columnEditor->show();
        columnEditor->raise();
        columnEditor->activateWindow();
    });

    connect(ui->actionUndo, &QAction::triggered, this, [=]() { currentEditor()->undo(); });
    connect(ui->actionRedo, &QAction::triggered, this, [=]() { currentEditor()->redo(); });
    connect(ui->actionCut, &QAction::triggered, this, [=]() {
        if (!isEditorFocused() && forwardClipboardToFocusWidget("cut")) return;
        currentEditor()->cutAllowLine();
    });
    connect(ui->actionCopy, &QAction::triggered, this, [=]() {
        if (!isEditorFocused() && forwardClipboardToFocusWidget("copy")) return;
        currentEditor()->copyAllowLine();
    });
    connect(ui->actionDelete, &QAction::triggered, this, [=]() { currentEditor()->clear(); });
    connect(ui->actionPaste, &QAction::triggered, this, [=]() {
        if (!isEditorFocused() && forwardClipboardToFocusWidget("paste")) return;
        currentEditor()->paste();
    });
    connect(ui->actionSelectAll, &QAction::triggered, this, [=]() {
        if (!isEditorFocused() && forwardClipboardToFocusWidget("selectAll")) return;
        currentEditor()->selectAll();
    });
    connect(ui->actionSelectNext, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();

        editor->setSearchFlags(SCFIND_NONE);
        editor->targetWholeDocument();
        editor->multipleSelectAddNext();
    });
    connect(ui->actionCopyFullPath, &QAction::triggered, this, [this]() {
        auto editor = currentEditor();
        if (editor->isFile()) {
            QApplication::clipboard()->setText(editor->getFilePath());
        }
    });
    connect(ui->actionCopyFileName, &QAction::triggered, this, [this]() {
        QApplication::clipboard()->setText(currentEditor()->getName());
    });
    connect(ui->actionCopyFileDirectory, &QAction::triggered, this, [this]() {
        auto editor = currentEditor();
        if (editor->isFile()) {
            QApplication::clipboard()->setText(editor->getPath());
        }
    });

    connect(ui->actionCopyAsHtml, &QAction::triggered, this, [this]() {
        HtmlConverter html(currentEditor());
        copyAsFormat(&html, "text/html");
    });

    connect(ui->actionCopyAsRtf, &QAction::triggered, this, [this]() {
        RtfConverter rtf(currentEditor());
        copyAsFormat(&rtf, "Rich Text Format");
    });

    connectEditorAction(ui->actionIncreaseIndent, &ScintillaNext::tab);
    connectEditorAction(ui->actionDecreaseIndent, &ScintillaNext::backTab);

    addAction(ui->actionToggleOverType);
    connect(ui->actionToggleOverType, &QAction::triggered, this, [this]() {
        currentEditor()->editToggleOvertype();
        ui->statusBar->refresh(currentEditor());
    });

    SearchResultsDock *srDock = new SearchResultsDock(this);
    addDockWidget(Qt::BottomDockWidgetArea, srDock);
    DockMiddleClickCloser::install(srDock);
    srDock->toggleViewAction()->setShortcut(Qt::Key_F7);
    ui->menuView->addAction(srDock->toggleViewAction());

    connect(srDock, &SearchResultsDock::searchResultActivated, this, [=, this](ScintillaNext *editor, int lineNumber, int startPositionFromBeginning, int endPositionFromBeginning) {
        dockedEditor->switchToEditor(editor);

        int linePos = editor->positionFromLine(lineNumber);
        editor->goToRange({linePos + startPositionFromBeginning, linePos + endPositionFromBeginning});
        editor->verticalCentreCaret();

        editor->grabFocus();
    });

    connect(ui->actionFind, &QAction::triggered, this, [this]() {
        showFindReplaceDialog(FindReplaceDialog::FIND_TAB);
    });

    connect(ui->actionFindNext, &QAction::triggered, this, [this]() {
        FindReplaceDialog *f = findChild<FindReplaceDialog *>(QString(), Qt::FindDirectChildrenOnly);

        if (f) {
            f->performNextSearch();
        }
    });

    connect(ui->actionFindPrevious, &QAction::triggered, this, [this]() {
        FindReplaceDialog *f = findChild<FindReplaceDialog *>(QString(), Qt::FindDirectChildrenOnly);

        if (f) {
            f->performPrevSearch();
        }
    });

    connect(ui->actionQuickFind, &QAction::triggered, this, [this]() {
        QuickFindWidget *quickFind = findChild<QuickFindWidget *>(QString(), Qt::FindDirectChildrenOnly);

        if (quickFind == Q_NULLPTR) {
            quickFind = new QuickFindWidget(this);
        }

        quickFind->setEditor(currentEditor());
        quickFind->setFocus();
        quickFind->show();
    });

    connect(ui->actionReplace, &QAction::triggered, this, [this]() {
        showFindReplaceDialog(FindReplaceDialog::REPLACE_TAB);
    });

    connect(ui->actionSearchAndBookmark, &QAction::triggered, this, [this]() {
        showFindReplaceDialog(FindReplaceDialog::MARK_TAB);
    });

    connect(ui->actionGoToLine, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        const int currentLine = editor->lineFromPosition(editor->currentPos()) + 1;
        const int maxLine = editor->lineCount();
        bool ok;

        QInputDialog d = QInputDialog(this);
        Qt::WindowFlags flags = d.windowFlags() & ~Qt::WindowContextHelpButtonHint;
        int lineToGoTo = d.getInt(this, tr("Go to line"), tr("Line Number (1 - %1)").arg(maxLine), currentLine, 1, maxLine, 1, &ok, flags);

        if (ok) {
            editor->ensureVisible(lineToGoTo - 1);
            editor->gotoLine(lineToGoTo - 1);
            editor->verticalCentreCaret();
        }
    });

    // Style all actions that have a MarkerNumber and interpret that as the color needed
    MarkerAppDecorator *markerAppDecorator = app->findChild<MarkerAppDecorator*>(QString(), Qt::FindDirectChildrenOnly);
    for (QAction* action : findChildren<QAction*>()) {
        if (action->property("MarkerNumber").isValid()) {
            int markerNumber = action->property("MarkerNumber").toInt();
            action->setIcon(ActionUtils::createSolidIcon(markerAppDecorator->markerColor(markerNumber)));
        }
    }

    auto mark_callback = [=, this]() {
        MarkerAppDecorator *markerAppDecorator = app->findChild<MarkerAppDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (markerAppDecorator && markerAppDecorator->isEnabled()) {
            if (sender()->property("MarkerNumber").isValid()) {
                ScintillaNext *editor = currentEditor();
                markerAppDecorator->mark(editor, sender()->property("MarkerNumber").toInt());
            }
        }
    };

    auto clear_mark_callback = [=, this]() {
        MarkerAppDecorator *markerAppDecorator = app->findChild<MarkerAppDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (markerAppDecorator && markerAppDecorator->isEnabled()) {
            if (sender()->property("MarkerNumber").isValid()) {
                ScintillaNext *editor = currentEditor();
                markerAppDecorator->clear(editor, sender()->property("MarkerNumber").toInt());
            }
        }
    };

    connect(ui->actionMarkStyle1, &QAction::triggered, this, mark_callback);
    connect(ui->actionMarkStyle2, &QAction::triggered, this, mark_callback);
    connect(ui->actionMarkStyle3, &QAction::triggered, this, mark_callback);

    connect(ui->actionClearStyle1, &QAction::triggered, this, clear_mark_callback);
    connect(ui->actionClearStyle2, &QAction::triggered, this, clear_mark_callback);
    connect(ui->actionClearStyle3, &QAction::triggered, this, clear_mark_callback);

    connect(ui->actionClearAllStyles, &QAction::triggered, this, [=, this]() {
        MarkerAppDecorator *markerAppDecorator = app->findChild<MarkerAppDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (markerAppDecorator && markerAppDecorator->isEnabled()) {
            markerAppDecorator->clearAll(currentEditor());
        }
    });

    connect(ui->actionToggleBookmark, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            editor->forEachLineInSelection(editor->mainSelection(), [&](int line) {
                bookMarkDecorator->toggleBookmark(line);
            });
        }
    });

    connect(ui->actionNextBookmark, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            int currentLine = editor->lineFromPosition(editor->currentPos());
            int nextBookmarkedLine = bookMarkDecorator->nextBookmarkAfter(currentLine + 1);

            if (nextBookmarkedLine != -1) {
                editor->ensureVisibleEnforcePolicy(nextBookmarkedLine);
                editor->gotoLine(nextBookmarkedLine);
            }
        }
    });

    connect(ui->actionClearBookmarks, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            bookMarkDecorator->clearAllBookmarks();
        }
    });

    connect(ui->actionInvertBookmarks, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            for (int line = 0; line < editor->lineCount(); line++) {
                bookMarkDecorator->toggleBookmark(line);
            }
        }
    });

    connect(ui->actionPreviousBookmark, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            int currentLine = editor->lineFromPosition(editor->currentPos());
            int prevBookmarkedLine = bookMarkDecorator->previousBookMarkBefore(currentLine - 1);

            if (prevBookmarkedLine != -1) {
                editor->ensureVisibleEnforcePolicy(prevBookmarkedLine);
                editor->gotoLine(prevBookmarkedLine);
            }
        }
    });

    connect(ui->actionCutBookmarkedLines, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            QString s = bookMarkDecorator->cutBookMarkedLines();

            if (!s.isEmpty()) {
                QApplication::clipboard()->setText(s);
            }
        }
    });

    connect(ui->actionCopyBookmarkedLines, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            QString s = bookMarkDecorator->copyBookMarkedLines();

            if (!s.isEmpty()) {
                QApplication::clipboard()->setText(s);
            }
        }
    });

    connect(ui->actionDeleteBookmarkedLines, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            bookMarkDecorator->deleteBookMarkedLines();
        }
    });


    // The action needs added to the window so it can be triggered via the keyboard
    addAction(ui->actionNextTab);
    ui->actionNextTab->setShortcuts(ui->actionNextTab->shortcuts() << QKeySequence(Qt::CTRL | Qt::Key_PageDown));
    connect(ui->actionNextTab, &QAction::triggered, this, [this]() {
        int index = dockedEditor->currentDockArea()->currentIndex();
        int total = dockedEditor->currentDockArea()->dockWidgetsCount();

        index++;
        dockedEditor->currentDockArea()->setCurrentIndex(index < total ? index : 0);
    });

    // The action needs added to the window so it can be triggered via the keyboard
    addAction(ui->actionPreviousTab);
    ui->actionPreviousTab->setShortcuts(ui->actionPreviousTab->shortcuts() << QKeySequence(Qt::CTRL | Qt::Key_PageUp));
    connect(ui->actionPreviousTab, &QAction::triggered, this, [this]() {
        int index = dockedEditor->currentDockArea()->currentIndex();
        int total = dockedEditor->currentDockArea()->dockWidgetsCount();

        index--;
        dockedEditor->currentDockArea()->setCurrentIndex(index >= 0 ? index : total - 1);
    });

    ui->pushExitFullScreen->setParent(this); // This is important
    ui->pushExitFullScreen->setVisible(false);
    connect(ui->pushExitFullScreen, &QPushButton::clicked, ui->actionFullScreen, &QAction::trigger);
    connect(ui->actionFullScreen, &QAction::triggered, this, [this](bool b) {
        static bool wasMaximized;

        if (b) {
            // NOTE: don't hide() these as it will cancel their actions they hold
            ui->menuBar->setMaximumHeight(0);
            ui->mainToolBar->setMaximumHeight(0);

            wasMaximized = isMaximized();
            if (wasMaximized) {
                // By default when calling showMaximized() from a full screen state, the window will resize
                // to its "normal" size and then immediately resize to the "maximized" size which is very ugly.
                // By calling setGeometry() to the size of the screen, it at least alleviates the ugly animation
                // going from: fullscreen -> small "normal" size -> full size of screen
                setGeometry(screen()->availableGeometry());
            }

            showFullScreen();

            ui->pushExitFullScreen->setGeometry(width() - 20, 0, 20, 20);
            ui->pushExitFullScreen->show();
            ui->pushExitFullScreen->raise();
        }
        else {
            ui->menuBar->setMaximumHeight(QWIDGETSIZE_MAX);
            ui->mainToolBar->setMaximumHeight(QWIDGETSIZE_MAX);

            if (wasMaximized)
                showMaximized();
            else
                showNormal();

            ui->pushExitFullScreen->hide();
        }
    });


    // Show All Characters is just a short cut to toggle whitespace and EOL on
    ui->actionShowAllCharacters->setChecked(app->getSettings()->showWhitespace() && app->getSettings()->showEndOfLine());
    connect(ui->actionShowAllCharacters, &QAction::triggered, app->getSettings(), &ApplicationSettings::setShowWhitespace);
    connect(ui->actionShowAllCharacters, &QAction::triggered, app->getSettings(), &ApplicationSettings::setShowEndOfLine);

    // Show White Space
    ui->actionShowWhitespace->setChecked(app->getSettings()->showWhitespace());
    connect(app->getSettings(), &ApplicationSettings::showWhitespaceChanged, ui->actionShowWhitespace, &QAction::setChecked);
    connect(ui->actionShowWhitespace, &QAction::toggled, app->getSettings(), &ApplicationSettings::setShowWhitespace);
    // Update the "Show All Character" action
    connect(ui->actionShowWhitespace, &QAction::toggled, this, [this](bool b) {
        ui->actionShowAllCharacters->setChecked(b && ui->actionShowEndofLine->isChecked());
    });

    // Show EOL
    ui->actionShowEndofLine->setChecked(app->getSettings()->showEndOfLine());
    connect(app->getSettings(), &ApplicationSettings::showEndOfLineChanged, ui->actionShowEndofLine, &QAction::setChecked);
    connect(ui->actionShowEndofLine, &QAction::toggled, app->getSettings(), &ApplicationSettings::setShowEndOfLine);
    // Update the "Show All Character" action
    connect(ui->actionShowEndofLine, &QAction::toggled, this, [this](bool b) {
        ui->actionShowAllCharacters->setChecked(b && ui->actionShowWhitespace->isChecked());
    });

    // Show Wrap Symbol
    ui->actionShowWrapSymbol->setChecked(app->getSettings()->showWrapSymbol());
    connect(app->getSettings(), &ApplicationSettings::showWrapSymbolChanged, ui->actionShowWrapSymbol, &QAction::setChecked);
    connect(ui->actionShowWrapSymbol, &QAction::toggled, app->getSettings(), &ApplicationSettings::setShowWrapSymbol);

    // Show Indentation Guide
    ui->actionShowIndentGuide->setChecked(app->getSettings()->showIndentGuide());
    connect(app->getSettings(), &ApplicationSettings::showIndentGuideChanged, ui->actionShowIndentGuide, &QAction::setChecked);
    connect(ui->actionShowIndentGuide, &QAction::toggled, app->getSettings(), &ApplicationSettings::setShowIndentGuide);

    // Word Wrap
    ui->actionWordWrap->setChecked(app->getSettings()->wordWrap());
    connect(app->getSettings(), &ApplicationSettings::wordWrapChanged, ui->actionWordWrap, &QAction::setChecked);
    connect(ui->actionWordWrap, &QAction::toggled, app->getSettings(), &ApplicationSettings::setWordWrap);

    // Inline Git Blame
    ui->actionToggleInlineBlame->setChecked(app->getSettings()->inlineBlameEnabled());
    connect(app->getSettings(), &ApplicationSettings::inlineBlameEnabledChanged, ui->actionToggleInlineBlame, &QAction::setChecked);
    connect(ui->actionToggleInlineBlame, &QAction::toggled, app->getSettings(), &ApplicationSettings::setInlineBlameEnabled);

    connect(app->getEditorManager(), &EditorManager::blameCommitClicked, this, [this](const QByteArray &sha) {
        if (auto *dock = m_activeWorkspace.data()) {
            dock->showGitCommitDetail(sha);
        } else {
            QApplication::clipboard()->setText(QString::fromLatin1(sha.left(8)));
        }
    });

    // Zooming controls all editors simulaneously
    connect(ui->actionZoomIn, &QAction::triggered, this, [this]() {
        for (ScintillaNext *editor : editors()) {
            editor->zoomIn();
        }
        zoomLevel = currentEditor()->zoom();

        showEditorZoomLevelIndicator();
    });
    connect(ui->actionZoomOut, &QAction::triggered, this, [this]() {
        for (ScintillaNext *editor : editors()) {
            editor->zoomOut();
        }
        zoomLevel = currentEditor()->zoom();

        showEditorZoomLevelIndicator();
    });
    connect(ui->actionZoomReset, &QAction::triggered, this, [this]() {
        for (ScintillaNext *editor : editors()) {
            editor->setZoom(0);
        }
        zoomLevel = 0;

        showEditorZoomLevelIndicator();
    });

    // Zoom watcher has detected a zoom event, so just trigger the UI action
    connect(zoomEventWatcher, &ZoomEventWatcher::zoomIn, ui->actionZoomIn, &QAction::trigger);
    connect(zoomEventWatcher, &ZoomEventWatcher::zoomOut, ui->actionZoomOut, &QAction::trigger);

    connectEditorAction(ui->actionFoldAll, &ScintillaNext::foldAll, SC_FOLDACTION_CONTRACT | SC_FOLDACTION_CONTRACT_EVERY_LEVEL);
    connectEditorAction(ui->actionFoldAll, &ScintillaNext::foldAll, SC_FOLDACTION_EXPAND | SC_FOLDACTION_CONTRACT_EVERY_LEVEL);

    connectEditorAction(ui->actionFoldLevel1, &ScintillaNext::foldAllLevels, 0);
    connectEditorAction(ui->actionFoldLevel2, &ScintillaNext::foldAllLevels, 1);
    connectEditorAction(ui->actionFoldLevel3, &ScintillaNext::foldAllLevels, 2);
    connectEditorAction(ui->actionFoldLevel4, &ScintillaNext::foldAllLevels, 3);
    connectEditorAction(ui->actionFoldLevel5, &ScintillaNext::foldAllLevels, 4);
    connectEditorAction(ui->actionFoldLevel6, &ScintillaNext::foldAllLevels, 5);
    connectEditorAction(ui->actionFoldLevel7, &ScintillaNext::foldAllLevels, 6);
    connectEditorAction(ui->actionFoldLevel8, &ScintillaNext::foldAllLevels, 7);
    connectEditorAction(ui->actionFoldLevel9, &ScintillaNext::foldAllLevels, 8);

    connectEditorAction(ui->actionUnfoldLevel1, &ScintillaNext::unFoldAllLevels, 0);
    connectEditorAction(ui->actionUnfoldLevel2, &ScintillaNext::unFoldAllLevels, 1);
    connectEditorAction(ui->actionUnfoldLevel3, &ScintillaNext::unFoldAllLevels, 2);
    connectEditorAction(ui->actionUnfoldLevel4, &ScintillaNext::unFoldAllLevels, 3);
    connectEditorAction(ui->actionUnfoldLevel5, &ScintillaNext::unFoldAllLevels, 4);
    connectEditorAction(ui->actionUnfoldLevel6, &ScintillaNext::unFoldAllLevels, 5);
    connectEditorAction(ui->actionUnfoldLevel7, &ScintillaNext::unFoldAllLevels, 6);
    connectEditorAction(ui->actionUnfoldLevel8, &ScintillaNext::unFoldAllLevels, 7);
    connectEditorAction(ui->actionUnfoldLevel9, &ScintillaNext::unFoldAllLevels, 8);

    languageActionGroup = new QActionGroup(this);
    languageActionGroup->setExclusive(true);

    connect(ui->actionPreferences, &QAction::triggered, this, [=, this] {
        PreferencesDialog *pd = findChild<PreferencesDialog *>(QString(), Qt::FindDirectChildrenOnly);

        if (pd == Q_NULLPTR) {
            pd = new PreferencesDialog(app->getSettings(), this);
        }

        pd->show();
        pd->raise();
        pd->activateWindow();
    });

    connect(ui->actionAiAgents, &QAction::triggered, this, [=] {
        AcpAgentSettingsDialog *dlg = findChild<AcpAgentSettingsDialog *>(QString(), Qt::FindDirectChildrenOnly);

        if (dlg == Q_NULLPTR) {
            AcpAgentRegistry *registry = app->getAiAgentManager()
                ? app->getAiAgentManager()->registry()
                : new AcpAgentRegistry(app->getSettings(), this);
            dlg = new AcpAgentSettingsDialog(registry, app->getSettings(), this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
        }

        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });

    connect(ui->actionScheduleTasks, &QAction::triggered, this, [=] {
        ScheduledTaskDialog *dlg = findChild<ScheduledTaskDialog *>(QString(), Qt::FindDirectChildrenOnly);

        if (dlg == Q_NULLPTR) {
            AcpAgentRegistry *registry = app->getAiAgentManager()
                ? app->getAiAgentManager()->registry()
                : new AcpAgentRegistry(app->getSettings(), this);
            ScheduledTaskRunner *runner = app->getScheduledTaskRunner();
            ScheduledTaskRegistry *taskRegistry = runner ? runner->registry() : nullptr;
            dlg = new ScheduledTaskDialog(
                taskRegistry,
                runner,
                registry,
                app->getSettings(),
                this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
        }

        dlg->setDefaultWorkspace(TerminalCwdResolver::resolveWorkspace(currentWorkspaceRoot()));
        dlg->setRecentWorkspaces(app->getRecentWorkspacesListManager()->fileList());
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });

    if (app->getScheduledTaskRunner()) {
        connect(app->getScheduledTaskRunner(), &ScheduledTaskRunner::taskFired, this, [this](AiAgentDock *dock) {
            attachAiAgentDock(dock, false);
            dock->setActivityIndicator(true);
        });
    }

    // The macro manager has already loaded any saved macros, so it might have some already
    ui->actionRunMacroMultipleTimes->setEnabled(macroManager.availableMacros().size() > 0);
    ui->actionEditMacros->setEnabled(macroManager.availableMacros().size() > 0);

    connect(ui->actionMacroRecording, &QAction::triggered, this, [this](bool b) {
        if (b) {
            macroManager.startRecording(currentEditor());
        }
        else {
            macroManager.stopRecording();
        }
    });

    connect(&macroManager, &MacroManager::recordingStarted, this, [this]() {
        ui->actionMacroRecording->setText(tr("Stop Recording"));

        // A macro is being recorded so disable some macro options
        ui->actionPlayback->setEnabled(false);
        ui->actionRunMacroMultipleTimes->setEnabled(false);
        ui->actionSaveCurrentRecordedMacro->setEnabled(false);
    });

    connect(&macroManager, &MacroManager::recordingStopped, this, [this]() {
        ui->actionMacroRecording->setText(tr("Start Recording"));

        // Only enable these if the macro manager recorded a valid macro
        ui->actionPlayback->setEnabled(macroManager.hasCurrentUnsavedMacro());
        ui->actionSaveCurrentRecordedMacro->setEnabled(macroManager.hasCurrentUnsavedMacro());

        // The macro manager might have other macros
        ui->actionRunMacroMultipleTimes->setEnabled(macroManager.availableMacros().size() > 0 || macroManager.hasCurrentUnsavedMacro());
    });

    connect(ui->actionPlayback, &QAction::triggered, this, [this]() {
        macroManager.replayCurrentMacro(currentEditor());
    });

    connect(ui->actionSaveCurrentRecordedMacro, &QAction::triggered, this, [=, this]() {
        MacroSaveDialog macroSaveDialog;

        macroSaveDialog.show();
        macroSaveDialog.raise();
        macroSaveDialog.activateWindow();

        if (macroSaveDialog.exec() == QDialog::Accepted) {
            // We have at least 1 saved macro at this point
            ui->actionEditMacros->setEnabled(true);

            // The macro has been saved so disable save option
            ui->actionSaveCurrentRecordedMacro->setEnabled(false);

            // TODO: does the macro name already exist? Make the user retry

            macroManager.saveCurrentMacro(macroSaveDialog.getName());

            // TODO handle shortcuts
            if (!macroSaveDialog.getShortcut().isEmpty()) {
                // do something with msd.getShortcut().isEmpty()
            }
        }
    });

    connect(ui->actionRunMacroMultipleTimes, &QAction::triggered, this, [this]() {
        MacroRunDialog *macroRunDialog = findChild<MacroRunDialog *>(QString(), Qt::FindDirectChildrenOnly);

        if (macroRunDialog == Q_NULLPTR) {
            macroRunDialog = new MacroRunDialog(this, &macroManager);

            connect(macroRunDialog, &MacroRunDialog::execute, this, [this](Macro *macro, int times) {
                if (times > 0)
                    macro->replay(currentEditor(), times);
                else if (times == -1)
                    macro->replayTillEndOfFile(currentEditor());
            });
        }

        macroRunDialog->show();
        macroRunDialog->raise();
        macroRunDialog->activateWindow();
    });

    connect(ui->actionEditMacros, &QAction::triggered, this, [this]() {
        MacroEditorDialog med(this, &macroManager);

        med.show();
        med.raise();
        med.activateWindow();

        med.exec();

        ui->actionEditMacros->setEnabled(macroManager.availableMacros().size() > 0);
    });

    connect(ui->menuMacro, &QMenu::aboutToShow, this, [this]() {
        // NOTE: its unfortunate that this has to be hard coded, but there's no way
        // to easily determine what should or shouldn't be there
        while (ui->menuMacro->actions().size() > 6) {
            delete ui->menuMacro->actions().takeLast();
        }

        for (const Macro *m : macroManager.availableMacros()) {
            ui->menuMacro->addAction(m->getName(), [=, this]() { m->replay(currentEditor()); });
        }
    });

    ui->actionAboutQt->setIcon(QPixmap(QLatin1String(":/qt-project.org/qmessagebox/images/qtlogo-64.png")));
    connect(ui->actionAboutQt, &QAction::triggered, &QApplication::aboutQt);

    ui->actionAboutNotepadNext->setShortcut(QKeySequence::HelpContents);
    connect(ui->actionAboutNotepadNext, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, QString(),
                            QStringLiteral("<h3>%1 v%2 %3</h3>"
                                    "<p>%4</p>"
                                    "<p><a href=\"https://github.com/nullmastermind/NotepadAI\">NotepadAI Home Page</a></p>"
                                    R"(<p>This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.</p> <p>This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.</p> <p>You should have received a copy of the GNU General Public License along with this program. If not, see &lt;<a href="https://www.gnu.org/licenses/">https://www.gnu.org/licenses/</a>&gt;.</p>)")
                                .arg(QApplication::applicationDisplayName(), APP_VERSION, APP_DISTRIBUTION, QStringLiteral(APP_COPYRIGHT).toHtmlEscaped()));
    });

    connect(ui->actionDebugInfo, &QAction::triggered, this, [=, this]() {
        QMessageBox mb(QMessageBox::Information, tr("Debug Info"), app->debugInfo().join('\n'), QMessageBox::Ok, this);

        mb.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
        mb.setTextInteractionFlags(Qt::TextSelectableByMouse);

        mb.exec();
    });

    tabsQuickActionsBar = new TabsQuickActionsBar(ui->menuBar);
    ui->menuBar->setCornerWidget(tabsQuickActionsBar, Qt::TopRightCorner);
    connect(tabsQuickActionsBar, &TabsQuickActionsBar::createNewTabClicked, this, &MainWindow::newFile);
    connect(tabsQuickActionsBar, &TabsQuickActionsBar::closeCurrentTabClicked, this, &MainWindow::closeCurrentFile);
    connect(tabsQuickActionsBar, &TabsQuickActionsBar::tabsMenuAboutToShow, this, [this](QMenu *editorsMenu) {
        const auto editorsList = editors();

        editorsMenu->clear();

        for (const auto editor : editorsList) {
            const auto iconPath = editor->isSavedToDisk() ? ":/icons/saved.png" : ":/icons/unsaved.png";
            const auto action = editorsMenu->addAction(QIcon(iconPath), editor->getName());

            if (editor->isActiveWindow()) {
                auto font = action->font();
                font.setBold(true);
                action->setFont(font);
            }

            connect(action, &QAction::triggered, this, [this, editor]() { switchToEditor(editor); });
        }
    });

#ifdef Q_OS_WIN
    connect(ui->actionShowInExplorer, &QAction::triggered, this, [this]() {
        QString filePath = QDir::toNativeSeparators(currentEditor()->getFileInfo().canonicalFilePath());
        QStringList arguments = {"/select,", filePath};
        QProcess::startDetached("explorer", arguments);
    });

    QString terminalName = app->getSettings()->value("App/TerminalName", "Command Prompt").toString();
    ui->actionOpenTerminalHere->setText(ui->actionOpenTerminalHere->text().arg(terminalName));

    connect(ui->actionOpenTerminalHere, &QAction::triggered, this, [=, this]() {
        QString command = app->getSettings()->value("App/TerminalCommand", "cmd").toString();
        QString filePath = QDir::toNativeSeparators(currentEditor()->getFileInfo().dir().canonicalPath());
        QStringList arguments = {"/c", "start", "/d", filePath, command};
        QProcess::startDetached("cmd", arguments);
    });
#endif

    // "Show in Workspace" / "Show in Folder" — cross-platform (NOT inside the
    // Q_OS_WIN block). tabBarRightClicked switches to the clicked editor before
    // the menu opens, so currentEditor() here IS the editor the menu was built
    // for — same as actionShowInExplorer's handler. The resolvers below are the
    // exact ones the gating in tabBarRightClicked uses, so gate == handler.
    connect(ui->actionShowInWorkspace, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        const QString filePath = editor->isFile() ? editor->getFilePath() : QString();
        FolderAsWorkspaceDock *dock = resolveShowInWorkspaceDock(filePath, editor->isFile());
        if (!dock) return;            // no silent fallback
        dock->setVisible(true);
        dock->raise();
        setActiveWorkspace(dock);
        dock->revealAndSelectPath(filePath);
    });

    } // MainWindow::ctor.actionWiring

    {
    PROFILE_SCOPE("MainWindow::ctor.inspectorAndDocks");
    DockMiddleClickCloser::installTabBarFilter(this);

    EditorInspectorDock *editorInspectorDock = new EditorInspectorDock(this);
    editorInspectorDock->hide();
    addDockWidget(Qt::RightDockWidgetArea, editorInspectorDock);
    DockMiddleClickCloser::install(editorInspectorDock);

    LanguageInspectorDock *languageInspectorDock = new LanguageInspectorDock(this);
    languageInspectorDock->hide();
    addDockWidget(Qt::RightDockWidgetArea, languageInspectorDock);
    DockMiddleClickCloser::install(languageInspectorDock);

    LuaConsoleDock *luaConsoleDock = new LuaConsoleDock(app->getLuaState(), this);
    luaConsoleDock->hide();
    addDockWidget(Qt::BottomDockWidgetArea, luaConsoleDock);
    DockMiddleClickCloser::install(luaConsoleDock);

    DebugLogDock *debugLogDock = new DebugLogDock(this);
    debugLogDock->hide();
    addDockWidget(Qt::RightDockWidgetArea, debugLogDock);
    DockMiddleClickCloser::install(debugLogDock);

    ui->menuHelp->insertActions(ui->menuHelp->actions().at(0), {
                                    luaConsoleDock->toggleViewAction(),
                                    languageInspectorDock->toggleViewAction(),
                                    editorInspectorDock->toggleViewAction(),
                                    debugLogDock->toggleViewAction()
                                });

    FileListDock *fileListDock = new FileListDock(this);
    fileListDock->hide();
    addDockWidget(Qt::LeftDockWidgetArea, fileListDock);
    DockMiddleClickCloser::install(fileListDock);
    ui->menuView->addAction(fileListDock->toggleViewAction());

    {
        auto makeTintedIcon = [](const QString &svgPath, const QColor &color) {
            QIcon source(svgPath);
            if (source.isNull()) return source;
            QIcon dst;
            for (int sz : {16, 20, 22, 24, 32, 48}) {
                QPixmap pm = source.pixmap(sz, sz);
                if (pm.isNull()) continue;
                QPainter p(&pm);
                p.setCompositionMode(QPainter::CompositionMode_SourceIn);
                p.fillRect(pm.rect(), color);
                p.end();
                dst.addPixmap(pm);
            }
            return dst;
        };
        QIcon icon = makeTintedIcon(QStringLiteral(":/icons/markdown-preview.svg"),
                                    palette().color(QPalette::ButtonText));
        m_actionMarkdownPreview = new QAction(icon, tr("Markdown Preview"), this);
    }
    m_actionMarkdownPreview->setCheckable(true);
    m_actionMarkdownPreview->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    m_actionMarkdownPreview->setEnabled(false);
    ui->menuView->addAction(m_actionMarkdownPreview);
    ui->mainToolBar->addAction(m_actionMarkdownPreview);

    connect(m_actionMarkdownPreview, &QAction::triggered, this, [this](bool checked) {
        Q_UNUSED(checked)
        ScintillaNext *editor = currentEditor();
        if (!editor) return;

        auto *mgr = this->app->getPreviewTabManager();
        if (!mgr) return;

        auto *existing = mgr->previewForEditor(editor);
        if (existing) {
            if (existing->hasFocus() || existing->isAncestorOf(focusWidget())) {
                dockedEditor->switchToEditor(editor);
            } else {
                existing->setFocus();
            }
        } else {
            mgr->openOrFocusPreview(editor);
        }
    });

    connect(app->getSettings(), &ApplicationSettings::showMenuBarChanged, this, [this](bool showMenuBar) {
        // Don't 'hide' it, else the actions won't be enabled
        ui->menuBar->setMaximumHeight(showMenuBar ? QWIDGETSIZE_MAX : 0);
    });
    connect(app->getSettings(), &ApplicationSettings::showToolBarChanged, ui->mainToolBar, &QToolBar::setVisible);
    connect(app->getSettings(), &ApplicationSettings::showStatusBarChanged, ui->statusBar, &QStatusBar::setVisible);
    connect(ui->statusBar, &EditorInfoStatusBar::customContextMenuRequestedForEOLLabel, this, [this](const QPoint &pos){
        ui->menuEOLConversion->popup(pos);
    });

    // It seems restoreState() does not affect the status bar so set it manually
    ui->statusBar->setVisible(app->getSettings()->showStatusBar());

    } // MainWindow::ctor.inspectorAndDocks

    {
        PROFILE_SCOPE("MainWindow::ctor.setupLanguageMenu");
        setupLanguageMenu();
    }

    {
    PROFILE_SCOPE("MainWindow::ctor.applyStyleSheet");
    applyStyleSheet();
    connect(app, &NotepadNextApplication::effectiveThemeChanged, this, &MainWindow::applyStyleSheet);
    }

    {
    PROFILE_SCOPE("MainWindow::ctor.terminalManager");
    terminalManager = new TerminalManager(app, this);
    connect(app, &NotepadNextApplication::effectiveThemeChanged, terminalManager, &TerminalManager::applyTheme);
    connect(app->getSettings(), &ApplicationSettings::terminalFontChanged, terminalManager, &TerminalManager::applyFont);

    connect(ui->menuTerminal, &QMenu::aboutToShow, this, [this]() {
        const QString workspaceRoot = currentWorkspaceRoot();
        QString activeFilePath;
        bool activeIsFile = false;
        if (ScintillaNext *editor = currentEditor()) {
            if (editor->isFile()) {
                activeFilePath = editor->getFilePath();
                activeIsFile = true;
            }
        }
        ui->actionOpenTerminalInWorkspace->setEnabled(TerminalCwdResolver::canOpenInWorkspace(workspaceRoot));
        ui->actionOpenTerminalInFolder->setEnabled(TerminalCwdResolver::canOpenInFolder(activeFilePath, activeIsFile, workspaceRoot));
    });

    connect(ui->actionOpenTerminalInWorkspace, &QAction::triggered, this, [this]() {
        const QString cwd = TerminalCwdResolver::resolveWorkspace(currentWorkspaceRoot());
        if (!cwd.isEmpty()) {
            terminalManager->openTerminal(cwd);
        }
    });

    connect(ui->actionOpenTerminalInFolder, &QAction::triggered, this, [this]() {
        const QString workspaceRoot = currentWorkspaceRoot();
        QString activeFilePath;
        bool activeIsFile = false;
        if (ScintillaNext *editor = currentEditor()) {
            if (editor->isFile()) {
                activeFilePath = editor->getFilePath();
                activeIsFile = true;
            }
        }
        const QString cwd = TerminalCwdResolver::resolveFolder(activeFilePath, activeIsFile, workspaceRoot);
        if (!cwd.isEmpty()) {
            terminalManager->openTerminal(cwd);
        }
    });

    connect(ui->menuTasks, &QMenu::aboutToShow, this, [this]() {
        const QString workspaceRoot = TerminalTaskRegistry::normalizeWorkspacePath(currentWorkspaceRoot());
        const bool hasWorkspace = TerminalCwdResolver::canOpenInWorkspace(workspaceRoot);

        ui->actionEditTasks->setEnabled(hasWorkspace);

        // Remove previously-built task actions (everything after the separator).
        // The separator is the second action; task actions follow it.
        const QList<QAction *> actions = ui->menuTasks->actions();
        bool pastSeparator = false;
        for (QAction *a : actions) {
            if (pastSeparator) {
                ui->menuTasks->removeAction(a);
                delete a;
            } else if (a->isSeparator()) {
                pastSeparator = true;
            }
        }

        if (!hasWorkspace)
            return;

        const QList<TerminalTask> tasks = terminalManager->tasksForWorkspace(workspaceRoot);
        if (tasks.isEmpty())
            return;

        for (const TerminalTask &task : tasks) {
            QAction *action = ui->menuTasks->addAction(task.name);
            connect(action, &QAction::triggered, this, [this, workspaceRoot, task]() {
                const QString cwd = TerminalCwdResolver::resolveWorkspace(workspaceRoot);
                if (cwd.isEmpty()) return;
                terminalManager->openTask(cwd, task);
            });
        }
    });

    connect(ui->actionEditTasks, &QAction::triggered, this, [this]() {
        const QString workspaceRoot = TerminalTaskRegistry::normalizeWorkspacePath(currentWorkspaceRoot());
        const QString cwd = TerminalCwdResolver::resolveWorkspace(workspaceRoot);
        if (cwd.isEmpty()) return;

        const QList<TerminalTask> existing = terminalManager->tasksForWorkspace(workspaceRoot);
        EditTasksDialog dlg(cwd, existing, this);
        if (dlg.exec() == QDialog::Accepted) {
            terminalManager->setTasks(workspaceRoot, dlg.tasks());
        }
    });

    // --- Mini Apps menu ---
    m_miniAppRegistry = new MiniAppRegistry(app->getSettings());
    m_miniAppManager = new MiniAppManager(app, m_miniAppRegistry, dockedEditor, this);

    connect(ui->menuMiniAppsSub, &QMenu::aboutToShow, this, [this]() {
        // Clear dynamic items (keep only the static "Edit Mini Apps..." action)
        while (ui->menuMiniAppsSub->actions().size() > 1) {
            delete ui->menuMiniAppsSub->actions().first();
        }

        const QString workspaceRoot = currentWorkspaceRoot();
        const QList<MiniAppDefinition> globalApps = m_miniAppRegistry->globalApps();
        const QList<MiniAppDefinition> wsApps = workspaceRoot.isEmpty()
            ? QList<MiniAppDefinition>()
            : m_miniAppRegistry->workspaceApps(workspaceRoot);

        QAction *beforeAction = ui->actionEditMiniApps;

        // Global apps
        for (const MiniAppDefinition &def : globalApps) {
            QAction *a = new QAction(def.name, ui->menuMiniAppsSub);
            connect(a, &QAction::triggered, this, [this, def]() {
                m_miniAppManager->launchApp(def);
            });
            ui->menuMiniAppsSub->insertAction(beforeAction, a);
        }

        // Separator + workspace apps
        if (!wsApps.isEmpty()) {
            if (!globalApps.isEmpty())
                ui->menuMiniAppsSub->insertSeparator(beforeAction);
            for (const MiniAppDefinition &def : wsApps) {
                QAction *a = new QAction(def.name, ui->menuMiniAppsSub);
                connect(a, &QAction::triggered, this, [this, def]() {
                    m_miniAppManager->launchApp(def);
                });
                ui->menuMiniAppsSub->insertAction(beforeAction, a);
            }
        }

        // Separator before Edit action (if any apps exist)
        if (!globalApps.isEmpty() || !wsApps.isEmpty())
            ui->menuMiniAppsSub->insertSeparator(beforeAction);
    });

    // Quick Browse action
    ui->actionQuickBrowser->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    connect(ui->actionQuickBrowser, &QAction::triggered, this, [this]() {
        ApplicationSettings *settings = this->app->getSettings();

        QDialog dlg(this);
        dlg.setWindowTitle(tr("Quick Browse"));
        auto *layout = new QVBoxLayout(&dlg);

        layout->addWidget(new QLabel(tr("Enter URL:"), &dlg));
        auto *urlEdit = new QLineEdit(&dlg);
        urlEdit->setPlaceholderText(QStringLiteral("https://example.com"));
        layout->addWidget(urlEdit);

        auto *cdpCheck = new QCheckBox(tr("Enable CDP debugging"), &dlg);
        cdpCheck->setChecked(true);
        layout->addWidget(cdpCheck);

        auto *crossOriginCheck = new QCheckBox(tr("Allow cross-origin iframe access (better automation, less security)"), &dlg);
        crossOriginCheck->setChecked(false);
        crossOriginCheck->setToolTip(
            tr("Enables page-agent to read content inside cross-origin iframes.\n"
               "Improves automation coverage but disables browser sandbox isolation."));
        layout->addWidget(crossOriginCheck);

        // Proxy row
        auto *proxyLayout = new QHBoxLayout;
        proxyLayout->addWidget(new QLabel(tr("Proxy:"), &dlg));
        auto *proxyCombo = new QComboBox(&dlg);
        proxyCombo->addItem(tr("None"), 0);
        proxyCombo->addItem(QStringLiteral("HTTP"), 1);
        proxyCombo->addItem(QStringLiteral("HTTPS"), 2);
        proxyCombo->addItem(QStringLiteral("SOCKS4"), 3);
        proxyCombo->addItem(QStringLiteral("SOCKS5"), 4);
        proxyLayout->addWidget(proxyCombo);
        proxyLayout->addWidget(new QLabel(tr("Host:"), &dlg));
        auto *proxyHostEdit = new QLineEdit(&dlg);
        proxyHostEdit->setPlaceholderText(QStringLiteral("proxy.example.com"));
        proxyLayout->addWidget(proxyHostEdit, 1);
        proxyLayout->addWidget(new QLabel(tr("Port:"), &dlg));
        auto *proxyPortSpin = new QSpinBox(&dlg);
        proxyPortSpin->setRange(0, 65535);
        proxyPortSpin->setSpecialValueText(tr("Default"));
        proxyLayout->addWidget(proxyPortSpin);
        layout->addLayout(proxyLayout);

        auto *bypassLayout = new QHBoxLayout;
        bypassLayout->addWidget(new QLabel(tr("Bypass:"), &dlg));
        auto *proxyBypassEdit = new QLineEdit(&dlg);
        proxyBypassEdit->setPlaceholderText(QStringLiteral("localhost;127.0.0.1;[::1];<local>"));
        bypassLayout->addWidget(proxyBypassEdit, 1);
        layout->addLayout(bypassLayout);

        // Enable/disable proxy fields based on combo
        auto updateProxyFields = [=]() {
            const bool enabled = proxyCombo->currentData().toInt() > 0;
            proxyHostEdit->setEnabled(enabled);
            proxyPortSpin->setEnabled(enabled);
            proxyBypassEdit->setEnabled(enabled);
        };
        connect(proxyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, updateProxyFields);

        // Load last-used proxy settings
        {
            int idx = proxyCombo->findData(settings->lastProxyType());
            proxyCombo->setCurrentIndex(idx == -1 ? 0 : idx);
        }
        proxyHostEdit->setText(settings->lastProxyHost());
        proxyPortSpin->setValue(settings->lastProxyPort());
        proxyBypassEdit->setText(settings->lastProxyBypassList());
        updateProxyFields();

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(buttons);
        urlEdit->setFocus();

        if (dlg.exec() != QDialog::Accepted)
            return;
        QString input = urlEdit->text().trimmed();
        if (input.isEmpty())
            return;
        if (!input.contains(QStringLiteral("://")))
            input = QStringLiteral("https://") + input;
        QUrl url(input, QUrl::TolerantMode);
        if (!url.isValid())
            return;

        // Save last-used proxy
        const int proxyType = proxyCombo->currentData().toInt();
        const QString proxyHost = proxyHostEdit->text().trimmed();
        const int proxyPort = proxyPortSpin->value();
        const QString proxyBypass = proxyBypassEdit->text().trimmed();
        settings->setLastProxyType(proxyType);
        settings->setLastProxyHost(proxyHost);
        settings->setLastProxyPort(proxyPort);
        settings->setLastProxyBypassList(proxyBypass);

        m_miniAppManager->launchQuickBrowser(url, cdpCheck->isChecked(),
                                             proxyType, proxyHost, proxyPort, proxyBypass,
                                             crossOriginCheck->isChecked());
    });

    connect(ui->actionEditMiniApps, &QAction::triggered, this, [this]() {
        const QString workspaceRoot = currentWorkspaceRoot();
        EditMiniAppsDialog dlg(m_miniAppRegistry, workspaceRoot, this);
        dlg.exec();
    });

    connect(app, &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_miniAppManager)
            m_miniAppManager->shutdown();
    });

    connect(ui->menuAi, &QMenu::aboutToShow, this, [this]() {
        const QString workspaceRoot = currentWorkspaceRoot();
        QString activeFilePath;
        bool activeIsFile = false;
        if (ScintillaNext *editor = currentEditor()) {
            if (editor->isFile()) {
                activeFilePath = editor->getFilePath();
                activeIsFile = true;
            }
        }
        const bool workspaceOk = TerminalCwdResolver::canOpenInWorkspace(workspaceRoot);
        const bool folderOk = TerminalCwdResolver::canOpenInFolder(activeFilePath, activeIsFile, workspaceRoot);

        AcpAgentManager *manager = this->app->getAiAgentManager();
        AcpAgentRegistry *registry = manager ? manager->registry() : nullptr;
        QList<AcpAgentDefinition> agents = registry ? registry->agents() : QList<AcpAgentDefinition>();
        const QString defaultId = registry ? registry->defaultAgentId() : QString();
        // Surface the default agent first so a single keystroke (Enter) picks it.
        std::stable_partition(agents.begin(), agents.end(),
            [&defaultId](const AcpAgentDefinition &a) { return a.id == defaultId; });

        auto rebuildSubmenu = [this, &agents, &defaultId](QMenu *submenu, bool enabled, bool isWorkspaceVariant) {
            submenu->clear();
            const bool hasAgents = !agents.isEmpty();
            submenu->setEnabled(enabled && hasAgents);
            if (!enabled || !hasAgents) {
                return;
            }
            for (const AcpAgentDefinition &agent : agents) {
                QAction *action = submenu->addAction(agent.name);
                if (agent.id == defaultId) {
                    QFont f = action->font();
                    f.setBold(true);
                    action->setFont(f);
                }
                const QString agentId = agent.id;
                connect(action, &QAction::triggered, this, [this, agentId, isWorkspaceVariant]() {
                    QString cwd;
                    if (isWorkspaceVariant) {
                        cwd = TerminalCwdResolver::resolveWorkspace(currentWorkspaceRoot());
                    } else {
                        const QString workspaceRoot = currentWorkspaceRoot();
                        QString activeFilePath;
                        bool activeIsFile = false;
                        if (ScintillaNext *editor = currentEditor()) {
                            if (editor->isFile()) {
                                activeFilePath = editor->getFilePath();
                                activeIsFile = true;
                            }
                        }
                        cwd = TerminalCwdResolver::resolveFolder(activeFilePath, activeIsFile, workspaceRoot);
                    }
                    if (cwd.isEmpty()) return;

                    AcpAgentManager *m = this->app->getAiAgentManager();
                    if (!m) return;
                    AiAgentDock *dock = m->openAgent(agentId, cwd, /*recordAsLastUsed=*/true);
                    if (dock) {
                        attachAiAgentDock(dock);
                    }
                });
            }
        };

        rebuildSubmenu(ui->menuOpenAiAgentInWorkspace, workspaceOk, /*isWorkspaceVariant=*/true);
        rebuildSubmenu(ui->menuOpenAiAgentInFolder, folderOk, /*isWorkspaceVariant=*/false);
    });

    // New AI Tab (Ctrl+Shift+I) — opens a tab with the agent the user last used
    // (falling back to the default agent if that id is unset/unknown). cwd
    // follows the standard active-workspace policy: workspace first, then the
    // active file's folder. The action stays always-enabled so the global
    // shortcut keeps firing; the handler bails silently when nothing resolves.
    ui->actionNewAiTab->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    connect(ui->actionNewAiTab, &QAction::triggered, this, [this]() {
        AcpAgentManager *manager = this->app->getAiAgentManager();
        if (!manager) return;
        AcpAgentRegistry *registry = manager->registry();
        if (!registry) return;

        // Prefer the last-used agent; fall back to the configured default when
        // it's empty or no longer registered (e.g. the user removed it).
        QString agentId = this->app->getSettings()->lastUsedAiAgentId();
        if (agentId.isEmpty() || !registry->contains(agentId)) {
            agentId = registry->defaultAgentId();
        }

        const QString workspaceRoot = currentWorkspaceRoot();
        QString activeFilePath;
        bool activeIsFile = false;
        if (ScintillaNext *editor = currentEditor()) {
            if (editor->isFile()) {
                activeFilePath = editor->getFilePath();
                activeIsFile = true;
            }
        }
        QString cwd = TerminalCwdResolver::resolveWorkspace(workspaceRoot);
        if (cwd.isEmpty()) {
            cwd = TerminalCwdResolver::resolveFolder(activeFilePath, activeIsFile, workspaceRoot);
        }
        if (cwd.isEmpty()) {
            statusBar()->showMessage(tr("Open a workspace or a saved file to start an AI tab"), 4000);
            return;
        }

        AiAgentDock *dock = manager->openAgent(agentId, cwd, /*recordAsLastUsed=*/true);
        if (dock) {
            attachAiAgentDock(dock);
        }
    });

    } // MainWindow::ctor.terminalManager

    // Sweep every QAction that setupUi (and constructor-time code above) has
    // already created. The ActionAddedFilter installed right after setupUi
    // handles anything added from this point onward (recent files, languages).
    {
        PROFILE_SCOPE("MainWindow::ctor.wireActionsSweep");
        for (QAction *a : findChildren<QAction *>()) {
            wireActionForCrashContext(a);
        }
    }

#ifndef NDEBUG
    {
        // Debug-only "Help → Debug → Trigger Crash" submenu. Mirrors the kinds
        // documented in CrashHandler::triggerCrashForTest so testers can
        // exercise each fault path interactively. Never compiled into Release.
        QMenu *debugMenu = ui->menuHelp->addMenu(tr("Debug"));
        QMenu *crashMenu = debugMenu->addMenu(tr("Trigger Crash"));
        const struct { const char *kind; const char *label; } kinds[] = {
            {"segv",      "Null pointer write (SEGV)"},
            {"abrt",      "std::abort (SIGABRT)"},
            {"sof",       "Stack overflow"},
            {"terminate", "std::terminate (uncaught std::exception)"},
            {"nonstd",    "std::terminate (non-std exception)"},
            {"div0",      "Integer divide by zero"},
        };
        for (const auto &k : kinds) {
            QAction *a = crashMenu->addAction(QString::fromLatin1(k.label));
            const QByteArray kindBytes(k.kind);
            connect(a, &QAction::triggered, this, [kindBytes]() {
                CrashHandler::triggerCrashForTest(kindBytes.constData());
            });
        }

        // Shutdown diagnostics toggle. Writes shutdown_report.txt on clean exit.
        ApplicationSettings *settings = app->getSettings();
        QAction *diagAction = debugMenu->addAction(tr("Shutdown Diagnostics"));
        diagAction->setCheckable(true);
        diagAction->setChecked(settings->shutdownDiagnosticsEnabled());
        connect(diagAction, &QAction::toggled, this, [settings](bool checked) {
            settings->setShutdownDiagnosticsEnabled(checked);
        });
        connect(settings, &ApplicationSettings::shutdownDiagnosticsEnabledChanged,
                diagAction, [diagAction](bool enabled) {
                    if (diagAction->isChecked() != enabled) {
                        QSignalBlocker block(diagAction);
                        diagAction->setChecked(enabled);
                    }
                });
    }
#endif

    {
        PROFILE_SCOPE("MainWindow::ctor.gitOperationManager");
        m_gitOpMgr = new GitOperationManager(this);
        setupGitOperationMenu();
    }

    {
        PROFILE_SCOPE("MainWindow::ctor.restoreSettings");
        restoreSettings();
    }

    {
        PROFILE_SCOPE("MainWindow::ctor.initUpdateCheck");
        initUpdateCheck();
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::applyCustomShortcuts()
{
    ApplicationSettings *settings = app->getSettings();
    settings->beginGroup("Shortcuts");

    for (const QString &actionName : settings->childKeys()) {
        QAction *action = findChild<QAction *>(QStringLiteral("action") + actionName, Qt::FindDirectChildrenOnly);

        if (!action) {
            qWarning() << "CustomShortcut: Cannot find action" << actionName;
            continue;
        }

        const QVariant value = settings->value(actionName);
        if (!value.canConvert<QStringList>()) {
            qWarning() << "CustomShortcut: Invalid shortcut format for" << actionName;
            continue;
        }

        QList<QKeySequence> shortcuts;
        for (const QString &shortcutString : value.toStringList()) {
            auto sequence = QKeySequence(shortcutString);

#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
                if (sequence.count() > 0 && sequence[0].key() != Qt::Key_unknown) {
#else
                if (sequence.count() > 0 && (sequence[0] & ~Qt::KeyboardModifierMask) != Qt::Key_unknown) {
#endif
                    shortcuts.append(sequence);
                }
                else {
                    qWarning() << "CustomShortcut: Cannot create QKeySequence(" << shortcutString << ") for " << actionName;
                }
        }

        if (!shortcuts.empty()) {
            action->setShortcuts(shortcuts);
        }
    }

    settings->endGroup();
}

void MainWindow::setupLanguageMenu()
{
    qInfo(Q_FUNC_INFO);

    QStringList language_names = app->getLanguages();

    int i = 0;
    while (i < language_names.size()) {
        QList<QAction *> actions;
        int j = i;

        // Get all consecutive names that start with the same letter
        // NOTE: this loop always runs once since i == j the first time
        while (j < language_names.size() && language_names[i][0].toUpper() == language_names[j][0].toUpper()) {
            const QString &key = language_names[j];
            QAction *action = new QAction(key);
            action->setCheckable(true);
            action->setData(key);
            connect(action, &QAction::triggered, this, &MainWindow::languageMenuTriggered);
            languageActionGroup->addAction(action);
            actions.append(action);

            ++j;
        }

        if (actions.size() == 1) {
            ui->menuLanguage->addActions(actions);
        }
        else {
            // Create a sub menu with the actions
            QMenu *compactMenu = new QMenu(actions[0]->text().at(0).toUpper());
            compactMenu->addActions(actions);
            ui->menuLanguage->addMenu(compactMenu);
        }
        i = j;
    }
}

ScintillaNext *MainWindow::currentEditor() const
{
    return dockedEditor->getCurrentEditor();
}

int MainWindow::editorCount() const
{
    return dockedEditor->count();
}

QVector<ScintillaNext *> MainWindow::editors() const
{
    // NOTE: this will need re-evaluated in the future.
    // So far it has been assumed 1 ScintillaNext instance is 1 DockedEditor widget instance.
    // If in the future a ScintillaNext can be cloned then the DockedEditor could return
    // the same ScintillaNext instance multiple times since 1 ScintillaNext could mean >= 1 DockedEditor widget instance
    return dockedEditor->editors();
}

void MainWindow::newFile()
{
    qInfo(Q_FUNC_INFO);

    static int count = 1;

    // NOTE: in theory need to check all editors in the editorManager to future proof this.
    // If there is another window it would need to check those too to see if New X exists. The editor
    // manager would encompass all editors

    forever {
        QString newFileName = tr("New %1").arg(count++);
        bool canUseName = true;

        for (const ScintillaNext *editor : editors()) {
            if (!editor->isFile() && editor->getName() == newFileName) {
                canUseName = false;
                break;
            }
        }

        if (canUseName) {
            ScintillaNext *editor = app->getEditorManager()->createEditor(newFileName);
            editor->grabFocus();
            break;
        }
    }
}

// One unedited, new blank document
ScintillaNext *MainWindow::getInitialEditor()
{
    if (editorCount() == 1) {
        ScintillaNext *editor = currentEditor();

        // currentEditor() can be null mid-close: the cached pointer in
        // DockedEditor was just auto-nulled by QPointer because its target
        // editor was destroyed. Treat as "no reusable initial editor".
        if (editor == Q_NULLPTR) {
            return Q_NULLPTR;
        }

        // If the editor:
        //   is a temporary file
        //   is a 'real' file (or a 'missing' file)
        //   can undo any actions
        //   can redo any actions
        // Then do not treat it as an 'initial editor' that can be transparently closed for the user
        if (editor->isTemporary() || editor->isFile() || editor->canUndo() || editor->canRedo()) {
            return Q_NULLPTR;
        }

        return editor;
    }

    return Q_NULLPTR;
}

void MainWindow::openFileList(const QStringList &fileNames)
{
    PROFILE_SCOPE("MainWindow::openFileList");
    qInfo(Q_FUNC_INFO);

    if (fileNames.size() == 0)
        return;

    QList<ScintillaNext *> openedEditors;
    ScintillaNext *initialEditor = getInitialEditor();

    for (const QString &filePath : fileNames) {
        qInfo("%s", qUtf8Printable(filePath));

        // Search currently open editors to see if it is already open
        ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(filePath);

        if (editor == Q_NULLPTR) {
            QFileInfo fileInfo(filePath);

            if (!fileInfo.isFile()) {
                auto reply = QMessageBox::question(this, tr("Create File"), tr("<b>%1</b> does not exist. Do you want to create it?").arg(filePath));

                if (reply == QMessageBox::Yes) {
                    editor = app->getEditorManager()->createEditorFromFile(filePath, true);
                }
                else {
                    // Make sure it is not still in the recent files list still.
                    // Normally when a file is opened it is removed from the file list,
                    // but if a user doesn't want to create the file, remove it explicitly.
                    app->getRecentFilesListManager()->removeFile(filePath);
                    continue;
                }
            }
            else {
                editor = app->getEditorManager()->createEditorFromFile(filePath);
            }
        }
        else if (editor == dockedEditor->previewEditor()) {
            dockedEditor->pinPreviewEditor();
        }

        if (editor) {
            openedEditors.append(editor);
        }
    }

    // If any were successful, switch to the last one
    if (!openedEditors.empty()) {
        dockedEditor->switchToEditor(openedEditors.last());
    }

    if (initialEditor) {
        initialEditor->close();
    }
}

bool MainWindow::checkEditorsBeforeClose(const QVector<ScintillaNext *> &editors)
{
    EditorManager *em = app->getEditorManager();
    QVector<ScintillaNext *> unsaved;
    for (auto *e : editors) {
        if (em->isDiffView(e)) continue;
        if (!e->isSavedToDisk()) unsaved.append(e);
    }

    if (unsaved.isEmpty()) return true;

    // Focus the user's attention on the first unsaved file
    dockedEditor->switchToEditor(unsaved.first());

    // Single point of interaction
    UserSaveAction action = promptForSave(unsaved);

    switch (action) {
        case UserSaveAction::DiscardAll:
            return true;
        case UserSaveAction::SaveAll:
            return saveAllEditors(unsaved);
        case UserSaveAction::Cancel:
        default:
            return false;
    }
}

void MainWindow::openFileDialog()
{
    const QString filter = app->getFileDialogFilter();

    QStringList fileNames = FileDialogHelpers::getOpenFileNames(this, QString(), defaultDirectoryManager->getDefaultDirectory(), filter);

    if (!fileNames.empty())
        emit fileDialogAccepted(fileNames.last());

    openFileList(fileNames);
}

void MainWindow::openFile(const QString &filePath)
{
    openFileList(QStringList() << filePath);
}

void MainWindow::previewFile(const QString &filePath)
{
    ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(filePath);

    if (editor) {
        dockedEditor->switchToEditor(editor);
        return;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile()) return;

    editor = app->getEditorManager()->createEditorFromFile(filePath);
    if (!editor) return;

    ScintillaNext *initialEditor = getInitialEditor();
    dockedEditor->addPreviewEditor(editor);
    dockedEditor->switchToEditor(editor);

    if (initialEditor) {
        initialEditor->close();
    }
}

void MainWindow::openFolderAsWorkspaceDialog()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Folder as Workspace"), defaultDirectoryManager->getDefaultDirectory(), QFileDialog::ShowDirsOnly);
    openFolderAsWorkspacePath(dir);
}

void MainWindow::setFolderAsWorkspacePath(const QString &dir)
{
    openFolderAsWorkspacePath(dir);
}

void MainWindow::openFolderAsWorkspacePath(const QString &dir, bool showGitTab)
{
    if (dir.isEmpty()) return;

    // Single chokepoint for every workspace entry — CLI --workspace, recent
    // list, session restore, dialog, gitOpenSubmoduleRequested. Resolving here
    // means a relative input like "." can never leak into the recent list, the
    // dock title, the persisted Workspaces list, or QFileSystemModel's root.
    const QString resolved = QDir(dir).absolutePath();

    app->getRecentWorkspacesListManager()->addFile(resolved);

    // If this workspace is already open in some dock, just focus it rather
    // than spawning a duplicate tab.
    const QString cleaned = QDir::cleanPath(resolved);
    const auto existing = findChildren<FolderAsWorkspaceDock *>();
    for (FolderAsWorkspaceDock *d : existing) {
        if (QDir::cleanPath(d->rootPath()) == cleaned) {
            d->setVisible(true);
            d->raise();
            setActiveWorkspace(d);
            if (showGitTab) d->showGitTab();
            return;
        }
    }

    FolderAsWorkspaceDock *anchor = nullptr;
    for (FolderAsWorkspaceDock *d : existing) {
        if (!d->rootPath().isEmpty()) { anchor = d; break; }
    }

    // Pull memoed UI state (tab/expanded/current) for this path, if any. The
    // lookup is O(1) on a hash that was populated once by restoreOpenWorkspaces
    // via loadAllWorkspaceStates. For paths opened mid-session (file menu /
    // CLI / drag-drop) the hash is also valid because saveSettings keeps it in
    // sync via persistWorkspaceStatesMerged on every clean exit.
    const WorkspaceStateSnapshot savedState =
        m_workspaceStateMemo.value(cleaned, WorkspaceStateSnapshot{});

    // Default ctor + explicit setRootPath sequence (rather than the 2-arg ctor
    // that bakes setRootPath in) so applySavedTreeState lands between them.
    auto *dock = new FolderAsWorkspaceDock(this);
    static int extraIdx = 0;
    // Counter-based name is stable across sessions for the steady-state spawn order:
    // restoreOpenWorkspaces walks the saved path list in order, so docks take
    // FolderAsWorkspaceDock_extra_1..N matching saved Workspaces[0..N-1]. Known
    // limitation: closing a non-last workspace mid-session then restarting shifts the
    // counter relative to the saved layout, which causes the remaining workspaces' tab
    // positions to drift. The active workspace is still raised correctly because
    // raiseSavedActiveWorkspace() re-raises by path after restoreWindowState. If this
    // drift ever becomes a real user complaint, switch to qHash(rootPath)-based names.
    dock->setObjectName(QStringLiteral("FolderAsWorkspaceDock_extra_%1").arg(++extraIdx));
    dock->setAttribute(Qt::WA_DeleteOnClose, true);

    addDockWidget(Qt::LeftDockWidgetArea, dock);
    DockMiddleClickCloser::install(dock);
    ui->menuView->addAction(dock->toggleViewAction());
    connect(dock, &FolderAsWorkspaceDock::fileDoubleClicked, this, &MainWindow::openFile);
    connect(dock, &FolderAsWorkspaceDock::fileClicked, this, &MainWindow::previewFile);
    wireWorkspaceGitSignals(dock);
    registerWorkspaceDock(dock);

    if (anchor) {
        tabifyDockWidget(anchor, dock);
    }

    dock->applySavedTreeState(savedState);
    dock->setRootPath(resolved);
    dock->setVisible(true);
    dock->raise();
    setActiveWorkspace(dock);
    if (showGitTab) dock->showGitTab();
}

void MainWindow::registerWorkspaceDock(FolderAsWorkspaceDock *dock)
{
    // The currently-raised tab in a tabified group is the one the user is
    // looking at, so use visibilityChanged(true) as the active-workspace signal.
    connect(dock, &QDockWidget::visibilityChanged, this, [this, dock](bool visible) {
        if (visible && !dock->rootPath().isEmpty()) {
            setActiveWorkspace(dock);
        }
    });

    // Mid-session close: snapshot state into the on-disk memo so reopening
    // this path later (from File menu, recent workspaces, or CLI) restores
    // the user's tree expansion / active tab / current item. Without this,
    // any state accumulated since the last clean exit or 60s autosave would
    // be lost when the user closes a workspace dock.
    connect(dock, &FolderAsWorkspaceDock::aboutToBeClosed, this,
            [this](const QString &path, FolderAsWorkspaceDock *self) {
        if (path.isEmpty() || !self) return;
        const WorkspaceStateSnapshot snap = self->captureState();
        m_workspaceStateMemo.insert(QDir::cleanPath(path), snap);
        persistOneWorkspaceState(snap);
        // Evict this workspace's file index — it is stale once the dock is gone
        // and would otherwise leak across the app's lifetime.
        m_fileIndexCache.remove(QDir::cleanPath(path));
        // The dirty bit no longer needs flushing for THIS dock — its state
        // just went to disk. Leave the bit for other docks that may still
        // have unflushed changes.
    });

    // User-driven tree / tab changes mark the workspace state dirty so the
    // 60s autosave timer flushes them (defense vs crash mid-session).
    connect(dock, &FolderAsWorkspaceDock::stateDirty, this,
            [this]() { m_workspaceStateDirty = true; });

    connect(dock, &FolderAsWorkspaceDock::treeContextMenuRequested, this,
            [this](QMenu *menu, const QString &absPath, bool isDir) {
        const QString wsRoot = currentWorkspaceRoot();

        // --- Preview (rendered preview for supported file types) ---
        if (!isDir) {
            auto *mgr = this->app->getPreviewTabManager();
            if (mgr && mgr->canPreview(absPath)) {
                QFileInfo fi(absPath);
                if (fi.size() <= 10 * 1024 * 1024) {
                    auto *previewAction = new QAction(tr("Preview"), menu);
                    connect(previewAction, &QAction::triggered, this, [this, absPath]() {
                        auto *mgr = this->app->getPreviewTabManager();
                        if (mgr) mgr->openPreviewFromFile(absPath);
                    });
                    menu->addAction(previewAction);
                    menu->addSeparator();
                }
            }
        }

        // --- Copy Path / Copy Relative Path ---
        auto *copyPath = new QAction(tr("Copy Path"), menu);
        connect(copyPath, &QAction::triggered, this, [absPath]() {
            QApplication::clipboard()->setText(QDir::toNativeSeparators(absPath));
        });
        menu->addAction(copyPath);

        auto *copyRelPath = new QAction(tr("Copy Relative Path"), menu);
        connect(copyRelPath, &QAction::triggered, this, [absPath, wsRoot]() {
            QString rel = absPath;
            if (!wsRoot.isEmpty() && rel.startsWith(wsRoot)) {
                rel = rel.mid(wsRoot.length());
                if (rel.startsWith(QLatin1Char('/')) || rel.startsWith(QLatin1Char('\\')))
                    rel = rel.mid(1);
            }
            QApplication::clipboard()->setText(QDir::toNativeSeparators(rel));
        });
        menu->addAction(copyRelPath);

        menu->addSeparator();

        // --- New File / New Folder (directory only) ---
        if (isDir) {
            auto *newFile = new QAction(tr("New File..."), menu);
            connect(newFile, &QAction::triggered, this, [this, absPath]() {
                bool ok = false;
                const QString name = QInputDialog::getText(this, tr("New File"),
                    tr("File name:"), QLineEdit::Normal, QString(), &ok);
                if (!ok || name.isEmpty()) return;
                if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
                    || name.contains(QStringLiteral(".."))) {
                    QMessageBox::warning(this, tr("New File"),
                        tr("Invalid file name."));
                    return;
                }
                const QString filePath = QDir(absPath).filePath(name);
                QFile f(filePath);
                if (f.exists()) {
                    QMessageBox::warning(this, tr("New File"),
                        tr("A file named \"%1\" already exists.").arg(name));
                    return;
                }
                if (!f.open(QIODevice::WriteOnly)) {
                    QMessageBox::warning(this, tr("New File"),
                        tr("Could not create file \"%1\".").arg(name));
                    return;
                }
                f.close();
            });
            menu->addAction(newFile);

            auto *newFolder = new QAction(tr("New Folder..."), menu);
            connect(newFolder, &QAction::triggered, this, [this, absPath]() {
                bool ok = false;
                const QString name = QInputDialog::getText(this, tr("New Folder"),
                    tr("Folder name:"), QLineEdit::Normal, QString(), &ok);
                if (!ok || name.isEmpty()) return;
                if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
                    || name.contains(QStringLiteral(".."))) {
                    QMessageBox::warning(this, tr("New Folder"),
                        tr("Invalid folder name."));
                    return;
                }
                QDir dir(absPath);
                if (dir.exists(name)) {
                    QMessageBox::warning(this, tr("New Folder"),
                        tr("A folder named \"%1\" already exists.").arg(name));
                    return;
                }
                if (!dir.mkdir(name)) {
                    QMessageBox::warning(this, tr("New Folder"),
                        tr("Could not create folder \"%1\".").arg(name));
                }
            });
            menu->addAction(newFolder);

            menu->addSeparator();
        }

        // --- Show in Explorer ---
        auto *showInExplorer = new QAction(tr("Show in Explorer"), menu);
        connect(showInExplorer, &QAction::triggered, this, [absPath, isDir]() {
#ifdef Q_OS_WIN
            if (isDir) {
                QProcess::startDetached(QStringLiteral("explorer"), {QDir::toNativeSeparators(absPath)});
            } else {
                QProcess::startDetached(QStringLiteral("explorer"),
                    {QStringLiteral("/select,"), QDir::toNativeSeparators(absPath)});
            }
#elif defined(Q_OS_MACOS)
            if (isDir) {
                QProcess::startDetached(QStringLiteral("open"), {absPath});
            } else {
                QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), absPath});
            }
#else
            const QString target = isDir ? absPath : QFileInfo(absPath).absolutePath();
            QProcess::startDetached(QStringLiteral("xdg-open"), {target});
#endif
        });
        menu->addAction(showInExplorer);

        // --- Open Terminal Here (directory only) ---
        if (isDir) {
            auto *openTerminal = new QAction(tr("Open Terminal Here"), menu);
            connect(openTerminal, &QAction::triggered, this, [this, absPath]() {
                terminalManager->openTerminal(absPath);
            });
            menu->addAction(openTerminal);
        }

        // --- Export via Pandoc (markdown files only) ---
        if (!isDir) {
            const QString suffix = QFileInfo(absPath).suffix().toLower();
            if (suffix == QLatin1String("md") || suffix == QLatin1String("markdown")) {
                auto *pandocMenu = new QMenu(tr("Export via Pandoc"), menu);

                auto addExportAction = [&](const QString &label, PandocExporter::Format fmt, const QString &ext) {
                    auto *action = new QAction(label, pandocMenu);
                    connect(action, &QAction::triggered, this, [this, absPath, fmt, ext]() {
                        if (!PandocExporter::isAvailable()) {
                            QMessageBox::warning(this, tr("Export via Pandoc"),
                                tr("Pandoc 2.19 or later is required but was not found.\n"
                                   "Install from pandoc.org/installing.html"));
                            return;
                        }

                        ScintillaNext *editor = this->app->getEditorManager()->getEditorByFilePath(absPath);
                        if (editor && editor->canSaveToDisk()) {
                            auto btn = QMessageBox::question(this, tr("Export via Pandoc"),
                                tr("This file has unsaved changes in the editor.\n"
                                   "Export will use the version on disk (without your recent edits)."),
                                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
                            if (btn == QMessageBox::Save) {
                                editor->save();
                            } else if (btn == QMessageBox::Cancel) {
                                return;
                            }
                        }

                        const QFileInfo fi(absPath);
                        const QString defaultOut = fi.absolutePath() + QLatin1Char('/')
                            + fi.completeBaseName() + QLatin1Char('.') + ext;
                        const QString filter = ext == QStringLiteral("docx")
                            ? tr("Word Documents (*.docx);;All files (*)")
                            : ext == QStringLiteral("html")
                                ? tr("HTML Files (*.html);;All files (*)")
                                : tr("EPUB Files (*.epub);;All files (*)");
                        const QString outPath = FileDialogHelpers::getSaveFileName(
                            this, tr("Export via Pandoc"), defaultOut, filter);
                        if (outPath.isEmpty()) return;

                        auto *exporter = new PandocExporter(this, this);
                        connect(exporter, &PandocExporter::finished, this,
                                [this](bool ok, const QString &err) {
                            if (ok)
                                FadingIndicator::showText(centralWidget(), tr("Exported successfully"));
                            else
                                QMessageBox::warning(this, tr("Export via Pandoc"),
                                    tr("Export failed: %1").arg(err));
                        });
                        exporter->exportFile(absPath, outPath, fmt);
                    });
                    pandocMenu->addAction(action);
                };

                addExportAction(tr("DOCX..."), PandocExporter::Docx, QStringLiteral("docx"));
                addExportAction(tr("HTML..."), PandocExporter::Html, QStringLiteral("html"));
                addExportAction(tr("EPUB..."), PandocExporter::Epub, QStringLiteral("epub"));
                menu->addMenu(pandocMenu);
            }
        }

        menu->addSeparator();

        // --- Delete (submenu: Move to Trash / Delete Permanently) ---
        auto *deleteMenu = new QMenu(tr("Delete"), menu);
        auto *moveToTrash = new QAction(tr("Move to Trash"), deleteMenu);
        connect(moveToTrash, &QAction::triggered, this, [this, absPath, isDir]() {
            const QString name = QFileInfo(absPath).fileName();
            const QString msg = isDir
                ? tr("Are you sure you want to move folder \"%1\" to the trash?").arg(name)
                : tr("Are you sure you want to move \"%1\" to the trash?").arg(name);
            if (QMessageBox::question(this, tr("Move to Trash"), msg) != QMessageBox::Yes)
                return;
            if (!QFile::moveToTrash(absPath)) {
                QMessageBox::warning(this, tr("Move to Trash"),
                    tr("Could not move \"%1\" to the trash.").arg(name));
            }
        });
        deleteMenu->addAction(moveToTrash);

        auto *deletePermanently = new QAction(tr("Delete Permanently"), deleteMenu);
        connect(deletePermanently, &QAction::triggered, this, [this, absPath, isDir]() {
            const QString name = QFileInfo(absPath).fileName();
            const QString msg = isDir
                ? tr("Are you sure you want to permanently delete folder \"%1\" and all its contents? This cannot be undone.")
                    .arg(name)
                : tr("Are you sure you want to permanently delete \"%1\"? This cannot be undone.")
                    .arg(name);
            if (QMessageBox::warning(this, tr("Delete Permanently"), msg,
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            bool ok;
            if (isDir) {
                QDir dir(absPath);
                ok = dir.removeRecursively();
            } else {
                ok = QFile::remove(absPath);
            }
            if (!ok) {
                QMessageBox::warning(this, tr("Delete Permanently"),
                    tr("Could not delete \"%1\".").arg(name));
            }
        });
        deleteMenu->addAction(deletePermanently);
        menu->addMenu(deleteMenu);

        menu->addSeparator();

        // --- Send to AI ---
        AiAgentDock *targetDock = activeAiDock();
        if (targetDock) {
            auto *sendToAi = new QAction(tr("Send to AI"), menu);
            connect(sendToAi, &QAction::triggered, this, [this, absPath, isDir, wsRoot]() {
                AiAgentDock *dock = activeAiDock();
                if (!dock) return;
                QString relPath = absPath;
                if (!wsRoot.isEmpty() && relPath.startsWith(wsRoot)) {
                    relPath = relPath.mid(wsRoot.length());
                    if (relPath.startsWith(QLatin1Char('/')) || relPath.startsWith(QLatin1Char('\\')))
                        relPath = relPath.mid(1);
                }
                QString text;
                if (isDir && !relPath.endsWith(QLatin1Char('/'))) {
                    text = QStringLiteral("@%1/ ").arg(relPath);
                } else {
                    text = QStringLiteral("@%1 ").arg(relPath);
                }
                dock->insertTextToInput(text);
                dock->setVisible(true);
                dock->raise();
            });
            menu->addAction(sendToAi);
        }
    });
}

void MainWindow::wireWorkspaceGitSignals(FolderAsWorkspaceDock *dock)
{
    dock->setGitOperationManager(m_gitOpMgr);

    connect(dock, &FolderAsWorkspaceDock::gitDiffRequested,
            this, [dock](const GitStatusEntry &entry) {
                dock->showGitDiffPreview(entry);
            });
    connect(dock, &FolderAsWorkspaceDock::gitOpenSubmoduleRequested,
            this, [this](const QString &path) {
                // The user navigated here from another workspace's Git tab, so
                // land them on Git in the new workspace too — they're clearly
                // working in the version-control axis right now.
                openFolderAsWorkspacePath(path, /*showGitTab=*/true);
            });
    connect(dock, &FolderAsWorkspaceDock::gitDiffPreviewRendered,
            this, [this](ScintillaNext *editor) {
                dockedEditor->switchToEditor(editor);
            });

    // Commit detail tab (History → click commit).
    connect(dock, &FolderAsWorkspaceDock::gitOpenCommitDetailRequested,
            this, [dock](const QByteArray &sha) {
                dock->showGitCommitDetail(sha);
            });
    // *EditorCreated handlers must NOT call dockedEditor->addEditor — the
    // editor is already added via the editorCreated → MainWindow::addEditor
    // chain in EditorManager::createEditor. A second add reparents the editor
    // to a fresh dock widget, leaving the first one as an empty placeholder.
    // For file-at-sha the Created signal is the only chance to raise the new
    // tab (no separate Rendered signal), so switch focus here.
    connect(dock, &FolderAsWorkspaceDock::gitFileAtShaEditorCreated,
            this, [this](ScintillaNext *editor) {
                dockedEditor->switchToEditor(editor);
            });
    connect(dock, &FolderAsWorkspaceDock::gitCommitEditorRendered,
            this, [this](ScintillaNext *editor) {
                dockedEditor->switchToEditor(editor);
            });
    connect(dock, &FolderAsWorkspaceDock::gitCommitEditorFocus,
            this, [this](ScintillaNext *editor) {
                dockedEditor->switchToEditor(editor);
            });
    connect(dock, &FolderAsWorkspaceDock::gitFileAtShaEditorFocus,
            this, [this](ScintillaNext *editor) {
                dockedEditor->switchToEditor(editor);
            });
    connect(dock, &FolderAsWorkspaceDock::gitCommitDetailFailed,
            this, [](const QByteArray &sha, const QString &message) {
                qWarning("Commit detail failed for %s: %s",
                         sha.constData(), qPrintable(message));
            });

    connect(dock, &FolderAsWorkspaceDock::gitChangesContextMenuRequested, this,
            [this](QMenu *menu, const GitStatusEntry &entry) {
        AiAgentDock *targetDock = activeAiDock();
        if (!targetDock) return;

        const QString relPath = entry.relPath;
        auto *sendToAi = new QAction(tr("Send to AI"), menu);
        connect(sendToAi, &QAction::triggered, this, [this, relPath]() {
            AiAgentDock *dock = activeAiDock();
            if (!dock) return;
            const QString text = QStringLiteral("@%1 ").arg(relPath);
            dock->insertTextToInput(text);
            dock->setVisible(true);
            dock->raise();
        });
        menu->addAction(sendToAi);
    });

    connect(dock, &FolderAsWorkspaceDock::gitMergeRequested, this,
            [this](FolderAsWorkspaceDock *wsDock) {
        auto *ctrl = wsDock->gitTabWidget() ? wsDock->gitTabWidget()->controller() : nullptr;
        if (!ctrl) return;
        auto *picker = new BranchPickerPopup(this);
        picker->setAttribute(Qt::WA_DeleteOnClose);
        picker->setSelectOnly(true, tr("Select branch to merge into %1…").arg(ctrl->currentBranch()));
        picker->setBranches(ctrl->branchesLocal(), ctrl->branchesRemote(),
                           ctrl->currentBranch());
        connect(picker, &BranchPickerPopup::branchSelected, this,
            [this, ctrl](const QString &branch) {
                m_gitOpMgr->startMerge(ctrl, branch);
            });
        picker->popupAt(QCursor::pos());
    });
    connect(dock, &FolderAsWorkspaceDock::gitRebaseRequested, this,
            [this](FolderAsWorkspaceDock *wsDock) {
        auto *ctrl = wsDock->gitTabWidget() ? wsDock->gitTabWidget()->controller() : nullptr;
        if (!ctrl) return;
        auto *picker = new BranchPickerPopup(this);
        picker->setAttribute(Qt::WA_DeleteOnClose);
        picker->setSelectOnly(true, tr("Select branch to rebase onto…"));
        picker->setBranches(ctrl->branchesLocal(), ctrl->branchesRemote(),
                           ctrl->currentBranch());
        connect(picker, &BranchPickerPopup::branchSelected, this,
            [this, ctrl](const QString &branch) {
                m_gitOpMgr->startRebase(ctrl, branch);
            });
        picker->popupAt(QCursor::pos());
    });
    connect(dock, &FolderAsWorkspaceDock::gitInteractiveRebaseRequested, this,
            [this](FolderAsWorkspaceDock *wsDock) {
        auto *ctrl = wsDock->gitTabWidget() ? wsDock->gitTabWidget()->controller() : nullptr;
        if (!ctrl) return;
        auto *picker = new BranchPickerPopup(this);
        picker->setAttribute(Qt::WA_DeleteOnClose);
        picker->setSelectOnly(true, tr("Select branch for interactive rebase…"));
        picker->setBranches(ctrl->branchesLocal(), ctrl->branchesRemote(),
                           ctrl->currentBranch());
        connect(picker, &BranchPickerPopup::branchSelected, this,
            [this, ctrl](const QString &branch) {
                m_gitOpMgr->startInteractiveRebase(ctrl, branch);
            });
        picker->popupAt(QCursor::pos());
    });
}

void MainWindow::restoreOpenWorkspaces()
{
    PROFILE_SCOPE("MainWindow::restoreOpenWorkspaces");
    // Qt's saveState/restoreState only preserves layout for docks that already
    // exist at restore time. Extra workspace docks are spawned on-demand, so we
    // recreate them here from the persisted path list before restoreWindowState
    // runs — that way their tab positions get restored too.

    // Populate the workspace-state memo once, here, so openFolderAsWorkspacePath
    // can do O(1) lookups during the restore loop (and afterwards for any
    // mid-session opens via File menu / drag-drop / CLI).
    loadAllWorkspaceStates();

    ApplicationSettings *settings = app->getSettings();
    const QStringList savedWorkspaces = settings->value("FolderAsWorkspace/Workspaces").toStringList();

    for (const QString &path : savedWorkspaces) {
        if (path.isEmpty()) continue;
        // Defense in depth against stale "." or relative entries written by
        // older builds. openFolderAsWorkspacePath would resolve them to the
        // current cwd and silently open it as a workspace — which is exactly
        // the bug we're avoiding. Skip anything that doesn't already point at
        // a real directory; the absolute form is then routed through the
        // chokepoint as usual.
        const QString abs = QDir(path).absolutePath();
        if (abs.isEmpty() || !QFileInfo(abs).isDir()) continue;
        // openFolderAsWorkspacePath deduplicates against existing docks (including
        // the initial one, which loaded its path from the legacy singular setting).
        openFolderAsWorkspacePath(abs);
    }
}

void MainWindow::raiseSavedActiveWorkspace()
{
    // Defense-in-depth on top of QMainWindow::restoreState: the saved windowState
    // only carries "which tab was raised" if the user actually closed the app with
    // it raised, and the lookup is keyed by objectName — which can drift if the
    // user closed a non-last workspace mid-session (see the comment on the extra
    // dock objectName). Re-raise by rootPath so the active workspace sticks.
    ApplicationSettings *settings = app->getSettings();
    const QString activePath = settings->value("FolderAsWorkspace/ActiveWorkspace").toString();
    if (activePath.isEmpty()) return;

    const QString cleanedActive = QDir::cleanPath(activePath);
    for (FolderAsWorkspaceDock *d : findChildren<FolderAsWorkspaceDock *>()) {
        if (QDir::cleanPath(d->rootPath()) == cleanedActive) {
            setActiveWorkspace(d);
            d->setVisible(true);
            d->raise();
            break;
        }
    }
}

FolderAsWorkspaceDock *MainWindow::activeWorkspaceDock() const
{
    if (m_activeWorkspace && !m_activeWorkspace->rootPath().isEmpty()) {
        return m_activeWorkspace.data();
    }
    // Fallback after the active dock was closed (QPointer auto-nulls) — pick
    // any remaining workspace with a path so cwd-aware actions keep working.
    const auto docks = findChildren<FolderAsWorkspaceDock *>();
    for (FolderAsWorkspaceDock *d : docks) {
        if (!d->rootPath().isEmpty()) return d;
    }
    return nullptr;
}

void MainWindow::setActiveWorkspace(FolderAsWorkspaceDock *dock)
{
    FolderAsWorkspaceDock *oldDock = m_activeWorkspace.data();
    if (oldDock == dock) return;
    m_activeWorkspace = dock;
    CrashContext::setActiveWorkspaceRoot(currentWorkspaceRoot());
    emit activeWorkspaceChanged(dock, oldDock);
}

QString MainWindow::currentWorkspaceRoot() const
{
    FolderAsWorkspaceDock *dock = activeWorkspaceDock();
    return dock ? dock->rootPath() : QString();
}

void MainWindow::onFileIndexReady(const QString &rootKey,
                                  std::shared_ptr<const FileIndexCache> snapshot)
{
    // Sole UI-thread writer of the cache (signal is queued). RCU-lite: store the
    // immutable snapshot; readers (the open dialog) hold their own shared_ptr.
    m_fileIndexCache.insert(rootKey, snapshot);

    // If a dialog is open for exactly this workspace, hand it the fresh snapshot
    // so its current filter re-runs against up-to-date data.
    if (m_quickFileOpenDialog && m_quickFileOpenRootKey == rootKey)
        m_quickFileOpenDialog->adoptSnapshot(std::move(snapshot));
}

QStringList MainWindow::workspaceMruFiles(const QString &workspaceRoot) const
{
    QStringList result;
    if (workspaceRoot.isEmpty())
        return result;

    RecentFilesListManager *rf = app->getRecentFilesListManager();
    if (!rf)
        return result;

    const QString cleanRoot = QDir::cleanPath(workspaceRoot);
    const QDir rootDir(cleanRoot);

    // Keep only recent files that live under the workspace, converted to
    // '/'-separated workspace-relative paths matching the snapshot's
    // displayPaths. Preserve MRU order. relativeFilePath escapes the root with
    // a leading ".." (or is absolute on a different drive) for outside paths.
    const QStringList recents = rf->fileList();
    result.reserve(recents.size());
    for (const QString &abs : recents) {
        if (abs.isEmpty())
            continue;
        const QString rel = rootDir.relativeFilePath(QDir::cleanPath(abs));
        if (rel.isEmpty() || rel == QStringLiteral(".") || rel.startsWith(QStringLiteral("../"))
            || rel == QStringLiteral("..") || QDir::isAbsolutePath(rel))
            continue;
        result.append(rel);
    }
    return result;
}

namespace {

// Normalize a path for boundary-safe containment comparison: resolve symlinks
// + ".." via canonicalFilePath when the path exists on disk, else fall back to
// cleanPath (the path may legitimately not exist, e.g. a just-deleted file).
// Both forms yield forward-slash separators on every OS.
QString normalizeForContainment(const QString &p)
{
    if (p.isEmpty()) return QString();
    const QString canonical = QFileInfo(p).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(p) : canonical;
}

// True if `childNorm` is `rootNorm` itself or lives beneath it. Both inputs
// must already be normalized (see normalizeForContainment). Case-insensitive
// on Windows, case-sensitive elsewhere.
bool pathContains(const QString &rootNorm, const QString &childNorm)
{
    if (rootNorm.isEmpty() || childNorm.isEmpty()) return false;
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return (childNorm.compare(rootNorm, cs) == 0) ||
           childNorm.startsWith(rootNorm + QLatin1Char('/'), cs);
}

} // namespace

FolderAsWorkspaceDock *MainWindow::resolveShowInWorkspaceDock(const QString &filePath, bool isFile) const
{
    // An unsaved file (no path) has nothing to locate in a tree.
    if (!isFile || filePath.isEmpty()) return nullptr;

    const QString fileNorm = normalizeForContainment(filePath);
    if (fileNorm.isEmpty()) return nullptr;

    // Prefer the active workspace when it contains the file.
    if (FolderAsWorkspaceDock *active = activeWorkspaceDock()) {
        const QString root = active->rootPath();
        if (!root.isEmpty() && pathContains(normalizeForContainment(root), fileNorm)) {
            return active;
        }
    }

    // Else pick the dock with the LONGEST matching root (most specific, so a
    // nested workspace wins over its parent workspace).
    FolderAsWorkspaceDock *best = nullptr;
    int bestRootLen = -1;
    const auto docks = findChildren<FolderAsWorkspaceDock *>();
    for (FolderAsWorkspaceDock *d : docks) {
        const QString root = d->rootPath();
        if (root.isEmpty()) continue;
        const QString rootNorm = normalizeForContainment(root);
        if (pathContains(rootNorm, fileNorm) && rootNorm.length() > bestRootLen) {
            best = d;
            bestRootLen = rootNorm.length();
        }
    }
    return best;
}

void MainWindow::reloadFile()
{
    auto editor = currentEditor();

    if (!editor->isFile() && !editor->isSavedToDisk()) {
        return;
    }

    const QString filePath = editor->getFilePath();
    auto reply = QMessageBox::question(this, tr("Reload File"), tr("Are you sure you want to reload <b>%1</b>? Any unsaved changes will be lost.").arg(filePath));

    if (reply == QMessageBox::Yes) {
        editor->reload();
    }
}

void MainWindow::closeCurrentFile()
{
    dockedEditor->closeFocusedTab();
}

void MainWindow::closeFile(ScintillaNext *editor)
{
    // Early out. If we aren't exiting on last tab closed, and it exists, there's no point in continuing
    if (!app->getSettings()->exitOnLastTabClosed() && getInitialEditor() != Q_NULLPTR) {
        return;
    }

    if (!checkEditorsBeforeClose({editor})) {
        return;
    }

    editor->close();

    // If the last document was closed, figure out what to do next
    if (editorCount() == 0) {
        if (app->getSettings()->exitOnLastTabClosed()) {
            close();
        }
        else {
            // Defer newFile() so the ADS close cascade fully unwinds before we
            // call addDockWidget — otherwise we crash in topLevelDockArea on a
            // container that's still mid-tear-down.
            QMetaObject::invokeMethod(this, &MainWindow::newFile, Qt::QueuedConnection);
        }
    }
}

void MainWindow::closeAllFiles()
{
    if (!checkEditorsBeforeClose(editors())) {
        return;
    }

    // Ask the manager to close the editors the dockedEditor knows about
    for (ScintillaNext *editor : editors()) {
        editor->close();
    }

    QMetaObject::invokeMethod(this, &MainWindow::newFile, Qt::QueuedConnection);
}

void MainWindow::closeAllExceptActive()
{
    auto e = currentEditor();
    auto editor_list = editors();

    editor_list.removeOne(e);

    if (checkEditorsBeforeClose(editor_list)) {
        for (ScintillaNext *editor : editor_list) {
            editor->close();
        }
    }
}

void MainWindow::closeAllToLeft()
{
    const int index = dockedEditor->currentDockArea()->currentIndex();
    QVector<ScintillaNext *> editors;

    for (int i = 0; i < index; ++i) {
        auto editor = qobject_cast<ScintillaNext *>(dockedEditor->currentDockArea()->dockWidget(i)->widget());
        if (!editor) continue;
        editors.append(editor);
    }

    if (checkEditorsBeforeClose(editors)) {
        for (ScintillaNext *editor : editors) {
            editor->close();
        }
    }
}

void MainWindow::closeAllToRight()
{
    const int index = dockedEditor->currentDockArea()->currentIndex();
    const int total = dockedEditor->currentDockArea()->dockWidgetsCount();
    QVector<ScintillaNext *> editors;

    for (int i = index + 1; i < total; ++i) {
        auto editor = qobject_cast<ScintillaNext *>(dockedEditor->currentDockArea()->dockWidget(i)->widget());
        if (!editor) continue;
        editors.append(editor);
    }

    if (checkEditorsBeforeClose(editors)) {
        for (ScintillaNext *editor : editors) {
            editor->close();
        }
    }
}

bool MainWindow::saveCurrentFile()
{
    return saveFile(currentEditor());
}

bool MainWindow::saveFile(ScintillaNext *editor)
{
    if (editor->isSavedToDisk())
        return true;

    if (!editor->isFile()) {
        // Switch to the editor and show the saveas dialog
        dockedEditor->switchToEditor(editor);
        return saveCurrentFileAsDialog();
    }
    else {
        QFileDevice::FileError error = editor->save();
        if (error == QFileDevice::NoError) {
            return true;
        }
        else {
            showSaveErrorMessage(editor, error);
            return false;
        }
    }
}

bool MainWindow::saveCurrentFileAsDialog()
{
    const QString filter = app->getFileDialogFilter();
    ScintillaNext *editor = currentEditor();

    QString selectedFilter = app->getFileDialogFilterForLanguage(editor->languageName);
    QString fileName = FileDialogHelpers::getSaveFileName(this, QString(), defaultDirectoryManager->getDefaultDirectory(), filter, &selectedFilter);

    if (fileName.size() == 0) {
        return false;
    }

    emit fileDialogAccepted(fileName);

    // TODO: distinguish between the above case (i.e. the user cancels the dialog) and a failure
    // calling editor->saveAs() as it might fail.

    return saveFileAs(editor, fileName);
}

bool MainWindow::saveCurrentFileAs(const QString &fileName)
{
    return saveFileAs(currentEditor(), fileName);
}

bool MainWindow::saveFileAs(ScintillaNext *editor, const QString &fileName)
{
    qInfo("saveFileAs(%s)", qUtf8Printable(fileName));

    QFileDevice::FileError error = editor->saveAs(fileName);

    if (error == QFileDevice::NoError) {
        return true;
    }
    else {
        showSaveErrorMessage(editor, error);
        return false;
    }
}

bool MainWindow::saveCopyAsDialog()
{
    const QString filter = app->getFileDialogFilter();
    const QString languageName = currentEditor()->languageName;

    QString selectedFilter = app->getFileDialogFilterForLanguage(languageName);
    const QString fileName = FileDialogHelpers::getSaveFileName(this, tr("Save a Copy As"), defaultDirectoryManager->getDefaultDirectory(), filter, &selectedFilter);

    if (fileName.size() == 0) {
        return false;
    }

    emit fileDialogAccepted(fileName);

    return saveCopyAs(fileName);
}

bool MainWindow::saveCopyAs(const QString &fileName)
{
    auto editor = currentEditor();

    QFileDevice::FileError error = editor->saveCopyAs(fileName);

    if (error == QFileDevice::NoError) {
        return true;
    }
    else {
        showSaveErrorMessage(editor, error);
        return false;
    }
}

bool MainWindow::saveAll()
{
    return saveAllEditors(editors());
}

bool MainWindow::saveAllEditors(const QVector<ScintillaNext *> &editors)
{
    for (ScintillaNext *editor : editors) {
        if (!saveFile(editor)){
            return false;
        }
    }

    return true;
}

void MainWindow::exportAsFormat(Converter *converter, const QString &filter)
{
    const QString fileName = FileDialogHelpers::getSaveFileName(this, tr("Export As"), QString(), filter + ";;All files (*)");

    if (fileName.isEmpty()) {
        return;
    }

    QFile f(fileName);

    f.open(QIODevice::WriteOnly);

    QTextStream s(&f);
    converter->convert(s);
    f.close();
}

void MainWindow::copyAsFormat(Converter *converter, const QString &mimeType)
{
    // This is not ideal as we are *assuming* the converter is currently associated with the currentEditor()
    ScintillaNext *editor = currentEditor();
    QByteArray buffer;
    QTextStream stream(&buffer);

    if (editor->selectionEmpty())
        converter->convert(stream);
    else {
        converter->convertRange(stream, editor->selectionStart(), editor->selectionEnd());
    }

    QMimeData *mimeData = new QMimeData();
    mimeData->setData(mimeType, buffer);

    QApplication::clipboard()->setMimeData(mimeData);
}

void MainWindow::renameFile()
{
    ScintillaNext *editor = currentEditor();

    if (editor->isFile()) {
        const QString filter = app->getFileDialogFilter();
        QString selectedFilter = app->getFileDialogFilterForLanguage(editor->languageName);
        QString fileName = FileDialogHelpers::getSaveFileName(this, tr("Rename"), editor->getFilePath(), filter, &selectedFilter);

        if (fileName.isEmpty()) {
            return;
        }

        emit fileDialogAccepted(fileName);

        // TODO
        // The new fileName might be to one of the existing editors.
        //auto otherEditor = app->getEditorByFilePath(fileName);

        bool renameSuccessful = editor->rename(fileName);
        Q_UNUSED(renameSuccessful)
    }
    else {
        bool ok;
        QString text = QInputDialog::getText(this, tr("Rename"), tr("Name:"), QLineEdit::Normal, editor->getName(), &ok);

        if (ok && !text.isEmpty()) {
            editor->setName(text);
        }
    }
}

void MainWindow::moveCurrentFileToTrash()
{
    ScintillaNext *editor = currentEditor();

    moveFileToTrash(editor);
}

void MainWindow::moveFileToTrash(ScintillaNext *editor)
{
    Q_ASSERT(editor->isFile());

    const QString filePath = editor->getFilePath();
    auto reply = QMessageBox::question(this, tr("Delete File"), tr("Are you sure you want to move <b>%1</b> to the trash?").arg(filePath));

    if (reply == QMessageBox::Yes) {
        if (editor->moveToTrash()) {
            closeCurrentFile();

            // Since the file no longer exists, specifically remove it from the recent files list
            app->getRecentFilesListManager()->removeFile(editor->getFilePath());
        }
        else {
            QMessageBox::warning(this, tr("Error Deleting File"),  tr("Something went wrong deleting <b>%1</b>?").arg(filePath));
        }
    }
}

void MainWindow::print()
{
    QPrintPreviewDialog printDialog(this, Qt::Window);
    EditorPrintPreviewRenderer renderer(currentEditor());

    connect(&printDialog, &QPrintPreviewDialog::paintRequested, &renderer, &EditorPrintPreviewRenderer::render);

    // TODO: load/save the page layout that was used and reload it next time
    //preview.printer()->setPageLayout( /* todo */ );

    printDialog.printer()->setPageMargins(QMarginsF(.5, .5, .5, .5), QPageLayout::Inch);

    connect(&printDialog, &QPrintPreviewDialog::accepted, this, [&]() {
        qInfo() << printDialog.printer()->pageLayout();
    });

    printDialog.exec();
}

void MainWindow::convertEOLs(int eolMode)
{
    ScintillaNext *editor = currentEditor();

    // TODO: does convertEOLs trigger SCN_MODIFIED notifications? If so can these be turned off to increase performance?
    editor->convertEOLs(eolMode);
    editor->setEOLMode(eolMode);

    updateEOLBasedUi(editor);

    // There's no simple Scintilla notification that the EOL mode has changed
    // So tell the status bar to refresh its info
    ui->statusBar->refresh(editor);
}

void MainWindow::showFindReplaceDialog(int index)
{
    ScintillaNext *editor = currentEditor();
    FindReplaceDialog *frd = findChild<FindReplaceDialog *>(QString(), Qt::FindDirectChildrenOnly);

    if (frd == Q_NULLPTR) {
        frd = new FindReplaceDialog(determineSearchResultsHandler(), this);
    }
    else {
        frd->setSearchResultsHandler(determineSearchResultsHandler());
    }

    // TODO: if dockedEditor::editorActivated() is fired, or if the editor get closed
    // the FindReplaceDialog's editor pointer needs updated...

    // Get any selected text
    if (!editor->selectionEmpty()) {
        int selection = editor->mainSelection();
        int start = editor->selectionNStart(selection);
        int end = editor->selectionNEnd(selection);
        if (end > start) {
            auto selText = editor->get_text_range(start, end);
            frd->setFindString(QString::fromUtf8(selText));
        }
    }
    else {
        int start = editor->wordStartPosition(editor->currentPos(), true);
        int end = editor->wordEndPosition(editor->currentPos(), true);
        if (end > start) {
            editor->setSelectionStart(start);
            editor->setSelectionEnd(end);
            auto selText = editor->get_text_range(start, end);
            frd->setFindString(QString::fromUtf8(selText));
        }
    }

    frd->setTab(index);
    frd->show();
    frd->raise();
    frd->activateWindow();
}

void MainWindow::updateFileStatusBasedUi(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    bool isFile = editor->isFile();
    QString fileName;

    if (isFile) {
        fileName = editor->getFilePath();
    }
    else {
        fileName = editor->getName();
    }

    QString title = QStringLiteral("[*]%1").arg(fileName);
    if (app->isRunningAsAdmin()) {
        title += QStringLiteral(" - [%1]").arg(tr("Administrator"));
    }
    setWindowTitle(title);

    ui->actionReload->setEnabled(isFile);
    ui->actionMoveToTrash->setEnabled(isFile);
    ui->actionCopyFullPath->setEnabled(isFile);
    ui->actionCopyFileDirectory->setEnabled(isFile);
    ui->actionShowInExplorer->setEnabled(isFile);
    ui->actionOpenTerminalHere->setEnabled(isFile);
}

bool MainWindow::isAnyUnsaved() const
{
    for (const ScintillaNext *editor : editors()) {
        if (!editor->isSavedToDisk()) {
            return true;
        }
    }

    return false;
}

void MainWindow::updateEOLBasedUi(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    switch(editor->eOLMode()) {
    case SC_EOL_CR:
        ui->actionMacintosh->setChecked(true);
        break;
    case SC_EOL_CRLF:
        ui->actionWindows->setChecked(true);
        break;
    case SC_EOL_LF:
        ui->actionUnix->setChecked(true);
        break;
    default:
        break;
    }
}

void MainWindow::updateSaveStatusBasedUi(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    bool isDirty = !editor->isSavedToDisk();

    setWindowModified(isDirty);

    ui->actionSave->setEnabled(isDirty);
    ui->actionSaveAll->setEnabled(isDirty || isAnyUnsaved());

    if (editorCount() == 1) {
        bool ableToClose = editor->isFile() || isDirty;
        ui->actionClose->setEnabled(ableToClose);
        ui->actionCloseAll->setEnabled(ableToClose);
    }
    else {
        ui->actionClose->setEnabled(true);
        ui->actionCloseAll->setEnabled(true);
    }
}

void MainWindow::updateEditorPositionBasedUi()
{
    const int index = dockedEditor->currentDockArea()->currentIndex();
    const int total = dockedEditor->currentDockArea()->dockWidgetsCount();

    ui->actionCloseAllToLeft->setEnabled(index > 0);
    ui->actionCloseAllToRight->setEnabled(index < (total - 1));
    ui->actionCloseAllExceptActive->setEnabled(editorCount() > 1);
}

void MainWindow::updateLanguageBasedUi(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    const QString language_name = editor->languageName;

    for (QAction *action : languageActionGroup->actions()) {
        if (action->data().toString() == language_name) {
            action->setChecked(true);

            // Found one, so we are completely done
            return;
        }
    }

    // The above loop did not set any action as checked, so make sure they are all unchecked now
    for (QAction *action : languageActionGroup->actions()) {
        if (action->isChecked()) {
            action->setChecked(false);
        }
    }
}

void MainWindow::updateGui(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    updateFileStatusBasedUi(editor);
    updateSaveStatusBasedUi(editor);
    updateEOLBasedUi(editor);
    updateEditorPositionBasedUi();
    updateSelectionBasedUi(editor);
    updateContentBasedUi(editor);
    updateLanguageBasedUi(editor);
}

void MainWindow::updateDocumentBasedUi(Scintilla::Update updated)
{
    ScintillaNext *editor = qobject_cast<ScintillaNext *>(sender());

    // TODO: what if this is triggered by an editor that is not the active editor?

    if (Scintilla::FlagSet(updated, Scintilla::Update::Content)) {
        updateSelectionBasedUi(editor);
    }

    if (Scintilla::FlagSet(updated, Scintilla::Update::Content) || Scintilla::FlagSet(updated, Scintilla::Update::Selection)) {
        updateContentBasedUi(editor);
    }
}

void MainWindow::updateSelectionBasedUi(ScintillaNext *editor)
{
    ui->actionUndo->setEnabled(editor->canUndo());
    ui->actionRedo->setEnabled(editor->canRedo());
}

void MainWindow::updateContentBasedUi(ScintillaNext *editor)
{
    bool hasAnySelections = !editor->selectionEmpty();

    ui->actionPaste->setEnabled(editor->canPaste());

    ui->actionLowerCase->setEnabled(hasAnySelections);
    ui->actionUpperCase->setEnabled(hasAnySelections);

    ui->actionBase64Encode->setEnabled(hasAnySelections);
    ui->actionURLEncode->setEnabled(hasAnySelections);
    ui->actionBase64Decode->setEnabled(hasAnySelections);
    ui->actionURLDecode->setEnabled(hasAnySelections);
}

void MainWindow::detectLanguage(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    if (!editor->isFile()) {
        // Default to some specific language if it is not a file.
        setLanguage(editor, "Text");
        return;
    }
    else {
        const QString language_name = app->detectLanguage(editor);

        setLanguage(editor, language_name);
    }
}

void MainWindow::activateEditor(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    checkFileForModification(editor);
    updateGui(editor);

    if (editor) {
        CrashContext::setActiveEditorPath(
            editor->isFile() ? editor->getFilePath() : editor->getName());
    }

    emit editorActivated(editor);

    if (m_actionMarkdownPreview) {
        const bool isMarkdown = editor && editor->languageName == QLatin1String("Markdown");
        m_actionMarkdownPreview->setEnabled(isMarkdown);

        bool previewActive = false;
        if (isMarkdown && app->getPreviewTabManager()) {
            previewActive = app->getPreviewTabManager()->previewForEditor(editor) != nullptr;
        }
        m_actionMarkdownPreview->setChecked(previewActive);
    }
}

void MainWindow::applyStyleSheet()
{
    qInfo(Q_FUNC_INFO);

    const QString resource = chooseStylesheetResource(app->isEffectiveThemeDark());

    QString sheet;
    QFile f(resource);
    qInfo() << "Loading stylesheet:" << f.fileName();

    f.open(QFile::ReadOnly);
    sheet = f.readAll();
    f.close();

    // If there is a "custom.css" file where the ini is located, load it as a style sheet addition
    QString directoryPath = QFileInfo(app->getSettings()->fileName()).absolutePath();
    QString fullPath = QDir(directoryPath).filePath("custom.css");
    if (QFile::exists(fullPath)) {
        QFile custom(fullPath);
        qInfo() << "Loading stylesheet:" << custom.fileName();

        custom.open(QFile::ReadOnly);
        sheet += custom.readAll();
        custom.close();
    }

    setStyleSheet(sheet);
}

void MainWindow::setLanguage(ScintillaNext *editor, const QString &languageName)
{
    qInfo(Q_FUNC_INFO);
    qInfo("Language Name: %s", qUtf8Printable(languageName));

    app->setEditorLanguage(editor, languageName);
}

void MainWindow::bringWindowToForeground()
{
    qInfo(Q_FUNC_INFO);

    // There doesn't seem to be a cross platform way to force the window to the foreground

#ifdef Q_OS_WIN
    HWND hWnd = reinterpret_cast<HWND>(effectiveWinId());

    if (hWnd) {
        // I have no idea what this does, but it seems to work on Windows
        // References:
        // https://stackoverflow.com/questions/916259/win32-bring-a-window-to-top
        // https://github.com/notepad-plus-plus/notepad-plus-plus/blob/ebe7648ee1a5a560d4fc65297cbdcf08055e56e3/PowerEditor/src/winmain.cpp#L596

        HWND hCurWnd = GetForegroundWindow();
        DWORD threadId = GetCurrentThreadId();
        DWORD procId = GetWindowThreadProcessId(hCurWnd, nullptr);

        int sw = 0;
        if (IsZoomed(hWnd)) {
            sw = SW_MAXIMIZE;
        } else if (IsIconic(hWnd)) {
            sw = SW_RESTORE;
        }

        if (sw != 0) {
            ShowWindow(hWnd, sw);
        }

        AttachThreadInput(procId, threadId, TRUE);
        SetForegroundWindow(hWnd);
        SetFocus(hWnd);
        AttachThreadInput(procId, threadId, FALSE);
    }
#else
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
#endif
}

bool MainWindow::checkFileForModification(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    auto state = editor->checkFileForStateChange();

    if (state == ScintillaNext::NoChange) {
        return false;
    }
    else if (state == ScintillaNext::Modified) {
        qInfo("ScintillaNext::Modified");
        const QString filePath = editor->getFilePath();
        auto reply = QMessageBox::question(this, tr("Reload File"), tr("<b>%1</b> has been modified by another program. Do you want to reload it?").arg(filePath));

        if (reply == QMessageBox::Yes) {
            editor->reload();
        }
        else {
            editor->omitModifications();
        }
    }
    else if (state == ScintillaNext::Deleted) {
        qInfo("ScintillaNext::Deleted");
    }
    else if (state == ScintillaNext::Restored) {
        qInfo("ScintillaNext::Restored");
    }

    return true;
}

void MainWindow::showSaveErrorMessage(ScintillaNext *editor, QFileDevice::FileError error)
{
    const QString name = editor->isFile() ? editor->getFilePath() : editor->getName();

    // Map error code to human-readable string
    QString errorString;
    switch (error) {
        case QFileDevice::ReadError:        errorString = tr("Read error"); break;
        case QFileDevice::WriteError:       errorString = tr("Write error"); break;
        case QFileDevice::FatalError:       errorString = tr("Fatal error"); break;
        case QFileDevice::ResourceError:    errorString = tr("Resource error"); break;
        case QFileDevice::OpenError:        errorString = tr("Open error"); break;
        case QFileDevice::AbortError:       errorString = tr("Abort error"); break;
        case QFileDevice::TimeOutError:     errorString = tr("Timeout error"); break;
        case QFileDevice::UnspecifiedError: errorString = tr("Unspecified error"); break;
        case QFileDevice::RemoveError:      errorString = tr("Remove error"); break;
        case QFileDevice::RenameError:      errorString = tr("Rename error"); break;
        case QFileDevice::PositionError:    errorString = tr("Position error"); break;
        case QFileDevice::ResizeError:      errorString = tr("Resize error"); break;
        case QFileDevice::PermissionsError: errorString = tr("Permissions error"); break;
        case QFileDevice::CopyError:        errorString = tr("Copy error"); break;
        default:                            errorString = tr("Unknown error (%1)").arg(static_cast<int>(error)); break;
    }

    QMessageBox::warning(this, tr("Error Saving File"),
        tr("An error occurred when saving <b>%1</b><br><br>Error: %2").arg(name, errorString));
}

void MainWindow::showEditorZoomLevelIndicator()
{
    // Not sure if Scintilla's zoom level matches up to an exact percentage, but visibly this is close
    FadingIndicator::showText(currentEditor(), tr("Zoom: %1%").arg(zoomLevel * 10 + 100));
}

MainWindow::UserSaveAction MainWindow::promptForSave(const QVector<ScintillaNext *> &editors)
{
    const int count = editors.count();
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("Save File"));
    msgBox.setIcon(QMessageBox::Question);

    // Using pluralization for the main text
    QString text = (count == 1)
                       ? tr("Save changes to <b>%1</b>?").arg(editors.first()->getName())
                       : tr("There are %n files with unsaved changes. Save them?", "", count);
    msgBox.setText(text);

    auto *saveBtn = msgBox.addButton(count > 1 ? tr("Save All") : tr("Save"), QMessageBox::AcceptRole);
    auto *discardBtn = msgBox.addButton(count > 1 ? tr("Discard All") : tr("Discard"), QMessageBox::DestructiveRole);
    msgBox.addButton(QMessageBox::Cancel);

    msgBox.setDefaultButton(saveBtn);
    msgBox.exec();

    if (msgBox.clickedButton() == saveBtn)    return UserSaveAction::SaveAll;
    if (msgBox.clickedButton() == discardBtn) return UserSaveAction::DiscardAll;
    return UserSaveAction::Cancel;
}

void MainWindow::saveSettings() const
{
    qInfo(Q_FUNC_INFO);

    ApplicationSettings *settings = app->getSettings();

    settings->setValue("MainWindow/geometry", saveGeometry());
    settings->setValue("MainWindow/maximized", isMaximized() || isFullScreen());
    settings->setValue("MainWindow/windowState", saveState());

    settings->setValue("Editor/ZoomLevel", zoomLevel);

    QStringList openWorkspaces;
    QVector<WorkspaceStateSnapshot> liveStates;
    for (const FolderAsWorkspaceDock *d : findChildren<FolderAsWorkspaceDock *>()) {
        const QString path = d->rootPath();
        if (path.isEmpty() || openWorkspaces.contains(path)) continue;
        openWorkspaces << path;
        liveStates    << d->captureState();
    }
    settings->setValue("FolderAsWorkspace/Workspaces", openWorkspaces);
    settings->setValue("FolderAsWorkspace/ActiveWorkspace",
                       m_activeWorkspace ? m_activeWorkspace->rootPath() : QString());

    // Snapshot all currently-open workspaces into the on-disk memo. Without
    // this, any state accumulated since the last 60s autosave would be lost
    // on a clean exit — strictly worse than the current behavior. Merge keeps
    // memo entries for paths that aren't currently open (so reopening them
    // later still restores their state), up to MAX_MEMOED via LRU eviction
    // inside persistWorkspaceStatesMerged.
    persistWorkspaceStatesMerged(liveStates);
}

void MainWindow::restoreSettings()
{
    qInfo(Q_FUNC_INFO);

    ApplicationSettings *settings = app->getSettings();

    zoomLevel = settings->value("Editor/ZoomLevel", 0).toInt();

    if (settings->contains("Gui/ToolBar")) {
        QStringList actionNames;
        actionNames = settings->value("Gui/ToolBar").toStringList();

        ui->mainToolBar->clear();

        ActionUtils::populateActionContainer(ui->mainToolBar, this, actionNames);
    }
}

ISearchResultsHandler *MainWindow::determineSearchResultsHandler()
{
    // Determine what will get the search results
    if (app->getSettings()->combineSearchResults()) {
        searchResults.reset(new SearchResultsCollector(findChild<SearchResultsDock *>()));

        return searchResults.data();
    }
    else {
        return findChild<SearchResultsDock *>();
    }
}

void MainWindow::restoreWindowState()
{
    PROFILE_SCOPE("MainWindow::restoreWindowState");
    ApplicationSettings *settings = app->getSettings();

    // Temporarily skip restoreState to isolate the white-space bug
    Q_UNUSED(settings);

    // Always hide the dock no matter how the application was closed
    SearchResultsDock *srDock = findChild<SearchResultsDock *>();
    srDock->hide();
}

void MainWindow::restoreWindowGeometry()
{
    // Intentionally skip restoreGeometry — it encodes a normal-state geometry
    // that can confuse QMainWindow's internal layout when the window is shown
    // maximized. We only need the maximized flag (handled at the show site).
}

void MainWindow::switchToEditor(const ScintillaNext *editor)
{
    dockedEditor->switchToEditor(editor);
}

void MainWindow::focusIn()
{
    qInfo(Q_FUNC_INFO);

    ScintillaNext *editor = currentEditor();

    if (editor) {
        if (checkFileForModification(currentEditor())) {
            updateGui(currentEditor());
        }
    }
}

void MainWindow::addEditor(ScintillaNext *editor)
{
    PROFILE_SCOPE("MainWindow::addEditor");
    qInfo(Q_FUNC_INFO);

    // Defer language detection to the next event-loop tick so the editor
    // tab appears instantly with no highlighting, then styling kicks in
    // one frame later (imperceptible). SC_IDLESTYLING_TOVISIBLE ensures
    // only visible content is styled on the deferred pass.
    QTimer::singleShot(0, editor, [this, editor]() {
        // Skip if a language was already assigned (e.g. by session restore)
        if (!editor->languageName.isEmpty()) return;
        // Diff views are owned by GitDiffPainter — assigning the "Text"
        // lexer here would emit lexerChanged and re-skin every style with
        // the chrome's defaultBack, corrupting canvasBg until the next
        // render pass undoes it.
        if (app->getEditorManager()->isDiffView(editor)) return;
        PROFILE_SCOPE("MainWindow::addEditor.detectLanguage");
        detectLanguage(editor);
    });

    // Defer file-watcher registration to the next event-loop tick.
    // canonicalFilePath() + QFileSystemWatcher::addPath are syscalls that
    // don't need to complete before the tab is painted.
    QTimer::singleShot(0, editor, [this, editor]() {
        fileWatcher->watchEditor(editor);
    });
    connect(editor, &ScintillaNext::closed, this, [this, editor]() {
        fileWatcher->unwatchEditor(editor);
    });
    connect(editor, &QObject::destroyed, this, [this, editor]() {
        fileWatcher->unwatchEditor(editor);
    });

    // These should only ever occur for the focused editor??
    // TODO: look at editor inspector as an example to ensure updates are only coming from one editor.
    // Can save the connection objects and disconnected from them and only connect to the editor as it is activated.
    connect(editor, &ScintillaNext::savePointChanged, this, [=, this]() { updateSaveStatusBasedUi(editor); });
    connect(editor, &ScintillaNext::renamed, this, [= ,this]() {
        if (app->getEditorManager()->isDiffView(editor)) return;
        detectLanguage(editor);
    });
    connect(editor, &ScintillaNext::renamed, this, [=, this]() { updateFileStatusBasedUi(editor); });
    connect(editor, &ScintillaNext::updateUi, this, &MainWindow::updateDocumentBasedUi);

    connect(editor, &ScintillaNext::lexerChanged, this, [this, editor]() {
        if (editor == currentEditor() && m_actionMarkdownPreview) {
            const bool isMarkdown = editor->languageName == QLatin1String("Markdown");
            m_actionMarkdownPreview->setEnabled(isMarkdown);
            if (!isMarkdown)
                m_actionMarkdownPreview->setChecked(false);
        }
    });

    // Watch for any zoom events (Ctrl+Scroll or pinch-to-zoom (Qt translates it as Ctrl+Scroll)) so that the event
    // can be handled before the ScintillaEditBase widget, so that it can be applied to all editors to keep zoom level equal.
    // NOTE: Need to install this on the scroll area's viewport, not on the editor widget itself...that was painful to learn
    editor->viewport()->installEventFilter(zoomEventWatcher);

    editor->setZoom(zoomLevel);

    editor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(editor, &ScintillaNext::customContextMenuRequested, this, [=, this](const QPoint &pos) {
        contextMenuPos = editor->positionFromPoint(pos.x(), pos.y());

        QStringList actionNames = {
            "Cut",
            "Copy",
            "Paste",
            "Delete",
            "",
            "SelectAll",
            "",
            "Base64Encode",
            "URLEncode",
            "",
            "Base64Decode",
            "URLDecode"
        };

        // If the entry exists in the settings, use that
        ApplicationSettings *settings = app->getSettings();
        if (settings->contains("Gui/EditorContextMenu")) {
            actionNames = settings->value("Gui/EditorContextMenu").toStringList();
        }

        // If the cursor is at a URL, prepend the action
        URLFinder *urlFinder = editor->findChild<URLFinder *>(QString(), Qt::FindDirectChildrenOnly);
        if (urlFinder && urlFinder->isEnabled() && urlFinder->isURL(contextMenuPos)) {
            actionNames.prepend("");
            actionNames.prepend("CopyURL");
        }

        auto menu = buildMenu(actionNames);
        menu->addSeparator();
        menu->addMenu(ui->menuMarkAllOccurrences);
        menu->addMenu(ui->menuClearMarks);

        // "Send to AI" — prepend when text is selected and an AI dock exists.
        // Skip for diff/history views and unsaved buffers — they have no real file path.
        const bool hasSelection = editor->selectionStart() != editor->selectionEnd();
        AiAgentDock *targetDock = nullptr;
        if (hasSelection && editor->isFile() && !app->getEditorManager()->isDiffView(editor)) {
            targetDock = activeAiDock();
        }
        if (targetDock) {
            auto *sendToAi = new QAction(tr("Send to AI"), menu);
            connect(sendToAi, &QAction::triggered, this, [this, editor]() {
                AiAgentDock *dock = activeAiDock();
                if (!dock) return;
                const sptr_t selStart = editor->selectionStart();
                const sptr_t selEnd = editor->selectionEnd();
                const int lineStart = static_cast<int>(editor->lineFromPosition(selStart)) + 1;
                const int lineEnd = static_cast<int>(editor->lineFromPosition(selEnd)) + 1;

                QString mention;
                if (editor->isFile()) {
                    QString filePath = editor->getFilePath();
                    const QString wsRoot = currentWorkspaceRoot();
                    if (!wsRoot.isEmpty() && filePath.startsWith(wsRoot)) {
                        filePath = filePath.mid(wsRoot.length());
                        if (filePath.startsWith(QLatin1Char('/')) || filePath.startsWith(QLatin1Char('\\')))
                            filePath = filePath.mid(1);
                    }
                    if (lineStart == lineEnd) {
                        mention = QStringLiteral("@%1#L%2").arg(filePath).arg(lineStart);
                    } else {
                        mention = QStringLiteral("@%1#L%2-%3").arg(filePath).arg(lineStart).arg(lineEnd);
                    }
                }

                QString text;
                if (!mention.isEmpty()) {
                    text = mention + QStringLiteral(" ");
                }
                dock->insertTextToInput(text);
                dock->setVisible(true);
                dock->raise();
            });
            menu->insertAction(menu->actions().isEmpty() ? nullptr : menu->actions().first(), sendToAi);
            menu->insertSeparator(menu->actions().at(1));
        }

        menu->popup(QCursor::pos());
    });

    // The editor has been entirely configured at this point, so add it to the docked editor
    dockedEditor->addEditor(editor);
}

void MainWindow::checkForUpdates(bool silent)
{
#ifdef Q_OS_WIN
    qInfo(Q_FUNC_INFO);

    QString url = "https://github.com/dail8859/NotepadNext/raw/master/updates.json";
    QSimpleUpdater::getInstance()->checkForUpdates(url);

    if (!silent) {
        connect(QSimpleUpdater::getInstance(), &QSimpleUpdater::checkingFinished, this, &MainWindow::checkForUpdatesFinished, Qt::UniqueConnection);
    }
    else {
        disconnect(QSimpleUpdater::getInstance(), &QSimpleUpdater::checkingFinished, this, &MainWindow::checkForUpdatesFinished);
    }


    app->getSettings()->setValue("App/LastUpdateCheck", QDateTime::currentDateTime());
#else
    Q_UNUSED(silent);
#endif
}

void MainWindow::checkForUpdatesFinished(const QString &url)
{
#ifdef Q_OS_WIN
    if (!QSimpleUpdater::getInstance()->getUpdateAvailable(url)) {
        QMessageBox::information(this, QString(), tr("No updates are available at this time."));
    }
#endif
}

void MainWindow::initUpdateCheck()
{
#ifdef Q_OS_WIN
#ifdef QT_DEBUG
    if (true) {
#else
    QSettings registry(QSettings::NativeFormat, QSettings::UserScope, QApplication::organizationName(), QApplication::applicationName());
    const bool autoUpdatesEnabled = registry.value("AutoUpdate", 0).toBool();
    qInfo("AutoUpdates: %d", autoUpdatesEnabled);

    if (autoUpdatesEnabled) {
#endif
        connect(ui->actionCheckForUpdates, &QAction::triggered, this, &MainWindow::checkForUpdates);

        // A bit after startup, see if we need to automatically check for an update
        QTimer::singleShot(15000, this, [this]() {
            ApplicationSettings settings;
            QDateTime dt = settings.value("App/LastUpdateCheck", QDateTime::currentDateTime()).toDateTime();

            if (dt.isValid()) {
                qInfo("Last checked for updates at: %s", qUtf8Printable(dt.toString()));

                if (dt.addDays(7) < QDateTime::currentDateTime()) {
                    checkForUpdates(true);
                }
            }
        });
    }
    else {
        ui->actionCheckForUpdates->setDisabled(true);
        ui->actionCheckForUpdates->setVisible(false);
    }
#else
    ui->actionCheckForUpdates->setDisabled(true);
    ui->actionCheckForUpdates->setVisible(false);
#endif
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    PROFILE_SCOPE("MainWindow::closeEvent");
    const SessionManager *sessionManager = app->getSessionManager();
    QVector<ScintillaNext *> e;

    {
        PROFILE_SCOPE("MainWindow::closeEvent.checkEditors");
        // Check all editors to see if the session manager will not handle it
        for (auto editor : editors()) {
            if (!sessionManager->willFileGetStoredInSession(editor)) {
                e.append(editor);
            }
        }

        if (!checkEditorsBeforeClose(e)) {
            event->ignore();
            return;
        }
    }

    if (terminalManager) {
        PROFILE_SCOPE("MainWindow::closeEvent.terminalShutdown");
        terminalManager->shutdown();
    }

    {
        PROFILE_SCOPE("MainWindow::closeEvent.aboutToClose");
        emit aboutToClose();
    }

    event->accept();

    {
        PROFILE_SCOPE("MainWindow::closeEvent.QMainWindowCloseEvent");
        QMainWindow::closeEvent(event);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    qInfo(Q_FUNC_INFO);

    // NOTE: for urls these can be dragged anywhere in the application, editor, tabs, menu
    // because the ScintillaNext editor ignores urls so they can be handled by the main
    // application
    // Text dragging within the editor object itself is handled by Scintilla, but if the text
    // is dragged to other parts (tabs, menu, etc) it will be handled by the application to
    // create a new editor from the text.

    // Accept urls and text
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
    else {
        event->ignore();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    qInfo(Q_FUNC_INFO);

    if (event->mimeData()->hasUrls()) {
        // Get the urls into a stringlist
        QStringList fileNames;
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                QFileInfo info(url.toLocalFile());

                if (info.exists()) {
                    if (info.isDir()) {
                        QDirIterator it(url.toLocalFile(), QDir::Files, QDirIterator::FollowSymlinks| QDirIterator::Subdirectories);
                        while (it.hasNext()) {
                            fileNames << it.next();
                        }
                    }
                    else {
                        fileNames.append(url.toLocalFile());
                    }
                }
            }
        }

        openFileList(fileNames);
        bringWindowToForeground();
        event->acceptProposedAction();
    }
    else if (event->mimeData()->hasText()) {
        if (event->source()) {
            // if it is from an editor, remove the text
            ScintillaNext *sn = qobject_cast<ScintillaNext *>(event->source());
            if (sn) {
                sn->replaceSel("");
            }
        }

        newFile();
        currentEditor()->setText(event->mimeData()->text().toLocal8Bit().constData());
        bringWindowToForeground();
        event->acceptProposedAction();
    }
    else {
        event->ignore();
    }
}

QMenu *MainWindow::buildMenu(const QStringList &actionNames)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    ActionUtils::populateActionContainer(menu, this, actionNames);

    return menu;
}

void MainWindow::attachAiAgentDock(AiAgentDock *dock, bool raise)
{
    AiAgentDock *existing = nullptr;
    const auto children = findChildren<AiAgentDock *>();
    for (auto *d : children) {
        if (d != dock) {
            existing = d;
            break;
        }
    }

    auto syncWorkspaceForDock = [this, dock]() {
        if (!isVisible() || !app->getSettings()->syncWorkspaceOnAiSwitch())
            return;
        const QString aiCwd = QDir::cleanPath(dock->workingDirectory());
        if (aiCwd.isEmpty())
            return;
        for (auto *ws : findChildren<FolderAsWorkspaceDock *>()) {
            if (QDir::cleanPath(ws->rootPath()) == aiCwd) {
                ws->setVisible(true);
                ws->raise();
                break;
            }
        }
    };

    connect(dock, &QDockWidget::visibilityChanged, this, [this, dock, syncWorkspaceForDock](bool visible) {
        if (visible) {
            m_activeAiDock = dock;
            dock->setActivityIndicator(false);
            syncWorkspaceForDock();
        }
    });

    connect(dock, &AiAgentDock::inputFocused, this, syncWorkspaceForDock);

    addDockWidget(AiAgentDock::defaultArea(), dock);
    DockMiddleClickCloser::install(dock);
    if (existing) {
        tabifyDockWidget(existing, dock);
    } else {
        resizeDocks({dock}, {600}, Qt::Horizontal);
    }
    dock->setVisible(true);
    if (raise) {
        dock->raise();
        m_activeAiDock = dock;
    }
}

AiAgentDock *MainWindow::activeAiDock() const
{
    if (m_activeAiDock)
        return m_activeAiDock.data();
    const auto docks = findChildren<AiAgentDock *>();
    return docks.isEmpty() ? nullptr : docks.first();
}

void MainWindow::tabBarRightClicked(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    // Focus on the correct tab
    dockedEditor->switchToEditor(editor);

    // Default actions
    QStringList actionNames{
        "Close",
        "CloseAllExceptActive",
        "CloseAllToLeft",
        "CloseAllToRight",
        "",
        "Save",
        "SaveAs",
        "Rename",
        "",
        "Reload",
        "",
#ifdef Q_OS_WIN
        "ShowInExplorer",
#endif
        // Directly below "Show in Explorer" per the feature request. Cross-platform,
        // so outside the Q_OS_WIN block; on Windows "Open Terminal in Folder" follows.
        "ShowInWorkspace",
#ifdef Q_OS_WIN
        "OpenTerminalInFolder",
#endif
        "",
        "CopyFullPath",
        "CopyFileName",
        "CopyFileDirectory"
    };

    // If the entry exists in the settings, use that
    ApplicationSettings *settings = app->getSettings();
    if (settings->contains("Gui/TabBarContextMenu")) {
        actionNames = settings->value("Gui/TabBarContextMenu").toStringList();
    }

    // Gate the workspace-reveal action using the SAME resolver the triggered
    // handler uses (gate == handler). `editor` is the clicked tab's editor; the
    // switchToEditor above already made it the currentEditor() the handler reads.
    {
        const QString filePath = editor->isFile() ? editor->getFilePath() : QString();
        const bool isFile = editor->isFile();
        ui->actionShowInWorkspace->setEnabled(resolveShowInWorkspaceDock(filePath, isFile) != nullptr);
    }

    auto *menu = buildMenu(actionNames);

    // "Send to AI" — when an AI dock exists and the editor has a file path.
    AiAgentDock *targetDock = nullptr;
    if (editor->isFile()) {
        targetDock = activeAiDock();
    }
    if (targetDock) {
        menu->addSeparator();
        auto *sendToAi = new QAction(tr("Send to AI"), menu);
        connect(sendToAi, &QAction::triggered, this, [this, editor]() {
            AiAgentDock *dock = activeAiDock();
            if (!dock) return;
            QString filePath = editor->getFilePath();
            const QString wsRoot = currentWorkspaceRoot();
            if (!wsRoot.isEmpty() && filePath.startsWith(wsRoot)) {
                filePath = filePath.mid(wsRoot.length());
                if (filePath.startsWith(QLatin1Char('/')) || filePath.startsWith(QLatin1Char('\\')))
                    filePath = filePath.mid(1);
            }
            const QString text = QStringLiteral("@%1 ").arg(filePath);
            dock->insertTextToInput(text);
            dock->setVisible(true);
            dock->raise();
        });
        menu->addAction(sendToAi);
    }

    menu->popup(QCursor::pos());
}

void MainWindow::languageMenuTriggered()
{
    const QAction *act = qobject_cast<QAction *>(sender());
    auto editor = currentEditor();
    QVariant v = act->data();

    setLanguage(editor, v.toString());
}

namespace {
// Hard cap on memoed workspace states. 50 covers extreme power users (typical
// is < 10). LRU eviction by lastUsedEpochMs keeps recently-visited workspaces.
constexpr int MAX_MEMOED_WORKSPACE_STATES = 50;
// On-disk schema version for each WorkspaceStates entry. Bump only on
// breaking layout changes; load code skips unknown versions silently.
constexpr int WORKSPACE_STATE_SCHEMA = 1;

void writeWorkspaceStatesArray(ApplicationSettings *settings,
                               const QHash<QString, WorkspaceStateSnapshot> &states)
{
    // Linearise + LRU-cap. Sort descending by lastUsedEpochMs; keep top N.
    QVector<WorkspaceStateSnapshot> entries;
    entries.reserve(states.size());
    for (auto it = states.cbegin(); it != states.cend(); ++it) {
        entries.append(it.value());
    }
    std::sort(entries.begin(), entries.end(),
              [](const WorkspaceStateSnapshot &a, const WorkspaceStateSnapshot &b) {
                  return a.lastUsedEpochMs > b.lastUsedEpochMs;
              });
    if (entries.size() > MAX_MEMOED_WORKSPACE_STATES) {
        entries.resize(MAX_MEMOED_WORKSPACE_STATES);
    }

    settings->beginGroup("FolderAsWorkspace");
    // Always clear the previous array before rewriting — partial overwrite
    // would leave dangling indices when the new size is smaller than the old.
    settings->remove("WorkspaceStates");
    settings->beginWriteArray("WorkspaceStates", entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        settings->setArrayIndex(i);
        const WorkspaceStateSnapshot &s = entries[i];
        settings->setValue("SchemaVersion", WORKSPACE_STATE_SCHEMA);
        settings->setValue("RootPath",     s.rootPath);
        settings->setValue("ActiveTab",    s.activeTabIndex);
        settings->setValue("CurrentItem",  s.currentItemPath);
        settings->setValue("LastUsed",     s.lastUsedEpochMs);

        // Nested per-path keys (P0000, P0001, ...) deliberately avoid the
        // QStringList CSV-escape path in QSettings INI: paths containing
        // commas, quotes, or unusual whitespace round-trip safely as scalars.
        settings->beginGroup("ExpandedFolders");
        settings->remove("");  // wipe stale entries from a prior, larger list
        settings->setValue("Count", s.expandedFolders.size());
        for (int j = 0; j < s.expandedFolders.size(); ++j) {
            settings->setValue(QStringLiteral("P%1").arg(j, 4, 10, QChar('0')),
                               s.expandedFolders[j]);
        }
        settings->endGroup();
    }
    settings->endArray();
    settings->endGroup();
}
} // namespace

void MainWindow::loadAllWorkspaceStates() const
{
    m_workspaceStateMemo.clear();

    ApplicationSettings *settings = app->getSettings();
    settings->beginGroup("FolderAsWorkspace");
    const int n = settings->beginReadArray("WorkspaceStates");
    for (int i = 0; i < n; ++i) {
        settings->setArrayIndex(i);

        const int ver = settings->value("SchemaVersion", 0).toInt();
        if (ver != WORKSPACE_STATE_SCHEMA) continue;  // forward-compat: skip unknown

        WorkspaceStateSnapshot s;
        s.rootPath = settings->value("RootPath").toString();
        if (s.rootPath.isEmpty()) continue;  // corrupted/partial: skip

        s.activeTabIndex  = settings->value("ActiveTab", 0).toInt();
        s.currentItemPath = settings->value("CurrentItem").toString();
        s.lastUsedEpochMs = settings->value("LastUsed", 0).toLongLong();

        settings->beginGroup("ExpandedFolders");
        const int count = settings->value("Count", 0).toInt();
        s.expandedFolders.reserve(count);
        for (int j = 0; j < count; ++j) {
            const QString p = settings->value(
                QStringLiteral("P%1").arg(j, 4, 10, QChar('0'))).toString();
            if (!p.isEmpty()) s.expandedFolders << p;
        }
        settings->endGroup();

        m_workspaceStateMemo.insert(QDir::cleanPath(s.rootPath), s);
    }
    settings->endArray();
    settings->endGroup();
}

void MainWindow::persistWorkspaceStatesMerged(const QVector<WorkspaceStateSnapshot> &live) const
{
    // Merge: live snapshots take precedence over the existing memo for
    // matching rootPath. Other memo entries (closed workspaces) survive.
    for (const WorkspaceStateSnapshot &s : live) {
        if (s.rootPath.isEmpty()) continue;
        m_workspaceStateMemo.insert(QDir::cleanPath(s.rootPath), s);
    }
    writeWorkspaceStatesArray(app->getSettings(), m_workspaceStateMemo);
}

void MainWindow::persistOneWorkspaceState(const WorkspaceStateSnapshot &snapshot) const
{
    if (snapshot.rootPath.isEmpty()) return;
    m_workspaceStateMemo.insert(QDir::cleanPath(snapshot.rootPath), snapshot);
    writeWorkspaceStatesArray(app->getSettings(), m_workspaceStateMemo);
}

void MainWindow::saveWorkspaceStatesOnly()
{
    // 60s autosave path: snapshot live docks and flush. Deliberately does NOT
    // touch MainWindow/geometry, MainWindow/windowState, Editor/ZoomLevel —
    // those are expensive serialisations that don't change often and are
    // covered by aboutToClose. We only rewrite the WorkspaceStates array.
    QVector<WorkspaceStateSnapshot> live;
    live.reserve(8);
    QStringList seen;
    for (const FolderAsWorkspaceDock *d : findChildren<FolderAsWorkspaceDock *>()) {
        const QString path = d->rootPath();
        if (path.isEmpty() || seen.contains(path)) continue;
        seen << path;
        live << d->captureState();
    }
    persistWorkspaceStatesMerged(live);
}

// --- Git Operation Wiring ---

void MainWindow::setupGitOperationMenu()
{
    // Connect operation manager signals to UI
    connect(m_gitOpMgr, &GitOperationManager::mergeConflicted, this,
            &MainWindow::showConflictListDock);
    connect(m_gitOpMgr, &GitOperationManager::rebaseConflicted, this,
            &MainWindow::showConflictListDock);

    connect(m_gitOpMgr, &GitOperationManager::mergeCompleted, this, [this](const QString &) {
        if (m_conflictListDock) m_conflictListDock->hide();
    });
    connect(m_gitOpMgr, &GitOperationManager::rebaseCompleted, this, [this](const QString &) {
        if (m_conflictListDock) m_conflictListDock->hide();
    });
    connect(m_gitOpMgr, &GitOperationManager::rebaseAborted, this, [this](const QString &) {
        if (m_conflictListDock) m_conflictListDock->hide();
    });

    connect(m_gitOpMgr, &GitOperationManager::conflictsUpdated, this,
        [this](const QString &repoPath, const ConflictEntries &entries) {
            if (m_conflictListDock) {
                m_conflictListDock->setConflicts(entries);
                m_conflictListDock->setOperationState(m_gitOpMgr->state(repoPath));
            }
        });

    connect(m_gitOpMgr, &GitOperationManager::operationStateChanged, this,
        [this](const QString &, GitOperationManager::OperationState state) {
            if (m_conflictListDock)
                m_conflictListDock->setOperationState(state);
        });

    connect(m_gitOpMgr, &GitOperationManager::interactiveRebaseRequested, this,
        [this](const QString &todoFilePath) {
            auto *dlg = new InteractiveRebaseDialog(todoFilePath, this);
            int result = dlg->exec();
            m_gitOpMgr->replyToEditor(result == QDialog::Accepted && dlg->wasAccepted());
            dlg->deleteLater();
        });

    connect(m_gitOpMgr, &GitOperationManager::commitMessageEditRequested, this,
        [this](const QString &) {
            // For now, auto-accept commit messages (use .git/MERGE_MSG as-is)
            m_gitOpMgr->replyToEditor(true);
        });
}

void MainWindow::showConflictListDock(const QString &repoPath)
{
    if (!m_conflictListDock) {
        m_conflictListDock = new ConflictListDock(m_gitOpMgr, this);
        addDockWidget(ConflictListDock::defaultArea(), m_conflictListDock);

        connect(m_conflictListDock, &ConflictListDock::openInMergeViewer, this,
                &MainWindow::openConflictMergeViewer);

        connect(m_conflictListDock, &ConflictListDock::continueRequested, this, [this]() {
            auto *dock = activeWorkspaceDock();
            if (!dock) return;
            auto *ctrl = dock->gitTabWidget() ? dock->gitTabWidget()->controller() : nullptr;
            if (!ctrl) return;
            auto state = m_gitOpMgr->state(ctrl->currentRepo());
            if (state == GitOperationManager::OperationState::MergeConflicted)
                m_gitOpMgr->commitMerge(ctrl);
            else
                m_gitOpMgr->continueRebase(ctrl);
        });

        connect(m_conflictListDock, &ConflictListDock::abortRequested, this, [this]() {
            auto *dock = activeWorkspaceDock();
            if (!dock) return;
            auto *ctrl = dock->gitTabWidget() ? dock->gitTabWidget()->controller() : nullptr;
            if (!ctrl) return;
            auto state = m_gitOpMgr->state(ctrl->currentRepo());
            if (state == GitOperationManager::OperationState::MergeConflicted)
                m_gitOpMgr->abortMerge(ctrl);
            else
                m_gitOpMgr->abortRebase(ctrl);
        });

        connect(m_conflictListDock, &ConflictListDock::skipRequested, this, [this]() {
            auto *dock = activeWorkspaceDock();
            if (!dock) return;
            auto *ctrl = dock->gitTabWidget() ? dock->gitTabWidget()->controller() : nullptr;
            if (!ctrl) return;
            m_gitOpMgr->skipRebase(ctrl);
        });

        connect(m_conflictListDock, &ConflictListDock::acceptOursRequested, this,
            [this](const QStringList &paths) {
                auto *dock = activeWorkspaceDock();
                if (!dock) return;
                auto *ctrl = dock->gitTabWidget() ? dock->gitTabWidget()->controller() : nullptr;
                if (!ctrl) return;
                m_gitOpMgr->acceptOurs(ctrl, paths);
            });

        connect(m_conflictListDock, &ConflictListDock::acceptTheirsRequested, this,
            [this](const QStringList &paths) {
                auto *dock = activeWorkspaceDock();
                if (!dock) return;
                auto *ctrl = dock->gitTabWidget() ? dock->gitTabWidget()->controller() : nullptr;
                if (!ctrl) return;
                m_gitOpMgr->acceptTheirs(ctrl, paths);
            });
    }

    auto entries = m_gitOpMgr->conflicts(repoPath);
    m_conflictListDock->setConflicts(entries);
    m_conflictListDock->setOperationState(m_gitOpMgr->state(repoPath));
    m_conflictListDock->setVisible(true);
    m_conflictListDock->raise();
}

void MainWindow::openConflictMergeViewer(const ConflictEntry &entry)
{
    // Check if a viewer for this file already exists
    const auto viewers = findChildren<ConflictMergeViewerDock *>();
    for (auto *v : viewers) {
        if (v->filePath() == entry.relPath) {
            v->setVisible(true);
            v->raise();
            return;
        }
    }

    auto *dock = activeWorkspaceDock();
    if (!dock) return;
    auto *ctrl = dock->gitTabWidget() ? dock->gitTabWidget()->controller() : nullptr;
    if (!ctrl) return;

    const QString repo = ctrl->currentRepo();
    const QString absPath = repo + QLatin1Char('/') + entry.relPath;

    // Load content from git index stages
    ConflictData data;
    data.filePath = entry.relPath;
    data.isReversed = (m_gitOpMgr->state(repo) == GitOperationManager::OperationState::RebaseSuspended);
    data.leftLabel = data.isReversed ? tr("Theirs (target)") : tr("Ours (current)");
    data.rightLabel = data.isReversed ? tr("Yours (rebased)") : tr("Theirs (incoming)");

    // Fetch stage content via git show :N:path
    auto fetchStage = [&](int stage) -> QByteArray {
        QProcess proc;
        proc.setWorkingDirectory(repo);
        proc.start(GitProcessRunner::gitExecutable(),
                   {QStringLiteral("show"),
                    QStringLiteral(":%1:%2").arg(stage).arg(entry.relPath)});
        proc.waitForFinished(5000);
        return proc.readAllStandardOutput();
    };

    data.baseContent = fetchStage(1);
    QByteArray oursRaw = fetchStage(2);
    QByteArray theirsRaw = fetchStage(3);

    if (data.isReversed) {
        data.oursContent = theirsRaw;
        data.theirsContent = oursRaw;
    } else {
        data.oursContent = oursRaw;
        data.theirsContent = theirsRaw;
    }

    auto *viewer = new ConflictMergeViewerDock(data, app->getEditorManager(), this);
    addDockWidget(ConflictMergeViewerDock::defaultArea(), viewer);

    // Tabify with existing viewers
    const auto existingViewers = findChildren<ConflictMergeViewerDock *>();
    for (auto *existing : existingViewers) {
        if (existing != viewer) {
            tabifyDockWidget(existing, viewer);
            break;
        }
    }
    viewer->setVisible(true);
    viewer->raise();

    connect(viewer, &ConflictMergeViewerDock::resolved, this,
        [this, ctrl](const QString &filePath, const QByteArray &content) {
            const QString repo = ctrl->currentRepo();
            const QString absPath = repo + QLatin1Char('/') + filePath;
            QFile f(absPath);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(content);
                f.close();
            }
            m_gitOpMgr->resolveFile(ctrl, filePath);
        });
}
