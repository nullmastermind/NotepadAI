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

#ifndef GIT_TAB_WIDGET_H
#define GIT_TAB_WIDGET_H

#include "GitController.h"
#include "GitStatusEntry.h"

#include <QPointer>
#include <QString>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QFrame;
class QLabel;
class QMenu;
class QModelIndex;
class QPushButton;
class QToolButton;
class QTreeView;

class BranchPickerPopup;
class CommitComposer;
class GitError;

class GitTabWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GitTabWidget(const QString &workspaceRoot, QWidget *parent = nullptr);
    ~GitTabWidget() override;

    void setWorkspaceRoot(const QString &root);
    void initializeIfNeeded();

    GitController *controller() const { return m_controller; }

signals:
    void fileActivated(const QString &absPath);
    // Single click on a status entry — host opens a diff preview tab.
    void diffRequested(const GitStatusEntry &entry);
    // Click on a submodule entry — host opens the submodule as a new workspace.
    void openSubmoduleRequested(const QString &absPath);

private slots:
    void onRepoSelected(int index);
    void onRefreshClicked();
    void onBranchButtonClicked();
    void onMenuButtonClicked();
    void onStageClicked();
    void onUnstageClicked();
    void onStageAllClicked();
    void onUnstageAllClicked();
    void onCommitRequested();
    void onTreeClicked(const QModelIndex &index);
    void onTreeDoubleClicked(const QModelIndex &index);

    void onControllerState(GitController::State s);
    void onStatusUpdated();
    void onBranchesUpdated();
    void onReposUpdated();
    void onOpSucceeded(const QString &name);
    void onCommitSucceeded();
    void onError(const GitError &err);
    void onGitMissing();
    void onDirtyTreePrompt(const QString &target);
    void onRemoteOpProgress(const QString &line);

    // AI commit-message generation.
    void onAiTriggerRequested();
    void onAiCancelRequested();
    void onFullDiffReady(const QByteArray &diff);
    void onFullDiffFailed(const QString &message);
    void onGeneratorStateChanged(int state);
    void onGeneratorError(const QString &message);

private:
    void buildUi();
    void rebuildController();
    void teardownController();
    void updateActionsEnabled();
    void updateBranchButtonText();
    void persistCommitDraft();
    void restoreCommitDraft();
    void showError(const QString &text, const QString &hint = {});
    void clearError();
    void appendStatus(const QString &msg);
    QString settingsKey(const QString &subkey) const;
    void handleBranchSelected(const QString &name);
    void handleCreateBranch(const QString &name, const QString &base);

    QString m_workspaceRoot;
    bool m_initialized = false;
    bool m_suppressRepoCombo = false;
    bool m_committing = false;

    GitController *m_controller = nullptr;
    QPointer<BranchPickerPopup> m_branchPicker;

    QComboBox *m_repoCombo = nullptr;
    QToolButton *m_branchBtn = nullptr;
    QToolButton *m_refreshBtn = nullptr;
    QToolButton *m_menuBtn = nullptr;
    QPushButton *m_stageBtn = nullptr;
    QPushButton *m_unstageBtn = nullptr;
    QPushButton *m_stageAllBtn = nullptr;
    QPushButton *m_unstageAllBtn = nullptr;
    QTreeView *m_tree = nullptr;
    CommitComposer *m_composer = nullptr;
    QLabel *m_statusLabel = nullptr;
    QFrame *m_errorBanner = nullptr;
    QLabel *m_errorLabel = nullptr;
    QToolButton *m_errorCloseBtn = nullptr;
    QLabel *m_emptyHint = nullptr;

    // AI generation: cache subject hint so the asynchronous fullDiffReady
    // handler can pair the diff with the user-typed first line at trigger time.
    QString m_pendingAiSubjectHint;
    bool m_aiAwaitingDiff = false;

    // Animated busy indicator for the status label during AI generation.
    QTimer m_aiBusyTimer;
    QString m_aiBusyBase;
    int m_aiDotPhase = 0;
};

#endif // GIT_TAB_WIDGET_H
