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

#ifndef CHANGES_PANEL_H
#define CHANGES_PANEL_H

#include "GitStatusEntry.h"

#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class QModelIndex;
class QPushButton;
class QTreeView;

class CommitComposer;
class GitStatusItemDelegate;
class GitStatusModel;

// Reusable widget bundling the stage-buttons row + status tree view +
// commit composer. Extracted from GitTabWidget so the Changes / History
// switcher in GitTabWidget can put this in a QStackedWidget page without
// re-implementing the existing wiring.
//
// Does NOT own any controller — it is a pure view + composer that emits
// user-action signals. GitTabWidget translates those to GitController
// calls so all backend orchestration stays in one place.
class ChangesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ChangesPanel(QWidget *parent = nullptr);

    // Bind the status model (data source for the tree view). Pass nullptr to
    // detach (e.g. when controller is destroyed).
    void setStatusModel(GitStatusModel *model);

    // Convenience accessors needed by GitTabWidget for cross-cutting flows
    // (AI generation animates a button on the composer; selection model is
    // needed to map clicks into relative paths).
    CommitComposer *composer() const { return m_composer; }
    QTreeView      *tree() const     { return m_tree; }

    // Refresh enabled state of stage/unstage/commit buttons based on the
    // current repo / model state. Caller passes the derived flags from
    // GitController.
    void updateActionsEnabled(bool hasRepo, bool hasConflicts,
                              bool anyStaged, bool anyEntries);

    // Expand all top-level sections — called after the status model is
    // refreshed so user immediately sees per-file rows.
    void expandTree();

signals:
    // Stage / unstage flow. `selectionRelPaths` is filtered to the side the
    // button is for (unstaged for stage, staged for unstage).
    void stageRequested(const QStringList &selectionRelPaths);
    void unstageRequested(const QStringList &selectionRelPaths);
    void stageAllRequested();
    void unstageAllRequested();

    // Commit. Fields mirror the composer's state at the time the user
    // clicked Commit.
    void commitRequested(const QString &message, bool amend,
                          bool signoff, bool trackedOnly);

    // Composer events for AI flow + draft persistence in GitTabWidget.
    void composerMessageChanged();
    void composerAmendToggled(bool checked);
    void composerTrackedOnlyToggled(bool checked);
    void aiTriggerRequested();
    void aiCancelRequested();

    // Tree interactions.
    void fileActivated(const QString &absPath);
    void diffRequested(const GitStatusEntry &entry);
    void openSubmoduleRequested(const QString &absPath);

private slots:
    void onStageClicked();
    void onUnstageClicked();
    void onStageAllClicked();
    void onUnstageAllClicked();
    void onCommitButtonClicked();
    void onTreeClicked(const QModelIndex &index);
    void onTreeDoubleClicked(const QModelIndex &index);

private:
    void buildUi();

    QPointer<GitStatusModel> m_statusModel;
    QPushButton *m_stageBtn      = nullptr;
    QPushButton *m_unstageBtn    = nullptr;
    QPushButton *m_stageAllBtn   = nullptr;
    QPushButton *m_unstageAllBtn = nullptr;
    QTreeView   *m_tree     = nullptr;
    CommitComposer *m_composer = nullptr;
    GitStatusItemDelegate *m_delegate = nullptr;
};

#endif // CHANGES_PANEL_H
