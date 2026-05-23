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
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QDirIterator>
#include <QProcess>
#include <QScreen>
#include <QFontDatabase>

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

#include "TerminalManager.h"
#include "TerminalCwdResolver.h"

#include "FindReplaceDialog.h"
#include "MacroRunDialog.h"
#include "MacroSaveDialog.h"
#include "PreferencesDialog.h"
#include "AcpAgentSettingsDialog.h"
#include "AcpAgentRegistry.h"
#include "AcpAgentManager.h"
#include "AiAgentDock.h"
#include "ColumnEditorDialog.h"

#include "TabsQuickActionsBar.h"

#include "QuickFindWidget.h"

#include "EditorPrintPreviewRenderer.h"
#include "MacroEditorDialog.h"

#include "ZoomEventWatcher.h"
#include "FileDialogHelpers.h"

#include "HtmlConverter.h"
#include "RtfConverter.h"

#include "FadingIndicator.h"

#include "ActionUtils.h"

#include "CrashContext.h"
#include "CrashHandler.h"
#include "ProfileScope.h"

#include <QEvent>
#include <QActionEvent>
#include <QPointer>
#include "DockWidget.h"

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

    defaultDirectoryManager = new DefaultDirectoryManager(this, app->getSettings(), this);

    connect(this, &MainWindow::aboutToClose, this, &MainWindow::saveSettings);

    // Create and set up the connections to the docked editor
    dockedEditor = new DockedEditor(this);
    connect(dockedEditor, &DockedEditor::editorCloseRequested, this, [=](ScintillaNext *editor) { closeFile(editor); });
    connect(dockedEditor, &DockedEditor::editorActivated, this, &MainWindow::activateEditor);
    connect(dockedEditor, &DockedEditor::contextMenuRequestedForEditor, this, &MainWindow::tabBarRightClicked);
    connect(dockedEditor, &DockedEditor::titleBarDoubleClicked, this, &MainWindow::newFile);

    // Set up the menus
    {
    PROFILE_SCOPE("MainWindow::ctor.actionWiring");
    connect(ui->actionNew, &QAction::triggered, this, &MainWindow::newFile);
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::openFileDialog);
    connect(ui->actionReload, &QAction::triggered, this, &MainWindow::reloadFile);
    connect(ui->actionClose, &QAction::triggered, this, &MainWindow::closeCurrentFile);
    connect(ui->actionCloseAll, &QAction::triggered, this, &MainWindow::closeAllFiles);
    connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);

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

    connect(ui->actionExportHtml, &QAction::triggered, this, [=]() {
        HtmlConverter html(currentEditor());
        exportAsFormat(&html, QStringLiteral("HTML files (*.html)"));
    });

    connect(ui->actionExportRtf, &QAction::triggered, this, [=]() {
        RtfConverter rtf(currentEditor());
        exportAsFormat(&rtf, QStringLiteral("RTF Files (*.rtf)"));
    });

    connect(ui->actionPrint, &QAction::triggered, this, &MainWindow::print);

    connect(ui->actionToggleSingleLineComment, &QAction::triggered, this, [=]() { currentEditor()->toggleCommentSelection(); });
    connect(ui->actionSingleLineComment, &QAction::triggered, this, [=]() { currentEditor()->commentLineSelection(); });
    connect(ui->actionSingleLineUncomment, &QAction::triggered, this, [=]() { currentEditor()->uncommentLineSelection(); });

    connect(ui->actionBase64Encode,&QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        const QByteArray selection = currentEditor()->getSelText();
        editor->replaceSel(selection.toBase64().constData());
    });
    connect(ui->actionURLEncode,&QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        const QByteArray selection = currentEditor()->getSelText();
        editor->replaceSel(selection.toPercentEncoding().constData());
    });
    connect(ui->actionBase64Decode,&QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        const QByteArray selection = editor->getSelText();
        if (auto result = QByteArray::fromBase64Encoding(selection)) {
            editor->replaceSel((*result).constData());
        }
    });
    connect(ui->actionURLDecode,&QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        const QByteArray selection = editor->getSelText();
        editor->replaceSel(QByteArray::fromPercentEncoding(selection).constData());
    });
    connect(ui->actionCopyURL, &QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        URLFinder *urlFinder = editor->findChild<URLFinder *>(QString(), Qt::FindDirectChildrenOnly);
        if (urlFinder && urlFinder->isEnabled()) {
            urlFinder->copyURLToClipboard(contextMenuPos);
        }
    });

    connect(ui->actionClearRecentFilesList, &QAction::triggered, app->getRecentFilesListManager(), &RecentFilesListManager::clear);

    connect(ui->actionMoveToTrash, &QAction::triggered, this, &MainWindow::moveCurrentFileToTrash);

    RecentFilesListMenuBuilder *recentFileListMenuBuilder = new RecentFilesListMenuBuilder(app->getRecentFilesListManager());
    connect(ui->menuRecentFiles, &QMenu::aboutToShow, this, [=]() {
        // NOTE: its unfortunate that this has to be hard coded, but there's no way
        // to easily determine what should or shouldn't be there
        while (ui->menuRecentFiles->actions().size() > 4) {
            delete ui->menuRecentFiles->actions().takeLast();
        }

        recentFileListMenuBuilder->populateMenu(ui->menuRecentFiles);
    });

    connect(ui->actionRestoreRecentlyClosedFile, &QAction::triggered, this, [=]() {
        if (app->getRecentFilesListManager()->count() > 0) {
            openFileList(QStringList() << app->getRecentFilesListManager()->mostRecentFile());
        }
    });

    connect(ui->actionOpenAllRecentFiles, &QAction::triggered, this, [=]() {
        openFileList(app->getRecentFilesListManager()->fileList());
    });

    connect(recentFileListMenuBuilder, &RecentFilesListMenuBuilder::fileOpenRequest, this, &MainWindow::openFile);

    QActionGroup *eolActionGroup = new QActionGroup(this);
    eolActionGroup->addAction(ui->actionWindows);
    eolActionGroup->addAction(ui->actionUnix);
    eolActionGroup->addAction(ui->actionMacintosh);

    connect(ui->actionWindows, &QAction::triggered, this, [=]() { convertEOLs(SC_EOL_CRLF); });
    connect(ui->actionUnix, &QAction::triggered, this, [=]() { convertEOLs(SC_EOL_LF); });
    connect(ui->actionMacintosh, &QAction::triggered, this, [=]() { convertEOLs(SC_EOL_CR); });

    connect(ui->actionUpperCase, &QAction::triggered, this, [=]() { currentEditor()->upperCase(); });
    connect(ui->actionLowerCase, &QAction::triggered, this, [=]() { currentEditor()->lowerCase(); });

    connect(ui->actionDuplicateCurrentLine, &QAction::triggered, this, [=]() { currentEditor()->lineDuplicate(); });
    connect(ui->actionMoveSelectedLinesUp, &QAction::triggered, this, [=]() { currentEditor()->moveSelectedLinesUp(); });
    connect(ui->actionMoveSelectedLinesDown, &QAction::triggered, this, [=]() { currentEditor()->moveSelectedLinesDown(); });
    connect(ui->actionSplitLines, &QAction::triggered, this, [=]() {
        currentEditor()->targetFromSelection();
        currentEditor()->linesSplit(0);
    });
    connect(ui->actionJoinLines, &QAction::triggered, this, [=]()  {
        currentEditor()->targetFromSelection();
        currentEditor()->linesJoin();
    });
    connect(ui->actionRemoveEmptyLines, &QAction::triggered, this, [=]() {
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
    connect(ui->actionRemoveDuplicateLines, &QAction::triggered, this, [=]() {
        currentEditor()->removeDuplicateLines();
    });
    connect(ui->actionRemoveConsecutiveDuplicateLines, &QAction::triggered, this, [=]() {
        currentEditor()->removeConsecutiveDuplicateLines();
    });
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

    connect(ui->actionColumnMode, &QAction::triggered, this, [=]() {
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
    connect(ui->actionCut, &QAction::triggered, this, [=]() { currentEditor()->cutAllowLine(); });
    connect(ui->actionCopy, &QAction::triggered, this, [=]() { currentEditor()->copyAllowLine(); });
    connect(ui->actionDelete, &QAction::triggered, this, [=]() { currentEditor()->clear(); });
    connect(ui->actionPaste, &QAction::triggered, this, [=]() { currentEditor()->paste(); });
    connect(ui->actionSelectAll, &QAction::triggered, this, [=]() { currentEditor()->selectAll(); });
    connect(ui->actionSelectNext, &QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();

        editor->setSearchFlags(SCFIND_NONE);
        editor->targetWholeDocument();
        editor->multipleSelectAddNext();
    });
    connect(ui->actionCopyFullPath, &QAction::triggered, this, [=]() {
        auto editor = currentEditor();
        if (editor->isFile()) {
            QApplication::clipboard()->setText(editor->getFilePath());
        }
    });
    connect(ui->actionCopyFileName, &QAction::triggered, this, [=]() {
        QApplication::clipboard()->setText(currentEditor()->getName());
    });
    connect(ui->actionCopyFileDirectory, &QAction::triggered, this, [=]() {
        auto editor = currentEditor();
        if (editor->isFile()) {
            QApplication::clipboard()->setText(editor->getPath());
        }
    });

    connect(ui->actionCopyAsHtml, &QAction::triggered, this, [=]() {
        HtmlConverter html(currentEditor());
        copyAsFormat(&html, "text/html");
    });

    connect(ui->actionCopyAsRtf, &QAction::triggered, this, [=]() {
        RtfConverter rtf(currentEditor());
        copyAsFormat(&rtf, "Rich Text Format");
    });

    connect(ui->actionIncreaseIndent, &QAction::triggered, this, [=]() { currentEditor()->tab(); });
    connect(ui->actionDecreaseIndent, &QAction::triggered, this, [=]() { currentEditor()->backTab(); });

    addAction(ui->actionToggleOverType);
    connect(ui->actionToggleOverType, &QAction::triggered, this, [=]() {
        currentEditor()->editToggleOvertype();
        ui->statusBar->refresh(currentEditor());
    });

    SearchResultsDock *srDock = new SearchResultsDock(this);
    addDockWidget(Qt::BottomDockWidgetArea, srDock);
    DockMiddleClickCloser::install(srDock);
    srDock->toggleViewAction()->setShortcut(Qt::Key_F7);
    ui->menuView->addAction(srDock->toggleViewAction());

    connect(srDock, &SearchResultsDock::searchResultActivated, this, [=](ScintillaNext *editor, int lineNumber, int startPositionFromBeginning, int endPositionFromBeginning) {
        dockedEditor->switchToEditor(editor);

        int linePos = editor->positionFromLine(lineNumber);
        editor->goToRange({linePos + startPositionFromBeginning, linePos + endPositionFromBeginning});
        editor->verticalCentreCaret();

        editor->grabFocus();
    });

    connect(ui->actionFind, &QAction::triggered, this, [=]() {
        showFindReplaceDialog(FindReplaceDialog::FIND_TAB);
    });

    connect(ui->actionFindNext, &QAction::triggered, this, [=]() {
        FindReplaceDialog *f = findChild<FindReplaceDialog *>(QString(), Qt::FindDirectChildrenOnly);

        if (f) {
            f->performNextSearch();
        }
    });

    connect(ui->actionFindPrevious, &QAction::triggered, this, [=]() {
        FindReplaceDialog *f = findChild<FindReplaceDialog *>(QString(), Qt::FindDirectChildrenOnly);

        if (f) {
            f->performPrevSearch();
        }
    });

    connect(ui->actionQuickFind, &QAction::triggered, this, [=]() {
        QuickFindWidget *quickFind = findChild<QuickFindWidget *>(QString(), Qt::FindDirectChildrenOnly);

        if (quickFind == Q_NULLPTR) {
            quickFind = new QuickFindWidget(this);
        }

        quickFind->setEditor(currentEditor());
        quickFind->setFocus();
        quickFind->show();
    });

    connect(ui->actionReplace, &QAction::triggered, this, [=]() {
        showFindReplaceDialog(FindReplaceDialog::REPLACE_TAB);
    });

    connect(ui->actionGoToLine, &QAction::triggered, this, [=]() {
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

    auto mark_callback = [=]() {
        MarkerAppDecorator *markerAppDecorator = app->findChild<MarkerAppDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (markerAppDecorator && markerAppDecorator->isEnabled()) {
            if (sender()->property("MarkerNumber").isValid()) {
                ScintillaNext *editor = currentEditor();
                markerAppDecorator->mark(editor, sender()->property("MarkerNumber").toInt());
            }
        }
    };

    auto clear_mark_callback = [=]() {
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

    connect(ui->actionClearAllStyles, &QAction::triggered, this, [=]() {
        MarkerAppDecorator *markerAppDecorator = app->findChild<MarkerAppDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (markerAppDecorator && markerAppDecorator->isEnabled()) {
            markerAppDecorator->clearAll(currentEditor());
        }
    });

    connect(ui->actionToggleBookmark, &QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            editor->forEachLineInSelection(editor->mainSelection(), [&](int line) {
                bookMarkDecorator->toggleBookmark(line);
            });
        }
    });

    connect(ui->actionNextBookmark, &QAction::triggered, this, [=]() {
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

    connect(ui->actionClearBookmarks, &QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            bookMarkDecorator->clearAllBookmarks();
        }
    });

    connect(ui->actionInvertBookmarks, &QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            for (int line = 0; line < editor->lineCount(); line++) {
                bookMarkDecorator->toggleBookmark(line);
            }
        }
    });

    connect(ui->actionPreviousBookmark, &QAction::triggered, this, [=]() {
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

    connect(ui->actionCutBookmarkedLines, &QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            QString s = bookMarkDecorator->cutBookMarkedLines();

            if (!s.isEmpty()) {
                QApplication::clipboard()->setText(s);
            }
        }
    });

    connect(ui->actionCopyBookmarkedLines, &QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            QString s = bookMarkDecorator->copyBookMarkedLines();

            if (!s.isEmpty()) {
                QApplication::clipboard()->setText(s);
            }
        }
    });

    connect(ui->actionDeleteBookmarkedLines, &QAction::triggered, this, [=]() {
        ScintillaNext *editor = currentEditor();
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            bookMarkDecorator->deleteBookMarkedLines();
        }
    });


    // The action needs added to the window so it can be triggered via the keyboard
    addAction(ui->actionNextTab);
    ui->actionNextTab->setShortcuts(ui->actionNextTab->shortcuts() << QKeySequence(Qt::CTRL | Qt::Key_PageDown));
    connect(ui->actionNextTab, &QAction::triggered, this, [=]() {
        int index = dockedEditor->currentDockArea()->currentIndex();
        int total = dockedEditor->currentDockArea()->dockWidgetsCount();

        index++;
        dockedEditor->currentDockArea()->setCurrentIndex(index < total ? index : 0);
    });

    // The action needs added to the window so it can be triggered via the keyboard
    addAction(ui->actionPreviousTab);
    ui->actionPreviousTab->setShortcuts(ui->actionPreviousTab->shortcuts() << QKeySequence(Qt::CTRL | Qt::Key_PageUp));
    connect(ui->actionPreviousTab, &QAction::triggered, this, [=]() {
        int index = dockedEditor->currentDockArea()->currentIndex();
        int total = dockedEditor->currentDockArea()->dockWidgetsCount();

        index--;
        dockedEditor->currentDockArea()->setCurrentIndex(index >= 0 ? index : total - 1);
    });

    ui->pushExitFullScreen->setParent(this); // This is important
    ui->pushExitFullScreen->setVisible(false);
    connect(ui->pushExitFullScreen, &QPushButton::clicked, ui->actionFullScreen, &QAction::trigger);
    connect(ui->actionFullScreen, &QAction::triggered, this, [=](bool b) {
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
    connect(ui->actionShowWhitespace, &QAction::toggled, this, [=](bool b) {
        ui->actionShowAllCharacters->setChecked(b && ui->actionShowEndofLine->isChecked());
    });

    // Show EOL
    ui->actionShowEndofLine->setChecked(app->getSettings()->showEndOfLine());
    connect(app->getSettings(), &ApplicationSettings::showEndOfLineChanged, ui->actionShowEndofLine, &QAction::setChecked);
    connect(ui->actionShowEndofLine, &QAction::toggled, app->getSettings(), &ApplicationSettings::setShowEndOfLine);
    // Update the "Show All Character" action
    connect(ui->actionShowEndofLine, &QAction::toggled, this, [=](bool b) {
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

    // Zooming controls all editors simulaneously
    connect(ui->actionZoomIn, &QAction::triggered, this, [=]() {
        for (ScintillaNext *editor : editors()) {
            editor->zoomIn();
        }
        zoomLevel = currentEditor()->zoom();

        showEditorZoomLevelIndicator();
    });
    connect(ui->actionZoomOut, &QAction::triggered, this, [=]() {
        for (ScintillaNext *editor : editors()) {
            editor->zoomOut();
        }
        zoomLevel = currentEditor()->zoom();

        showEditorZoomLevelIndicator();
    });
    connect(ui->actionZoomReset, &QAction::triggered, this, [=]() {
        for (ScintillaNext *editor : editors()) {
            editor->setZoom(0);
        }
        zoomLevel = 0;

        showEditorZoomLevelIndicator();
    });

    // Zoom watcher has detected a zoom event, so just trigger the UI action
    connect(zoomEventWatcher, &ZoomEventWatcher::zoomIn, ui->actionZoomIn, &QAction::trigger);
    connect(zoomEventWatcher, &ZoomEventWatcher::zoomOut, ui->actionZoomOut, &QAction::trigger);

    connect(ui->actionFoldAll, &QAction::triggered, this, [=]() { currentEditor()->foldAll(SC_FOLDACTION_CONTRACT | SC_FOLDACTION_CONTRACT_EVERY_LEVEL); });
    connect(ui->actionUnfoldAll, &QAction::triggered, this, [=]() { currentEditor()->foldAll(SC_FOLDACTION_EXPAND | SC_FOLDACTION_CONTRACT_EVERY_LEVEL); });

    connect(ui->actionFoldLevel1, &QAction::triggered, this, [=]() { currentEditor()->foldAllLevels(0); });
    connect(ui->actionFoldLevel2, &QAction::triggered, this, [=]() { currentEditor()->foldAllLevels(1); });
    connect(ui->actionFoldLevel3, &QAction::triggered, this, [=]() { currentEditor()->foldAllLevels(2); });
    connect(ui->actionFoldLevel4, &QAction::triggered, this, [=]() { currentEditor()->foldAllLevels(3); });
    connect(ui->actionFoldLevel5, &QAction::triggered, this, [=]() { currentEditor()->foldAllLevels(4); });
    connect(ui->actionFoldLevel6, &QAction::triggered, this, [=]() { currentEditor()->foldAllLevels(5); });
    connect(ui->actionFoldLevel7, &QAction::triggered, this, [=]() { currentEditor()->foldAllLevels(6); });
    connect(ui->actionFoldLevel8, &QAction::triggered, this, [=]() { currentEditor()->foldAllLevels(7); });
    connect(ui->actionFoldLevel9, &QAction::triggered, this, [=]() { currentEditor()->foldAllLevels(8); });

    connect(ui->actionUnfoldLevel1, &QAction::triggered, this, [=]() { currentEditor()->unFoldAllLevels(0); });
    connect(ui->actionUnfoldLevel2, &QAction::triggered, this, [=]() { currentEditor()->unFoldAllLevels(1); });
    connect(ui->actionUnfoldLevel3, &QAction::triggered, this, [=]() { currentEditor()->unFoldAllLevels(2); });
    connect(ui->actionUnfoldLevel4, &QAction::triggered, this, [=]() { currentEditor()->unFoldAllLevels(3); });
    connect(ui->actionUnfoldLevel5, &QAction::triggered, this, [=]() { currentEditor()->unFoldAllLevels(4); });
    connect(ui->actionUnfoldLevel6, &QAction::triggered, this, [=]() { currentEditor()->unFoldAllLevels(5); });
    connect(ui->actionUnfoldLevel7, &QAction::triggered, this, [=]() { currentEditor()->unFoldAllLevels(6); });
    connect(ui->actionUnfoldLevel8, &QAction::triggered, this, [=]() { currentEditor()->unFoldAllLevels(7); });
    connect(ui->actionUnfoldLevel9, &QAction::triggered, this, [=]() { currentEditor()->unFoldAllLevels(8); });

    languageActionGroup = new QActionGroup(this);
    languageActionGroup->setExclusive(true);

    connect(ui->actionPreferences, &QAction::triggered, this, [=] {
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
            dlg = new AcpAgentSettingsDialog(registry, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
        }

        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });

    // The macro manager has already loaded any saved macros, so it might have some already
    ui->actionRunMacroMultipleTimes->setEnabled(macroManager.availableMacros().size() > 0);
    ui->actionEditMacros->setEnabled(macroManager.availableMacros().size() > 0);

    connect(ui->actionMacroRecording, &QAction::triggered, this, [=](bool b) {
        if (b) {
            macroManager.startRecording(currentEditor());
        }
        else {
            macroManager.stopRecording();
        }
    });

    connect(&macroManager, &MacroManager::recordingStarted, this, [=]() {
        ui->actionMacroRecording->setText(tr("Stop Recording"));

        // A macro is being recorded so disable some macro options
        ui->actionPlayback->setEnabled(false);
        ui->actionRunMacroMultipleTimes->setEnabled(false);
        ui->actionSaveCurrentRecordedMacro->setEnabled(false);
    });

    connect(&macroManager, &MacroManager::recordingStopped, this, [=]() {
        ui->actionMacroRecording->setText(tr("Start Recording"));

        // Only enable these if the macro manager recorded a valid macro
        ui->actionPlayback->setEnabled(macroManager.hasCurrentUnsavedMacro());
        ui->actionSaveCurrentRecordedMacro->setEnabled(macroManager.hasCurrentUnsavedMacro());

        // The macro manager might have other macros
        ui->actionRunMacroMultipleTimes->setEnabled(macroManager.availableMacros().size() > 0 || macroManager.hasCurrentUnsavedMacro());
    });

    connect(ui->actionPlayback, &QAction::triggered, this, [=]() {
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

    connect(ui->actionRunMacroMultipleTimes, &QAction::triggered, this, [=]() {
        MacroRunDialog *macroRunDialog = findChild<MacroRunDialog *>(QString(), Qt::FindDirectChildrenOnly);

        if (macroRunDialog == Q_NULLPTR) {
            macroRunDialog = new MacroRunDialog(this, &macroManager);

            connect(macroRunDialog, &MacroRunDialog::execute, this, [=](Macro *macro, int times) {
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

    connect(ui->actionEditMacros, &QAction::triggered, this, [=]() {
        MacroEditorDialog med(this, &macroManager);

        med.show();
        med.raise();
        med.activateWindow();

        med.exec();

        ui->actionEditMacros->setEnabled(macroManager.availableMacros().size() > 0);
    });

    connect(ui->menuMacro, &QMenu::aboutToShow, this, [=]() {
        // NOTE: its unfortunate that this has to be hard coded, but there's no way
        // to easily determine what should or shouldn't be there
        while (ui->menuMacro->actions().size() > 6) {
            delete ui->menuMacro->actions().takeLast();
        }

        for (const Macro *m : macroManager.availableMacros()) {
            ui->menuMacro->addAction(m->getName(), [=]() { m->replay(currentEditor()); });
        }
    });

    ui->actionAboutQt->setIcon(QPixmap(QLatin1String(":/qt-project.org/qmessagebox/images/qtlogo-64.png")));
    connect(ui->actionAboutQt, &QAction::triggered, &QApplication::aboutQt);

    ui->actionAboutNotepadNext->setShortcut(QKeySequence::HelpContents);
    connect(ui->actionAboutNotepadNext, &QAction::triggered, this, [=]() {
        QMessageBox::about(this, QString(),
                            QStringLiteral("<h3>%1 v%2 %3</h3>"
                                    "<p>%4</p>"
                                    "<p><a href=\"https://github.com/nullmastermind/NotepadAI\">NotepadAI Home Page</a></p>"
                                    R"(<p>This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.</p> <p>This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.</p> <p>You should have received a copy of the GNU General Public License along with this program. If not, see &lt;<a href="https://www.gnu.org/licenses/">https://www.gnu.org/licenses/</a>&gt;.</p>)")
                                .arg(QApplication::applicationDisplayName(), APP_VERSION, APP_DISTRIBUTION, QStringLiteral(APP_COPYRIGHT).toHtmlEscaped()));
    });

    connect(ui->actionDebugInfo, &QAction::triggered, this, [=]() {
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
    connect(ui->actionShowInExplorer, &QAction::triggered, this, [=]() {
        QString filePath = QDir::toNativeSeparators(currentEditor()->getFileInfo().canonicalFilePath());
        QStringList arguments = {"/select,", filePath};
        QProcess::startDetached("explorer", arguments);
    });

    QString terminalName = app->getSettings()->value("App/TerminalName", "Command Prompt").toString();
    ui->actionOpenTerminalHere->setText(ui->actionOpenTerminalHere->text().arg(terminalName));

    connect(ui->actionOpenTerminalHere, &QAction::triggered, this, [=]() {
        QString command = app->getSettings()->value("App/TerminalCommand", "cmd").toString();
        QString filePath = QDir::toNativeSeparators(currentEditor()->getFileInfo().dir().canonicalPath());
        QStringList arguments = {"/c", "start", "/d", filePath, command};
        QProcess::startDetached("cmd", arguments);
    });
#endif

    } // MainWindow::ctor.actionWiring

    {
    PROFILE_SCOPE("MainWindow::ctor.inspectorAndDocks");
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

    FolderAsWorkspaceDock *fawDock = new FolderAsWorkspaceDock(this);
    fawDock->hide();
    addDockWidget(Qt::LeftDockWidgetArea, fawDock);
    DockMiddleClickCloser::install(fawDock);
    ui->menuView->addAction(fawDock->toggleViewAction());
    connect(fawDock, &FolderAsWorkspaceDock::fileDoubleClicked, this, &MainWindow::openFile);
    wireWorkspaceGitSignals(fawDock);
    registerWorkspaceDock(fawDock);

    FileListDock *fileListDock = new FileListDock(this);
    fileListDock->hide();
    addDockWidget(Qt::LeftDockWidgetArea, fileListDock);
    DockMiddleClickCloser::install(fileListDock);
    ui->menuView->addAction(fileListDock->toggleViewAction());

    connect(app->getSettings(), &ApplicationSettings::showMenuBarChanged, this, [=](bool showMenuBar) {
        // Don't 'hide' it, else the actions won't be enabled
        ui->menuBar->setMaximumHeight(showMenuBar ? QWIDGETSIZE_MAX : 0);
    });
    connect(app->getSettings(), &ApplicationSettings::showToolBarChanged, ui->mainToolBar, &QToolBar::setVisible);
    connect(app->getSettings(), &ApplicationSettings::showStatusBarChanged, ui->statusBar, &QStatusBar::setVisible);
    connect(ui->statusBar, &EditorInfoStatusBar::customContextMenuRequestedForEOLLabel, this, [=](const QPoint &pos){
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
        ui->actionOpenAiAgentInWorkspace->setEnabled(TerminalCwdResolver::canOpenInWorkspace(workspaceRoot));
        ui->actionOpenAiAgentInFolder->setEnabled(TerminalCwdResolver::canOpenInFolder(activeFilePath, activeIsFile, workspaceRoot));
    });

    connect(ui->actionOpenAiAgentInWorkspace, &QAction::triggered, this, [this]() {
        const QString cwd = TerminalCwdResolver::resolveWorkspace(currentWorkspaceRoot());
        if (cwd.isEmpty()) return;

        AcpAgentManager *manager = this->app->getAiAgentManager();
        if (!manager) return;
        AiAgentDock *dock = manager->openAgent(manager->registry()->defaultAgentId(), cwd);
        if (dock) {
            attachAiAgentDock(dock);
        }
    });

    connect(ui->actionOpenAiAgentInFolder, &QAction::triggered, this, [this]() {
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
        if (cwd.isEmpty()) return;

        AcpAgentManager *manager = this->app->getAiAgentManager();
        if (!manager) return;
        AiAgentDock *dock = manager->openAgent(manager->registry()->defaultAgentId(), cwd);
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
            const QString key = language_names[j];
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

    app->getRecentWorkspacesListManager()->addFile(dir);

    // If this workspace is already open in some dock, just focus it rather
    // than spawning a duplicate tab.
    const QString cleaned = QDir::cleanPath(dir);
    const auto existing = findChildren<FolderAsWorkspaceDock *>();
    for (FolderAsWorkspaceDock *d : existing) {
        if (QDir::cleanPath(d->rootPath()) == cleaned) {
            d->setVisible(true);
            d->raise();
            m_activeWorkspace = d;
            CrashContext::setActiveWorkspaceRoot(currentWorkspaceRoot());
            if (showGitTab) d->showGitTab();
            return;
        }
    }

    // Reuse the initial (always-present) workspace dock when it has no path yet
    // so the first open doesn't sit alongside an empty ghost tab. Any subsequent
    // open creates a new tabified dock so the user can keep multiple workspaces
    // in one window.
    FolderAsWorkspaceDock *vacant = nullptr;
    FolderAsWorkspaceDock *anchor = nullptr;
    for (FolderAsWorkspaceDock *d : existing) {
        if (!anchor && !d->rootPath().isEmpty()) anchor = d;
        if (!vacant && d->rootPath().isEmpty()) vacant = d;
    }

    // Pull memoed UI state (tab/expanded/current) for this path, if any. The
    // lookup is O(1) on a hash that was populated once by restoreOpenWorkspaces
    // via loadAllWorkspaceStates. For paths opened mid-session (file menu /
    // CLI / drag-drop) the hash is also valid because saveSettings keeps it in
    // sync via persistWorkspaceStatesMerged on every clean exit.
    const WorkspaceStateSnapshot savedState =
        m_workspaceStateMemo.value(cleaned, WorkspaceStateSnapshot{});

    if (vacant && !anchor) {
        // applySavedTreeState must precede setRootPath so the dock's
        // directoryLoaded handler sees pendingExpansion populated when the
        // first model load completes.
        vacant->applySavedTreeState(savedState);
        vacant->setRootPath(dir);
        vacant->setVisible(true);
        vacant->raise();
        m_activeWorkspace = vacant;
        CrashContext::setActiveWorkspaceRoot(currentWorkspaceRoot());
        if (showGitTab) vacant->showGitTab();
        return;
    }

    // Default ctor + explicit setRootPath sequence (rather than the 2-arg ctor
    // that bakes setRootPath in) so applySavedTreeState lands between them.
    auto *dock = new FolderAsWorkspaceDock(this);
    static int extraIdx = 0;
    // Counter-based name is stable across sessions for the steady-state spawn order
    // (initial dock takes Workspaces[0], extras take Workspaces[1..N] in order). Known
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
    wireWorkspaceGitSignals(dock);
    registerWorkspaceDock(dock);

    if (anchor) {
        tabifyDockWidget(anchor, dock);
    }

    dock->applySavedTreeState(savedState);
    dock->setRootPath(dir);
    dock->setVisible(true);
    dock->raise();
    m_activeWorkspace = dock;
    CrashContext::setActiveWorkspaceRoot(currentWorkspaceRoot());
    if (showGitTab) dock->showGitTab();
}

void MainWindow::registerWorkspaceDock(FolderAsWorkspaceDock *dock)
{
    // The currently-raised tab in a tabified group is the one the user is
    // looking at, so use visibilityChanged(true) as the active-workspace signal.
    connect(dock, &QDockWidget::visibilityChanged, this, [this, dock](bool visible) {
        if (visible && !dock->rootPath().isEmpty()) {
            m_activeWorkspace = dock;
            CrashContext::setActiveWorkspaceRoot(currentWorkspaceRoot());
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
        // The dirty bit no longer needs flushing for THIS dock — its state
        // just went to disk. Leave the bit for other docks that may still
        // have unflushed changes.
    });

    // User-driven tree / tab changes mark the workspace state dirty so the
    // 60s autosave timer flushes them (defense vs crash mid-session).
    connect(dock, &FolderAsWorkspaceDock::stateDirty, this,
            [this]() { m_workspaceStateDirty = true; });
}

void MainWindow::wireWorkspaceGitSignals(FolderAsWorkspaceDock *dock)
{
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
        // openFolderAsWorkspacePath deduplicates against existing docks (including
        // the initial one, which loaded its path from the legacy singular setting).
        openFolderAsWorkspacePath(path);
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
            m_activeWorkspace = d;
            CrashContext::setActiveWorkspaceRoot(currentWorkspaceRoot());
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

QString MainWindow::currentWorkspaceRoot() const
{
    FolderAsWorkspaceDock *dock = activeWorkspaceDock();
    return dock ? dock->rootPath() : QString();
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
    closeFile(currentEditor());
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

    return;
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
        DWORD procId = GetWindowThreadProcessId(hCurWnd, NULL);

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

    restoreGeometry(settings->value("MainWindow/geometry").toByteArray());
    restoreState(settings->value("MainWindow/windowState").toByteArray());

    // Always hide the dock no matter how the application was closed
    SearchResultsDock *srDock = findChild<SearchResultsDock *>();
    srDock->hide();
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

    {
        PROFILE_SCOPE("MainWindow::addEditor.detectLanguage");
        detectLanguage(editor);
    }

    // These should only ever occur for the focused editor??
    // TODO: look at editor inspector as an example to ensure updates are only coming from one editor.
    // Can save the connection objects and disconnected from them and only connect to the editor as it is activated.
    connect(editor, &ScintillaNext::savePointChanged, this, [=]() { updateSaveStatusBasedUi(editor); });
    connect(editor, &ScintillaNext::renamed, this, [=]() { detectLanguage(editor); });
    connect(editor, &ScintillaNext::renamed, this, [=]() { updateFileStatusBasedUi(editor); });
    connect(editor, &ScintillaNext::updateUi, this, &MainWindow::updateDocumentBasedUi);

    // Watch for any zoom events (Ctrl+Scroll or pinch-to-zoom (Qt translates it as Ctrl+Scroll)) so that the event
    // can be handled before the ScintillaEditBase widget, so that it can be applied to all editors to keep zoom level equal.
    // NOTE: Need to install this on the scroll area's viewport, not on the editor widget itself...that was painful to learn
    editor->viewport()->installEventFilter(zoomEventWatcher);

    editor->setZoom(zoomLevel);

    editor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(editor, &ScintillaNext::customContextMenuRequested, this, [=](const QPoint &pos) {
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

void MainWindow::checkForUpdatesFinished(QString url)
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
        QTimer::singleShot(15000, this, [=]() {
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

QMenu *MainWindow::buildMenu(QStringList actionNames)
{
    QMenu *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    ActionUtils::populateActionContainer(menu, this, actionNames);

    return menu;
}

void MainWindow::attachAiAgentDock(AiAgentDock *dock)
{
    AiAgentDock *existing = nullptr;
    const auto children = findChildren<AiAgentDock *>();
    for (auto *d : children) {
        if (d != dock) {
            existing = d;
            break;
        }
    }

    addDockWidget(AiAgentDock::defaultArea(), dock);
    if (existing) {
        tabifyDockWidget(existing, dock);
    }
    dock->setVisible(true);
    dock->raise();
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
        "OpenTerminalInFolder",
        "",
#endif
        "CopyFullPath",
        "CopyFileName",
        "CopyFileDirectory"
    };

    // If the entry exists in the settings, use that
    ApplicationSettings *settings = app->getSettings();
    if (settings->contains("Gui/TabBarContextMenu")) {
        actionNames = settings->value("Gui/TabBarContextMenu").toStringList();
    }

    buildMenu(actionNames)->popup(QCursor::pos());
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
