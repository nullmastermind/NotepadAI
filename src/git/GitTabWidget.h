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

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QFrame;
class QLabel;
class QMenu;
class QStackedWidget;
class QToolButton;

class BranchPickerPopup;
class ChangesPanel;
class GitError;
class GitHistoryView;
class GitTabSegmentedBar;

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
    void diffRequested(const GitStatusEntry &entry);
    void openSubmoduleRequested(const QString &absPath);
    // History tab: user clicked a commit row.
    void openCommitDetailRequested(const QByteArray &sha);

private slots:
    void onRepoSelected(int index);
    void onRefreshClicked();
    void onBranchButtonClicked();
    void onMenuButtonClicked();
    void onCommitRequested(const QString &message, bool amend,
                           bool signoff, bool trackedOnly);
    void onChangesFileActivated(const QString &relPath);
    void onChangesOpenSubmoduleRequested(const QString &relPath);

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

    void onTabChanged(int index);

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
    void restoreSettingsForWorkspace();

    QString m_workspaceRoot;
    bool m_initialized = false;
    bool m_suppressRepoCombo = false;
    bool m_committing = false;

    GitController *m_controller = nullptr;
    QPointer<BranchPickerPopup> m_branchPicker;

    // Header row.
    QComboBox *m_repoCombo = nullptr;
    QToolButton *m_branchBtn = nullptr;
    QToolButton *m_refreshBtn = nullptr;
    QToolButton *m_menuBtn = nullptr;

    // Segmented bar + stacked content.
    GitTabSegmentedBar *m_segmentedBar = nullptr;
    QStackedWidget     *m_stack = nullptr;
    ChangesPanel       *m_changesPanel = nullptr;
    GitHistoryView     *m_historyView = nullptr;

    // Persistent chrome.
    QLabel *m_statusLabel = nullptr;
    QFrame *m_errorBanner = nullptr;
    QLabel *m_errorLabel = nullptr;
    QToolButton *m_errorCloseBtn = nullptr;
    QLabel *m_emptyHint = nullptr;

    // AI generation state.
    QString m_pendingAiSubjectHint;
    bool m_aiAwaitingDiff = false;
    QTimer m_aiBusyTimer;
    QString m_aiBusyBase;
    int m_aiDotPhase = 0;

    // Tab indices in segmented bar / stack.
    static constexpr int kTabChanges = 0;
    static constexpr int kTabHistory = 1;
};

#endif // GIT_TAB_WIDGET_H
