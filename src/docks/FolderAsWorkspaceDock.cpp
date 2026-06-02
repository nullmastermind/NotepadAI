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
#include "../remote/RemoteDirectoryWatcher.h"
#include "../remote/RemoteExecutionContext.h"
#include "../remote/RemoteFileSystemModel.h"
#include "../remote/RemoteFsBackend.h"
#include "../remote/RemoteTransferManager.h"
#include "../remote/SshProfile.h"
#include "../widgets/TransferProgressBar.h"
#include "../dialogs/TransferLogDialog.h"
#include "ui_FolderAsWorkspaceDock.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QShowEvent>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

#include <utility>

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

    fsModel = model;
    proxy->setSourceModel(model);
    ui->treeView->setModel(proxy);
    ui->treeView->header()->hideSection(1);
    ui->treeView->header()->hideSection(2);
    ui->treeView->header()->hideSection(3);
    // Multi-select support (D7): Ctrl+click / Shift+click for batch transfers.
    ui->treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    wireFileTreeGitDecorations();

    connect(ui->treeView, &QTreeView::doubleClicked, this, [=](const QModelIndex &index) {
        if (!proxy->isDir(index)) {
            emit fileDoubleClicked(resolvedFilePath(index));
        }
    });

    connect(ui->treeView, &QTreeView::clicked, this, [=](const QModelIndex &index) {
        if (!proxy->isDir(index)) {
            emit fileClicked(resolvedFilePath(index));
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
    // Connected by signature off the active model (see connectModelSignals).
    connectModelSignals();
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

    fsModel = model;
    proxy->setSourceModel(model);
    ui->treeView->setModel(proxy);
    ui->treeView->header()->hideSection(1);
    ui->treeView->header()->hideSection(2);
    ui->treeView->header()->hideSection(3);
    // Multi-select support (D7): Ctrl+click / Shift+click for batch transfers.
    ui->treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    wireFileTreeGitDecorations();

    connect(ui->treeView, &QTreeView::doubleClicked, this, [=](const QModelIndex &index) {
        if (!proxy->isDir(index)) {
            emit fileDoubleClicked(resolvedFilePath(index));
        }
    });

    connect(ui->treeView, &QTreeView::clicked, this, [=](const QModelIndex &index) {
        if (!proxy->isDir(index)) {
            emit fileClicked(resolvedFilePath(index));
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

    connectModelSignals();
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
    // Disarm the remote poll-watcher FIRST: stop its timer + drop in-flight
    // bookkeeping so a queued readdir callback can never fire into a
    // half-destroyed dock (the lambda touches proxy/treeView via
    // collectExpandedDirs). The watcher is parented to `this` and will be
    // deleted in the QObject child sweep regardless.
    if (m_remoteWatcher) {
        m_remoteWatcher->stop();
    }
    if (m_watchedWindow) {
        m_watchedWindow->removeEventFilter(this);
        m_watchedWindow = nullptr;
    }

    // Defensive: clear the model's index pointer before m_pathStatus is
    // destroyed by the member-destruction phase, in case the model is asked
    // to paint during Qt's child-cleanup walk. Remote workspaces have no local
    // `model` (useRemoteBackend deleted it) — the null guard covers that.
    if (model) {
        model->setStatusIndex(nullptr);
    }
    delete ui;
}

void FolderAsWorkspaceDock::connectModelSignals()
{
    // directoryLoaded is not on QAbstractItemModel — it lives on the concrete
    // model (QFileSystemModel or RemoteFileSystemModel), both with the identical
    // `directoryLoaded(QString)` shape. Connect by signature so this resolves
    // against whichever model currently backs fsModel. It stays on the SOURCE
    // model (the proxy does not relay it) and must be live BEFORE setRootPath so
    // the very first (root-level) load is captured.
    if (fsModel) {
        connect(fsModel->asModel(), SIGNAL(directoryLoaded(QString)),
                this, SLOT(onDirectoryLoaded(QString)));
    }
}

void FolderAsWorkspaceDock::useRemoteBackend(remote::RemoteFsBackend *backend)
{
    if (!backend) return;

    // Idempotency guard for the reconnect path: if the dock already has a remote
    // model AND it is backed by the same backend object (same connection), skip
    // the full rebuild — setRootPath (called by the caller right after) will
    // re-root and refresh the stale tree cheaply.
    //
    // When `backend != m_remoteBackend` the old SshConnection was torn down and a
    // brand-new RemoteFsBackend was created (ExecutionContextRegistry::connect
    // deletes the old entry before making a new one). The old RemoteFileSystemModel
    // holds a QPointer<RemoteFsBackend> that is now null, so every listing call
    // returns "No SSH connection". We must fall through and rebuild with the new
    // backend.
    if (!model && fsModel && backend == m_remoteBackend) {
        if (qobject_cast<remote::RemoteFileSystemModel *>(fsModel->asModel())) {
            // Same backend, same model — re-root is sufficient.
            return;
        }
    }

    // Tear down the local model + its signal wiring. The git-decoration lambdas
    // were connected with `model` as the receiver, so Qt auto-disconnects them
    // when it is destroyed; the settings/theme connects with `this` as receiver
    // are dropped explicitly below via the model disconnect. Detach it from the
    // proxy first so the proxy doesn't briefly hold a dangling source.
    if (model) {
        model->disconnect(this);
        disconnect(model, nullptr, this, nullptr);
    }
    proxy->setSourceModel(nullptr);
    if (model) {
        model->deleteLater();
        model = nullptr;
    }

    // When reconnecting with a new backend (old connection was torn down and
    // replaced), the old RemoteFileSystemModel and RemoteDirectoryWatcher still
    // exist as children of this dock, but their captured QPointer<RemoteFsBackend>
    // has been nulled out by the delete in ExecutionContextRegistry::disconnect.
    // Tear them down so the new model and watcher are built cleanly.
    if (auto *oldRemoteModel =
            qobject_cast<remote::RemoteFileSystemModel *>(fsModel ? fsModel->asModel() : nullptr)) {
        oldRemoteModel->deleteLater();
        fsModel = nullptr; // proxy source already cleared above; prevent dangling use
    }
    if (m_remoteWatcher) {
        m_remoteWatcher->stop();
        m_remoteWatcher->deleteLater();
        m_remoteWatcher = nullptr;
    }
    m_remoteBackend = nullptr;

    // Build the SFTP-backed model. The lister adapts RemoteFsBackend::readdirAsync
    // (async, results queued to the UI thread) to the model's ListFn, converting
    // RemoteDirEntry → RemoteFileSystemModel::Entry (same fields; the model owns no
    // SSH dependency). directoryChanged from the backend (Batch C poll-watcher /
    // local-watch relay) drives the model's re-list/diff seam.
    QPointer<remote::RemoteFsBackend> safeBackend(backend);
    auto *remoteModel = new remote::RemoteFileSystemModel(
        [safeBackend](const QString &path,
                      remote::RemoteFileSystemModel::ListDoneCallback done) {
            if (!safeBackend) {
                if (done) done(false, {}, QObject::tr("No SSH connection"));
                return;
            }
            safeBackend->readdirAsync(
                path, [done = std::move(done)](bool ok,
                                               const QList<remote::RemoteDirEntry> &entries,
                                               const QString &error) {
                    if (!done) return;
                    QList<remote::RemoteFileSystemModel::Entry> converted;
                    converted.reserve(entries.size());
                    for (const remote::RemoteDirEntry &e : entries) {
                        converted.append({e.name, e.isDir, e.size, e.mtimeSecs});
                    }
                    done(ok, converted, error);
                });
        },
        this);

    connect(backend, &remote::RemoteFsBackend::directoryChanged,
            remoteModel, &remote::RemoteFileSystemModel::onDirectoryChanged);

    fsModel = remoteModel;
    proxy->setSourceModel(remoteModel);
    connectModelSignals();

    remoteModel->setStatusIndex(&m_pathStatus);
    if (auto *app = qobject_cast<NotepadNextApplication *>(qApp)) {
        remoteModel->setDarkPalette(app->isEffectiveThemeDark());
        connect(app, &NotepadNextApplication::effectiveThemeChanged,
                remoteModel, [remoteModel, app]() {
                    remoteModel->setDarkPalette(app->isEffectiveThemeDark());
                });
        ApplicationSettings *settings = app->getSettings();
        if (settings) {
            remoteModel->setColorsEnabled(settings->fileTreeGitColors());
            connect(settings, &ApplicationSettings::fileTreeGitColorsChanged,
                    remoteModel, &remote::RemoteFileSystemModel::setColorsEnabled);
        }
    }

    // Stand up the bounded poll-watcher (D3): the remote analogue of the local
    // QFileSystemWatcher. It polls SFTP readdir for ONLY the expanded/visible
    // dirs and, on a diff, drives the model's onDirectoryChanged re-list — the
    // same seam the backend's own directoryChanged feeds, so the model is the
    // single source of truth for row state either way.
    setupRemoteWatcher(backend);

    // Cache the backend pointer so context menu actions can reach the remote
    // mutation API (rename/mkdir/unlink) without having to go through MainWindow.
    m_remoteBackend = backend;

    // Set up the transfer progress bar + manager for this backend.
    setupTransferManager(backend);
}

void FolderAsWorkspaceDock::setupTransferManager(remote::RemoteFsBackend *backend)
{
    // Destroy old manager (if reconnecting with a new backend).
    if (m_transferManager) {
        delete m_transferManager;
        m_transferManager = nullptr;
    }

    if (!backend) return;

    // Create manager as a QObject child of this dock.
    m_transferManager = new remote::RemoteTransferManager(backend, this);

    // C1 fix: wire connectionLost from RemoteExecutionContext, which is backend's
    // parent (RemoteExecutionContext::fsBackend() creates the backend with `this`
    // as parent). This is the correct signal source — walking for SshConnection in
    // the constructor was wrong because the parent chain is backend → context, not
    // backend → SshConnection.
    if (auto *ctx = qobject_cast<remote::RemoteExecutionContext *>(backend->parent())) {
        connect(ctx, &remote::ExecutionContext::connectionLost,
                m_transferManager, &remote::RemoteTransferManager::onConnectionLost);
    }

    // Helper: ensure the log dialog exists and is visible; returns its progress bar.
    auto ensureDialog = [this]() -> TransferProgressBar * {
        if (!m_transferLogDialog) {
            m_transferLogDialog = new TransferLogDialog(this->window());
            m_transferLogDialog->setTransferActive(true);
            if (m_transferManager) {
                const QString dest = m_transferManager->currentLocalDestination();
                if (!dest.isEmpty())
                    m_transferLogDialog->setLocalDestination(dest);
                connect(m_transferLogDialog, &TransferLogDialog::cancelRequested,
                        m_transferManager, &remote::RemoteTransferManager::cancel,
                        Qt::UniqueConnection);
            }
            m_transferLogDialog->show();
        } else if (!m_transferLogDialog->isVisible()) {
            m_transferLogDialog->setTransferActive(true);
            m_transferLogDialog->show();
        }
        return m_transferLogDialog->progressBar();
    };

    // Wire manager signals to the log dialog's embedded progress bar.
    connect(m_transferManager, &remote::RemoteTransferManager::progressUpdated,
            this, [this, ensureDialog](int current, int total, qint64 bytesXfr, qint64 totalBytes,
                         const QString &currentFile, int queuedCount) {
                auto *bar = ensureDialog();
                if (!bar) return;
                bar->triggerShow();
                bar->updateProgress(current, total, bytesXfr, totalBytes, queuedCount);
                if (!currentFile.isEmpty()) {
                    if (totalBytes == 0 && bytesXfr == 0 && total == 0)
                        bar->setStatusLabel(currentFile);
                    else if (totalBytes == 0 && bytesXfr == 0 && current <= total &&
                             currentFile.startsWith(QLatin1String("Scanning")))
                        bar->setStatusLabel(currentFile);
                    else
                        bar->setCurrentFile(currentFile);
                }
            });
    connect(m_transferManager, &remote::RemoteTransferManager::transferCompleted,
            this, [this](int fileCount) {
                if (m_transferLogDialog) {
                    m_transferLogDialog->progressBar()->showCompleted(fileCount);
                    m_transferLogDialog->setFinished();
                }
            });
    connect(m_transferManager, &remote::RemoteTransferManager::transferCancelled,
            this, [this]() {
                if (m_transferLogDialog) {
                    m_transferLogDialog->setTransferActive(false);
                    m_transferLogDialog->progressBar()->hideImmediate();
                    m_transferLogDialog->appendInfo(tr("Transfer cancelled."));
                }
            });
    connect(m_transferManager, &remote::RemoteTransferManager::transferError,
            this, [this](const QString &message) {
                if (m_transferLogDialog) {
                    m_transferLogDialog->progressBar()->showError(message);
                    m_transferLogDialog->appendInfo(message);
                }
            });

    // Wire per-file status to the transfer log dialog.
    connect(m_transferManager, &remote::RemoteTransferManager::fileTransferStatus,
            this, [this, ensureDialog](const QString &path, bool ok, const QString &error) {
                ensureDialog();
                if (m_transferLogDialog)
                    m_transferLogDialog->appendStatus(path, ok, error);
            });

    // Wire dialog's progress bar cancel button to manager (deferred until dialog exists).
    connect(m_transferManager, &remote::RemoteTransferManager::progressUpdated,
            this, [this]() {
                if (m_transferLogDialog && m_transferManager) {
                    // Connect cancel only once.
                    connect(m_transferLogDialog->progressBar(), &TransferProgressBar::cancelRequested,
                            m_transferManager, &remote::RemoteTransferManager::cancel,
                            Qt::UniqueConnection);
                }
            }, Qt::UniqueConnection);
}

QStringList FolderAsWorkspaceDock::selectedPaths() const{
    QStringList paths;
    const QModelIndexList selection = ui->treeView->selectionModel()->selectedIndexes();
    paths.reserve(selection.size());
    for (const QModelIndex &idx : selection) {
        if (idx.column() != 0) continue; // multi-column model: skip non-name columns
        paths.append(resolvedFilePath(idx));
    }
    return paths;
}

QList<bool> FolderAsWorkspaceDock::selectedAreDirs() const
{
    QList<bool> result;
    const QModelIndexList selection = ui->treeView->selectionModel()->selectedIndexes();
    result.reserve(selection.size());
    for (const QModelIndex &idx : selection) {
        if (idx.column() != 0) continue;
        result.append(proxy->isDir(idx));
    }
    return result;
}

// --- SSH connection banner (D10 / Batch H) ----------------------------------

void FolderAsWorkspaceDock::ensureConnectionBanner()
{
    if (m_connectionBanner) {
        return;
    }

    // The banner is inserted at the TOP of the Files tab layout, above the tree.
    // Palette-driven chrome; status accent colors (soft warning yellow / error
    // red) are applied only on the failed/lost states, per ui-dna. The neutral
    // connecting state uses palette(window) so it blends into chrome.
    m_connectionBanner = new QFrame(ui->filesTab);
    m_connectionBanner->setObjectName(QStringLiteral("SshConnectionBanner"));
    m_connectionBanner->setFrameShape(QFrame::NoFrame);

    auto *outer = new QVBoxLayout(m_connectionBanner);
    outer->setContentsMargins(8, 6, 8, 6);
    outer->setSpacing(4);

    auto *row = new QHBoxLayout();
    row->setSpacing(6);

    m_bannerLabel = new QLabel(m_connectionBanner);
    m_bannerLabel->setWordWrap(true);
    m_bannerLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(m_bannerLabel, 1);

    // Indeterminate spinner shown only while connecting/reconnecting.
    m_bannerSpinner = new QProgressBar(m_connectionBanner);
    m_bannerSpinner->setRange(0, 0); // indeterminate
    m_bannerSpinner->setTextVisible(false);
    m_bannerSpinner->setFixedWidth(64);
    m_bannerSpinner->setVisible(false);
    row->addWidget(m_bannerSpinner, 0);

    // Reconnect button — focusable so the banner is fully keyboard-accessible.
    m_bannerReconnectBtn = new QPushButton(tr("Reconnect"), m_connectionBanner);
    m_bannerReconnectBtn->setVisible(false);
    m_bannerReconnectBtn->setFocusPolicy(Qt::StrongFocus);
    connect(m_bannerReconnectBtn, &QPushButton::clicked, this, [this]() {
        // Re-arm the connecting visual immediately so the click feels responsive;
        // MainWindow re-runs registry->connect on the reconnectRequested signal.
        showConnectingState(m_bannerLabel ? QString() : QString());
        emit reconnectRequested(this);
    });
    row->addWidget(m_bannerReconnectBtn, 0);

    outer->addLayout(row);

    // Channel-queued indicator — a muted whisper below the main row.
    m_channelQueuedLabel = new QLabel(tr("Waiting for an available channel…"),
                                      m_connectionBanner);
    m_channelQueuedLabel->setVisible(false);
    {
        QFont f = m_channelQueuedLabel->font();
        f.setItalic(true);
        m_channelQueuedLabel->setFont(f);
        QPalette pal = m_channelQueuedLabel->palette();
        pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
        m_channelQueuedLabel->setPalette(pal);
    }
    outer->addWidget(m_channelQueuedLabel);

    // Insert at index 0 of the Files tab layout (above the tree view).
    if (auto *filesLayout = qobject_cast<QVBoxLayout *>(ui->filesTab->layout())) {
        filesLayout->insertWidget(0, m_connectionBanner);
    }
    m_connectionBanner->setVisible(false);
}

void FolderAsWorkspaceDock::showConnectingState(const QString &userHost)
{
    ensureConnectionBanner();
    m_connectionBanner->setStyleSheet(QString()); // neutral chrome
    m_bannerLabel->setText(userHost.isEmpty()
                               ? tr("Connecting…")
                               : tr("Connecting to %1…").arg(userHost));
    m_bannerSpinner->setVisible(true);
    m_bannerReconnectBtn->setVisible(false);
    m_connectionBanner->setVisible(true);
}

void FolderAsWorkspaceDock::showConnectedState()
{
    if (!m_connectionBanner) {
        return; // never showed a banner (e.g. local dock)
    }
    m_connectionBanner->setVisible(false);
    if (m_channelQueuedLabel) {
        m_channelQueuedLabel->setVisible(false);
    }
}

void FolderAsWorkspaceDock::showFailedState(const QString &reason)
{
    ensureConnectionBanner();
    // Soft error red (ui-dna status accent). Color is never the sole carrier —
    // it is paired with explicit text and the Reconnect button.
    m_connectionBanner->setStyleSheet(QStringLiteral(
        "QFrame#SshConnectionBanner { background: #f8d7da; border: 1px solid #f5c6cb;"
        " border-radius: 4px; }"));
    m_bannerLabel->setText(reason.isEmpty()
                               ? tr("Could not connect to the remote host.")
                               : tr("Could not connect — %1").arg(reason));
    m_bannerSpinner->setVisible(false);
    m_bannerReconnectBtn->setVisible(true);
    m_bannerReconnectBtn->setDefault(true);
    m_connectionBanner->setVisible(true);
}

void FolderAsWorkspaceDock::showConnectionLostState(const QString &reason)
{
    ensureConnectionBanner();
    m_connectionBanner->setStyleSheet(QStringLiteral(
        "QFrame#SshConnectionBanner { background: #f8d7da; border: 1px solid #f5c6cb;"
        " border-radius: 4px; }"));
    m_bannerLabel->setText(reason.isEmpty()
                               ? tr("Connection lost — Reconnect")
                               : tr("Connection lost — %1").arg(reason));
    m_bannerSpinner->setVisible(false);
    m_bannerReconnectBtn->setVisible(true);
    m_bannerReconnectBtn->setDefault(true);
    m_connectionBanner->setVisible(true);
}

void FolderAsWorkspaceDock::showChannelQueuedState()
{
    ensureConnectionBanner();
    m_channelQueuedLabel->setVisible(true);
    // The queued whisper can appear while the tree is already live (Connected),
    // so make sure the banner frame is visible to host it without disturbing the
    // main row's current contents.
    m_connectionBanner->setVisible(true);
}

void FolderAsWorkspaceDock::clearChannelQueuedState()
{
    if (m_channelQueuedLabel) {
        m_channelQueuedLabel->setVisible(false);
    }
    // If the main banner row is otherwise idle (Connected, no spinner / button),
    // hide the whole frame again so it doesn't reserve space.
    if (m_connectionBanner && m_bannerSpinner && m_bannerReconnectBtn
        && !m_bannerSpinner->isVisible() && !m_bannerReconnectBtn->isVisible()) {
        m_connectionBanner->setVisible(false);
    }
}

void FolderAsWorkspaceDock::setupRemoteWatcher(remote::RemoteFsBackend *backend)
{
    if (m_remoteWatcher || !backend) {
        return;
    }

    // PollFn: adapt RemoteFsBackend::readdirAsync (one reused SFTP channel,
    // results queued to the UI thread) to the watcher's POD entries. The backend
    // serializes every SFTP op on the worker, so per-interval polls just queue
    // behind interactive reads — no new channel is opened.
    QPointer<remote::RemoteFsBackend> safeBackend(backend);
    auto pollFn = [safeBackend](const QString &path,
                                remote::RemoteDirectoryWatcher::PollDoneCallback done) {
        if (!safeBackend) {
            if (done) done(false, {});
            return;
        }
        safeBackend->readdirAsync(
            path, [done = std::move(done)](bool ok,
                                           const QList<remote::RemoteDirEntry> &entries,
                                           const QString & /*error*/) {
                if (!done) return;
                QList<remote::RemoteDirectoryWatcher::DirEntry> converted;
                converted.reserve(entries.size());
                for (const remote::RemoteDirEntry &e : entries) {
                    converted.append({e.name, e.isDir, e.size, e.mtimeSecs});
                }
                done(ok, converted);
            });
    };

    // VisibleDirsFn: the expanded-dir set walked from the (proxy) tree —
    // non-recursive by construction (collapsed subtrees contribute nothing).
    QPointer<FolderAsWorkspaceDock> self(this);
    auto visibleFn = [self]() -> QStringList {
        return self ? self->collectExpandedDirs() : QStringList();
    };

    m_remoteWatcher = new remote::RemoteDirectoryWatcher(std::move(pollFn),
                                                         std::move(visibleFn), this);
    // The watcher's diff result funnels through the model's existing
    // change-notification slot — identical to the backend's directoryChanged
    // wiring above. fsModel is the freshly-installed RemoteFileSystemModel here.
    if (auto *remoteModel =
            qobject_cast<remote::RemoteFileSystemModel *>(fsModel->asModel())) {
        connect(m_remoteWatcher, &remote::RemoteDirectoryWatcher::directoryChanged,
                remoteModel, &remote::RemoteFileSystemModel::onDirectoryChanged);
    }

    // Seed visibility now; focus/minimize are hooked off the top-level window
    // (in showEvent or here if the dock is already shown).
    m_remoteWatcher->setDockVisible(isVisible());
    connect(this, &QDockWidget::visibilityChanged, m_remoteWatcher,
            [this](bool visible) {
                if (m_remoteWatcher) m_remoteWatcher->setDockVisible(visible);
            });

    // App-level focus (gives 2s vs 10s). Window minimize is handled by the
    // window-state event filter (hookWindowStateForWatcher).
    if (auto *guiApp = qobject_cast<QGuiApplication *>(qApp)) {
        connect(guiApp, &QGuiApplication::applicationStateChanged, m_remoteWatcher,
                [this](Qt::ApplicationState state) {
                    if (m_remoteWatcher)
                        m_remoteWatcher->setWindowFocused(state == Qt::ApplicationActive);
                });
        m_remoteWatcher->setWindowFocused(guiApp->applicationState() == Qt::ApplicationActive);
    }

    hookWindowStateForWatcher();
    m_remoteWatcher->start();
}

QStringList FolderAsWorkspaceDock::collectExpandedDirs() const
{
    QStringList dirs;
    if (!proxy) {
        return dirs;
    }
    // PR is the synthetic root (the workspace dir). Only descend through
    // EXPANDED dirs — an unexpanded dir is skipped entirely, so its subtree is
    // never polled (the non-recursive guarantee).
    const QModelIndex pr = proxy->index(0, 0, QModelIndex());
    if (!pr.isValid()) {
        return dirs;
    }
    if (ui->treeView->isExpanded(pr)) {
        dirs << QDir::cleanPath(proxy->filePath(pr));
        QList<QModelIndex> stack;
        stack.reserve(64);
        for (int r = 0, n = proxy->rowCount(pr); r < n; ++r) {
            stack.append(proxy->index(r, 0, pr));
        }
        while (!stack.isEmpty()) {
            const QModelIndex idx = stack.takeLast();
            if (!idx.isValid() || !proxy->isDir(idx)) continue;
            if (!ui->treeView->isExpanded(idx)) continue; // skip → non-recursive
            dirs << QDir::cleanPath(proxy->filePath(idx));
            for (int r = 0, n = proxy->rowCount(idx); r < n; ++r) {
                stack.append(proxy->index(r, 0, idx));
            }
        }
    }
    return dirs;
}

void FolderAsWorkspaceDock::hookWindowStateForWatcher()
{
    if (!m_remoteWatcher) {
        return;
    }
    QWidget *win = window();
    if (!win) {
        return;
    }
    if (m_watchedWindow != win) {
        if (m_watchedWindow) {
            m_watchedWindow->removeEventFilter(this);
        }
        win->installEventFilter(this);
        m_watchedWindow = win;
    }
    // Sync the current minimize state immediately (a dock created while the
    // window is already minimized must start paused).
    m_remoteWatcher->setWindowMinimized(win->isMinimized());
}

void FolderAsWorkspaceDock::wireFileTreeGitDecorations()
{
    // Git decorations are local-only (they colour QFileSystemModel rows via the
    // local PathStatusIndex). A remote workspace has no local `model` — bail.
    if (!model) return;

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
                        if (!model) return; // remote workspace: no local model
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

        // indexAt returns a proxy index — resolvedFilePath maps through the proxy
        // then prepends the SSH URI prefix for remote workspaces so consumers
        // receive a routable path (ssh://profileId/remote/path) rather than a raw
        // POSIX path that only the worker thread can interpret.
        const QString absPath = resolvedFilePath(index);
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
    // Reset the one-shot root-expand guard: the model reset inside
    // fsModel->setRootPath (remote) or the proxy reset in setRootSourcePath
    // collapses the tree, so directoryLoaded must re-expand the root.
    m_rootExpandApplied = false;

    if (fsModel) fsModel->setRootPath(dir);
    // Point the proxy at the new root. The view's root index stays
    // QModelIndex() so the workspace dir shows as the single top node — we do
    // NOT call setRootIndex(model->index(dir)) (that would hide the root).
    proxy->setRootSourcePath(dir);

    if (gitTab) {
        gitTab->setWorkspaceRoot(m_sshWorkspaceUri.isEmpty() ? dir : m_sshWorkspaceUri);
    }

    // Window title doubles as the tab label when several workspaces are tabified
    // alongside each other, so make it the folder basename rather than the static
    // .ui label. SSH workspaces keep the "SSH: user@host" title set by MainWindow
    // (the dir here is the remote POSIX path, not a meaningful local basename).
    if (!m_sshWorkspaceUri.isEmpty()) {
        // Leave the existing title (already set to "SSH: user@host").
    } else if (dir.isEmpty()) {
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

QString FolderAsWorkspaceDock::resolvedPath(const QString &posixPath) const
{
    if (!m_sshWorkspaceUri.isEmpty() && !posixPath.isEmpty()) {
        const remote::SshUri uri = remote::parseSshUri(m_sshWorkspaceUri);
        if (uri.valid)
            return remote::formatSshUri(uri.profileId, posixPath);
    }
    return posixPath;
}

QString FolderAsWorkspaceDock::resolvedFilePath(const QModelIndex &proxyIdx) const
{
    return resolvedPath(proxy->filePath(proxyIdx));
}

QString FolderAsWorkspaceDock::rootPath() const
{
    // SSH workspaces store their identity as an ssh:// URI (D5a). Return it for
    // deduplication, persistence, and context resolution. The model's rootPath is
    // the POSIX path used for SFTP operations — callers that need the exec-cwd
    // should go through TerminalCwdResolver, not rootPath().
    if (!m_sshWorkspaceUri.isEmpty()) {
        return m_sshWorkspaceUri;
    }
    return fsModel ? fsModel->rootPath() : QString();
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
    // Top-level window state for the remote poll-watcher's interval gating. We
    // observe (never consume) WindowStateChange (minimize/restore) and
    // ActivationChange (focus) on the window we hooked in
    // hookWindowStateForWatcher; app-level focus is also tracked via
    // applicationStateChanged, but ActivationChange catches the per-window case.
    if (m_remoteWatcher && watched == m_watchedWindow) {
        if (event->type() == QEvent::WindowStateChange) {
            if (auto *w = qobject_cast<QWidget *>(watched)) {
                m_remoteWatcher->setWindowMinimized(w->isMinimized());
            }
        } else if (event->type() == QEvent::ActivationChange) {
            if (auto *w = qobject_cast<QWidget *>(watched)) {
                m_remoteWatcher->setWindowFocused(w->isActiveWindow());
            }
        }
    }
    return QDockWidget::eventFilter(watched, event);
}

void FolderAsWorkspaceDock::showEvent(QShowEvent *event)
{
    QDockWidget::showEvent(event);
    // The top-level window is reliably resolvable once shown; (re)hook the
    // focus/minimize filter for the remote watcher and mark the dock visible.
    if (m_remoteWatcher) {
        hookWindowStateForWatcher();
        m_remoteWatcher->setDockVisible(true);
    }
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
            if (model) model->notifyPathsChanged(stale);
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
    if (model) {
        model->notifyPathsChanged(delta);
    } else if (auto *rm = qobject_cast<remote::RemoteFileSystemModel *>(fsModel ? fsModel->asModel() : nullptr)) {
        rm->notifyPathsChanged(delta);
    }
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
        m_pendingEditorOpenPath = resolvedPath(absPath);
        return;
    }
    emit fileDoubleClicked(resolvedPath(absPath));
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
    gitCommitView->setRunnerScope(gitTab->controller()->runnerScope());
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
        if (gitTab && gitTab->controller()) {
            c->setRunnerScope(gitTab->controller()->runnerScope());
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
    const bool isSsh = !m_sshWorkspaceUri.isEmpty();
    const QString cleaned = isSsh ? remote::parseSshUri(absolutePath).remotePath
                                  : QDir::cleanPath(absolutePath);
    if (cleaned.isEmpty()) return;

    const QString root = isSsh ? remote::parseSshUri(m_sshWorkspaceUri).remotePath
                               : QDir::cleanPath(rootPath());
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
        const bool isSsh = !m_sshWorkspaceUri.isEmpty();
        auto parentOf = [isSsh](const QString &p) -> QString {
            if (isSsh) {
                const int slash = p.lastIndexOf(QLatin1Char('/'));
                return (slash > 0) ? p.left(slash) : QString();
            }
            return QFileInfo(p).absolutePath();
        };
        QString cur = parentOf(cleaned);
        while (!cur.isEmpty()) {
            ancestorsDesc.prepend(cur);
            if (cur.compare(root, cs) == 0) break;
            const QString parent = parentOf(cur);
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
        // is expanded like any ancestor. fsModel->indexForPath(dir) is a SOURCE
        // index; map it to the proxy before the view call.
        const QModelIndex srcIdx = fsModel ? fsModel->indexForPath(dir) : QModelIndex();
        if (!srcIdx.isValid() || !fsModel->isDir(srcIdx)) break;
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
    const QModelIndex leafSrc = fsModel ? fsModel->indexForPath(cleaned) : QModelIndex();
    if (leafSrc.isValid()) {
        const QModelIndex leafIdx = proxy->mapFromSource(leafSrc);
        // Cancel any still-pending select from an earlier reveal/restore whose
        // parent hasn't loaded yet — this newer explicit reveal must win, and a
        // stale pendingCurrentItem would otherwise steal the selection back when
        // its directoryLoaded eventually fires.
        setProperty("pendingCurrentItem", QVariant());
        m_programmaticToggle = true;
        ui->treeView->selectionModel()->clearSelection(); // D7: reset multi-select state
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
    const bool isSsh = !m_sshWorkspaceUri.isEmpty();
    for (const QString &p : snapshot.expandedFolders) {
        if (p.isEmpty()) continue;
        const QString cleaned = QDir::cleanPath(p);
        // For SSH (POSIX) paths, QFileInfo::absolutePath resolves against the
        // local drive on Windows — use string-based parent extraction instead.
        QString parent;
        if (isSsh) {
            const int lastSlash = cleaned.lastIndexOf(QLatin1Char('/'));
            parent = (lastSlash > 0) ? cleaned.left(lastSlash) : QString();
        } else {
            parent = QFileInfo(cleaned).absolutePath();
        }
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
        // For SSH workspaces rootPath is the full ssh:// URI; expandedFolders
        // stores POSIX paths from the remote model. Extract the remote path for
        // comparison. Local paths pass through unchanged.
        QString cleanedRoot;
        if (remote::isSshUri(snapshot.rootPath)) {
            const remote::SshUri uri = remote::parseSshUri(snapshot.rootPath);
            cleanedRoot = QDir::cleanPath(uri.remotePath);
        } else {
            cleanedRoot = QDir::cleanPath(snapshot.rootPath);
        }
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
    // For SSH workspaces the memo key is the full ssh:// URI (matches the lookup
    // in openFolderAsWorkspacePath). For local workspaces use the cleaned path.
    if (!m_sshWorkspaceUri.isEmpty()) {
        s.rootPath = m_sshWorkspaceUri;
    } else {
        s.rootPath = QDir::cleanPath(rootPath());
    }
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
    const bool isSsh = !m_sshWorkspaceUri.isEmpty();
    QString cur = cleanedChild;
    for (;;) {
        QString parent;
        if (isSsh) {
            const int slash = cur.lastIndexOf(QLatin1Char('/'));
            parent = (slash > 0) ? cur.left(slash) : QString();
        } else {
            parent = QFileInfo(cur).absolutePath();
        }
        if (parent.isEmpty() || parent == cur) break;
        if (m_userVetoed.contains(parent)) return true;
        cur = parent;
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
            if (m_rootShouldExpand && !m_userVetoed.contains(cleanRoot)) {
                const QModelIndex pr = proxy->index(0, 0, QModelIndex());
                if (pr.isValid()) {
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

            const QModelIndex srcIdx = fsModel ? fsModel->indexForPath(child) : QModelIndex();
            if (!srcIdx.isValid() || !fsModel->isDir(srcIdx)) continue;  // stale path / now-file
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
        QString itemParent;
        if (!m_sshWorkspaceUri.isEmpty()) {
            const int slash = pendingItem.lastIndexOf(QLatin1Char('/'));
            itemParent = (slash > 0) ? pendingItem.left(slash) : QString();
        } else {
            itemParent = QFileInfo(pendingItem).absolutePath();
        }
        if (itemParent == key) {
            const QModelIndex itemSrc = fsModel ? fsModel->indexForPath(pendingItem) : QModelIndex();
            if (itemSrc.isValid()) {
                const QModelIndex itemIdx = proxy->mapFromSource(itemSrc);
                m_programmaticToggle = true;
                ui->treeView->selectionModel()->clearSelection(); // D7: reset multi-select state
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
