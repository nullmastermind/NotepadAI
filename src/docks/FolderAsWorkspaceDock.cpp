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


#include "FolderAsWorkspaceDock.h"
#include "ApplicationSettings.h"
#include "FolderAsWorkspaceFsModel.h"
#include "FolderAsWorkspaceProxyModel.h"
#include "GitCommitView.h"
#include "GitController.h"
#include "GitDiffViewController.h"
#include "GitStatusModel.h"
#include "GitTabWidget.h"
#include "NotepadNextApplication.h"
#include "ProfileScope.h"
#include "SubmoduleStatusFetcher.h"
#include "ui_FolderAsWorkspaceDock.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHelpEvent>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMetaObject>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

// Deprecated as of the multi-workspace refactor. The authoritative state now lives
// in FolderAsWorkspace/Workspaces (list) and FolderAsWorkspace/ActiveWorkspace
// (path), both managed by MainWindow. This key is read once at app startup by
// NotepadNextApplication::init() as a one-shot migration source when the new
// list is empty, and is no longer written to from anywhere. The value left on
// disk from older versions is kept untouched for theoretical downgrade safety.
// NOLINTNEXTLINE(bugprone-throwing-static-initialization) — default is an empty QString (no allocation, cannot throw); matches the file-scope ApplicationSetting idiom used elsewhere (e.g. FileListDock).
ApplicationSetting<QString> rootPathSetting{"FolderAsWorkspace/RootPath"};

namespace {

// (No helpers — entries are read from GitStatusModel::allEntries() directly.)

} // namespace

FolderAsWorkspaceDock::FolderAsWorkspaceDock(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::FolderAsWorkspaceDock),
    model(new FolderAsWorkspaceFsModel(this)),
    proxy(new FolderAsWorkspaceProxyModel(this)),
    tooltipTimer(new QTimer(this))
{
    ui->setupUi(this);

    proxy->setSourceModel(model);
    ui->treeView->setModel(proxy);
    ui->treeView->header()->hideSection(1);
    ui->treeView->header()->hideSection(2);
    ui->treeView->header()->hideSection(3);

    wireFileTreeGitDecorations();

    connect(ui->treeView, &QTreeView::doubleClicked, this, [=](const QModelIndex &index) {
        if (!proxy->isDir(index)) {
            emit fileDoubleClicked(proxy->filePath(index));
        }
    });

    connect(ui->treeView, &QTreeView::clicked, this, [=](const QModelIndex &index) {
        if (!proxy->isDir(index)) {
            emit fileClicked(proxy->filePath(index));
        }
    });

    wireTreeContextMenu();

    const int wakeUpDelay = QApplication::style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay);
    tooltipTimer->setSingleShot(true);
    tooltipTimer->setInterval(wakeUpDelay > 0 ? wakeUpDelay : 700);
    connect(tooltipTimer, &QTimer::timeout, this, [this]() {
        QWidget *viewport = ui->treeView->viewport();
        const QPoint localPos = viewport->mapFromGlobal(QCursor::pos());
        const QModelIndex index = ui->treeView->indexAt(localPos);
        if (!index.isValid() || QPersistentModelIndex(index) != pendingTooltipIndex) {
            return;
        }
        // Route through the view's model (the proxy) so QAbstractProxyModel
        // auto-maps to the FsModel's ToolTipRole.
        const QString text = proxy->data(index, Qt::ToolTipRole).toString();
        if (text.isEmpty()) {
            return;
        }
        QToolTip::showText(QCursor::pos(), text, viewport, ui->treeView->visualRect(index));
    });

    ui->treeView->viewport()->installEventFilter(this);

    connect(ui->tabs, &QTabWidget::currentChanged, this, &FolderAsWorkspaceDock::onTabChanged);
    connect(ui->tabs, &QTabWidget::currentChanged, this, [this](int) {
        if (!m_programmaticToggle) emit stateDirty();
    });

    // Tree state restoration plumbing. directoryLoaded must be connected BEFORE
    // setRootPath fires so the very first model-load (root level) is captured.
    // It stays on the SOURCE model — it is a QFileSystemModel-specific,
    // path-carrying signal that the proxy does not relay.
    connect(model, &QFileSystemModel::directoryLoaded,
            this, &FolderAsWorkspaceDock::onDirectoryLoaded);
    connect(ui->treeView, &QTreeView::expanded,
            this, &FolderAsWorkspaceDock::onTreeExpanded);
    connect(ui->treeView, &QTreeView::collapsed,
            this, &FolderAsWorkspaceDock::onTreeCollapsed);

    // Empty default ctor — caller must invoke setRootPath() before showing the dock.
}

FolderAsWorkspaceDock::FolderAsWorkspaceDock(const QString &initialPath, QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::FolderAsWorkspaceDock),
    model(new FolderAsWorkspaceFsModel(this)),
    proxy(new FolderAsWorkspaceProxyModel(this)),
    tooltipTimer(new QTimer(this))
{
    ui->setupUi(this);

    proxy->setSourceModel(model);
    ui->treeView->setModel(proxy);
    ui->treeView->header()->hideSection(1);
    ui->treeView->header()->hideSection(2);
    ui->treeView->header()->hideSection(3);

    wireFileTreeGitDecorations();

    connect(ui->treeView, &QTreeView::doubleClicked, this, [=](const QModelIndex &index) {
        if (!proxy->isDir(index)) {
            emit fileDoubleClicked(proxy->filePath(index));
        }
    });

    connect(ui->treeView, &QTreeView::clicked, this, [=](const QModelIndex &index) {
        if (!proxy->isDir(index)) {
            emit fileClicked(proxy->filePath(index));
        }
    });

    wireTreeContextMenu();

    const int wakeUpDelay = QApplication::style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay);
    tooltipTimer->setSingleShot(true);
    tooltipTimer->setInterval(wakeUpDelay > 0 ? wakeUpDelay : 700);
    connect(tooltipTimer, &QTimer::timeout, this, [this]() {
        QWidget *viewport = ui->treeView->viewport();
        const QPoint localPos = viewport->mapFromGlobal(QCursor::pos());
        const QModelIndex index = ui->treeView->indexAt(localPos);
        if (!index.isValid() || QPersistentModelIndex(index) != pendingTooltipIndex) {
            return;
        }
        // Route through the view's model (the proxy) so QAbstractProxyModel
        // auto-maps to the FsModel's ToolTipRole.
        const QString text = proxy->data(index, Qt::ToolTipRole).toString();
        if (text.isEmpty()) {
            return;
        }
        QToolTip::showText(QCursor::pos(), text, viewport, ui->treeView->visualRect(index));
    });

    ui->treeView->viewport()->installEventFilter(this);

    connect(ui->tabs, &QTabWidget::currentChanged, this, &FolderAsWorkspaceDock::onTabChanged);
    connect(ui->tabs, &QTabWidget::currentChanged, this, [this](int) {
        if (!m_programmaticToggle) emit stateDirty();
    });

    connect(model, &QFileSystemModel::directoryLoaded,
            this, &FolderAsWorkspaceDock::onDirectoryLoaded);
    connect(ui->treeView, &QTreeView::expanded,
            this, &FolderAsWorkspaceDock::onTreeExpanded);
    connect(ui->treeView, &QTreeView::collapsed,
            this, &FolderAsWorkspaceDock::onTreeCollapsed);

    // Explicit-path ctor: skip the saved-setting load so additional workspaces
    // don't briefly flash the previous global root before showing their own.
    setRootPath(initialPath);
}

FolderAsWorkspaceDock::~FolderAsWorkspaceDock()
{
    // Defensive: clear the model's index pointer before m_pathStatus is
    // destroyed by the member-destruction phase, in case the model is asked
    // to paint during Qt's child-cleanup walk.
    if (model) {
        model->setStatusIndex(nullptr);
    }
    delete ui;
}

void FolderAsWorkspaceDock::wireFileTreeGitDecorations()
{
    model->setStatusIndex(&m_pathStatus);

    if (auto *app = qobject_cast<NotepadNextApplication *>(qApp)) {
        model->setDarkPalette(app->isEffectiveThemeDark());
        connect(app, &NotepadNextApplication::effectiveThemeChanged,
                model, [this, app]() {
                    model->setDarkPalette(app->isEffectiveThemeDark());
                });

        ApplicationSettings *settings = app->getSettings();
        if (settings) {
            model->setColorsEnabled(settings->fileTreeGitColors());
            connect(settings, &ApplicationSettings::fileTreeGitColorsChanged,
                    this, [this](bool enabled) {
                        model->setColorsEnabled(enabled);
                        if (enabled) maybeScheduleGitTabForDecoration();
                    });
        }
    }
}

void FolderAsWorkspaceDock::wireTreeContextMenu()
{
    ui->treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->treeView, &QTreeView::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = ui->treeView->indexAt(pos);
        if (!index.isValid()) return;

        // indexAt returns a proxy index — map through the proxy helpers.
        const QString absPath = proxy->filePath(index);
        const bool isDir = proxy->isDir(index);

        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);

        emit treeContextMenuRequested(menu, absPath, isDir);

        if (menu->isEmpty()) {
            delete menu;
            return;
        }

        menu->popup(ui->treeView->viewport()->mapToGlobal(pos));
    });
}

void FolderAsWorkspaceDock::setGitOperationManager(GitOperationManager *mgr)
{
    m_gitOpMgr = mgr;
    if (gitTab) gitTab->setOperationManager(mgr);
}

void FolderAsWorkspaceDock::maybeScheduleGitTabForDecoration()
{
    if (m_gitTabScheduled || gitTab != nullptr) return;
    if (rootPath().isEmpty()) return;

    auto *app = qobject_cast<NotepadNextApplication *>(qApp);
    if (app && app->getSettings() && !app->getSettings()->fileTreeGitColors()) {
        return;
    }

    m_gitTabScheduled = true;
    QMetaObject::invokeMethod(this, [this]() {
        ensureGitTab();
    }, Qt::QueuedConnection);
}

void FolderAsWorkspaceDock::setRootPath(const QString &dir)
{
    model->setRootPath(dir);
    // Point the proxy at the new root. The view's root index stays
    // QModelIndex() so the workspace dir shows as the single top node — we do
    // NOT call setRootIndex(model->index(dir)) (that would hide the root).
    proxy->setRootSourcePath(dir);

    if (gitTab) {
        gitTab->setWorkspaceRoot(dir);
    }

    // Window title doubles as the tab label when several workspaces are tabified
    // alongside each other, so make it the folder basename rather than the static
    // .ui label.
    if (dir.isEmpty()) {
        setWindowTitle(tr("Folder as Workspace"));
    } else {
        QString basename = QFileInfo(QDir::cleanPath(dir)).fileName();
        if (basename.isEmpty()) basename = dir;
        setWindowTitle(basename);
    }

    // Defer the GitController spawn to the next event-loop tick so the
    // first paint of the tree is not blocked by a process. When the user
    // has the decoration setting off, we skip the spawn entirely.
    maybeScheduleGitTabForDecoration();
}

QString FolderAsWorkspaceDock::rootPath() const
{
    return model->rootPath();
}

bool FolderAsWorkspaceDock::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->treeView->viewport()) {
        switch (event->type()) {
        case QEvent::ToolTip: {
            auto *helpEvent = static_cast<QHelpEvent *>(event);
            const QModelIndex index = ui->treeView->indexAt(helpEvent->pos());

            if (!index.isValid()) {
                tooltipTimer->stop();
                pendingTooltipIndex = QPersistentModelIndex();
                QToolTip::hideText();
                return true;
            }

            const QPersistentModelIndex pIndex(index);

            // Same item we're already tracking (timer running or tooltip shown) — leave it alone.
            if (pIndex == pendingTooltipIndex) {
                return true;
            }

            pendingTooltipIndex = pIndex;

            if (!QToolTip::isVisible() && !tooltipTimer->isActive()) {
                // No tooltip in flight — Qt has already waited the standard delay,
                // so show the first one immediately. Route through the proxy (the
                // view's model) so it auto-maps to the FsModel's ToolTipRole.
                const QString text = proxy->data(index, Qt::ToolTipRole).toString();
                if (!text.isEmpty()) {
                    QToolTip::showText(helpEvent->globalPos(), text, ui->treeView->viewport(),
                                       ui->treeView->visualRect(index));
                }
            } else {
                // Switching from one file's tooltip to another — hide and force a fresh wait.
                QToolTip::hideText();
                tooltipTimer->start();
            }
            return true;
        }
        case QEvent::Leave:
            tooltipTimer->stop();
            pendingTooltipIndex = QPersistentModelIndex();
            QToolTip::hideText();
            break;
        default:
            break;
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void FolderAsWorkspaceDock::onTabChanged(int index)
{
    // Lazy-create the Git tab the first time the user looks at it,
    // so docks that never need it don't spawn a GitController.
    if (index < 0) return;
    QWidget *page = ui->tabs->widget(index);
    if (page == ui->gitTab) {
        ensureGitTab();
    }
}

void FolderAsWorkspaceDock::ensureGitTab()
{
    if (gitTab) {
        gitTab->initializeIfNeeded();
        return;
    }
    gitTab = new GitTabWidget(rootPath(), this);
    if (m_gitOpMgr) gitTab->setOperationManager(m_gitOpMgr);
    auto *layout = qobject_cast<QVBoxLayout *>(ui->gitTab->layout());
    if (layout) {
        layout->addWidget(gitTab);
    } else {
        // Defensive: .ui should always provide a layout, but fall back if not.
        auto *fallback = new QVBoxLayout(ui->gitTab);
        fallback->setContentsMargins(0, 0, 0, 0);
        fallback->setSpacing(0);
        fallback->addWidget(gitTab);
    }
    connect(gitTab, &GitTabWidget::fileActivated,
            this, &FolderAsWorkspaceDock::onGitTabFileActivated);
    connect(gitTab, &GitTabWidget::diffRequested,
            this, &FolderAsWorkspaceDock::gitDiffRequested);
    connect(gitTab, &GitTabWidget::openSubmoduleRequested,
            this, &FolderAsWorkspaceDock::gitOpenSubmoduleRequested);
    connect(gitTab, &GitTabWidget::openCommitDetailRequested,
            this, &FolderAsWorkspaceDock::gitOpenCommitDetailRequested);
    connect(gitTab, &GitTabWidget::changesTreeContextMenuRequested,
            this, &FolderAsWorkspaceDock::gitChangesContextMenuRequested);
    connect(gitTab, &GitTabWidget::mergeRequested, this, [this]() {
        emit gitMergeRequested(this);
    });
    connect(gitTab, &GitTabWidget::rebaseRequested, this, [this]() {
        emit gitRebaseRequested(this);
    });
    connect(gitTab, &GitTabWidget::interactiveRebaseRequested, this, [this]() {
        emit gitInteractiveRebaseRequested(this);
    });

    // File-tree decoration: subscribe to status changes so the FsModel
    // repaints only the rows that changed. The controller is created lazily
    // inside GitTabWidget; we have to wait until initializeIfNeeded() runs
    // for the controller pointer to be non-null. Using a queued one-shot
    // captures the controller post-init.
    if (GitController *c = gitTab->controller()) {
        connect(c, &GitController::statusUpdated,
                this, &FolderAsWorkspaceDock::onStatusUpdated);
    }

    gitTab->initializeIfNeeded();

    // A controller may have been created during initializeIfNeeded() rather
    // than at construction; cover that case too.
    if (GitController *c = gitTab->controller()) {
        // Guard against double-connect (Qt deduplicates the same method ptr +
        // sender + receiver tuple by default when UniqueConnection is set).
        connect(c, &GitController::statusUpdated,
                this, &FolderAsWorkspaceDock::onStatusUpdated,
                Qt::UniqueConnection);
    }

    // Drain any status the controller has cached at this point (rare, but
    // covers the case where the new workspace inherits an in-flight refresh
    // from a quick re-open of the same path).
    onStatusUpdated();
}

void FolderAsWorkspaceDock::onStatusUpdated()
{
    PROFILE_SCOPE("FolderAsWorkspaceDock::onStatusUpdated");

    if (!gitTab || !gitTab->controller() || !gitTab->controller()->statusModel()) {
        // Nothing to read yet. If we previously had entries, clear them so
        // stale decorations disappear (e.g. controller errored out).
        if (!m_pathStatus.isEmpty()) {
            const QSet<QString> stale = m_pathStatus.allIndexedPaths();
            m_pathStatus.clear();
            model->notifyPathsChanged(stale);
        }
        return;
    }

    const GitController *c = gitTab->controller();
    const GitStatusEntries parentEntries = c->statusModel()->allEntries();
    const QString repoTop = c->currentRepo();

    // Stage 1: paint immediately with parent entries only. The submodule
    // folder itself is coloured here (its entry has isSubmodule=true →
    // PathStatusIndex registers it as a folder), but files inside the
    // submodule remain undecorated until the async sub-status returns.
    applyMergedEntries(parentEntries, repoTop);

    // Stage 2: collect submodule paths from the parent's entries and kick off
    // a fan-out fetch. The fetcher cancels any prior in-flight round, so
    // back-to-back refreshes coalesce safely.
    QVector<SubmoduleStatusFetcher::Submodule> submodules;
    for (const GitStatusEntry &e : parentEntries) {
        if (!e.isSubmodule || e.relPath.isEmpty()) continue;
        SubmoduleStatusFetcher::Submodule s;
        s.absPath = QDir::cleanPath(repoTop + QLatin1Char('/') + e.relPath);
        s.relFromRoot = e.relPath;
        submodules.append(s);
    }

    if (submodules.isEmpty()) {
        // No submodules → no second pass. Drop any cached parent snapshot
        // from a prior round so a future submodule-bearing refresh starts
        // from a clean slate.
        m_pendingParentEntries.clear();
        m_pendingRepoTop.clear();
        return;
    }

    m_pendingParentEntries = parentEntries;
    m_pendingRepoTop = repoTop;
    ensureSubmoduleFetcher()->fetch(submodules);
}

void FolderAsWorkspaceDock::onSubmoduleEntriesReady(const GitStatusEntries &entries)
{
    PROFILE_SCOPE("FolderAsWorkspaceDock::onSubmoduleEntriesReady");

    if (m_pendingRepoTop.isEmpty()) return; // stale callback after dock root cleared

    GitStatusEntries merged = m_pendingParentEntries;
    merged.reserve(merged.size() + entries.size());
    for (const GitStatusEntry &e : entries) merged.append(e);

    applyMergedEntries(merged, m_pendingRepoTop);

    m_pendingParentEntries.clear();
    m_pendingRepoTop.clear();
}

void FolderAsWorkspaceDock::applyMergedEntries(const GitStatusEntries &merged,
                                               const QString &repoTop)
{
    PathStatusIndex next;
    next.rebuild(merged, repoTop);

    const QSet<QString> delta = next.deltaPaths(m_pathStatus);
    m_pathStatus = std::move(next);
    model->notifyPathsChanged(delta);
}

SubmoduleStatusFetcher *FolderAsWorkspaceDock::ensureSubmoduleFetcher()
{
    if (m_subFetcher) return m_subFetcher;
    m_subFetcher = new SubmoduleStatusFetcher(this);
    connect(m_subFetcher, &SubmoduleStatusFetcher::entriesReady,
            this, &FolderAsWorkspaceDock::onSubmoduleEntriesReady);
    return m_subFetcher;
}

GitDiffViewController *FolderAsWorkspaceDock::ensureGitDiffViewController()
{
    if (gitDiffViewController) return gitDiffViewController;

    ensureGitTab();
    if (!gitTab || !gitTab->controller()) return nullptr;

    auto *app = qobject_cast<NotepadNextApplication*>(qApp);
    if (!app) return nullptr;

    gitDiffViewController = new GitDiffViewController(gitTab->controller(),
                                                      app->getEditorManager(),
                                                      this);
    gitDiffViewController->setDarkPalette(app->isEffectiveThemeDark());

    connect(gitDiffViewController, &GitDiffViewController::diffRendered,
            this, &FolderAsWorkspaceDock::onGitDiffPreviewRendered);
    connect(gitDiffViewController, &GitDiffViewController::diffFailed,
            this, &FolderAsWorkspaceDock::onGitDiffPreviewFailed);

    connect(app, &NotepadNextApplication::effectiveThemeChanged,
            gitDiffViewController, [this, app]() {
                gitDiffViewController->setDarkPalette(app->isEffectiveThemeDark());
            });

    return gitDiffViewController;
}

void FolderAsWorkspaceDock::showGitDiffPreview(const GitStatusEntry &entry)
{
    if (auto *c = ensureGitDiffViewController()) {
        m_diffRenderPending = true;
        c->showDiffFor(entry);
    }
}

void FolderAsWorkspaceDock::onGitTabFileActivated(const QString &absPath)
{
    // If a diff is currently being fetched (single-click triggered it just
    // before the double-click), defer the editor open so it activates AFTER
    // the diff tab. Otherwise fire it now.
    if (m_diffRenderPending) {
        m_pendingEditorOpenPath = absPath;
        return;
    }
    emit fileDoubleClicked(absPath);
}

void FolderAsWorkspaceDock::onGitDiffPreviewRendered(ScintillaNext *editor)
{
    m_diffRenderPending = false;
    emit gitDiffPreviewRendered(editor);
    if (!m_pendingEditorOpenPath.isEmpty()) {
        const QString p = m_pendingEditorOpenPath;
        m_pendingEditorOpenPath.clear();
        emit fileDoubleClicked(p);
    }
}

void FolderAsWorkspaceDock::onGitDiffPreviewFailed(const QString &relPath, const QString &message)
{
    m_diffRenderPending = false;
    emit gitDiffPreviewFailed(relPath, message);
    if (!m_pendingEditorOpenPath.isEmpty()) {
        const QString p = m_pendingEditorOpenPath;
        m_pendingEditorOpenPath.clear();
        emit fileDoubleClicked(p);
    }
}

GitCommitView *FolderAsWorkspaceDock::ensureGitCommitView()
{
    if (gitCommitView) return gitCommitView;

    ensureGitTab();
    if (!gitTab || !gitTab->controller()) return nullptr;

    auto *app = qobject_cast<NotepadNextApplication*>(qApp);
    if (!app) return nullptr;

    gitCommitView = new GitCommitView(gitTab->controller()->currentRepo(),
                                       app->getEditorManager(), this);
    gitCommitView->setDarkPalette(app->isEffectiveThemeDark());

    connect(gitCommitView, &GitCommitView::newCommitEditorCreated,
            this, &FolderAsWorkspaceDock::gitCommitEditorCreated);
    connect(gitCommitView, &GitCommitView::commitDetailRendered,
            this, &FolderAsWorkspaceDock::gitCommitEditorRendered);
    connect(gitCommitView, &GitCommitView::focusExistingCommitEditor,
            this, &FolderAsWorkspaceDock::gitCommitEditorFocus);
    connect(gitCommitView, &GitCommitView::newFileAtShaEditorCreated,
            this, &FolderAsWorkspaceDock::gitFileAtShaEditorCreated);
    connect(gitCommitView, &GitCommitView::focusExistingFileAtShaEditor,
            this, &FolderAsWorkspaceDock::gitFileAtShaEditorFocus);
    connect(gitCommitView, &GitCommitView::fetchFailed,
            this, &FolderAsWorkspaceDock::gitCommitDetailFailed);

    connect(app, &NotepadNextApplication::effectiveThemeChanged,
            gitCommitView, [this, app]() {
                gitCommitView->setDarkPalette(app->isEffectiveThemeDark());
            });

    return gitCommitView;
}

void FolderAsWorkspaceDock::showGitCommitDetail(const QByteArray &sha)
{
    if (auto *c = ensureGitCommitView()) {
        // Repo may have switched since constructor; sync.
        if (gitTab && gitTab->controller()) {
            c->setRepoRoot(gitTab->controller()->currentRepo());
        }
        c->openForSha(sha);
    }
}

void FolderAsWorkspaceDock::showGitTab()
{
    // Setting currentWidget triggers QTabWidget::currentChanged → onTabChanged,
    // which lazy-constructs the GitTabWidget. No need to call ensureGitTab here.
    if (ui->tabs->currentWidget() != ui->gitTab) {
        ui->tabs->setCurrentWidget(ui->gitTab);
    } else {
        // Already on the Git tab — make sure the underlying widget exists
        // (no currentChanged would fire on a no-op switch).
        ensureGitTab();
    }
}

void FolderAsWorkspaceDock::revealAndSelectPath(const QString &absolutePath)
{
    const QString cleaned = QDir::cleanPath(absolutePath);
    if (cleaned.isEmpty()) return;

    const QString root = QDir::cleanPath(rootPath());
    if (root.isEmpty()) return;

#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    // Defensive: caller already verified membership, bail if the path isn't
    // under this dock's root (cleanPath yields forward-slash form on all OSes).
    const bool within = (cleaned.compare(root, cs) == 0) ||
                        cleaned.startsWith(root + QLatin1Char('/'), cs);
    if (!within) return;

    // Make the Files tab visible so the tree is on screen. Guarded by
    // m_programmaticToggle so the currentChanged emission doesn't dirty
    // workspace state — this is a programmatic reveal, not a user tab switch.
    if (ui->tabs->currentIndex() != 0) {
        m_programmaticToggle = true;
        ui->tabs->setCurrentIndex(0);
        m_programmaticToggle = false;
    }

    // Build the ancestor chain top-down: [root, level1, ..., fileParent].
    QStringList ancestorsDesc;
    {
        QString cur = QFileInfo(cleaned).absolutePath(); // file's parent dir (cleaned)
        while (!cur.isEmpty()) {
            ancestorsDesc.prepend(cur);
            if (cur.compare(root, cs) == 0) break;
            const QString parent = QFileInfo(cur).absolutePath();
            if (parent == cur) break; // filesystem-root guard
            cur = parent;
        }
    }

    // Reveal-vs-restore collision handling:
    //  - m_pendingExpansion is a QMultiHash; insert() is ADDITIVE, so reveal
    //    entries coexist with any in-flight session-restore entries and both
    //    drain in onDirectoryLoaded — neither clobbers the other.
    //  - pendingCurrentItem has a single slot. A reveal is an explicit, later
    //    user action and SHOULD supersede a stale restore's saved current item,
    //    so we overwrite it (set below).
    //  - We do NOT clear m_userVetoed wholesale (that would fight the user's own
    //    collapses elsewhere in the tree). But a reveal is an explicit request
    //    to show THIS path, so we drop any veto sitting on the reveal target's
    //    own ancestor chain — otherwise ancestorVetoed() in onDirectoryLoaded
    //    would block re-expansion of a previously-collapsed ancestor.
    for (const QString &dir : ancestorsDesc) {
        m_userVetoed.remove(dir);
    }

    // Drive the descent synchronously as far as the model has already loaded.
    // model->index(dir) is valid once dir's PARENT has loaded (its row exists);
    // expanding a not-yet-loaded dir kicks off its async child-load, after which
    // directoryLoaded(dir) fires. We stop at the first dir whose parent hasn't
    // finished loading. Track the deepest dir we actually expanded — it is, by
    // construction, freshly loading (if it were already loaded, its child's
    // index would be valid and the loop would have continued), so its
    // directoryLoaded is guaranteed to fire and can drain a seeded tail.
    int deepestExpandedIdx = -1;
    for (int i = 0; i < ancestorsDesc.size(); ++i) {
        const QString &dir = ancestorsDesc[i];
        // The root is now a real top node (PR), not the view's rootIndex, so it
        // is expanded like any ancestor. model->index(dir) is a SOURCE index;
        // map it to the proxy before the view call.
        const QModelIndex srcIdx = model->index(dir);
        if (!srcIdx.isValid() || !model->isDir(srcIdx)) break;
        const QModelIndex proxyIdx = proxy->mapFromSource(srcIdx);
        if (!proxyIdx.isValid()) break;
        m_programmaticToggle = true;
        ui->treeView->setExpanded(proxyIdx, true);
        m_programmaticToggle = false;
        deepestExpandedIdx = i;
    }

    // If the whole chain was already loaded the leaf is reachable now — select it
    // immediately and return WITHOUT seeding anything. This is the steady-state
    // path for repeated reveals of an already-navigated subtree, so
    // m_pendingExpansion stays empty across consecutive reveals (no accumulation).
    const QModelIndex leafSrc = model->index(cleaned);
    if (leafSrc.isValid()) {
        const QModelIndex leafIdx = proxy->mapFromSource(leafSrc);
        // Cancel any still-pending select from an earlier reveal/restore whose
        // parent hasn't loaded yet — this newer explicit reveal must win, and a
        // stale pendingCurrentItem would otherwise steal the selection back when
        // its directoryLoaded eventually fires.
        setProperty("pendingCurrentItem", QVariant());
        m_programmaticToggle = true;
        ui->treeView->setCurrentIndex(leafIdx);
        ui->treeView->scrollTo(leafIdx, QAbstractItemView::PositionAtCenter);
        m_programmaticToggle = false;
        ui->treeView->setFocus();
        return;
    }

    // Leaf not reachable yet → seed only the UNDRAINED tail so onDirectoryLoaded
    // cascades the rest of the expansion. The pair we seed first must have a
    // parent whose directoryLoaded is still PENDING — otherwise it never re-fires
    // and the entry lingers forever (that was the per-reveal stale-entry leak).
    //  - deepestExpandedIdx >= 0: we just expanded ancestorsDesc[deepestExpandedIdx],
    //    which is freshly loading (its child's index was invalid, which is why the
    //    descent stopped) → seed from there down.
    //  - deepestExpandedIdx == -1: even the first level under root wasn't loaded,
    //    i.e. root's own directoryLoaded hasn't fired yet (reveal raced workspace
    //    open) → seed from root (index 0); directoryLoaded(root) is still pending
    //    and will start the cascade.
    // Everything ABOVE seedStart was expanded against already-loaded dirs, so
    // seeding those pairs would leak. We deliberately skip them.
    const int seedStart = deepestExpandedIdx >= 0 ? deepestExpandedIdx : 0;
    for (int i = seedStart; i + 1 < ancestorsDesc.size(); ++i) {
        m_pendingExpansion.insert(ancestorsDesc[i], ancestorsDesc[i + 1]);
    }

    // Leaf select target. onDirectoryLoaded's existing section-2 pickup realises
    // it once directoryLoaded fires for the leaf's parent (matches
    // QFileInfo(pendingItem).absolutePath() == loadedKey). Overwrites any stale
    // restore target (a reveal is an explicit, later user action).
    setProperty("pendingCurrentItem", cleaned);

    // Edge: if a seeded dir never finishes loading (deleted/inaccessible mid-load)
    // its m_pendingExpansion entry + pendingCurrentItem linger until the next
    // reveal/restore overwrites them or the dock is destroyed. Bounded to one
    // chain and harmless — no timers, matching the restore path's behavior.

    // Mirror "switch focus to the dock": focus the tree so the selection is
    // visibly active. Raising/showing the dock is the caller's job.
    ui->treeView->setFocus();
}

void FolderAsWorkspaceDock::applySavedTreeState(const WorkspaceStateSnapshot &snapshot)
{
    // Pre-populate the parentDir → child map. directoryLoaded handler drains
    // it as the model finishes loading each parent. Setup MUST happen before
    // setRootPath() so the root's own directoryLoaded fires after we're ready.
    m_pendingExpansion.clear();
    m_userVetoed.clear();
    for (const QString &p : snapshot.expandedFolders) {
        if (p.isEmpty()) continue;
        const QString cleaned = QDir::cleanPath(p);
        const QString parent = QFileInfo(cleaned).absolutePath();
        if (parent.isEmpty()) continue;
        m_pendingExpansion.insert(QDir::cleanPath(parent), cleaned);
    }

    // Root-node (PR) restore decision (see design.md D8). The root is one more
    // absolute path in expandedFolders — no schema change. A default-constructed
    // snapshot (empty rootPath) is the first-run / no-saved-state case ⇒ expand
    // by default. A real saved snapshot that OMITS the root path means the user
    // collapsed it last session ⇒ restore collapsed. The root cascade can't run
    // through m_pendingExpansion (its parent dir is outside the tree), so this is
    // applied directly in onDirectoryLoaded(root).
    m_rootExpandApplied = false;
    if (snapshot.rootPath.isEmpty()) {
        m_rootShouldExpand = true;
    } else {
        const QString cleanedRoot = QDir::cleanPath(snapshot.rootPath);
        m_rootShouldExpand = false;
        for (const QString &p : snapshot.expandedFolders) {
            if (QDir::cleanPath(p) == cleanedRoot) {
                m_rootShouldExpand = true;
                break;
            }
        }
    }

    // Defer Git tab construction to the next event-loop tick so the GitController
    // subprocess spawn doesn't extend window->show() latency. From the user's
    // perspective the tab header is already at index=Git when the window paints;
    // content populates a few ms later.
    //
    // Guarded by m_programmaticToggle so the resulting currentChanged emission
    // doesn't mark workspace state dirty — this is a restore action, not a
    // user-initiated tab switch.
    if (snapshot.activeTabIndex == 1) {
        QMetaObject::invokeMethod(this, [this]() {
            m_programmaticToggle = true;
            showGitTab();
            m_programmaticToggle = false;
        }, Qt::QueuedConnection);
    }

    // Stash the current item path on the dock so we can apply it after the
    // first directoryLoaded for the row's parent. Reuse pendingExpansion's map
    // semantics: a special sentinel value isn't needed — we just call
    // scrollTo when directoryLoaded fires for the item's parent.
    if (!snapshot.currentItemPath.isEmpty()) {
        const QString cleaned = QDir::cleanPath(snapshot.currentItemPath);
        // Inject into pendingExpansion's parent map so directoryLoaded picks it up
        // for the scrollTo side-effect. Stored under a separate property to avoid
        // confusing it with expansion entries; we use a Q_OBJECT dynamic property.
        setProperty("pendingCurrentItem", cleaned);
    } else {
        setProperty("pendingCurrentItem", QVariant());
    }
}

WorkspaceStateSnapshot FolderAsWorkspaceDock::captureState() const
{
    WorkspaceStateSnapshot s;
    s.rootPath = QDir::cleanPath(rootPath());
    s.activeTabIndex = ui->tabs->currentIndex();
    s.lastUsedEpochMs = QDateTime::currentMSecsSinceEpoch();

    if (auto *sel = ui->treeView->selectionModel(); sel) {
        const QModelIndex curr = sel->currentIndex();
        if (curr.isValid()) {
            // currentIndex() is a proxy index — read the path through the proxy.
            s.currentItemPath = QDir::cleanPath(proxy->filePath(curr));
        }
    }

    // Walk the tree depth-first collecting expanded directory paths. Cost is
    // O(visible expanded rows); cheap because only loaded rows can be expanded.
    // The walk runs in PROXY coordinates now that the view binds to the proxy.
    const QModelIndex pr = proxy->index(0, 0, QModelIndex());
    if (pr.isValid()) {
        // The root is a real node now (PR), so it is itself expandable/capturable.
        // Persist it as one more absolute path in expandedFolders (no schema
        // change) iff the user has it expanded.
        if (ui->treeView->isExpanded(pr)) {
            s.expandedFolders << QDir::cleanPath(rootPath());
        }
        QList<QModelIndex> stack;
        stack.reserve(64);
        // Seed with PR's direct children (PR itself handled just above).
        for (int r = 0, n = proxy->rowCount(pr); r < n; ++r) {
            stack.append(proxy->index(r, 0, pr));
        }
        while (!stack.isEmpty()) {
            const QModelIndex idx = stack.takeLast();
            if (!idx.isValid() || !proxy->isDir(idx)) continue;
            if (!ui->treeView->isExpanded(idx)) continue;
            s.expandedFolders << QDir::cleanPath(proxy->filePath(idx));
            const int n = proxy->rowCount(idx);
            for (int r = 0; r < n; ++r) {
                stack.append(proxy->index(r, 0, idx));
            }
        }
    }
    return s;
}

bool FolderAsWorkspaceDock::ancestorVetoed(const QString &cleanedChild) const
{
    if (m_userVetoed.isEmpty()) return false;
    // Walk up parents until root. QFileInfo::absolutePath returns the cleaned
    // parent for an already-cleaned input.
    QString cur = QFileInfo(cleanedChild).absolutePath();
    while (!cur.isEmpty() && cur != QFileInfo(cur).absolutePath()) {
        if (m_userVetoed.contains(cur)) return true;
        cur = QFileInfo(cur).absolutePath();
    }
    return false;
}

void FolderAsWorkspaceDock::onDirectoryLoaded(const QString &loadedPath)
{
    // Root-node (PR) default-expand / restore — one-shot, fires on the root's
    // own directoryLoaded. The root's parent dir is outside the tree, so the
    // m_pendingExpansion cascade can't reach it; drive it directly here. The
    // cheap !m_rootExpandApplied bool keeps steady-state navigation O(1) once
    // the root has been handled.
    if (!m_rootExpandApplied) {
        const QString cleanRoot = QDir::cleanPath(rootPath());
        if (!cleanRoot.isEmpty() && QDir::cleanPath(loadedPath) == cleanRoot) {
            m_rootExpandApplied = true;
            if (m_rootShouldExpand) {
                const QModelIndex pr = proxy->index(0, 0, QModelIndex());
                if (pr.isValid()) {
                    // m_programmaticToggle so the resulting expanded() emission
                    // doesn't dirty state. Live-session collapses of the root are
                    // handled by the normal onTreeCollapsed/m_userVetoed path now
                    // that PR is a real node.
                    m_programmaticToggle = true;
                    ui->treeView->setExpanded(pr, true);
                    m_programmaticToggle = false;
                }
            }
        }
    }

    if (m_pendingExpansion.isEmpty() && !property("pendingCurrentItem").isValid()) {
        return;  // O(1) short-circuit covers steady-state navigation
    }

    const QString key = QDir::cleanPath(loadedPath);

    // 1) Drain expansion entries whose parent matches this loaded dir.
    const QList<QString> children = m_pendingExpansion.values(key);
    if (!children.isEmpty()) {
        m_pendingExpansion.remove(key);
        for (const QString &child : children) {
            // Per-path veto: user explicitly collapsed this exact path during restore.
            if (m_userVetoed.contains(child)) continue;
            // Ancestor veto: any user-collapsed ancestor wipes the entire subtree.
            if (ancestorVetoed(child)) continue;

            const QModelIndex srcIdx = model->index(child);
            if (!srcIdx.isValid() || !model->isDir(srcIdx)) continue;  // stale path / now-file
            const QModelIndex idx = proxy->mapFromSource(srcIdx);
            if (!idx.isValid()) continue;

            m_programmaticToggle = true;
            ui->treeView->setExpanded(idx, true);
            m_programmaticToggle = false;
            // Expanding triggers another async load → directoryLoaded(child)
            // fires later → recursive drain.
        }
    }

    // 2) If the saved current/selected item lives under this loaded parent,
    // realise it now. Single-shot — clear after applying.
    const QVariant pendingItemVar = property("pendingCurrentItem");
    if (pendingItemVar.isValid()) {
        const QString pendingItem = pendingItemVar.toString();
        if (QFileInfo(pendingItem).absolutePath() == key) {
            const QModelIndex itemSrc = model->index(pendingItem);
            if (itemSrc.isValid()) {
                const QModelIndex itemIdx = proxy->mapFromSource(itemSrc);
                m_programmaticToggle = true;
                ui->treeView->setCurrentIndex(itemIdx);
                ui->treeView->scrollTo(itemIdx, QAbstractItemView::PositionAtCenter);
                m_programmaticToggle = false;
            }
            setProperty("pendingCurrentItem", QVariant());
        }
    }
}

void FolderAsWorkspaceDock::onTreeExpanded(const QModelIndex &index)
{
    if (m_programmaticToggle) return;
    // User manually expanded — drop any prior veto on this path so future
    // restore passes don't fight the user, then notify host to flush state.
    // `index` is a proxy index — read the path through the proxy helper.
    const QString p = QDir::cleanPath(proxy->filePath(index));
    m_userVetoed.remove(p);
    emit stateDirty();
}

void FolderAsWorkspaceDock::onTreeCollapsed(const QModelIndex &index)
{
    if (m_programmaticToggle) return;
    const QString p = QDir::cleanPath(proxy->filePath(index));
    m_userVetoed.insert(p);
    // Drop any still-pending expansion entries under this path so they don't
    // re-expand a moment later when their parent finishes loading.
    if (!m_pendingExpansion.isEmpty()) {
        for (auto it = m_pendingExpansion.begin(); it != m_pendingExpansion.end(); ) {
            if (it.value() == p || it.value().startsWith(p + QLatin1Char('/'))) {
                it = m_pendingExpansion.erase(it);
            } else {
                ++it;
            }
        }
    }
    emit stateDirty();
}

void FolderAsWorkspaceDock::closeEvent(QCloseEvent *event)
{
    // Emit synchronously while rootPath() and the tree are still queryable.
    // WA_DeleteOnClose schedules destruction after closeEvent returns, so any
    // host-side persistence must happen here.
    emit aboutToBeClosed(rootPath(), this);
    QDockWidget::closeEvent(event);
}
