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

#include <sentry.h>

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
#include <QUuid>
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
#include <memory>

#ifdef Q_OS_WIN
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopServices>
#include <QRegularExpression>
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
#include "FindInFolderDock.h"
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

#include "remote/ExecutionContext.h"
#include "remote/ExecutionContextRegistry.h"
#include "remote/IFileSystemBackend.h"
#include "remote/LocalExecutionContext.h"
#include "remote/RemoteExecutionContext.h"
#include "remote/RemoteFsBackend.h"
#include "remote/SshConnection.h"
#include "remote/SshProfile.h"
#include "remote/SshProfileRegistry.h"
#include "SshConnectionManagerDialog.h"
#include "SshDebugDialog.h"
#include "SshConnectDialog.h"
#include "SshRemoteFolderPickerDialog.h"
#include "remote/RemoteTransferManager.h"
#include "TransferConflictDialog.h"

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
    // QApplication::focusWidget() for a QAbstractScrollArea (QTextBrowser /
    // QTextEdit / QPlainTextEdit) is its *viewport* — a plain QWidget with no
    // copy/cut/paste/selectAll slot. Invoking the slot on the viewport silently
    // fails, so Ctrl+C inside a chat bubble used to fall through to the editor.
    // Climb to the first ancestor that actually declares the no-arg slot and
    // invoke it there. Read-only browsers don't accept the Copy ShortcutOverride
    // (TextBrowserInteraction omits TextSelectableByKeyboard), so the window
    // action fires and this forwarding is the only path that reaches them.
    const QByteArray signature = QByteArray(slot) + "()";
    for (QWidget *w = QApplication::focusWidget(); w; w = w->parentWidget()) {
        // QLabel (used by user chat bubbles) is selectable but has no copy()
        // slot — copy its selection directly. selectAll has no QLabel analogue,
        // and a read-only label can't cut/paste, so only "copy" is meaningful.
        if (auto *label = qobject_cast<QLabel *>(w)) {
            if (qstrcmp(slot, "copy") == 0) {
                // Consume copy at the focused label whether or not there's a
                // selection: only write the clipboard when there IS one (an
                // empty selection leaves existing clipboard contents intact),
                // but always return true so an empty-selection copy does NOT
                // fall through to the editor's copyAllowLine() and grab an
                // unrelated current line while the user is on a chat bubble.
                if (label->hasSelectedText()) {
                    QApplication::clipboard()->setText(label->selectedText());
                }
                return true;
            }
            // Non-copy clipboard ops don't apply to a static label; let the
            // climb continue in case a real slot-bearing ancestor exists.
            continue;
        }
        if (w->metaObject()->indexOfMethod(signature.constData()) >= 0) {
            return QMetaObject::invokeMethod(w, slot);
        }
    }
    return false;
}

// Tints an SVG that declares stroke="currentColor" to `color` by compositing
// SourceIn over each rendered pixmap. Qt's icon engine otherwise resolves
// currentColor to opaque black and never follows theme switches, so callers
// re-run this on PaletteChange/effectiveThemeChanged.
QIcon makeTintedIcon(const QString &svgPath, const QColor &color)
{
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
        if (m_actionPreview)
            m_actionPreview->setChecked(true);
    });
    connect(dockedEditor, &DockedEditor::contextMenuRequestedForEditor, this, &MainWindow::tabBarRightClicked);
    connect(dockedEditor, &DockedEditor::titleBarDoubleClicked, this, [this]() { newFile(true); });
    // Single source of truth for "the editor area is now empty" — fires for the
    // last tab of ANY kind (editor, preview, browser, mini-app, future types),
    // so closing the last browser tab respawns "New X" just like the last
    // editor would. Queued so the handler runs AFTER the synchronous ADS close
    // cascade fully unwinds — calling addDockWidget mid-tear-down crashes in
    // topLevelDockArea. See handleEditorAreaEmptied().
    connect(dockedEditor, &DockedEditor::lastTabClosed, this, &MainWindow::handleEditorAreaEmptied, Qt::QueuedConnection);

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
    connect(ui->actionNew, &QAction::triggered, this, [this]() { newFile(true); });
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

        // Static head of the submenu = "Open New Folder..." + "Open Remote
        // Folder via SSH..." + separator (3 actions).
        // Strip any previously-built recent entries before rebuilding.
        while (ui->menuOpenFolderAsWorkspace->actions().size() > 3) {
            delete ui->menuOpenFolderAsWorkspace->actions().takeLast();
        }

        // Partition recents into remote and local for visual grouping.
        QStringList remoteRecents, localRecents;
        for (const QString &path : recents->fileList()) {
            if (remote::isSshUri(path))
                remoteRecents.append(path);
            else
                localRecents.append(path);
        }

        int i = 0;

        if (!remoteRecents.isEmpty()) {
            ui->menuOpenFolderAsWorkspace->addSection(tr("Recent Remote"));
            for (const QString &path : remoteRecents) {
                ++i;
                const QString prefix = QString("%1%2: ").arg(i < 10 ? "&" : "").arg(i);

                const remote::SshUri uri = remote::parseSshUri(path);
                QString display = path;
                if (uri.valid) {
                    remote::SshProfileRegistry *profiles =
                        app ? app->getSshProfileRegistry() : nullptr;
                    if (profiles && profiles->contains(uri.profileId)) {
                        const remote::SshProfile p = profiles->profile(uri.profileId);
                        const QString userHost = p.username.isEmpty()
                            ? p.host
                            : (p.username + QLatin1Char('@') + p.host);
                        display = userHost + QLatin1Char(':') + uri.remotePath;
                    }
                }

                QString status = tr("disconnected");
                remote::ExecutionContextRegistry *contexts =
                    app ? app->getExecutionContextRegistry() : nullptr;
                if (contexts && uri.valid) {
                    if (auto *ctx = contexts->remoteContext(uri.profileId)) {
                        switch (ctx->state()) {
                        case remote::ExecutionContext::State::Connected:
                            status = tr("connected"); break;
                        case remote::ExecutionContext::State::Connecting:
                            status = tr("connecting"); break;
                        case remote::ExecutionContext::State::Reconnecting:
                            status = tr("reconnecting"); break;
                        case remote::ExecutionContext::State::Failed:
                        case remote::ExecutionContext::State::Disconnected:
                            status = tr("disconnected"); break;
                        }
                    }
                }

                QAction *action = new QAction(
                    QString("%1%2  (%3)").arg(prefix, display, status),
                    ui->menuOpenFolderAsWorkspace);
                action->setIcon(makeTintedIcon(QStringLiteral(":/icons/ssh-badge.svg"),
                                               palette().color(QPalette::ButtonText)));
                action->setData(path);
                connect(action, &QAction::triggered, this, [this, path]() {
                    openFolderAsWorkspacePath(path);
                });
                ui->menuOpenFolderAsWorkspace->addAction(action);
            }
        }

        if (!localRecents.isEmpty()) {
            ui->menuOpenFolderAsWorkspace->addSection(tr("Recent Local"));
            for (const QString &path : localRecents) {
                ++i;
                const QString prefix = QString("%1%2: ").arg(i < 10 ? "&" : "").arg(i);
                const QString native = QDir::toNativeSeparators(path);
                QAction *action = new QAction(
                    QString("%1%2").arg(prefix, native),
                    ui->menuOpenFolderAsWorkspace);
                action->setData(path);
                connect(action, &QAction::triggered, this, [this, path]() {
                    openFolderAsWorkspacePath(path);
                });
                ui->menuOpenFolderAsWorkspace->addAction(action);
            }
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
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        HtmlConverter html(editor);
        exportAsFormat(&html, QStringLiteral("HTML files (*.html)"));
    });

    connect(ui->actionExportRtf, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        RtfConverter rtf(editor);
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
        if (!editor) return;
        const QByteArray selection = editor->getSelText();
        editor->replaceSel(selection.toBase64().constData());
    });
    connect(ui->actionURLEncode, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        const QByteArray selection = editor->getSelText();
        editor->replaceSel(selection.toPercentEncoding().constData());
    });
    connect(ui->actionBase64Decode, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        const QByteArray selection = editor->getSelText();
        if (auto result = QByteArray::fromBase64Encoding(selection)) {
            editor->replaceSel((*result).constData());
        }
    });
    connect(ui->actionURLDecode,&QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        const QByteArray selection = editor->getSelText();
        editor->replaceSel(QByteArray::fromPercentEncoding(selection).constData());
    });
    connect(ui->actionCopyURL, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
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
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        editor->targetFromSelection();
        editor->linesSplit(0);
    });

    connect(ui->actionJoinLines, &QAction::triggered, this, [this]()  {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        editor->targetFromSelection();
        editor->linesJoin();
    });

    connect(ui->actionRemoveEmptyLines, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
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
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        ScintillaSorter scintillaSorter(editor);
        scintillaSorter.sort(ReverseSorter(Sorter::Direction::Ascending));
    });
    connect(ui->actionSortLinesAsc, &QAction::triggered, this, [=, this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        ScintillaSorter scintillaSorter(editor);
        scintillaSorter.sort(CaseSensitiveSorter(Sorter::Direction::Ascending));
    });
    connect(ui->actionSortLinesAscCaseInsensitive, &QAction::triggered, this, [=, this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        ScintillaSorter scintillaSorter(editor);
        scintillaSorter.sort(CaseInsensitiveSorter(Sorter::Direction::Ascending));
    });
    connect(ui->actionSortLinesbyLengthAsc, &QAction::triggered, this, [=, this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        ScintillaSorter scintillaSorter(editor);
        scintillaSorter.sort(LineLengthSorter(Sorter::Direction::Ascending));
    });
    connect(ui->actionSortLinesDesc, &QAction::triggered, this, [=, this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        ScintillaSorter scintillaSorter(editor);
        scintillaSorter.sort(CaseSensitiveSorter(Sorter::Direction::Descending));
    });
    connect(ui->actionSortLinesDescCaseInsensitive, &QAction::triggered, this, [=, this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        ScintillaSorter scintillaSorter(editor);
        scintillaSorter.sort(CaseInsensitiveSorter(Sorter::Direction::Descending));
    });
    connect(ui->actionSortLinesbyLengthDesc, &QAction::triggered, this, [=, this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        ScintillaSorter scintillaSorter(editor);
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

    connect(ui->actionUndo, &QAction::triggered, this, [=]() { if (auto *e = currentEditor()) e->undo(); });
    connect(ui->actionRedo, &QAction::triggered, this, [=]() { if (auto *e = currentEditor()) e->redo(); });
    connect(ui->actionCut, &QAction::triggered, this, [=]() {
        if (!isEditorFocused() && forwardClipboardToFocusWidget("cut")) return;
        if (auto *e = currentEditor()) e->cutAllowLine();
    });
    connect(ui->actionCopy, &QAction::triggered, this, [=]() {
        if (!isEditorFocused() && forwardClipboardToFocusWidget("copy")) return;
        if (auto *e = currentEditor()) e->copyAllowLine();
    });
    connect(ui->actionDelete, &QAction::triggered, this, [=]() { if (auto *e = currentEditor()) e->clear(); });
    connect(ui->actionPaste, &QAction::triggered, this, [=]() {
        if (!isEditorFocused() && forwardClipboardToFocusWidget("paste")) return;
        if (auto *e = currentEditor()) e->paste();
    });
    connect(ui->actionSelectAll, &QAction::triggered, this, [=]() {
        if (!isEditorFocused() && forwardClipboardToFocusWidget("selectAll")) return;
        if (auto *e = currentEditor()) e->selectAll();
    });
    connect(ui->actionSelectNext, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;

        editor->setSearchFlags(SCFIND_NONE);
        editor->targetWholeDocument();
        editor->multipleSelectAddNext();
    });
    connect(ui->actionCopyFullPath, &QAction::triggered, this, [this]() {
        auto editor = currentEditor();
        if (!editor) return;
        if (editor->isFile()) {
            if (editor->isRemote()) {
                QApplication::clipboard()->setText(editor->remotePath());
            } else {
                QApplication::clipboard()->setText(editor->getFilePath());
            }
        }
    });
    connect(ui->actionCopyFileName, &QAction::triggered, this, [this]() {
        auto editor = currentEditor();
        if (!editor) return;
        QApplication::clipboard()->setText(editor->getName());
    });
    connect(ui->actionCopyFileDirectory, &QAction::triggered, this, [this]() {
        auto editor = currentEditor();
        if (!editor) return;
        if (editor->isFile()) {
            QApplication::clipboard()->setText(editor->getPath());
        }
    });

    connect(ui->actionCopyAsHtml, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        HtmlConverter html(editor);
        copyAsFormat(&html, "text/html");
    });

    connect(ui->actionCopyAsRtf, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        RtfConverter rtf(editor);
        copyAsFormat(&rtf, "Rich Text Format");
    });

    connectEditorAction(ui->actionIncreaseIndent, &ScintillaNext::tab);
    connectEditorAction(ui->actionDecreaseIndent, &ScintillaNext::backTab);

    addAction(ui->actionToggleOverType);
    connect(ui->actionToggleOverType, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        editor->editToggleOvertype();
        ui->statusBar->refresh(editor);
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

    findInFolderDock = new FindInFolderDock(this);
    addDockWidget(Qt::BottomDockWidgetArea, findInFolderDock);
    DockMiddleClickCloser::install(findInFolderDock);
    findInFolderDock->hide();
    ui->menuView->addAction(findInFolderDock->toggleViewAction());

    connect(findInFolderDock, &FindInFolderDock::resultActivated, this,
            [this, app](const QString &filePath, int lineNumber, int matchStart, int matchEnd) {
        openFile(filePath);
        ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(filePath);
        if (editor) {
            dockedEditor->switchToEditor(editor);
            int linePos = editor->positionFromLine(lineNumber);
            editor->goToRange({linePos + matchStart, linePos + matchEnd});
            editor->verticalCentreCaret();
            editor->grabFocus();
        }
    });

    connect(findInFolderDock, &FindInFolderDock::resultClicked, this,
            [this, app](const QString &filePath, int lineNumber, int matchStart, int matchEnd) {
        previewFile(filePath);
        ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(filePath);
        if (editor) {
            dockedEditor->switchToEditor(editor);
            int linePos = editor->positionFromLine(lineNumber);
            editor->goToRange({linePos + matchStart, linePos + matchEnd});
            editor->verticalCentreCaret();
        }
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
        ScintillaNext *editor = currentEditor();
        if (!editor) return;

        QuickFindWidget *quickFind = findChild<QuickFindWidget *>(QString(), Qt::FindDirectChildrenOnly);

        if (quickFind == Q_NULLPTR) {
            quickFind = new QuickFindWidget(this);
        }

        quickFind->setEditor(editor);
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
        if (!editor) return;
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
                if (!editor) return;
                markerAppDecorator->mark(editor, sender()->property("MarkerNumber").toInt());
            }
        }
    };

    auto clear_mark_callback = [=, this]() {
        MarkerAppDecorator *markerAppDecorator = app->findChild<MarkerAppDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (markerAppDecorator && markerAppDecorator->isEnabled()) {
            if (sender()->property("MarkerNumber").isValid()) {
                ScintillaNext *editor = currentEditor();
                if (!editor) return;
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
            ScintillaNext *editor = currentEditor();
            if (!editor) return;
            markerAppDecorator->clearAll(editor);
        }
    });

    connect(ui->actionToggleBookmark, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            editor->forEachLineInSelection(editor->mainSelection(), [&](int line) {
                bookMarkDecorator->toggleBookmark(line);
            });
        }
    });

    connect(ui->actionNextBookmark, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
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
        if (!editor) return;
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            bookMarkDecorator->clearAllBookmarks();
        }
    });

    connect(ui->actionInvertBookmarks, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        BookMarkDecorator *bookMarkDecorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);

        if (bookMarkDecorator && bookMarkDecorator->isEnabled()) {
            for (int line = 0; line < editor->lineCount(); line++) {
                bookMarkDecorator->toggleBookmark(line);
            }
        }
    });

    connect(ui->actionPreviousBookmark, &QAction::triggered, this, [this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
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
        if (!editor) return;
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
        if (!editor) return;
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
        if (!editor) return;
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
        if (auto *e = currentEditor()) zoomLevel = e->zoom();

        showEditorZoomLevelIndicator();
    });
    connect(ui->actionZoomOut, &QAction::triggered, this, [this]() {
        for (ScintillaNext *editor : editors()) {
            editor->zoomOut();
        }
        if (auto *e = currentEditor()) zoomLevel = e->zoom();

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
            ScintillaNext *editor = currentEditor();
            if (!editor) return;
            macroManager.startRecording(editor);
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
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        macroManager.replayCurrentMacro(editor);
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
                ScintillaNext *editor = currentEditor();
                if (!editor) return;
                if (times > 0)
                    macro->replay(editor, times);
                else if (times == -1)
                    macro->replayTillEndOfFile(editor);
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
            ui->menuMacro->addAction(m->getName(), [=, this]() {
                if (auto *e = currentEditor()) m->replay(e);
            });
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
    connect(tabsQuickActionsBar, &TabsQuickActionsBar::createNewTabClicked, this, [this]() { newFile(true); });
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
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        QString filePath = QDir::toNativeSeparators(editor->getFileInfo().canonicalFilePath());
        QStringList arguments = {"/select,", filePath};
        QProcess::startDetached("explorer", arguments);
    });

    QString terminalName = app->getSettings()->value("App/TerminalName", "Command Prompt").toString();
    ui->actionOpenTerminalHere->setText(ui->actionOpenTerminalHere->text().arg(terminalName));

    connect(ui->actionOpenTerminalHere, &QAction::triggered, this, [=, this]() {
        ScintillaNext *editor = currentEditor();
        if (!editor) return;
        QString command = app->getSettings()->value("App/TerminalCommand", "cmd").toString();
        QString filePath = QDir::toNativeSeparators(editor->getFileInfo().dir().canonicalPath());
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
        QIcon icon = makeTintedIcon(QStringLiteral(":/icons/eye.svg"),
                                    palette().color(QPalette::ButtonText));
        // Generic "Preview" — the action is no longer Markdown-specific. The
        // label/tooltip are made type-specific dynamically per active editor
        // (see updatePreviewActionForEditor). Ctrl+Shift+M is kept for muscle
        // memory even though it now previews any supported type.
        m_actionPreview = new QAction(icon, tr("Preview"), this);
    }
    m_actionPreview->setCheckable(true);
    m_actionPreview->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    m_actionPreview->setEnabled(false);
    ui->menuView->addAction(m_actionPreview);
    ui->mainToolBar->addAction(m_actionPreview);

    connect(m_actionPreview, &QAction::triggered, this, [this](bool checked) {
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

    // The eye icon declares stroke="currentColor"; Qt resolves that to opaque
    // black once and never follows theme switches. Re-tint on theme change, and
    // re-run the per-editor update so a disabled (greyed) icon isn't left with
    // the previous theme's color — the disabled variant is derived from the
    // Normal pixmaps we just rebuilt.
    connect(app, &NotepadNextApplication::effectiveThemeChanged, this, [this]() {
        retintPreviewActionIcon();
        updatePreviewActionForEditor(currentEditor());
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
    ui->actionOpenTerminalInWorkspace->setEnabled(false);
    ui->actionOpenTerminalInFolder->setEnabled(false);

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
        remote::ExecutionContext *ctx = activeExecutionContext();
        ui->actionOpenTerminalInWorkspace->setEnabled(
            TerminalCwdResolver::canOpenInWorkspaceForContext(ctx, workspaceRoot));
        ui->actionOpenTerminalInFolder->setEnabled(
            TerminalCwdResolver::canOpenInFolderForContext(ctx, activeFilePath, activeIsFile, workspaceRoot));
    });

    connect(ui->actionOpenTerminalInWorkspace, &QAction::triggered, this, [this]() {
        const QString workspaceRoot = currentWorkspaceRoot();
        remote::ExecutionContext *ctx = activeExecutionContext();
        const QString cwd = TerminalCwdResolver::resolveWorkspaceForContext(ctx, workspaceRoot);
        if (cwd.isEmpty()) return;
        if (ctx && ctx->isRemote()) {
            terminalManager->openRemoteTerminal(ctx, cwd, QString());
        } else {
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
        remote::ExecutionContext *ctx = activeExecutionContext();
        const QString cwd = TerminalCwdResolver::resolveFolderForContext(ctx, activeFilePath, activeIsFile, workspaceRoot);
        if (cwd.isEmpty()) return;
        if (ctx && ctx->isRemote()) {
            terminalManager->openRemoteTerminal(ctx, cwd, QString());
        } else {
            terminalManager->openTerminal(cwd);
        }
    });

    connect(ui->menuTasks, &QMenu::aboutToShow, this, [this]() {
        const QString rawWorkspaceRoot = currentWorkspaceRoot();
        const bool isSshWorkspace = remote::isSshUri(rawWorkspaceRoot);
        const QString workspaceRoot = isSshWorkspace
            ? rawWorkspaceRoot
            : TerminalTaskRegistry::normalizeWorkspacePath(rawWorkspaceRoot);
        const bool hasWorkspace = !isSshWorkspace
            && TerminalCwdResolver::canOpenInWorkspace(workspaceRoot);

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
        const QString rawWorkspaceRoot = currentWorkspaceRoot();
        if (remote::isSshUri(rawWorkspaceRoot)) return;
        const QString workspaceRoot = TerminalTaskRegistry::normalizeWorkspacePath(rawWorkspaceRoot);
        const QString cwd = TerminalCwdResolver::resolveWorkspace(workspaceRoot);
        if (cwd.isEmpty()) return;

        const QList<TerminalTask> existing = terminalManager->tasksForWorkspace(workspaceRoot);
        EditTasksDialog dlg(cwd, existing, this);
        if (dlg.exec() == QDialog::Accepted) {
            terminalManager->setTasks(workspaceRoot, dlg.tasks());
        }
    });

    setupSshMenu();

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
        remote::ExecutionContext *ctx = activeExecutionContext();
        const bool workspaceOk = TerminalCwdResolver::canOpenInWorkspaceForContext(ctx, workspaceRoot);
        const bool folderOk = TerminalCwdResolver::canOpenInFolderForContext(ctx, activeFilePath, activeIsFile, workspaceRoot);

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
                    remote::ExecutionContext *ctx = activeExecutionContext();
                    QString cwd;
                    if (isWorkspaceVariant) {
                        cwd = TerminalCwdResolver::resolveWorkspaceForContext(ctx, currentWorkspaceRoot());
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
                        cwd = TerminalCwdResolver::resolveFolderForContext(ctx, activeFilePath, activeIsFile, workspaceRoot);
                    }
                    if (cwd.isEmpty()) return;

                    AcpAgentManager *m = this->app->getAiAgentManager();
                    if (!m) return;
                    AiAgentDock *dock = m->openAgent(agentId, cwd, /*recordAsLastUsed=*/true, ctx);
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
        remote::ExecutionContext *ctx = activeExecutionContext();
        QString cwd = TerminalCwdResolver::resolveWorkspaceForContext(ctx, workspaceRoot);
        if (cwd.isEmpty()) {
            cwd = TerminalCwdResolver::resolveFolderForContext(ctx, activeFilePath, activeIsFile, workspaceRoot);
        }
        if (cwd.isEmpty()) {
            statusBar()->showMessage(tr("Open a workspace or a saved file to start an AI tab"), 4000);
            return;
        }

        AiAgentDock *dock = manager->openAgent(agentId, cwd, /*recordAsLastUsed=*/true, ctx);
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

        // SSH transport debug log. Shows connection lifecycle, SFTP ops, exec
        // commands, and git timeouts for the currently active SSH workspace.
        // Disabled when no SSH workspace is active.
        QAction *sshDebugAction = debugMenu->addAction(tr("SSH Debug Log"));
        sshDebugAction->setEnabled(remote::isSshUri(currentWorkspaceRoot()));
        connect(this, &MainWindow::activeWorkspaceChanged, this,
                [this, sshDebugAction](FolderAsWorkspaceDock *, FolderAsWorkspaceDock *) {
                    sshDebugAction->setEnabled(remote::isSshUri(currentWorkspaceRoot()));
                    rebindSshDebugDialog();
                });
        connect(sshDebugAction, &QAction::triggered, this, [this]() {
            if (!m_sshDebugDialog) {
                m_sshDebugDialog = new SshDebugDialog(this);
                rebindSshDebugDialog();
            }
            m_sshDebugDialog->show();
            m_sshDebugDialog->raise();
            m_sshDebugDialog->activateWindow();
        });

        QAction *sentryTestAction = debugMenu->addAction(tr("Test Sentry"));
        connect(sentryTestAction, &QAction::triggered, this, []() {
            sentry_capture_event(sentry_value_new_message_event(
                SENTRY_LEVEL_INFO, "custom", "It works!"));
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
    // Destroy fileWatcher before the base-class dtor tears down child widgets
    // (CDockManager → CDockWidget → ScintillaNext → QObject::destroyed signal).
    // fileWatcher is a child of this, created AFTER DockedEditor/CDockManager, so
    // Qt's reverse-order child destruction frees it first — leaving the lambdas
    // connected to editors' destroyed signal with a dangling pointer. Explicit
    // early delete prevents that.
    delete fileWatcher;
    fileWatcher = nullptr;

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

void MainWindow::newFile(bool replaceInitialEditor)
{
    qInfo(Q_FUNC_INFO);

    static int count = 1;

    // Snapshot the reusable pristine "New X" editor BEFORE creating the new one:
    // once the new editor exists editorCount() is 2 and getInitialEditor() would
    // return null. Same close-after-open mechanism as openFileList(). We close it
    // at the end so totalTabCount() never hits 0 mid-flight (a new tab is always
    // present), and lastTabClosed never fires.
    ScintillaNext *initialEditor = replaceInitialEditor ? getInitialEditor() : Q_NULLPTR;

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

    // Drop the now-redundant empty scratch buffer. Guarded so we never close the
    // editor we just created (getInitialEditor() was evaluated before creation,
    // so initialEditor can't alias it).
    if (initialEditor) {
        initialEditor->close();
    }
}

// One unedited, new blank document. Delegates to DockedEditor::initialEditor()
// so the "pristine scratch tab" definition lives in exactly one place and is
// shared with MainWindow-less tab-spawners (e.g. MiniAppManager web tabs).
ScintillaNext *MainWindow::getInitialEditor()
{
    return dockedEditor->initialEditor();
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

        // Search currently open editors to see if it is already open. For an
        // ssh:// path getEditorByFilePath dedupes by URI (no local QFileInfo).
        ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(filePath);

        if (editor == Q_NULLPTR) {
            if (remote::isSshUri(filePath)) {
                // Remote (ssh://) open (D5 branch point 1): SKIP the local
                // QFileInfo(filePath).isFile() stat + "Create File?" prompt — the
                // path lives on another host. Resolve the backend from the active
                // execution context; trust the tree that surfaced the path. The
                // shell tab is added synchronously by createEditorFromRemote, so
                // the initialEditor snapshot/close ordering below still holds.
                remote::ExecutionContext *ctx = activeExecutionContext();
                remote::IFileSystemBackend *backend = ctx ? ctx->fsBackend() : nullptr;
                if (!backend || !backend->isRemote()) {
                    qWarning("openFileList: no remote backend for %s", qUtf8Printable(filePath));
                    continue;
                }
                const remote::SshUri parsed = remote::parseSshUri(filePath);
                editor = app->getEditorManager()->createEditorFromRemote(
                    backend, filePath, parsed.remotePath);
            }
            else {
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
        }
        else if (editor == dockedEditor->previewEditor()) {
            dockedEditor->pinPreviewEditor();
        }

        if (editor) {
            openedEditors.append(editor);
        }
    }

    // If any were successful
    if (!openedEditors.empty()) {
        dockedEditor->switchToEditor(openedEditors.last());

        if (initialEditor) {
            initialEditor->close();
        }
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

    // Snapshot the reusable pristine "New X" editor BEFORE creating the preview
    // editor: createEditorFromFile()/createEditorFromRemote() add the new editor
    // to the layout synchronously (editorCreated → addEditor), so by the time it
    // returns editorCount() is already 2 and getInitialEditor() — which requires
    // editorCount() == 1 — would return null, leaving the scratch tab behind.
    // Same ordering rule as newFile(). Closed at the end so totalTabCount()
    // never hits 0 mid-flight and lastTabClosed never fires.
    if (remote::isSshUri(filePath)) {
        // Remote (ssh://) preview (D5 branch point 2): SKIP the local QFileInfo
        // stat. Resolve the backend from the active execution context.
        remote::ExecutionContext *ctx = activeExecutionContext();
        remote::IFileSystemBackend *backend = ctx ? ctx->fsBackend() : nullptr;
        if (!backend || !backend->isRemote()) {
            qWarning("previewFile: no remote backend for %s", qUtf8Printable(filePath));
            return;
        }

        ScintillaNext *initialEditor = getInitialEditor();

        const remote::SshUri parsed = remote::parseSshUri(filePath);
        editor = app->getEditorManager()->createEditorFromRemote(
            backend, filePath, parsed.remotePath);
        if (!editor) return;

        dockedEditor->addPreviewEditor(editor);
        dockedEditor->switchToEditor(editor);

        if (initialEditor) {
            initialEditor->close();
        }
        return;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile()) return;

    ScintillaNext *initialEditor = getInitialEditor();

    editor = app->getEditorManager()->createEditorFromFile(filePath);
    if (!editor) return;

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

    const bool isSsh = remote::isSshUri(dir);

    // Single chokepoint for every workspace entry — CLI --workspace, recent
    // list, session restore, dialog, gitOpenSubmoduleRequested. Resolving here
    // means a relative input like "." can never leak into the recent list, the
    // dock title, the persisted Workspaces list, or QFileSystemModel's root.
    // SSH URIs are already absolute and must NOT be mangled by QDir.
    const QString resolved = isSsh ? dir : QDir(dir).absolutePath();

    app->getRecentWorkspacesListManager()->addFile(resolved);

    // If this workspace is already open in some dock, just focus it rather
    // than spawning a duplicate tab.
    const QString cleaned = isSsh ? resolved : QDir::cleanPath(resolved);
    const auto existing = findChildren<FolderAsWorkspaceDock *>();
    for (FolderAsWorkspaceDock *d : existing) {
        const QString dRoot = d->rootPath();
        const QString dCleaned = remote::isSshUri(dRoot) ? dRoot : QDir::cleanPath(dRoot);
        if (dCleaned == cleaned) {
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

    if (isSsh) {
        // --- SSH workspace: wire the remote backend and set the URI identity ---
        const remote::SshUri uri = remote::parseSshUri(resolved);
        dock->setSshWorkspaceUri(resolved);

        remote::ExecutionContextRegistry *contexts =
            app ? app->getExecutionContextRegistry() : nullptr;
        remote::SshProfileRegistry *profiles =
            app ? app->getSshProfileRegistry() : nullptr;

        // Derive the "SSH: user@host" title from the profile.
        QString userHost;
        if (profiles && uri.valid && profiles->contains(uri.profileId)) {
            const remote::SshProfile p = profiles->profile(uri.profileId);
            userHost = p.username.isEmpty() ? p.host
                                            : (p.username + QLatin1Char('@') + p.host);
        }
        dock->setWindowTitle(userHost.isEmpty() ? QStringLiteral("SSH")
                                                : (QStringLiteral("SSH: ") + userHost));

        // Pre-populate the pending-expansion map BEFORE the model loads (the
        // backend swap + setRootPath inside wireSshDockToContext fire the first
        // directoryLoaded). Same ordering contract as the local path.
        dock->applySavedTreeState(savedState);

        // Wire the reconnect signal so clicking Reconnect re-runs the connect.
        // wireSshDockToContext is idempotent (guarded); reconnectSshWorkspace
        // re-triggers connectToHost on the existing (dropped) connection —
        // registry->connect() returns an existing context without re-dialing.
        connect(dock, &FolderAsWorkspaceDock::reconnectRequested, this,
                &MainWindow::reconnectSshWorkspace);

        // Wire the dock to its context if one already exists (mid-session open
        // after the connect dialog succeeded). wireSshDockToContext drives the
        // current state immediately — Connected → useRemoteBackend + setRootPath,
        // Connecting → spinner. If no context exists yet (startup restore), the
        // caller (restoreOpenWorkspaces) creates + wires it after registry->connect;
        // until then show the connecting placeholder so the dock isn't blank.
        remote::ExecutionContext *ctx = nullptr;
        if (contexts && uri.valid) {
            ctx = contexts->remoteContext(uri.profileId);
            if (!ctx) {
                ctx = contexts->connect(uri.profileId);
            }
        }
        if (ctx) {
            wireSshDockToContext(dock, ctx);
        } else {
            dock->showConnectingState(userHost);
        }
    } else {
        // --- Local workspace: existing path ---
        dock->applySavedTreeState(savedState);
        dock->setRootPath(resolved);
    }

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

        // Tear down the SSH connection when an SSH workspace closes so the
        // bulk SFTP lane (and any wedged ops) is fully reset. Without this,
        // the SshConnection stays alive in State::Ready and re-opening the
        // same workspace returns the same wedged context.
        if (remote::isSshUri(path)) {
            if (remote::ExecutionContextRegistry *reg = app ? app->getExecutionContextRegistry() : nullptr) {
                const remote::SshUri uri = remote::parseSshUri(path);
                if (uri.valid) {
                    reg->disconnect(uri.profileId);
                }
            }
        }
        const WorkspaceStateSnapshot snap = self->captureState();
        const QString memoKey = remote::isSshUri(snap.rootPath) ? snap.rootPath
                                                                 : QDir::cleanPath(path);
        m_workspaceStateMemo.insert(memoKey, snap);
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
            [this, dock](QMenu *menu, const QString &absPath, bool isDir) {
        const QString wsRoot = currentWorkspaceRoot();

        // --- Preview (rendered preview for supported file types) ---
        if (!isDir) {
            auto *mgr = this->app->getPreviewTabManager();
            if (mgr && mgr->canPreview(absPath)) {
                // For SSH workspaces absPath is now a full ssh:// URI (after resolvedFilePath fix).
                // Pass it directly to previewFile. For local workspaces the existing size
                // guard and openPreviewFromFile path are unchanged.
                const bool isSshWorkspace = remote::isSshUri(absPath);
                QFileInfo fi(isSshWorkspace ? QString() : absPath);
                if (isSshWorkspace || mgr->previewWantsFilePath(absPath) || fi.size() <= 10 * 1024 * 1024) {
                    auto *previewAction = new QAction(tr("Preview"), menu);
                    if (isSshWorkspace) {
                        connect(previewAction, &QAction::triggered, this, [this, absPath]() {
                            previewFile(absPath);
                        });
                    } else {
                        connect(previewAction, &QAction::triggered, this, [this, absPath]() {
                            auto *mgr = this->app->getPreviewTabManager();
                            if (mgr) mgr->openPreviewFromFile(absPath);
                        });
                    }
                    menu->addAction(previewAction);
                    menu->addSeparator();
                }
            }
        }

        // --- Copy Path / Copy Relative Path ---
        // Use the dock's own rootPath() — not currentWorkspaceRoot() — so that
        // right-clicking in a non-active workspace dock still gets the correct
        // SSH/local classification for that dock.
        const QString dockRoot = dock ? dock->rootPath() : wsRoot;
        const bool isSshDock = remote::isSshUri(dockRoot);
        auto *copyPath = new QAction(tr("Copy Path"), menu);
        connect(copyPath, &QAction::triggered, this, [absPath, isSshDock]() {
            // SSH: copy the POSIX remote path (strip the ssh://profileId prefix).
            // Local: convert to native separators.
            const QString text = isSshDock
                ? remote::parseSshUri(absPath).remotePath
                : QDir::toNativeSeparators(absPath);
            QApplication::clipboard()->setText(text);
        });
        menu->addAction(copyPath);

        auto *copyRelPath = new QAction(tr("Copy Relative Path"), menu);
        connect(copyRelPath, &QAction::triggered, this, [absPath, dockRoot, isSshDock]() {
            // For SSH workspaces: absPath and dockRoot are both ssh:// URIs.
            // Strip the remote workspace root path from the remote file path.
            QString rel;
            if (isSshDock) {
                const QString remotePath = remote::parseSshUri(absPath).remotePath;
                const QString remoteRoot = remote::parseSshUri(dockRoot).remotePath;
                rel = remotePath;
                if (!remoteRoot.isEmpty() && rel.startsWith(remoteRoot)) {
                    rel = rel.mid(remoteRoot.length());
                    if (rel.startsWith(QLatin1Char('/')))
                        rel = rel.mid(1);
                }
            } else {
                rel = absPath;
                if (!dockRoot.isEmpty() && rel.startsWith(dockRoot)) {
                    rel = rel.mid(dockRoot.length());
                    if (rel.startsWith(QLatin1Char('/')) || rel.startsWith(QLatin1Char('\\')))
                        rel = rel.mid(1);
                }
                rel = QDir::toNativeSeparators(rel);
            }
            QApplication::clipboard()->setText(rel);
        });
        menu->addAction(copyRelPath);

        menu->addSeparator();

        // --- Rename... (not on the workspace root node of THIS dock) ---
        // Renaming the root would require re-rooting the whole workspace; that
        // is out of scope. Compare against the raising dock's own root so the
        // gate is correct for tabified multi-workspace setups.
        // For SSH workspaces both absPath and dock->rootPath() are ssh:// URIs —
        // compare them directly (case-sensitive; POSIX remote paths are case-sensitive).
        // For local workspaces use QDir::cleanPath + case-insensitive (Windows compat).
        const bool isDockRoot = dock && [&]() -> bool {
            if (isSshDock) return absPath == dock->rootPath();
            return QDir::cleanPath(absPath).compare(QDir::cleanPath(dock->rootPath()),
                                                    Qt::CaseInsensitive) == 0;
        }();
        if (!isDockRoot) {
            auto *renameAction = new QAction(tr("Rename..."), menu);
            connect(renameAction, &QAction::triggered, this, [this, dock, absPath, isSshDock, isDir]() {
                // For SSH workspaces absPath is "ssh://profileId/remote/path".
                const QString displayPath = isSshDock ? remote::parseSshUri(absPath).remotePath : absPath;
                const QString oldName = QFileInfo(displayPath).fileName();
                bool ok = false;
                const QString newName = QInputDialog::getText(this, tr("Rename"),
                    tr("New name:"), QLineEdit::Normal, oldName, &ok);
                if (!ok || newName.isEmpty() || newName == oldName) return;
                // Same validation as New File / New Folder.
                if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\'))
                    || newName.contains(QStringLiteral(".."))) {
                    QMessageBox::warning(this, tr("Rename"), tr("Invalid name."));
                    return;
                }
                if (isSshDock) {
                    remote::RemoteFsBackend *backend = dock ? dock->remoteBackend() : nullptr;
                    if (!backend) return;
                    const remote::SshUri uri = remote::parseSshUri(absPath);
                    if (!uri.valid) return;
                    const QString parentDir = uri.remotePath.left(uri.remotePath.lastIndexOf(QLatin1Char('/')));
                    const QString newRemotePath = parentDir + QLatin1Char('/') + newName;
                    backend->renameAsync(uri.remotePath, newRemotePath,
                        [guard = QPointer<MainWindow>(this), newName](bool renameOk, const QString &error) {
                            if (guard && !renameOk)
                                QMessageBox::warning(guard, tr("Rename"),
                                    tr("Could not rename to \"%1\": %2").arg(newName, error));
                        });
                } else {
                    const QString newPath =
                        QDir(QFileInfo(absPath).absolutePath()).filePath(newName);
                    renameWorkspaceEntry(absPath, newPath, isDir);
                }
            });
            menu->addAction(renameAction);
            menu->addSeparator();
        }

        // --- New File / New Folder (directory only) ---
        if (isDir) {
            auto *newFile = new QAction(tr("New File..."), menu);
            connect(newFile, &QAction::triggered, this, [this, dock, absPath, isSshDock]() {
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
                if (isSshDock) {
                    remote::RemoteFsBackend *backend = dock ? dock->remoteBackend() : nullptr;
                    if (!backend) return;
                    const remote::SshUri uri = remote::parseSshUri(absPath);
                    if (!uri.valid) return;
                    const QString remotePath = uri.remotePath + QLatin1Char('/') + name;
                    backend->writeFileAsync(remotePath, QByteArray(),
                        [guard = QPointer<MainWindow>(this), name](bool writeOk, const QString &error) {
                            if (guard && !writeOk)
                                QMessageBox::warning(guard, tr("New File"),
                                    tr("Could not create file \"%1\": %2").arg(name, error));
                        });
                } else {
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
                }
            });
            menu->addAction(newFile);

            auto *newFolder = new QAction(tr("New Folder..."), menu);
            connect(newFolder, &QAction::triggered, this, [this, dock, absPath, isSshDock]() {
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
                if (isSshDock) {
                    remote::RemoteFsBackend *backend = dock ? dock->remoteBackend() : nullptr;
                    if (!backend) return;
                    const remote::SshUri uri = remote::parseSshUri(absPath);
                    if (!uri.valid) return;
                    const QString remotePath = uri.remotePath + QLatin1Char('/') + name;
                    backend->mkdirAsync(remotePath,
                        [guard = QPointer<MainWindow>(this), name](bool mkdirOk, const QString &error) {
                            if (guard && !mkdirOk)
                                QMessageBox::warning(guard, tr("New Folder"),
                                    tr("Could not create folder \"%1\": %2").arg(name, error));
                        });
                } else {
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
                }
            });
            menu->addAction(newFolder);

            menu->addSeparator();
        }

        // --- Show in Explorer ---
        // Not applicable for remote SSH workspaces — the path lives on the server.
        auto *showInExplorer = new QAction(tr("Show in Explorer"), menu);
        showInExplorer->setEnabled(!isSshDock);
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

        // --- Download / Upload (SSH workspaces only) ---
        if (isSshDock) {
            menu->addSeparator();

            remote::RemoteTransferManager *txMgr = dock ? dock->transferManager() : nullptr;
            const QStringList selectedPaths = dock ? dock->selectedPaths() : QStringList();
            const QList<bool> selectedDirs = dock ? dock->selectedAreDirs() : QList<bool>();

            // Decide which selection to operate on:
            //   - If more than one item is selected, use all selected items.
            //   - Otherwise use absPath (the right-clicked item).
            const bool multiSelect = selectedPaths.size() > 1;
            const QStringList operationPaths = multiSelect ? selectedPaths : QStringList{absPath};
            // For multi-select, treat as "has directory" if any selected item is a dir.
            bool anyDir = isDir;
            if (multiSelect) {
                for (bool d : selectedDirs) { if (d) { anyDir = true; break; } }
            }
            const bool allFiles = !anyDir && !multiSelect;

            // --- Download ---
            if (!multiSelect && !isDir) {
                // Single file: simple "Download File" action.
                auto *dlAction = new QAction(tr("Download File"), menu);
                if (!txMgr) dlAction->setEnabled(false);
                connect(dlAction, &QAction::triggered, this,
                        [this, dock, absPath, txMgr]() {
                    if (!txMgr) return;
                    const remote::SshUri uri = remote::parseSshUri(absPath);
                    if (!uri.valid) return;
                    const QString fileName = uri.remotePath.section(QLatin1Char('/'), -1);
                    const QString destPath = QFileDialog::getSaveFileName(
                        this, tr("Download File"), QDir::homePath() + QLatin1Char('/') + fileName);
                    if (destPath.isEmpty()) return;
                    txMgr->downloadFile(uri.remotePath, destPath);
                });
                menu->addAction(dlAction);
            } else {
                // Folder or multi-select: Download submenu.
                auto *dlMenu = new QMenu(tr("Download"), menu);

                auto *dlAllAction = new QAction(tr("Download All"), dlMenu);
                if (!txMgr) dlAllAction->setEnabled(false);
                connect(dlAllAction, &QAction::triggered, this,
                        [this, dock, operationPaths, txMgr]() {
                    if (!txMgr) return;
                    const QString destDir = QFileDialog::getExistingDirectory(
                        this, tr("Download To"), QDir::homePath());
                    if (destDir.isEmpty()) return;
                    QStringList remotePaths;
                    remotePaths.reserve(operationPaths.size());
                    for (const QString &p : operationPaths) {
                        const remote::SshUri uri = remote::parseSshUri(p);
                        if (uri.valid) remotePaths.append(uri.remotePath);
                    }
                    txMgr->downloadRecursive(remotePaths, destDir,
                                             remote::RemoteTransferManager::GitignoreFilter::None);
                });
                dlMenu->addAction(dlAllAction);

                auto *dlGitignoreAction = new QAction(tr("Download (Skip .gitignore)"), dlMenu);
                if (!txMgr) dlGitignoreAction->setEnabled(false);
                connect(dlGitignoreAction, &QAction::triggered, this,
                        [this, dock, operationPaths, txMgr]() {
                    if (!txMgr) return;
                    const QString destDir = QFileDialog::getExistingDirectory(
                        this, tr("Download To"), QDir::homePath());
                    if (destDir.isEmpty()) return;
                    QStringList remotePaths;
                    remotePaths.reserve(operationPaths.size());
                    for (const QString &p : operationPaths) {
                        const remote::SshUri uri = remote::parseSshUri(p);
                        if (uri.valid) remotePaths.append(uri.remotePath);
                    }
                    txMgr->downloadRecursive(remotePaths, destDir,
                                             remote::RemoteTransferManager::GitignoreFilter::Apply);
                });
                dlMenu->addAction(dlGitignoreAction);

                menu->addMenu(dlMenu);
            }

            // --- Upload (directory target only) ---
            if (isDir) {
                const remote::SshUri targetUri = remote::parseSshUri(absPath);

                auto *ulFilesAction = new QAction(tr("Upload Files..."), menu);
                if (!txMgr || !targetUri.valid) ulFilesAction->setEnabled(false);
                connect(ulFilesAction, &QAction::triggered, this,
                        [this, txMgr, targetUri]() {
                    if (!txMgr || !targetUri.valid) return;
                    const QStringList localFiles = QFileDialog::getOpenFileNames(
                        this, tr("Upload Files"), QDir::homePath());
                    if (localFiles.isEmpty()) return;

                    // W3: Ask upfront whether to override all conflicts or detect them.
                    QMessageBox ask(this);
                    ask.setWindowTitle(tr("Upload Files"));
                    ask.setText(tr("How should existing remote files be handled?"));
                    QPushButton *detectBtn   = ask.addButton(tr("Detect Conflicts"), QMessageBox::AcceptRole);
                    QPushButton *overrideBtn = ask.addButton(tr("Overwrite All"),    QMessageBox::DestructiveRole);
                    ask.addButton(QMessageBox::Cancel);
                    ask.setDefaultButton(detectBtn);
                    ask.exec();
                    if (ask.clickedButton() == nullptr ||
                        ask.button(QMessageBox::Cancel) == ask.clickedButton())
                        return;
                    const bool overrideAll = (ask.clickedButton() == overrideBtn);

                    if (overrideAll) {
                        txMgr->uploadFiles(localFiles, targetUri.remotePath, {}, true);
                        return;
                    }

                    // Run conflict detection; if conflicts found, show TransferConflictDialog.
                    QPointer<MainWindow> guard(this);
                    QPointer<remote::RemoteTransferManager> mgr(txMgr);
                    connect(txMgr, &remote::RemoteTransferManager::uploadConflictsDetected,
                            this, [guard, mgr, localFiles](const QStringList &conflicts,
                                  const QString & /*remoteDir*/, const QStringList & /*lPaths*/) {
                        if (guard.isNull() || mgr.isNull()) return;
                        auto *dlg = new TransferConflictDialog(conflicts, guard);
                        dlg->setAttribute(Qt::WA_DeleteOnClose);
                        if (dlg->exec() == QDialog::Accepted) {
                            // Convert dialog result to manager's type.
                            QHash<QString, remote::RemoteTransferManager::ConflictResolution> resMap;
                            for (auto it = dlg->result().cbegin(); it != dlg->result().cend(); ++it) {
                                resMap.insert(it.key(),
                                    it.value() == TransferConflictDialog::Skip
                                        ? remote::RemoteTransferManager::ConflictResolution::Skip
                                        : remote::RemoteTransferManager::ConflictResolution::Replace);
                            }
                            mgr->resolveUploadConflicts(resMap, dlg->overrideAll());
                        }
                    }, Qt::SingleShotConnection);
                    txMgr->uploadFiles(localFiles, targetUri.remotePath);
                });
                menu->addAction(ulFilesAction);

                auto *ulFolderAction = new QAction(tr("Upload Folder..."), menu);
                if (!txMgr || !targetUri.valid) ulFolderAction->setEnabled(false);
                connect(ulFolderAction, &QAction::triggered, this,
                        [this, txMgr, targetUri]() {
                    if (!txMgr || !targetUri.valid) return;
                    const QString localFolder = QFileDialog::getExistingDirectory(
                        this, tr("Select Folder to Upload"), QDir::homePath());
                    if (localFolder.isEmpty()) return;

                    // W3: Ask upfront whether to override all conflicts or detect them.
                    QMessageBox ask(this);
                    ask.setWindowTitle(tr("Upload Folder"));
                    ask.setText(tr("How should existing remote files be handled?"));
                    QPushButton *detectBtn2   = ask.addButton(tr("Detect Conflicts"), QMessageBox::AcceptRole);
                    QPushButton *overrideBtn2 = ask.addButton(tr("Overwrite All"),    QMessageBox::DestructiveRole);
                    ask.addButton(QMessageBox::Cancel);
                    ask.setDefaultButton(detectBtn2);
                    ask.exec();
                    if (ask.clickedButton() == nullptr ||
                        ask.button(QMessageBox::Cancel) == ask.clickedButton())
                        return;
                    const bool overrideAll2 = (ask.clickedButton() == overrideBtn2);

                    if (overrideAll2) {
                        txMgr->uploadFolder(localFolder, targetUri.remotePath, {}, true);
                        return;
                    }

                    QPointer<MainWindow> guard(this);
                    QPointer<remote::RemoteTransferManager> mgr(txMgr);
                    connect(txMgr, &remote::RemoteTransferManager::uploadConflictsDetected,
                            this, [guard, mgr, localFolder](const QStringList &conflicts,
                                  const QString & /*remoteDir*/, const QStringList & /*lPaths*/) {
                        if (guard.isNull() || mgr.isNull()) return;
                        auto *dlg = new TransferConflictDialog(conflicts, guard);
                        dlg->setAttribute(Qt::WA_DeleteOnClose);
                        if (dlg->exec() == QDialog::Accepted) {
                            QHash<QString, remote::RemoteTransferManager::ConflictResolution> resMap;
                            for (auto it = dlg->result().cbegin(); it != dlg->result().cend(); ++it) {
                                resMap.insert(it.key(),
                                    it.value() == TransferConflictDialog::Skip
                                        ? remote::RemoteTransferManager::ConflictResolution::Skip
                                        : remote::RemoteTransferManager::ConflictResolution::Replace);
                            }
                            mgr->resolveUploadConflicts(resMap, dlg->overrideAll());
                        }
                    }, Qt::SingleShotConnection);
                    txMgr->uploadFolder(localFolder, targetUri.remotePath);
                });
                menu->addAction(ulFolderAction);
            }
        }
        if (isDir) {
            auto *openTerminal = new QAction(tr("Open Terminal Here"), menu);
            if (remote::isSshUri(wsRoot)) {
                remote::ExecutionContext *ctx = nullptr;
                const remote::SshUri uri = remote::parseSshUri(wsRoot);
                if (uri.valid) {
                    if (remote::ExecutionContextRegistry *registry = app ? app->getExecutionContextRegistry() : nullptr) {
                        ctx = registry->remoteContext(uri.profileId);
                    }
                }
                const bool remoteReady = ctx && ctx->isRemote()
                    && ctx->state() == remote::ExecutionContext::State::Connected;
                openTerminal->setEnabled(remoteReady);
                connect(openTerminal, &QAction::triggered, this, [this, wsRoot, absPath]() {
                    const remote::SshUri uri = remote::parseSshUri(wsRoot);
                    if (!uri.valid) return;
                    remote::ExecutionContextRegistry *registry = app ? app->getExecutionContextRegistry() : nullptr;
                    remote::ExecutionContext *ctx = registry ? registry->remoteContext(uri.profileId) : nullptr;
                    // absPath is an ssh:// URI after resolvedFilePath fix — extract the
                    // POSIX remote path before passing to resolveForContext (which expects
                    // a POSIX path, not an SSH URI).
                    const QString remotePath = remote::parseSshUri(absPath).remotePath;
                    const QString cwd = TerminalCwdResolver::resolveForContext(ctx, remotePath);
                    if (!ctx || !ctx->isRemote() || cwd.isEmpty()) {
                        return;
                    }
                    terminalManager->openRemoteTerminal(ctx, cwd, QString());
                });
            } else {
                connect(openTerminal, &QAction::triggered, this, [this, absPath]() {
                    terminalManager->openTerminal(absPath);
                });
            }
            menu->addAction(openTerminal);
        }

        // --- Find in Folder (local directories only) ---
        if (isDir && !isSshDock) {
            auto *findInFolder = new QAction(tr("Find in Folder..."), menu);
            connect(findInFolder, &QAction::triggered, this, [this, absPath, wsRoot]() {
                findInFolderDock->setFolder(absPath, wsRoot);
            });
            menu->addAction(findInFolder);
        }

        // --- Export via Pandoc (markdown files only, local workspaces only) ---
        if (!isDir && !isSshDock) {
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
        // Move to Trash is a local OS operation — not available for remote SSH paths.
        moveToTrash->setEnabled(!isSshDock);
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
        connect(deletePermanently, &QAction::triggered, this, [this, dock, absPath, isSshDock, isDir]() {
            const QString displayPath = isSshDock ? remote::parseSshUri(absPath).remotePath : absPath;
            const QString name = QFileInfo(displayPath).fileName();
            if (isSshDock) {
                remote::RemoteFsBackend *backend = dock ? dock->remoteBackend() : nullptr;
                if (!backend) return;
                const remote::SshUri uri = remote::parseSshUri(absPath);
                if (!uri.valid) return;
                if (isDir) {
                    QMessageBox::information(this, tr("Delete Permanently"),
                        tr("Recursive directory deletion is not supported for remote SSH workspaces.\n"
                           "Use the terminal to run: rm -rf \"%1\"").arg(uri.remotePath));
                    return;
                }
                if (QMessageBox::warning(this, tr("Delete Permanently"),
                        tr("Are you sure you want to permanently delete \"%1\"? This cannot be undone.").arg(name),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                    return;
                backend->unlinkAsync(uri.remotePath,
                    [guard = QPointer<MainWindow>(this), name](bool unlinkOk, const QString &error) {
                        if (guard && !unlinkOk)
                            QMessageBox::warning(guard, tr("Delete Permanently"),
                                tr("Could not delete \"%1\": %2").arg(name, error));
                    });
            } else {
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
            }
        });
        deleteMenu->addAction(deletePermanently);
        menu->addMenu(deleteMenu);

        menu->addSeparator();

        // --- Send to AI ---
        AiAgentDock *targetDock = activeAiDock();
        if (targetDock) {
            auto *sendToAi = new QAction(tr("Send to AI"), menu);
            connect(sendToAi, &QAction::triggered, this, [this, absPath, isSshDock, isDir, wsRoot]() {
                AiAgentDock *dock = activeAiDock();
                if (!dock) return;
                // For SSH workspaces both absPath and wsRoot are ssh:// URIs.
                // Strip the remote workspace root path from the remote file path.
                QString relPath;
                if (isSshDock) {
                    const QString remotePath = remote::parseSshUri(absPath).remotePath;
                    const QString remoteRoot = remote::parseSshUri(wsRoot).remotePath;
                    relPath = remotePath;
                    if (!remoteRoot.isEmpty() && relPath.startsWith(remoteRoot)) {
                        relPath = relPath.mid(remoteRoot.length());
                        if (relPath.startsWith(QLatin1Char('/')))
                            relPath = relPath.mid(1);
                    }
                } else {
                    relPath = absPath;
                    const QString &prefix = wsRoot;
                    if (!prefix.isEmpty() && relPath.startsWith(prefix)) {
                        relPath = relPath.mid(prefix.length());
                        if (relPath.startsWith(QLatin1Char('/')) || relPath.startsWith(QLatin1Char('\\')))
                            relPath = relPath.mid(1);
                    }
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
        // Interactive rebase is local-only (D7): it relies on the notepadai-editor
        // helper IPC-ing back to this local window, which a remote host has no
        // channel to. The action is also disabled in the menu for remote
        // workspaces; this is the authoritative runtime guard.
        if (remote::ExecutionContext *ctx = activeExecutionContext(); ctx && ctx->isRemote()) {
            return;
        }
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

        if (remote::isSshUri(path)) {
            // --- SSH workspace: 4-step non-blocking auto-reconnect (D10) ---
            // (1) Create the dock immediately in Connecting state so layout
            //     restores correctly (the dock exists before the connection is up).
            // (2) Parse the URI → look up Ssh/Profiles by profileId.
            // (3) registry->connect(profileId) (worker thread; secret fetch on
            //     worker; UI never blocks).
            // (4) The dock subscribes to stateChanged: Connecting → spinner;
            //     Connected → populate tree; Failed → inline Reconnect (NO modal).
            const remote::SshUri uri = remote::parseSshUri(path);
            if (!uri.valid) continue;

            remote::SshProfileRegistry *profiles =
                app ? app->getSshProfileRegistry() : nullptr;
            remote::ExecutionContextRegistry *contexts =
                app ? app->getExecutionContextRegistry() : nullptr;
            if (!profiles || !contexts) continue;
            if (!profiles->contains(uri.profileId)) continue;

            // openFolderAsWorkspacePath handles the dock creation, dedup, and
            // SSH-specific wiring (setSshWorkspaceUri, title, connecting state).
            openFolderAsWorkspacePath(path);

            // Now find the dock we just created and wire it to the context.
            FolderAsWorkspaceDock *dock = nullptr;
            for (FolderAsWorkspaceDock *d : findChildren<FolderAsWorkspaceDock *>()) {
                if (d->sshWorkspaceUri() == path) { dock = d; break; }
            }
            if (!dock) continue;

            // (3) Start the background connect (worker thread; UI never blocks).
            auto *ctx = contexts->connect(uri.profileId);
            if (!ctx) continue;

            // (4) Wire the dock to the context's stateChanged for the lifecycle.
            wireSshDockToContext(dock, ctx);
            continue;
        }

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

    const bool isSsh = remote::isSshUri(activePath);
    const QString cleanedActive = isSsh ? activePath : QDir::cleanPath(activePath);
    for (FolderAsWorkspaceDock *d : findChildren<FolderAsWorkspaceDock *>()) {
        const QString dRoot = d->rootPath();
        const QString dCleaned = remote::isSshUri(dRoot) ? dRoot : QDir::cleanPath(dRoot);
        if (dCleaned == cleanedActive) {
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

remote::ExecutionContext *MainWindow::activeExecutionContext() const
{
    remote::ExecutionContextRegistry *registry =
        app ? app->getExecutionContextRegistry() : nullptr;
    if (!registry) {
        return nullptr; // very early startup; callers null-check
    }

    // If the active workspace is SSH-backed (its rootPath is an ssh:// URI) and
    // that profile has a live remote context, return it; otherwise the shared
    // local context. Mirrors activeWorkspaceDock()'s resolution. (Phase 1 does
    // not yet write workspace ssh:// URIs from the tree, so in practice this
    // resolves to local until Phase 2 — but the resolution path exists now.)
    if (FolderAsWorkspaceDock *dock = activeWorkspaceDock()) {
        const QString root = dock->rootPath();
        if (remote::isSshUri(root)) {
            const remote::SshUri uri = remote::parseSshUri(root);
            if (uri.valid) {
                if (auto *ctx = registry->remoteContext(uri.profileId)) {
                    return ctx;
                }
            }
        }
    }
    return registry->localContext();
}

#ifndef NDEBUG
void MainWindow::rebindSshDebugDialog()
{
    if (!m_sshDebugDialog)
        return;
    remote::SshConnection *conn = nullptr;
    if (auto *ctx = qobject_cast<remote::RemoteExecutionContext *>(activeExecutionContext()))
        conn = ctx->connection();
    m_sshDebugDialog->bindToConnection(conn);
}
#endif

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
    if (remote::isSshUri(p)) return p;
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
    if (!editor) return;

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
    // Early out. If we aren't exiting-on-last-tab-closed and the thing being
    // closed is a single pristine "initial" editor, closing it would just force
    // an immediate respawn — so keep it. But ONLY when it is genuinely the last
    // tab of ANY kind (totalTabCount() == 1): getInitialEditor() looks at
    // editorCount() alone, which ignores preview/browser/mini-app tabs, so
    // without the totalTabCount() guard this would wrongly refuse to close a
    // pristine "New X" while a preview/browser tab is still open. If another tab
    // survives, closing this editor leaves it behind without spawning anything —
    // exactly what the user wants — so don't block it.
    if (!app->getSettings()->exitOnLastTabClosed()
        && getInitialEditor() != Q_NULLPTR
        && dockedEditor->totalTabCount() == 1) {
        return;
    }

    if (!checkEditorsBeforeClose({editor})) {
        return;
    }

    editor->close();

    // No respawn/exit decision here. editor->close() removes the dock widget; if
    // it was the last tab of any kind, DockedEditor::lastTabClosed fires and
    // handleEditorAreaEmptied() does the exit-or-newFile decision centrally.
}

void MainWindow::handleEditorAreaEmptied()
{
    // Reached via a queued DockedEditor::lastTabClosed, so the ADS close cascade
    // has fully unwound and addDockWidget is safe again.

    // During application shutdown the window tears its own tabs down; never
    // resurrect a buffer then.
    if (m_isClosing) {
        return;
    }

    // Defensive re-check: between the signal and this queued slot the user could
    // have opened something (CLI drop, session, a new tab). Only act if the area
    // is genuinely still empty — covers every tab kind, not just editors.
    if (dockedEditor->totalTabCount() != 0) {
        return;
    }

    if (app->getSettings()->exitOnLastTabClosed()) {
        close();
    }
    else {
        newFile();
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

    // No respawn here either — if a browser/preview tab survives, the area is
    // not empty and must stay as-is; if nothing remains, lastTabClosed fires and
    // handleEditorAreaEmptied() spawns the fresh buffer. See that handler.
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
    ScintillaNext *editor = currentEditor();
    if (!editor) return false;
    return saveFile(editor);
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
    if (!editor) return false;

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
    ScintillaNext *editor = currentEditor();
    if (!editor) return false;
    return saveFileAs(editor, fileName);
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
    ScintillaNext *editor = currentEditor();
    if (!editor) return false;
    const QString languageName = editor->languageName;

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
    if (!editor) return false;

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
    if (!editor) return;
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

bool MainWindow::renameWorkspaceEntry(const QString &oldPath, const QString &newPath, bool isDir)
{
    const QString oldClean = QDir::cleanPath(oldPath);
    const QString newClean = QDir::cleanPath(newPath);
    const QString newName = QFileInfo(newClean).fileName();

    // A case-only rename on a case-insensitive FS (foo.txt -> Foo.txt) maps to
    // the same inode, so it is NOT a real collision and Qt's plain rename would
    // refuse it. Detect it up front.
    const bool caseOnlySelf =
        oldClean.compare(newClean, Qt::CaseInsensitive) == 0 && oldClean != newClean;

    // Collision check (mirrors New File / New Folder), excluding the case-only
    // self-match above.
    if (!caseOnlySelf && QFileInfo::exists(newClean)) {
        QMessageBox::warning(this, tr("Rename"),
            tr("A file or folder named \"%1\" already exists.").arg(newName));
        return false;
    }

    // Snapshot affected open editors BEFORE the disk op (old paths still
    // resolve). Folder: rebase every editor whose file lives beneath it; file:
    // the single exact match. Case-insensitive compare matches
    // EditorManager::getEditorByFilePath's convention.
    QVector<QPair<QPointer<ScintillaNext>, QString>> rebase;
    const QString prefix = oldClean + QLatin1Char('/');
    for (ScintillaNext *e : app->getEditorManager()->getEditors()) {
        if (!e || !e->isFile() || e->isRemote()) continue;
        const QString p = QDir::cleanPath(e->getFileInfo().absoluteFilePath());
        QString rebased;
        if (isDir) {
            if (p.compare(oldClean, Qt::CaseInsensitive) == 0)
                rebased = newClean;
            else if (p.startsWith(prefix, Qt::CaseInsensitive))
                rebased = newClean + p.mid(oldClean.length());
        } else if (p.compare(oldClean, Qt::CaseInsensitive) == 0) {
            rebased = newClean;
        }
        if (!rebased.isEmpty())
            rebase.append({QPointer<ScintillaNext>(e), QDir::cleanPath(rebased)});
    }

    // Perform the on-disk rename. On ANY failure: warn, return, and crucially
    // DO NOT rebase — leaving editors on the still-valid old path (no spurious
    // FileMissing). renameOnDisk handles the case-only two-step + rollback.
    if (!renameOnDisk(oldClean, newClean, caseOnlySelf)) {
        QMessageBox::warning(this, tr("Rename"),
            tr("Could not rename \"%1\".").arg(QFileInfo(oldClean).fileName()));
        return false;
    }

    // Success only: rebase open tabs synchronously, before returning to the
    // event loop, so FileWatcher's old->new remap (driven by renamed()) lands
    // before any queued fileChanged(oldPath) can be dispatched.
    for (const auto &entry : rebase) {
        if (entry.first)
            entry.first->updatePathAfterMove(entry.second);
    }

    return true;
}

bool MainWindow::renameOnDisk(const QString &oldClean, const QString &newClean, bool caseOnly)
{
    QDir parent = QFileInfo(oldClean).absoluteDir();
    const QString oldName = QFileInfo(oldClean).fileName();
    const QString newName = QFileInfo(newClean).fileName();

    if (!caseOnly)
        return parent.rename(oldName, newName);

    // Case-only on a case-insensitive FS: QDir::rename refuses (target "exists"
    // = same inode). Go via a guaranteed-unique temp.
    QString tempName;
    do {
        tempName = oldName + QStringLiteral(".rename-")
                 + QUuid::createUuid().toString(QUuid::Id128);
    } while (parent.exists(tempName));

    if (!parent.rename(oldName, tempName))
        return false; // step 1 failed: nothing moved, entry intact at oldName

    if (!parent.rename(tempName, newName)) {
        // Step 2 failed — roll back so the entry is never stranded under temp.
        if (!parent.rename(tempName, oldName)) {
            // Rollback also failed (extremely rare): tell the user exactly where
            // the entry is so it is never silently lost.
            QMessageBox::warning(this, tr("Rename"),
                tr("Rename failed and the original name could not be restored. "
                   "The item is currently named \"%1\" in the same folder.")
                   .arg(tempName));
        }
        return false;
    }
    return true;
}

void MainWindow::renameFile()
{
    ScintillaNext *editor = currentEditor();
    if (!editor) return;

    if (editor->isFile()) {
        if (editor->isRemote()) {
            const remote::SshUri uri = remote::parseSshUri(editor->getFilePath());
            if (!uri.valid) return;
            remote::ExecutionContextRegistry *registry = app ? app->getExecutionContextRegistry() : nullptr;
            if (!registry) return;
            remote::ExecutionContext *ctx = registry->remoteContext(uri.profileId);
            if (!ctx) return;
            auto *backend = qobject_cast<remote::RemoteFsBackend *>(ctx->fsBackend());
            if (!backend) return;

            const QString oldName = uri.remotePath.mid(uri.remotePath.lastIndexOf(QLatin1Char('/')) + 1);
            bool ok = false;
            const QString newName = QInputDialog::getText(this, tr("Rename"),
                tr("New name:"), QLineEdit::Normal, oldName, &ok);
            if (!ok || newName.isEmpty() || newName == oldName) return;
            if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\'))
                || newName.contains(QStringLiteral(".."))) {
                QMessageBox::warning(this, tr("Rename"), tr("Invalid name."));
                return;
            }

            const QString parentDir = remote::posixParentPath(uri.remotePath);
            const QString newRemotePath = parentDir + QLatin1Char('/') + newName;
            const QString newUri = remote::formatSshUri(uri.profileId, newRemotePath);
            QPointer<ScintillaNext> guard(editor);
            QPointer<MainWindow> self(this);

            auto doRename = [backend, guard, self, uri, newRemotePath, newUri, newName]() {
                if (!guard || !self) return;
                backend->renameAsync(uri.remotePath, newRemotePath,
                    [guard, self, newRemotePath, newUri, newName](bool renameOk, const QString &error) {
                        if (!self) return;
                        if (!renameOk) {
                            QMessageBox::warning(self, tr("Rename"),
                                tr("Could not rename to \"%1\": %2").arg(newName, error));
                            return;
                        }
                        if (guard) {
                            guard->setRemoteIdentity(newRemotePath, newUri);
                        }
                    });
            };

            if (!editor->isSavedToDisk()) {
                auto handled = std::make_shared<bool>(false);
                connect(editor, &ScintillaNext::saved, this, [=]() {
                    if (*handled) return;
                    *handled = true;
                    doRename();
                }, Qt::SingleShotConnection);
                connect(editor, &ScintillaNext::saveFailed, this, [=](const QString &error) {
                    if (*handled) return;
                    *handled = true;
                    if (!self) return;
                    QMessageBox::warning(self, tr("Rename"),
                        tr("Cannot rename \"%1\": save failed — %2").arg(newName, error));
                }, Qt::SingleShotConnection);
                editor->save();
            } else {
                doRename();
            }
            return;
        }

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
    if (!editor) return;

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
    ScintillaNext *editor = currentEditor();
    if (!editor) return;

    QPrintPreviewDialog printDialog(this, Qt::Window);
    EditorPrintPreviewRenderer renderer(editor);

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
    if (!editor) return;

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
    if (!editor) return;
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

    updatePreviewActionForEditor(editor);
}

void MainWindow::updatePreviewActionForEditor(ScintillaNext *editor)
{
    if (!m_actionPreview) return;

    auto *mgr = app->getPreviewTabManager();
    // One resolver behind enabled-state, tooltip, and checked: the action is
    // enabled iff the active editor resolves to a registered preview type.
    const bool canPreview = mgr && mgr->canPreviewEditor(editor);
    m_actionPreview->setEnabled(canPreview);

    if (canPreview) {
        const QString typeName = mgr->previewTypeName(editor);
        // "Preview Markdown" / "Preview HTML" / "Preview CSV" / … — fall back to
        // the plain label if the type carries no display name.
        m_actionPreview->setToolTip(typeName.isEmpty()
                                        ? tr("Preview")
                                        : tr("Preview %1").arg(typeName));
    } else {
        m_actionPreview->setToolTip(tr("Preview"));
    }

    bool previewActive = false;
    if (canPreview)
        previewActive = mgr->previewForEditor(editor) != nullptr;
    m_actionPreview->setChecked(previewActive);
}

void MainWindow::retintPreviewActionIcon()
{
    if (!m_actionPreview) return;
    m_actionPreview->setIcon(makeTintedIcon(QStringLiteral(":/icons/eye.svg"),
                                            palette().color(QPalette::ButtonText)));
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
    ScintillaNext *editor = currentEditor();
    if (!editor) return;
    // Not sure if Scintilla's zoom level matches up to an exact percentage, but visibly this is close
    FadingIndicator::showText(editor, tr("Zoom: %1%").arg(zoomLevel * 10 + 100));
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
        if (fileWatcher) fileWatcher->watchEditor(editor);
    });
    connect(editor, &ScintillaNext::closed, this, [this, editor]() {
        if (fileWatcher) fileWatcher->unwatchEditor(editor);
    });
    connect(editor, &QObject::destroyed, this, [this, editor]() {
        if (fileWatcher) fileWatcher->unwatchEditor(editor);
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
        if (editor == currentEditor())
            updatePreviewActionForEditor(editor);
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

static bool compareVersions(const QString &remote, const QString &local)
{
    static QRegularExpression re("v?(\\d+)(?:\\.(\\d+))?(?:\\.(\\d+))?");
    QRegularExpressionMatch remoteMatch = re.match(remote);
    QRegularExpressionMatch localMatch = re.match(local);

    if (!remoteMatch.hasMatch() || !localMatch.hasMatch())
        return false;

    for (int i = 1; i <= 3; ++i) {
        int r = remoteMatch.captured(i).toInt();
        int l = localMatch.captured(i).toInt();
        if (r > l) return true;
        if (l > r) return false;
    }
    return false;
}

void MainWindow::checkForUpdates(bool silent)
{
#ifdef Q_OS_WIN
    qInfo(Q_FUNC_INFO);

    auto *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl("https://api.github.com/repos/nullmastermind/NotepadAI/releases/latest"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("NotepadAI/%1").arg(APP_VERSION));
    request.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager, silent]() {
        reply->deleteLater();
        manager->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            if (!silent)
                QMessageBox::warning(this, QString(), tr("Failed to check for updates: %1").arg(reply->errorString()));
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (doc.isNull()) {
            if (!silent)
                QMessageBox::warning(this, QString(), tr("Failed to parse update information."));
            return;
        }

        QJsonObject obj = doc.object();
        QString tagName = obj.value("tag_name").toString();
        QString htmlUrl = obj.value("html_url").toString();
        QString body = obj.value("body").toString();

        if (compareVersions(tagName, APP_VERSION)) {
            QString qtSuffix = QStringLiteral("qt%1%2").arg(QT_VERSION_MAJOR).arg(QT_VERSION_MINOR);
            QString downloadUrl;
            QJsonArray assets = obj.value("assets").toArray();
            for (const auto &val : assets) {
                QString name = val.toObject().value("name").toString();
                if (name.contains(qtSuffix) && name.contains("win64") && name.endsWith(".zip")) {
                    downloadUrl = val.toObject().value("browser_download_url").toString();
                    break;
                }
            }
            if (downloadUrl.isEmpty()) {
                for (const auto &val : assets) {
                    QString name = val.toObject().value("name").toString();
                    if (name.contains(qtSuffix) && name.contains("Installer") && name.endsWith(".exe")) {
                        downloadUrl = val.toObject().value("browser_download_url").toString();
                        break;
                    }
                }
            }

            QString text = tr("A new version <b>%1</b> is available.").arg(tagName);
            if (!body.isEmpty())
                text += tr("<br><br><b>Release notes:</b><br>%1").arg(body.left(500).toHtmlEscaped().replace("\n", "<br>"));
            text += tr("<br><br>Would you like to open the download page?");

            auto result = QMessageBox::information(this, tr("Update Available"), text,
                                                   QMessageBox::Yes | QMessageBox::No);
            if (result == QMessageBox::Yes) {
                QDesktopServices::openUrl(QUrl(downloadUrl.isEmpty() ? htmlUrl : downloadUrl));
            }
        } else if (!silent) {
            QMessageBox::information(this, QString(), tr("No updates are available at this time."));
        }
    });

    app->getSettings()->setValue("App/LastUpdateCheck", QDateTime::currentDateTime());
#else
    Q_UNUSED(silent);
#endif
}

void MainWindow::checkForUpdatesFinished(const QString &url)
{
    Q_UNUSED(url);
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

    // Past the point of no return: the window is closing. Latch this so the
    // queued handleEditorAreaEmptied() slot won't respawn a "New X" buffer as
    // the editor tabs get torn down below. Set only after the cancel guard above
    // so an ignored close leaves the window fully usable.
    m_isClosing = true;

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

    // SSH remote gates: disable local-only actions, enable remote-capable ones.
    {
        const bool isRemote = editor->isRemote();
        ui->actionShowInExplorer->setEnabled(editor->isFile() && !isRemote);
        ui->actionSaveAs->setEnabled(!isRemote);
        if (isRemote) {
            ui->actionSaveAs->setToolTip(tr("Not available for remote files"));
        } else {
            ui->actionSaveAs->setToolTip(QString());
        }

        const QString workspaceRoot = currentWorkspaceRoot();
        const QString activeFilePath = editor->isFile() ? editor->getFilePath() : QString();
        remote::ExecutionContext *ctx = activeExecutionContext();
        ui->actionOpenTerminalInFolder->setEnabled(
            TerminalCwdResolver::canOpenInFolderForContext(ctx, activeFilePath, editor->isFile(), workspaceRoot));
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
    if (!editor) return;
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

        const QString key = remote::isSshUri(s.rootPath) ? s.rootPath
                                                            : QDir::cleanPath(s.rootPath);
        m_workspaceStateMemo.insert(key, s);
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
        const QString key = remote::isSshUri(s.rootPath) ? s.rootPath
                                                         : QDir::cleanPath(s.rootPath);
        m_workspaceStateMemo.insert(key, s);
    }
    writeWorkspaceStatesArray(app->getSettings(), m_workspaceStateMemo);
}

void MainWindow::persistOneWorkspaceState(const WorkspaceStateSnapshot &snapshot) const
{
    if (snapshot.rootPath.isEmpty()) return;
    const QString key = remote::isSshUri(snapshot.rootPath) ? snapshot.rootPath
                                                            : QDir::cleanPath(snapshot.rootPath);
    m_workspaceStateMemo.insert(key, snapshot);
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

void MainWindow::setupSshMenu()
{
    // Gate the SSH actions when the menu is about to show. "Open Remote
    // Terminal" is enabled only when a remote context is currently Connected
    // (the active SSH workspace, or the last one we connected to this session).
    connect(ui->menuFile, &QMenu::aboutToShow, this, [this]() {
        remote::ExecutionContext *ctx = activeExecutionContext();
        bool remoteReady = ctx && ctx->isRemote()
            && ctx->state() == remote::ExecutionContext::State::Connected;
        if (!remoteReady && m_lastConnectedRemote
            && m_lastConnectedRemote->state() == remote::ExecutionContext::State::Connected) {
            remoteReady = true;
        }
        ui->actionOpenRemoteTerminal->setEnabled(remoteReady);
    });

    connect(ui->actionOpenRemoteTerminal, &QAction::triggered, this, [this]() {
        remote::ExecutionContext *ctx = activeExecutionContext();
        if (!ctx || !ctx->isRemote()
            || ctx->state() != remote::ExecutionContext::State::Connected) {
            // Fall back to the last context we connected this session.
            ctx = m_lastConnectedRemote.data();
        }
        if (!ctx || !ctx->isRemote()
            || ctx->state() != remote::ExecutionContext::State::Connected) {
            return; // gated in aboutToShow; bail with no silent fallback
        }
        // "Open Remote Terminal" has no associated directory. Pass an empty cwd
        // so SshPtyProcess skips the `cd` entirely and the SSH session starts in
        // the remote user's $HOME by default. resolveCwd("") is intentionally
        // NOT used here — it would pick up the profile's lastRemotePath, which
        // may be a local path that doesn't exist on the remote host.
        terminalManager->openRemoteTerminal(ctx, QString(), QString());
    });

    // Crash breadcrumbs for the new actions (the ActionAddedFilter also catches
    // these, but wire explicitly to be robust to ordering).
    wireActionForCrashContext(ui->actionOpenRemoteTerminal);

    // "Open Remote Folder via SSH…" — the first item of the Open Folder as
    // Workspace submenu (D10). Profile pick → staged connect → folder picker →
    // open the remote workspace dock.
    connect(ui->actionOpenRemoteFolderSsh, &QAction::triggered, this,
            &MainWindow::openRemoteFolderViaSshFlow);
    wireActionForCrashContext(ui->actionOpenRemoteFolderSsh);
}

void MainWindow::openRemoteFolderViaSshFlow()
{
    remote::SshProfileRegistry *profiles = app ? app->getSshProfileRegistry() : nullptr;
    remote::ExecutionContextRegistry *contexts =
        app ? app->getExecutionContextRegistry() : nullptr;
    if (!profiles || !contexts) return;

    // Step 1: profile selection (reuse the SSH Connection Manager dialog).
    // Stack-allocated modal.
    SshConnectionManagerDialog dlg(profiles,
                                   app ? app->getCredentialStore() : nullptr, this);

    // The committed ssh:// URI to open once the manager dialog closes. Opening
    // the workspace AFTER dlg.exec() returns keeps the dialog stack unwound.
    QString committedUri;

    connect(&dlg, &SshConnectionManagerDialog::connectRequested, this,
            [this, contexts, profiles, &dlg, &committedUri](const QString &profileId) {
        // Step 2: staged connect (reuse the P1 SshConnectDialog: Connecting →
        // Authenticating → Ready, with Cancel + error/Retry at each stage).
        remote::RemoteExecutionContext *ctx = contexts->connect(profileId);
        if (!ctx) return;
        if (ctx->state() != remote::ExecutionContext::State::Connected) {
            SshConnectDialog connectDlg(ctx, &dlg);
            if (connectDlg.exec() != QDialog::Accepted
                || ctx->state() != remote::ExecutionContext::State::Connected) {
                return;
            }
        }
        m_lastConnectedRemote = ctx;

        // Step 3: remote folder picker (SFTP readdir tree). Start at the remote
        // root so the whole filesystem is navigable; the user drills down and
        // selects a directory as the workspace root.
        auto *backend = qobject_cast<remote::RemoteFsBackend *>(ctx->fsBackend());
        if (!backend) return;
        SshRemoteFolderPickerDialog picker(backend, QStringLiteral("/"), &dlg);
        if (picker.exec() != QDialog::Accepted) {
            return; // cancelled — nothing opened
        }
        const QString remotePath = picker.selectedPath();
        if (remotePath.isEmpty()) return;

        // Commit and dismiss the manager. The workspace is opened after exec()
        // returns (below) so the modal stack is fully unwound first.
        committedUri = remote::formatSshUri(profileId, remotePath);
        dlg.accept();
    });

    dlg.exec();

    // Step 4: open the remote workspace dock. openFolderAsWorkspacePath detects
    // the ssh:// prefix, wires useRemoteBackend, and persists the URI.
    if (!committedUri.isEmpty()) {
        openFolderAsWorkspacePath(committedUri);
    }
}

void MainWindow::wireSshDockToContext(FolderAsWorkspaceDock *dock,
                                      remote::ExecutionContext *ctx)
{
    if (!dock || !ctx) return;

    // Idempotency: only wire a given (dock, ctx) pair once. The connect/reconnect
    // paths can both reach here, and the reconnect button can fire repeatedly.
    // Store the wired context pointer (as an integer) in a dynamic property; bail
    // on the duplicate-connect work if it matches, but still re-sync the visual.
    const char *wiredKey = "sshWiredContext";
    const quintptr ctxId = reinterpret_cast<quintptr>(ctx);
    const bool alreadyWired =
        dock->property(wiredKey).toULongLong() == static_cast<qulonglong>(ctxId);
    if (!alreadyWired) {
        dock->setProperty(wiredKey, static_cast<qulonglong>(ctxId));

        const QString wsUri = dock->sshWorkspaceUri();
        const remote::SshUri uri = remote::parseSshUri(wsUri);

        // Derive "user@host" for the inline status text.
        QString userHost;
        remote::SshProfileRegistry *profiles = app ? app->getSshProfileRegistry() : nullptr;
        if (profiles && uri.valid && profiles->contains(uri.profileId)) {
            const remote::SshProfile p = profiles->profile(uri.profileId);
            userHost = p.username.isEmpty() ? p.host : (p.username + QLatin1Char('@') + p.host);
        }

        QPointer<FolderAsWorkspaceDock> dockGuard(dock);
        QPointer<remote::ExecutionContext> ctxGuard(ctx);

        // stateChanged drives the dock's inline banner. Connected → wire the
        // backend + populate tree. Failed → inline Reconnect (never a modal).
        connect(ctx, &remote::ExecutionContext::stateChanged, dock,
                [this, dockGuard, ctxGuard, userHost, uri](remote::ExecutionContext::State state) {
            if (!dockGuard || !ctxGuard) return;
            switch (state) {
            case remote::ExecutionContext::State::Connecting:
            case remote::ExecutionContext::State::Reconnecting:
                dockGuard->showConnectingState(userHost);
                break;
            case remote::ExecutionContext::State::Connected: {
                auto *rctx = qobject_cast<remote::RemoteExecutionContext *>(ctxGuard.data());
                auto *backend = rctx ? qobject_cast<remote::RemoteFsBackend *>(rctx->fsBackend())
                                     : nullptr;
                if (backend) {
                    dockGuard->useRemoteBackend(backend);
                    dockGuard->setRootPath(uri.remotePath);
                }
                dockGuard->showConnectedState();
                break;
            }
            case remote::ExecutionContext::State::Failed:
                dockGuard->showFailedState(tr("Could not reach the remote host."));
                break;
            case remote::ExecutionContext::State::Disconnected:
                // No-op visual; a deliberate disconnect (workspace closed) tears
                // the dock down anyway. A mid-session drop comes through
                // connectionLost.
                break;
            }
        });

        // Mid-session drop → "Connection lost — Reconnect" banner (D10).
        connect(ctx, &remote::ExecutionContext::connectionLost, dock,
                [dockGuard](const QString &reason) {
            if (dockGuard) dockGuard->showConnectionLostState(reason);
        });

        // FIX-2 back-pressure indicator: surface channelQueued / channelReady from
        // the underlying SshConnection while a channel open is queued.
        if (auto *rctx = qobject_cast<remote::RemoteExecutionContext *>(ctx)) {
            if (remote::SshConnection *conn = rctx->connection()) {
                connect(conn, &remote::SshConnection::channelQueued, dock,
                        [dockGuard](int) { if (dockGuard) dockGuard->showChannelQueuedState(); });
                connect(conn, &remote::SshConnection::channelReady, dock,
                        [dockGuard](int) { if (dockGuard) dockGuard->clearChannelQueuedState(); });
            }
        }
    }

    // Drive the CURRENT state immediately — stateChanged may already have fired
    // before we connected (e.g. the context connected synchronously, or we are
    // re-wiring an already-Connected context). This makes the banner reflect
    // reality without waiting for the next transition.
    const QString wsUri = dock->sshWorkspaceUri();
    const remote::SshUri uri = remote::parseSshUri(wsUri);
    switch (ctx->state()) {
    case remote::ExecutionContext::State::Connected: {
        auto *rctx = qobject_cast<remote::RemoteExecutionContext *>(ctx);
        auto *backend = rctx ? qobject_cast<remote::RemoteFsBackend *>(rctx->fsBackend())
                             : nullptr;
        if (backend) {
            dock->useRemoteBackend(backend);
            dock->setRootPath(uri.remotePath);
        }
        dock->showConnectedState();
        break;
    }
    case remote::ExecutionContext::State::Connecting:
    case remote::ExecutionContext::State::Reconnecting: {
        QString userHost;
        remote::SshProfileRegistry *profiles = app ? app->getSshProfileRegistry() : nullptr;
        if (profiles && uri.valid && profiles->contains(uri.profileId)) {
            const remote::SshProfile p = profiles->profile(uri.profileId);
            userHost = p.username.isEmpty() ? p.host : (p.username + QLatin1Char('@') + p.host);
        }
        dock->showConnectingState(userHost);
        break;
    }
    case remote::ExecutionContext::State::Failed:
        dock->showFailedState(tr("Could not reach the remote host."));
        break;
    case remote::ExecutionContext::State::Disconnected:
        break;
    }
}

void MainWindow::reconnectSshWorkspace(FolderAsWorkspaceDock *dock)
{
    if (!dock) return;
    const QString wsUri = dock->sshWorkspaceUri();
    if (wsUri.isEmpty()) return;
    const remote::SshUri uri = remote::parseSshUri(wsUri);
    if (!uri.valid) return;

    remote::ExecutionContextRegistry *contexts =
        app ? app->getExecutionContextRegistry() : nullptr;
    if (!contexts) return;

    auto *ctx = contexts->connect(uri.profileId);
    if (!ctx) return;

    wireSshDockToContext(dock, ctx);
}

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
