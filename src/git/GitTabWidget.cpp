/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
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

#include "GitTabWidget.h"

#include "ApplicationSettings.h"
#include "BranchPickerPopup.h"
#include "../ai/CommitMessageGenerator.h"
#include "../NotepadNextApplication.h"
#include "../dialogs/MainWindow.h"
#include "../docks/FolderAsWorkspaceDock.h"
#include "CommitComposer.h"
#include "GitError.h"
#include "GitRepoModel.h"
#include "GitStatusEntry.h"
#include "GitStatusModel.h"

#include <QAction>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

// Hand-painted stop glyph so the cancel affordance renders identically across
// platforms regardless of which symbol fonts are installed. Earlier attempts
// at U+25FC ◼ and U+25A0 ■ tofu'd on stock Windows when the active QFont fell
// back to a non-symbol family.
QIcon makeStopIcon(const QColor &color)
{
    const int size = 16;
    const qreal dpr = 1.0;
    QPixmap pm(static_cast<int>(size * dpr), static_cast<int>(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const qreal inset = 4.0;
    p.drawRoundedRect(QRectF(inset, inset, size - 2 * inset, size - 2 * inset),
                      1.5, 1.5);
    return QIcon(pm);
}

} // namespace

GitTabWidget::GitTabWidget(const QString &workspaceRoot, QWidget *parent)
    : QWidget(parent), m_workspaceRoot(workspaceRoot)
{
    buildUi();
    updateActionsEnabled();

    // Busy-indicator animation: cycle dot count 0→3 every 400ms so the status
    // label visibly progresses during AI generation. Timer is a member (not a
    // QTimer::singleShot per tick) so it's allocation-free once instantiated.
    m_aiBusyTimer.setInterval(400);
    connect(&m_aiBusyTimer, &QTimer::timeout, this, [this]() {
        m_aiDotPhase = (m_aiDotPhase + 1) % 4;
        const QString dots = QString(m_aiDotPhase, QLatin1Char('.'));
        m_statusLabel->setText(m_aiBusyBase + dots);
    });

    // AI generator wiring (singleton owned by NotepadNextApplication).
    if (auto *app = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance())) {
        if (auto *gen = app->getCommitMessageGenerator()) {
            connect(gen, &ai::CommitMessageGenerator::stateChanged, this,
                    [this](ai::CommitMessageGenerator::State s) {
                        onGeneratorStateChanged(static_cast<int>(s));
                    });
            connect(gen, &ai::CommitMessageGenerator::errorOccurred,
                    this, &GitTabWidget::onGeneratorError);
        }
        // Cancel in-flight generation if the user switches to a different
        // workspace dock — the target composer becomes invisible / stale.
        if (auto *mw = qobject_cast<MainWindow *>(app->activeWindow())) {
            connect(mw, &MainWindow::activeWorkspaceChanged, this,
                    [this](FolderAsWorkspaceDock *, FolderAsWorkspaceDock *) {
                        if (auto *app2 = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance())) {
                            if (auto *gen = app2->getCommitMessageGenerator()) {
                                gen->cancelIfTarget(m_composer);
                            }
                        }
                    });
        }
    }
}

GitTabWidget::~GitTabWidget()
{
    persistCommitDraft();
    teardownController();
}

void GitTabWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    // Empty hint shown when no workspace is open.
    m_emptyHint = new QLabel(tr("Open a folder as workspace to use Git."), this);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setStyleSheet(QStringLiteral("color: palette(mid); padding: 24px;"));
    m_emptyHint->hide();
    root->addWidget(m_emptyHint);

    // Top row: repo combo + branch button + refresh + menu
    auto *topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(4);

    m_repoCombo = new QComboBox(this);
    m_repoCombo->setToolTip(tr("Select repository"));
    m_repoCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_repoCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_branchBtn = new QToolButton(this);
    m_branchBtn->setText(tr("(no repo)"));
    m_branchBtn->setToolTip(tr("Switch / create branch"));
    m_branchBtn->setPopupMode(QToolButton::InstantPopup);
    m_branchBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);

    m_refreshBtn = new QToolButton(this);
    m_refreshBtn->setText(tr("⟳"));
    m_refreshBtn->setToolTip(tr("Refresh"));

    m_menuBtn = new QToolButton(this);
    m_menuBtn->setText(tr("⋯"));
    m_menuBtn->setToolTip(tr("More actions"));
    m_menuBtn->setPopupMode(QToolButton::InstantPopup);

    topRow->addWidget(m_repoCombo, 1);
    topRow->addWidget(m_branchBtn);
    topRow->addWidget(m_refreshBtn);
    topRow->addWidget(m_menuBtn);
    root->addLayout(topRow);

    // Error banner (hidden by default)
    m_errorBanner = new QFrame(this);
    m_errorBanner->setFrameShape(QFrame::StyledPanel);
    m_errorBanner->setStyleSheet(
        QStringLiteral("QFrame { background-color: #fdecea; border: 1px solid #f5c2c0; border-radius: 3px; }"
                       "QLabel { color: #8a1f1c; }"));
    {
        auto *lay = new QHBoxLayout(m_errorBanner);
        lay->setContentsMargins(8, 4, 4, 4);
        lay->setSpacing(6);
        m_errorLabel = new QLabel(m_errorBanner);
        m_errorLabel->setWordWrap(true);
        m_errorCloseBtn = new QToolButton(m_errorBanner);
        m_errorCloseBtn->setText(QStringLiteral("×"));
        m_errorCloseBtn->setAutoRaise(true);
        lay->addWidget(m_errorLabel, 1);
        lay->addWidget(m_errorCloseBtn, 0, Qt::AlignTop);
    }
    m_errorBanner->hide();
    root->addWidget(m_errorBanner);

    // Stage / Unstage row
    auto *stageRow = new QHBoxLayout();
    stageRow->setContentsMargins(0, 0, 0, 0);
    stageRow->setSpacing(4);
    m_stageBtn = new QPushButton(tr("Stage"), this);
    m_unstageBtn = new QPushButton(tr("Unstage"), this);
    m_stageAllBtn = new QPushButton(tr("Stage All"), this);
    m_unstageAllBtn = new QPushButton(tr("Unstage All"), this);
    for (auto *b : {m_stageBtn, m_unstageBtn, m_stageAllBtn, m_unstageAllBtn}) {
        b->setAutoDefault(false);
        b->setDefault(false);
    }
    stageRow->addWidget(m_stageBtn);
    stageRow->addWidget(m_unstageBtn);
    stageRow->addStretch(1);
    stageRow->addWidget(m_stageAllBtn);
    stageRow->addWidget(m_unstageAllBtn);
    root->addLayout(stageRow);

    // Status tree
    m_tree = new QTreeView(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setUniformRowHeights(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    root->addWidget(m_tree, 1);

    // Commit composer
    m_composer = new CommitComposer(this);
    root->addWidget(m_composer);

    // Status label
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    connect(m_repoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GitTabWidget::onRepoSelected);
    connect(m_refreshBtn, &QToolButton::clicked, this, &GitTabWidget::onRefreshClicked);
    connect(m_branchBtn, &QToolButton::clicked, this, &GitTabWidget::onBranchButtonClicked);
    connect(m_menuBtn, &QToolButton::clicked, this, &GitTabWidget::onMenuButtonClicked);
    connect(m_stageBtn, &QPushButton::clicked, this, &GitTabWidget::onStageClicked);
    connect(m_unstageBtn, &QPushButton::clicked, this, &GitTabWidget::onUnstageClicked);
    connect(m_stageAllBtn, &QPushButton::clicked, this, &GitTabWidget::onStageAllClicked);
    connect(m_unstageAllBtn, &QPushButton::clicked, this, &GitTabWidget::onUnstageAllClicked);
    connect(m_composer, &CommitComposer::submitRequested, this, &GitTabWidget::onCommitRequested);
    connect(m_composer, &CommitComposer::messageChanged, this, [this]() {
        persistCommitDraft();
        updateActionsEnabled();
    });
    connect(m_composer, &CommitComposer::aiTriggerRequested,
            this, &GitTabWidget::onAiTriggerRequested);
    connect(m_composer, &CommitComposer::aiCancelRequested,
            this, &GitTabWidget::onAiCancelRequested);
    connect(m_errorCloseBtn, &QToolButton::clicked, this, &GitTabWidget::clearError);
}

void GitTabWidget::setWorkspaceRoot(const QString &root)
{
    if (root == m_workspaceRoot) return;
    persistCommitDraft();
    m_workspaceRoot = root;
    if (m_initialized) {
        rebuildController();
    }
}

void GitTabWidget::initializeIfNeeded()
{
    if (m_initialized) return;
    m_initialized = true;
    rebuildController();
}

void GitTabWidget::teardownController()
{
    if (!m_controller) return;
    m_controller->disconnect(this);
    m_controller->cancelCurrent();
    m_tree->setModel(nullptr);
    m_repoCombo->setModel(nullptr);
    m_controller->deleteLater();
    m_controller = nullptr;
}

void GitTabWidget::rebuildController()
{
    teardownController();
    m_composer->clear();
    m_statusLabel->clear();
    clearError();

    const bool hasWorkspace = !m_workspaceRoot.isEmpty()
                              && QDir(m_workspaceRoot).exists();
    m_emptyHint->setVisible(!hasWorkspace);
    m_repoCombo->setEnabled(hasWorkspace);
    m_branchBtn->setEnabled(false);
    m_refreshBtn->setEnabled(hasWorkspace);
    m_menuBtn->setEnabled(false);
    m_composer->setSubmitEnabled(false);

    if (!hasWorkspace) {
        updateActionsEnabled();
        return;
    }

    m_controller = new GitController(m_workspaceRoot, this);
    m_repoCombo->setModel(m_controller->repoModel());
    m_tree->setModel(m_controller->statusModel());

    connect(m_controller, &GitController::stateChanged, this, &GitTabWidget::onControllerState);
    connect(m_controller, &GitController::statusUpdated, this, &GitTabWidget::onStatusUpdated);
    connect(m_controller, &GitController::branchesUpdated, this, &GitTabWidget::onBranchesUpdated);
    connect(m_controller, &GitController::reposUpdated, this, &GitTabWidget::onReposUpdated);
    connect(m_controller, &GitController::opSucceeded, this, &GitTabWidget::onOpSucceeded);
    connect(m_controller, &GitController::errorOccurred, this, &GitTabWidget::onError);
    connect(m_controller, &GitController::gitMissing, this, &GitTabWidget::onGitMissing);
    connect(m_controller, &GitController::dirtyTreePromptRequested,
            this, &GitTabWidget::onDirtyTreePrompt);
    connect(m_controller, &GitController::remoteOpProgress,
            this, &GitTabWidget::onRemoteOpProgress);
    connect(m_controller, &GitController::fullDiffReady, this, &GitTabWidget::onFullDiffReady);
    connect(m_controller, &GitController::fullDiffFailed, this, &GitTabWidget::onFullDiffFailed);
    connect(m_tree, &QTreeView::doubleClicked, this, &GitTabWidget::onTreeDoubleClicked);
    connect(m_tree, &QTreeView::clicked, this, &GitTabWidget::onTreeClicked);

    restoreCommitDraft();
    m_controller->initialize();
}

QString GitTabWidget::settingsKey(const QString &subkey) const
{
    const QByteArray hash = QCryptographicHash::hash(
        m_workspaceRoot.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    return QStringLiteral("Git/%1/%2").arg(QString::fromLatin1(hash), subkey);
}

void GitTabWidget::persistCommitDraft()
{
    if (!m_composer) return;
    if (m_workspaceRoot.isEmpty()) return;
    ApplicationSettings settings;
    settings.setValue(settingsKey(QStringLiteral("commitDraft")), m_composer->message());
}

void GitTabWidget::restoreCommitDraft()
{
    if (!m_composer) return;
    ApplicationSettings settings;
    const QString draft = settings.value(settingsKey(QStringLiteral("commitDraft"))).toString();
    if (!draft.isEmpty()) m_composer->setMessage(draft);
}

void GitTabWidget::onReposUpdated()
{
    if (!m_controller) return;
    m_suppressRepoCombo = true;

    ApplicationSettings settings;
    const QString saved = settings.value(settingsKey(QStringLiteral("lastRepo"))).toString();
    int pick = -1;
    if (!saved.isEmpty()) pick = m_controller->repoModel()->indexOf(saved);
    if (pick < 0 && m_controller->repoModel()->rowCount() > 0) pick = 0;

    m_repoCombo->setCurrentIndex(pick);
    m_suppressRepoCombo = false;

    if (pick >= 0) {
        const auto *info = m_controller->repoModel()->infoAt(pick);
        if (info) m_controller->selectRepo(info->toplevel);
    }
    updateActionsEnabled();
}

void GitTabWidget::onRepoSelected(int index)
{
    if (m_suppressRepoCombo || !m_controller) return;
    if (index < 0) return;
    const auto *info = m_controller->repoModel()->infoAt(index);
    if (!info) return;

    // Cancel any in-flight AI generation pinned to this composer when the user
    // switches repos within the same dock — the diff context just changed.
    if (auto *app = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance())) {
        if (auto *gen = app->getCommitMessageGenerator()) {
            gen->cancelIfTarget(m_composer);
        }
    }

    ApplicationSettings settings;
    settings.setValue(settingsKey(QStringLiteral("lastRepo")), info->toplevel);
    m_controller->selectRepo(info->toplevel);
}

void GitTabWidget::onRefreshClicked()
{
    if (m_controller) m_controller->refresh();
}

void GitTabWidget::onBranchesUpdated()
{
    updateBranchButtonText();
    updateActionsEnabled();
}

void GitTabWidget::updateBranchButtonText()
{
    if (!m_controller || m_controller->currentRepo().isEmpty()) {
        m_branchBtn->setText(tr("(no repo)"));
        return;
    }
    if (m_controller->isEmptyRepo()) {
        m_branchBtn->setText(tr("(empty repo)"));
        return;
    }
    const QString br = m_controller->currentBranch();
    if (!br.isEmpty()) {
        m_branchBtn->setText(br);
    } else {
        const QString sha = m_controller->detachedShortSha();
        m_branchBtn->setText(sha.isEmpty() ? tr("(detached)")
                                           : tr("(detached @ %1)").arg(sha));
    }
}

void GitTabWidget::onBranchButtonClicked()
{
    if (!m_controller || m_controller->currentRepo().isEmpty()) return;

    if (!m_branchPicker) {
        m_branchPicker = new BranchPickerPopup(this);
        connect(m_branchPicker, &BranchPickerPopup::branchSelected,
                this, &GitTabWidget::handleBranchSelected);
        connect(m_branchPicker, &BranchPickerPopup::createBranchRequested,
                this, &GitTabWidget::handleCreateBranch);
    }
    m_branchPicker->setBranches(m_controller->branchesLocal(),
                                m_controller->branchesRemote(),
                                m_controller->currentBranch());
    const QPoint pos = m_branchBtn->mapToGlobal(m_branchBtn->rect().bottomLeft());
    m_branchPicker->popupAt(pos);
}

void GitTabWidget::handleBranchSelected(const QString &name)
{
    if (!m_controller) return;
    QString target = name;
    // Remote ref like "origin/feature/x" → tracked local "feature/x".
    const QStringList remotes = m_controller->remotes();
    for (const auto &r : remotes) {
        const QString prefix = r + QLatin1Char('/');
        if (target.startsWith(prefix)) {
            target = target.mid(prefix.size());
            break;
        }
    }

    const auto *st = m_controller->statusModel();
    const bool dirty = st && (st->hasStaged() || st->totalEntries() > 0
                              ? (st->totalEntries() - st->entriesInSection(GitStatusEntry::Untracked)) > 0
                              : false);
    if (dirty) {
        QMessageBox box(QMessageBox::Question, tr("Working tree has changes"),
                        tr("Switch to '%1'? Your working tree has uncommitted changes.").arg(target),
                        QMessageBox::NoButton, this);
        auto *stash = box.addButton(tr("Stash and switch"), QMessageBox::AcceptRole);
        auto *force = box.addButton(tr("Switch anyway"), QMessageBox::DestructiveRole);
        auto *cancel = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(stash);
        box.exec();
        auto *clicked = box.clickedButton();
        if (clicked == cancel) return;
        if (clicked == stash) {
            m_controller->switchBranch(target, GitController::BranchSwitchPolicy::StashAndSwitch);
            return;
        }
        if (clicked == force) {
            m_controller->switchBranch(target, GitController::BranchSwitchPolicy::SwitchAnyway);
            return;
        }
    } else {
        m_controller->switchBranch(target, GitController::BranchSwitchPolicy::Cancel);
    }
}

void GitTabWidget::handleCreateBranch(const QString &name, const QString &base)
{
    if (!m_controller) return;
    m_controller->createBranch(name, base, /*checkout=*/true);
}

void GitTabWidget::onMenuButtonClicked()
{
    if (!m_controller || m_controller->currentRepo().isEmpty()) return;

    QMenu menu(this);
    const QStringList remotes = m_controller->remotes();
    const bool hasRemote = !remotes.isEmpty();

    QAction *aFetch = menu.addAction(tr("Fetch"));
    QMenu *fetchFrom = menu.addMenu(tr("Fetch from"));
    QAction *aPull = menu.addAction(tr("Pull"));
    QAction *aPullR = menu.addAction(tr("Pull (rebase)"));
    menu.addSeparator();
    QAction *aPush = menu.addAction(tr("Push"));
    QMenu *pushTo = menu.addMenu(tr("Push to"));
    QAction *aForce = menu.addAction(tr("Force push (with lease)"));

    aFetch->setEnabled(hasRemote);
    fetchFrom->setEnabled(hasRemote);
    aPull->setEnabled(hasRemote);
    aPullR->setEnabled(hasRemote);
    aPush->setEnabled(hasRemote);
    pushTo->setEnabled(hasRemote);
    aForce->setEnabled(hasRemote);

    for (const auto &r : remotes) {
        QAction *f = fetchFrom->addAction(r);
        connect(f, &QAction::triggered, this, [this, r]() { m_controller->fetch(r); });
        QAction *p = pushTo->addAction(r);
        connect(p, &QAction::triggered, this, [this, r]() {
            m_controller->push(r, /*setUpstream=*/true);
        });
    }

    connect(aFetch, &QAction::triggered, this, [this]() { m_controller->fetch(); });
    connect(aPull, &QAction::triggered, this, [this]() { m_controller->pull(false); });
    connect(aPullR, &QAction::triggered, this, [this]() { m_controller->pull(true); });
    connect(aPush, &QAction::triggered, this, [this]() { m_controller->push(); });
    connect(aForce, &QAction::triggered, this, [this]() {
        if (QMessageBox::warning(this, tr("Force push?"),
                tr("Force push with --force-with-lease. This rewrites remote history. Continue?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
            m_controller->forcePush();
        }
    });

    menu.exec(m_menuBtn->mapToGlobal(m_menuBtn->rect().bottomLeft()));
}

void GitTabWidget::onStageClicked()
{
    if (!m_controller) return;
    const auto *st = m_controller->statusModel();
    const QStringList paths = st->unstagedSelectionPaths(m_tree->selectionModel()->selectedIndexes());
    if (paths.isEmpty()) return;
    m_controller->stagePaths(paths);
}

void GitTabWidget::onUnstageClicked()
{
    if (!m_controller) return;
    const auto *st = m_controller->statusModel();
    const QStringList paths = st->stagedSelectionPaths(m_tree->selectionModel()->selectedIndexes());
    if (paths.isEmpty()) return;
    m_controller->unstagePaths(paths);
}

void GitTabWidget::onStageAllClicked()
{
    if (m_controller) m_controller->stageAll();
}

void GitTabWidget::onUnstageAllClicked()
{
    if (m_controller) m_controller->unstageAll();
}

void GitTabWidget::onCommitRequested()
{
    if (!m_controller) return;
    const QString msg = m_composer->message().trimmed();
    if (msg.isEmpty() && !m_composer->amendChecked()) {
        showError(tr("Commit message is empty."));
        return;
    }
    m_committing = true;
    m_controller->commit(m_composer->message(),
                         m_composer->amendChecked(),
                         m_composer->signoffChecked(),
                         m_composer->trackedOnly());
}

void GitTabWidget::onTreeClicked(const QModelIndex &index)
{
    if (!m_controller) return;
    if (!index.isValid()) return;
    if (index.data(GitStatusModel::IsSectionRole).toBool()) return;
    const QString rel = index.data(GitStatusModel::RelPathRole).toString();
    if (rel.isEmpty()) return;
    const QString repo = m_controller->currentRepo();
    if (repo.isEmpty()) return;

    const QString abs = QDir(repo).filePath(rel);
    // Submodule heuristic: status entry whose worktree path is a directory.
    // Catches both registered submodules and embedded git repos.
    if (QFileInfo(abs).isDir()) {
        emit openSubmoduleRequested(abs);
        return;
    }

    GitStatusEntry entry;
    entry.relPath = rel;
    entry.origRelPath = index.data(GitStatusModel::OrigPathRole).toString();
    entry.change = static_cast<GitStatusEntry::Change>(
        index.data(GitStatusModel::ChangeRole).toInt());
    entry.stagedSide = index.data(GitStatusModel::StagedSideRole).toBool();
    entry.section = static_cast<GitStatusEntry::Section>(
        index.data(GitStatusModel::SectionRole).toInt());
    entry.xy = index.data(GitStatusModel::XyRole).toString();
    entry.hasUnstableEncoding = index.data(GitStatusModel::HasUnstableEncodingRole).toBool();
    entry.isBinary = index.data(GitStatusModel::IsBinaryRole).toBool();
    entry.addedLines = index.data(GitStatusModel::AddedLinesRole).toInt();
    entry.deletedLines = index.data(GitStatusModel::DeletedLinesRole).toInt();
    entry.oursSha = index.data(GitStatusModel::OursShaRole).toString();
    entry.theirsSha = index.data(GitStatusModel::TheirsShaRole).toString();
    emit diffRequested(entry);
}

void GitTabWidget::onTreeDoubleClicked(const QModelIndex &index)
{
    if (!m_controller) return;
    if (!index.isValid()) return;
    if (index.data(GitStatusModel::IsSectionRole).toBool()) return;
    const QString rel = index.data(GitStatusModel::RelPathRole).toString();
    if (rel.isEmpty()) return;
    const QString repo = m_controller->currentRepo();
    if (repo.isEmpty()) return;
    const QString abs = QDir(repo).filePath(rel);
    // Symmetric with onTreeClicked: a submodule entry is a directory, and
    // fileActivated would funnel it into MainWindow::openFile which prompts
    // "Create File?" on a non-file path. Route it through the workspace path.
    if (QFileInfo(abs).isDir()) {
        emit openSubmoduleRequested(abs);
        return;
    }
    emit fileActivated(abs);
}

void GitTabWidget::onControllerState(GitController::State s)
{
    const bool busy = (s == GitController::State::Running
                       || s == GitController::State::Refreshing
                       || s == GitController::State::Discovering);
    m_refreshBtn->setEnabled(!busy && m_controller);
    updateActionsEnabled();

    switch (s) {
    case GitController::State::Idle:        m_statusLabel->clear(); break;
    case GitController::State::Discovering: appendStatus(tr("Discovering repositories…")); break;
    case GitController::State::Refreshing:  appendStatus(tr("Refreshing…")); break;
    case GitController::State::Running:     appendStatus(tr("Running…")); break;
    case GitController::State::Error:       break; // error banner already shown
    }
}

void GitTabWidget::onStatusUpdated()
{
    // Expand all sections so changes are visible immediately.
    m_tree->expandAll();
    updateActionsEnabled();
}

void GitTabWidget::onOpSucceeded(const QString &name)
{
    appendStatus(tr("%1 succeeded.").arg(name));
    if (m_committing && name == QLatin1String("commit")) {
        m_committing = false;
        m_composer->clear();
        m_composer->setAmendChecked(false);
        ApplicationSettings settings;
        settings.remove(settingsKey(QStringLiteral("commitDraft")));
    }
    clearError();
}

void GitTabWidget::onError(const GitError &err)
{
    m_committing = false;
    QString text = err.humanMessage;
    if (text.isEmpty()) text = tr("Git operation failed.");
    showError(text, err.hint);
    appendStatus(text);
}

void GitTabWidget::onGitMissing()
{
    showError(tr("git was not found on PATH."),
              tr("Install git and ensure it is reachable, then refresh."));
    m_repoCombo->setEnabled(false);
    m_branchBtn->setEnabled(false);
    m_menuBtn->setEnabled(false);
}

void GitTabWidget::onDirtyTreePrompt(const QString &target)
{
    // The controller delegates the prompt back to the UI when it detects
    // a dirty tree on its own (e.g. before checkout).
    QMessageBox box(QMessageBox::Question, tr("Working tree has changes"),
                    tr("Switch to '%1'? Your working tree has uncommitted changes.").arg(target),
                    QMessageBox::NoButton, this);
    auto *stash = box.addButton(tr("Stash and switch"), QMessageBox::AcceptRole);
    auto *force = box.addButton(tr("Switch anyway"), QMessageBox::DestructiveRole);
    auto *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(stash);
    box.exec();
    auto *clicked = box.clickedButton();
    if (clicked == cancel) return;
    if (clicked == stash) {
        m_controller->switchBranch(target, GitController::BranchSwitchPolicy::StashAndSwitch);
    } else if (clicked == force) {
        m_controller->switchBranch(target, GitController::BranchSwitchPolicy::SwitchAnyway);
    }
}

void GitTabWidget::onRemoteOpProgress(const QString &line)
{
    appendStatus(line);
}

void GitTabWidget::updateActionsEnabled()
{
    const bool hasController = m_controller != nullptr;
    const bool hasRepo = hasController && !m_controller->currentRepo().isEmpty();
    const bool empty = hasController && m_controller->isEmptyRepo();
    const auto *st = hasController ? m_controller->statusModel() : nullptr;
    const bool anyEntries = st && st->totalEntries() > 0;
    const bool anyStaged = st && st->hasStaged();
    const bool hasConflicts = st && st->hasConflicts();

    m_branchBtn->setEnabled(hasRepo && !empty);
    m_menuBtn->setEnabled(hasRepo);

    m_stageBtn->setEnabled(hasRepo);
    m_unstageBtn->setEnabled(hasRepo && anyStaged);
    m_stageAllBtn->setEnabled(hasRepo && anyEntries);
    m_unstageAllBtn->setEnabled(hasRepo && anyStaged);

    const QString msg = m_composer ? m_composer->message().trimmed() : QString();
    const bool canCommit = hasRepo && !hasConflicts
                           && (!msg.isEmpty() || (m_composer && m_composer->amendChecked()));
    m_composer->setSubmitEnabled(canCommit);
}

void GitTabWidget::showError(const QString &text, const QString &hint)
{
    QString full = text;
    if (!hint.isEmpty()) full += QStringLiteral("\n") + hint;
    m_errorLabel->setText(full);
    m_errorBanner->show();
}

void GitTabWidget::clearError()
{
    m_errorBanner->hide();
    m_errorLabel->clear();
}

void GitTabWidget::appendStatus(const QString &msg)
{
    m_statusLabel->setText(msg);
}

// --- AI commit-message generation --------------------------------------------

void GitTabWidget::onAiTriggerRequested()
{
    auto *app = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance());
    if (!app) return;
    auto *gen = app->getCommitMessageGenerator();
    if (!gen) return;

    // If a generation is already in flight for this composer, treat the click
    // as a cancel.
    using State = ai::CommitMessageGenerator::State;
    if (gen->state() != State::Idle) {
        gen->cancelIfTarget(m_composer);
        return;
    }

    QString why;
    if (!gen->canFireGenerate(m_workspaceRoot, m_composer, &why)) {
        showError(why);
        return;
    }
    if (!m_controller || m_controller->currentRepo().isEmpty()) {
        showError(tr("No repository selected for AI generation."));
        return;
    }

    m_pendingAiSubjectHint = m_composer->subjectLine();
    m_aiAwaitingDiff = true;
    clearError();
    m_controller->requestFullDiff();
}

void GitTabWidget::onAiCancelRequested()
{
    if (auto *app = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance())) {
        if (auto *gen = app->getCommitMessageGenerator()) {
            gen->cancelIfTarget(m_composer);
        }
    }
    m_aiAwaitingDiff = false;
}

void GitTabWidget::onFullDiffReady(const QByteArray &diff)
{
    if (!m_aiAwaitingDiff) return;
    m_aiAwaitingDiff = false;

    auto *app = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance());
    if (!app) return;
    auto *gen = app->getCommitMessageGenerator();
    if (!gen) return;

    const QString submoduleRoot = m_controller ? m_controller->currentRepo() : QString();
    gen->trigger(m_workspaceRoot, submoduleRoot, m_composer, m_pendingAiSubjectHint, diff);
}

void GitTabWidget::onFullDiffFailed(const QString &message)
{
    if (!m_aiAwaitingDiff) return;
    m_aiAwaitingDiff = false;
    showError(tr("AI: %1").arg(message));
}

void GitTabWidget::onGeneratorStateChanged(int state)
{
    using State = ai::CommitMessageGenerator::State;
    auto *app = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance());
    if (!app) return;
    auto *gen = app->getCommitMessageGenerator();
    if (!gen) return;

    // Only react when this composer is the target.
    const bool isTarget = (gen->state() == State::Idle)
                          ? false
                          : (gen->currentRepoKey() == m_workspaceRoot);

    QToolButton *btn = m_composer ? m_composer->aiButton() : nullptr;
    if (!btn) return;

    const auto s = static_cast<State>(state);
    const bool busy = (s == State::Authenticating || s == State::Streaming
                       || s == State::Cancelling);

    if (isTarget && busy) {
        btn->setText(QString());
        btn->setIcon(makeStopIcon(btn->palette().color(QPalette::ButtonText)));
        btn->setToolTip(tr("Cancel generation (Esc)"));
        m_composer->setGenerationActive(true);
        m_composer->setSubmitEnabled(false);

        // Drive the animated status indicator. Phase resets so the user sees
        // motion immediately on each state transition.
        m_aiDotPhase = 0;
        switch (s) {
        case State::Authenticating: m_aiBusyBase = tr("AI: Connecting");  break;
        case State::Streaming:      m_aiBusyBase = tr("AI: Generating");  break;
        case State::Cancelling:     m_aiBusyBase = tr("AI: Cancelling");  break;
        default: break;
        }
        m_statusLabel->setText(m_aiBusyBase);
        if (!m_aiBusyTimer.isActive()) m_aiBusyTimer.start();
    } else {
        btn->setIcon(QIcon());
        btn->setText(QString::fromUtf8("\xE2\x9C\xA8"));   // U+2728 ✨ sparkles
        btn->setToolTip(tr("Generate commit message with AI"));
        m_composer->setGenerationActive(false);
        updateActionsEnabled();   // re-derives commit button enabled state
        if (m_aiBusyTimer.isActive()) m_aiBusyTimer.stop();
        if (isTarget || s == State::Idle) {
            // Clear the AI line so the status label doesn't keep a stale dot
            // trail after completion / cancel.
            m_aiBusyBase.clear();
            m_statusLabel->clear();
        }
    }
}

void GitTabWidget::onGeneratorError(const QString &message)
{
    // Only show if this composer is the one that triggered the request — but
    // because the generator is a single global at most one error is active at
    // a time, so display unconditionally.
    showError(tr("AI: %1").arg(message));
}

