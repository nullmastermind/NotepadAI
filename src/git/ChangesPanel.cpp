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

#include "ChangesPanel.h"

#include "CommitComposer.h"
#include "GitStatusItemDelegate.h"
#include "GitStatusModel.h"
#include "NotepadNextApplication.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

ChangesPanel::ChangesPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
}

void ChangesPanel::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Stage / Unstage row.
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

    // Tree view.
    m_tree = new QTreeView(this);
    m_tree->setHeaderHidden(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setUniformRowHeights(true);
    m_tree->setFrameShape(QFrame::NoFrame);

    // Custom delegate paints the per-row "+added -deleted" numstat suffix
    // in green/red. Without it the tree falls back to the model's DisplayRole
    // ("M  src/x.cpp") with no numstat at all — see GitStatusItemDelegate::paint.
    m_delegate = new GitStatusItemDelegate(m_tree);
    m_tree->setItemDelegate(m_delegate);

    // Initial dark-mode sync + follow live theme switches. Mirrors the
    // pattern GitTabWidget already uses to talk to NotepadNextApplication.
    if (auto *app = qobject_cast<NotepadNextApplication *>(QCoreApplication::instance())) {
        m_delegate->setDarkPalette(app->isEffectiveThemeDark());
        connect(app, &NotepadNextApplication::effectiveThemeChanged,
                this, [this, app]() {
                    if (!m_delegate) return;
                    m_delegate->setDarkPalette(app->isEffectiveThemeDark());
                    if (m_tree && m_tree->viewport()) m_tree->viewport()->update();
                });
    }

    root->addWidget(m_tree, 1);

    // Commit composer.
    m_composer = new CommitComposer(this);
    root->addWidget(m_composer);

    // Connections.
    connect(m_stageBtn, &QPushButton::clicked,
            this, &ChangesPanel::onStageClicked);
    connect(m_unstageBtn, &QPushButton::clicked,
            this, &ChangesPanel::onUnstageClicked);
    connect(m_stageAllBtn, &QPushButton::clicked,
            this, &ChangesPanel::onStageAllClicked);
    connect(m_unstageAllBtn, &QPushButton::clicked,
            this, &ChangesPanel::onUnstageAllClicked);
    connect(m_composer, &CommitComposer::submitRequested,
            this, &ChangesPanel::onCommitButtonClicked);
    connect(m_composer, &CommitComposer::messageChanged,
            this, &ChangesPanel::composerMessageChanged);
    connect(m_composer, &CommitComposer::amendToggled,
            this, &ChangesPanel::composerAmendToggled);
    connect(m_composer, &CommitComposer::trackedOnlyToggled,
            this, &ChangesPanel::composerTrackedOnlyToggled);
    connect(m_composer, &CommitComposer::aiTriggerRequested,
            this, &ChangesPanel::aiTriggerRequested);
    connect(m_composer, &CommitComposer::aiCancelRequested,
            this, &ChangesPanel::aiCancelRequested);
    connect(m_tree, &QTreeView::clicked,
            this, &ChangesPanel::onTreeClicked);
    connect(m_tree, &QTreeView::doubleClicked,
            this, &ChangesPanel::onTreeDoubleClicked);
}

void ChangesPanel::setStatusModel(GitStatusModel *model)
{
    m_statusModel = model;
    m_tree->setModel(model);
}

void ChangesPanel::updateActionsEnabled(bool hasRepo, bool hasConflicts,
                                        bool anyStaged, bool anyEntries)
{
    m_stageBtn->setEnabled(hasRepo);
    m_unstageBtn->setEnabled(hasRepo && anyStaged);
    m_stageAllBtn->setEnabled(hasRepo && anyEntries);
    m_unstageAllBtn->setEnabled(hasRepo && anyStaged);

    const QString msg = m_composer ? m_composer->message().trimmed() : QString();
    const bool amend = m_composer && m_composer->amendChecked();
    const bool somethingToCommit = anyStaged || amend || anyEntries;
    const bool canCommit = hasRepo && !hasConflicts
                           && (!msg.isEmpty() || amend)
                           && somethingToCommit;
    if (m_composer) m_composer->setSubmitEnabled(canCommit);
}

void ChangesPanel::expandTree()
{
    m_tree->expandAll();
}

void ChangesPanel::onStageClicked()
{
    if (!m_statusModel) return;
    const QStringList paths = m_statusModel->unstagedSelectionPaths(
        m_tree->selectionModel()->selectedIndexes());
    if (paths.isEmpty()) return;
    emit stageRequested(paths);
}

void ChangesPanel::onUnstageClicked()
{
    if (!m_statusModel) return;
    const QStringList paths = m_statusModel->stagedSelectionPaths(
        m_tree->selectionModel()->selectedIndexes());
    if (paths.isEmpty()) return;
    emit unstageRequested(paths);
}

void ChangesPanel::onStageAllClicked()
{
    emit stageAllRequested();
}

void ChangesPanel::onUnstageAllClicked()
{
    emit unstageAllRequested();
}

void ChangesPanel::onCommitButtonClicked()
{
    if (!m_composer) return;
    emit commitRequested(m_composer->message(),
                         m_composer->amendChecked(),
                         m_composer->signoffChecked(),
                         m_composer->trackedOnly());
}

void ChangesPanel::onTreeClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    if (index.data(GitStatusModel::IsSectionRole).toBool()) return;
    const QString rel = index.data(GitStatusModel::RelPathRole).toString();
    if (rel.isEmpty()) return;

    // Host (GitTabWidget) knows the repo root; we pass the relative path
    // via the entry's relPath. For absolute paths we still need the repo;
    // delegate that resolution to the host by re-using the existing
    // GitStatusEntry / GitTabWidget contract.
    GitStatusEntry entry;
    entry.relPath        = rel;
    entry.origRelPath    = index.data(GitStatusModel::OrigPathRole).toString();
    entry.change         = static_cast<GitStatusEntry::Change>(
                              index.data(GitStatusModel::ChangeRole).toInt());
    entry.stagedSide     = index.data(GitStatusModel::StagedSideRole).toBool();
    entry.section        = static_cast<GitStatusEntry::Section>(
                              index.data(GitStatusModel::SectionRole).toInt());
    entry.xy             = index.data(GitStatusModel::XyRole).toString();
    entry.hasUnstableEncoding =
        index.data(GitStatusModel::HasUnstableEncodingRole).toBool();
    entry.isBinary       = index.data(GitStatusModel::IsBinaryRole).toBool();
    entry.addedLines     = index.data(GitStatusModel::AddedLinesRole).toInt();
    entry.deletedLines   = index.data(GitStatusModel::DeletedLinesRole).toInt();
    entry.oursSha        = index.data(GitStatusModel::OursShaRole).toString();
    entry.theirsSha      = index.data(GitStatusModel::TheirsShaRole).toString();
    emit diffRequested(entry);
}

void ChangesPanel::onTreeDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    if (index.data(GitStatusModel::IsSectionRole).toBool()) return;
    const QString rel = index.data(GitStatusModel::RelPathRole).toString();
    if (rel.isEmpty()) return;
    // Caller (GitTabWidget) prepends repo root before forwarding to the host
    // MainWindow — we only know the relative path here. We emit the relative
    // path in fileActivated and let the host resolve; but the existing
    // contract is absolute path. Instead, route both clicks through the host
    // via diffRequested (single click) and a second signal for "open file":
    // emit fileActivated with relPath only — host adapter converts.
    emit fileActivated(rel);
}
