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
#include "GitController.h"
#include "GitDiffViewController.h"
#include "GitTabWidget.h"
#include "NotepadNextApplication.h"
#include "ui_FolderAsWorkspaceDock.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHelpEvent>
#include <QItemSelectionModel>
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
ApplicationSetting<QString> rootPathSetting{"FolderAsWorkspace/RootPath"};

namespace {

class FolderAsWorkspaceFsModel : public QFileSystemModel
{
public:
    using QFileSystemModel::QFileSystemModel;

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::ToolTipRole && index.isValid()) {
            return QDir::toNativeSeparators(filePath(index));
        }
        return QFileSystemModel::data(index, role);
    }
};

} // namespace

FolderAsWorkspaceDock::FolderAsWorkspaceDock(QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::FolderAsWorkspaceDock),
    model(new FolderAsWorkspaceFsModel(this)),
    tooltipTimer(new QTimer(this))
{
    ui->setupUi(this);

    ui->treeView->setModel(model);
    ui->treeView->header()->hideSection(1);
    ui->treeView->header()->hideSection(2);
    ui->treeView->header()->hideSection(3);

    connect(ui->treeView, &QTreeView::doubleClicked, this, [=](const QModelIndex &index) {
        if (!model->isDir(index)) {
            emit fileDoubleClicked(model->filePath(index));
        }
    });

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
        const QString text = model->data(index, Qt::ToolTipRole).toString();
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
    connect(model, &QFileSystemModel::directoryLoaded,
            this, &FolderAsWorkspaceDock::onDirectoryLoaded);
    connect(ui->treeView, &QTreeView::expanded,
            this, &FolderAsWorkspaceDock::onTreeExpanded);
    connect(ui->treeView, &QTreeView::collapsed,
            this, &FolderAsWorkspaceDock::onTreeCollapsed);

    // The initial dock starts empty; MainWindow::restoreOpenWorkspaces assigns
    // Workspaces[0] into it via the vacant-reuse path in openFolderAsWorkspacePath.
}

FolderAsWorkspaceDock::FolderAsWorkspaceDock(const QString &initialPath, QWidget *parent) :
    QDockWidget(parent),
    ui(new Ui::FolderAsWorkspaceDock),
    model(new FolderAsWorkspaceFsModel(this)),
    tooltipTimer(new QTimer(this))
{
    ui->setupUi(this);

    ui->treeView->setModel(model);
    ui->treeView->header()->hideSection(1);
    ui->treeView->header()->hideSection(2);
    ui->treeView->header()->hideSection(3);

    connect(ui->treeView, &QTreeView::doubleClicked, this, [=](const QModelIndex &index) {
        if (!model->isDir(index)) {
            emit fileDoubleClicked(model->filePath(index));
        }
    });

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
        const QString text = model->data(index, Qt::ToolTipRole).toString();
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
    delete ui;
}

void FolderAsWorkspaceDock::setRootPath(const QString dir)
{
    model->setRootPath(dir);
    ui->treeView->setRootIndex(model->index(dir));

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
                // so show the first one immediately.
                const QString text = model->data(index, Qt::ToolTipRole).toString();
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
            this, &FolderAsWorkspaceDock::fileDoubleClicked);
    connect(gitTab, &GitTabWidget::diffRequested,
            this, &FolderAsWorkspaceDock::gitDiffRequested);
    connect(gitTab, &GitTabWidget::openSubmoduleRequested,
            this, &FolderAsWorkspaceDock::gitOpenSubmoduleRequested);
    gitTab->initializeIfNeeded();
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
            this, &FolderAsWorkspaceDock::gitDiffPreviewRendered);
    connect(gitDiffViewController, &GitDiffViewController::diffFailed,
            this, &FolderAsWorkspaceDock::gitDiffPreviewFailed);

    connect(app, &NotepadNextApplication::effectiveThemeChanged,
            gitDiffViewController, [this, app]() {
                gitDiffViewController->setDarkPalette(app->isEffectiveThemeDark());
            });

    return gitDiffViewController;
}

void FolderAsWorkspaceDock::showGitDiffPreview(const GitStatusEntry &entry)
{
    if (auto *c = ensureGitDiffViewController()) {
        c->showDiffFor(entry);
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
            s.currentItemPath = QDir::cleanPath(model->filePath(curr));
        }
    }

    // Walk the tree depth-first collecting expanded directory paths. Cost is
    // O(visible expanded rows); cheap because only loaded rows can be expanded.
    const QString root = s.rootPath;
    const QModelIndex rootIdx = model->index(root);
    if (rootIdx.isValid()) {
        QList<QModelIndex> stack;
        stack.reserve(64);
        // Seed with direct children of root (root itself is the view's rootIndex,
        // not part of the tree, so it's neither "expanded" nor capturable).
        for (int r = 0, n = model->rowCount(rootIdx); r < n; ++r) {
            stack.append(model->index(r, 0, rootIdx));
        }
        while (!stack.isEmpty()) {
            const QModelIndex idx = stack.takeLast();
            if (!idx.isValid() || !model->isDir(idx)) continue;
            if (!ui->treeView->isExpanded(idx)) continue;
            s.expandedFolders << QDir::cleanPath(model->filePath(idx));
            const int n = model->rowCount(idx);
            for (int r = 0; r < n; ++r) {
                stack.append(model->index(r, 0, idx));
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

            const QModelIndex idx = model->index(child);
            if (!idx.isValid() || !model->isDir(idx)) continue;  // stale path / now-file

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
            const QModelIndex itemIdx = model->index(pendingItem);
            if (itemIdx.isValid()) {
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
    const QString p = QDir::cleanPath(model->filePath(index));
    m_userVetoed.remove(p);
    emit stateDirty();
}

void FolderAsWorkspaceDock::onTreeCollapsed(const QModelIndex &index)
{
    if (m_programmaticToggle) return;
    const QString p = QDir::cleanPath(model->filePath(index));
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
