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

#ifndef GIT_CONTROLLER_H
#define GIT_CONTROLLER_H

#include "GitError.h"
#include "GitRepoInfo.h"
#include "GitStatusEntry.h"

#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cstdint>

class GitProcessRunner;
class IGitProcessRunner;
class GitRepoModel;
class GitStatusModel;
class GitWatcher;
class QTimer;

class GitController : public QObject
{
    Q_OBJECT
public:
    enum class State : std::uint8_t { Idle, Discovering, Refreshing, Running, Error };
    Q_ENUM(State)

    enum class BranchSwitchPolicy : std::uint8_t { Cancel, SwitchAnyway, StashAndSwitch };

    explicit GitController(const QString &workspaceRoot, QObject *parent = nullptr);
    ~GitController() override;

    // For tests: inject a mock runner. Takes ownership.
    void setRunnerForTesting(IGitProcessRunner *runner);

    State state() const { return m_state; }
    QString workspaceRoot() const { return m_workspaceRoot; }
    GitRepoModel *repoModel() const { return m_repos; }
    GitStatusModel *statusModel() const { return m_status; }

    QString currentRepo() const { return m_currentRepo; }

    // Ahead/behind counts vs the current branch's upstream, populated from
    // `git status --porcelain=v2 --branch`. Both stay at 0 when no upstream
    // is configured or HEAD is detached; check hasUpstream() to distinguish.
    int  ahead() const       { return m_ahead; }
    int  behind() const      { return m_behind; }
    bool hasUpstream() const { return m_hasUpstream; }
    QString currentBranch() const { return m_currentBranch; }
    QString detachedShortSha() const { return m_detachedSha; }
    QStringList branchesLocal() const { return m_localBranches; }
    QStringList branchesRemote() const { return m_remoteBranches; }
    QStringList remotes() const { return m_remoteList; }
    bool isEmptyRepo() const { return m_empty; }
    bool hasRemote() const { return !m_remoteList.isEmpty(); }
    bool hasConflicts() const;
    QString runningOperationName() const { return m_busy ? m_current.humanName : QString(); }

public slots:
    void initialize();
    void selectRepo(const QString &repoToplevel);
    void refresh();
    void stagePaths(const QStringList &relPaths);
    void unstagePaths(const QStringList &relPaths);
    void stageAll();
    void unstageAll();
    void commit(const QString &message, bool amend, bool signoff, bool trackedOnly);
    void switchBranch(const QString &name, BranchSwitchPolicy policy);
    void createBranch(const QString &name, const QString &base, bool checkout, bool setUpstream = true);
    void setUpstream(const QString &remoteBranch);
    void configBranchTracking(const QString &branchName, const QString &remote);
    void fetch(const QString &remote = {});
    void pull(bool rebase);
    void push(const QString &remote = {}, bool setUpstream = false);
    void forcePush(const QString &remote = {});
    void renameBranch(const QString &oldName, const QString &newName, bool updateRemote);
    void deleteBranch(const QString &branchName, bool force);
    void revertPaths(const QStringList &relPaths);
    void cancelCurrent();

    // Request a unified diff for a single path. Side: false = working-tree
    // (unstaged), true = index (staged). Emits diffReady on completion.
    void requestDiff(const QString &relPath, bool stagedSide);

    // Request the full repository diff. If the repo has any staged changes,
    // the staged diff (index vs HEAD) is returned; otherwise the worktree
    // diff (worktree vs HEAD). Emits fullDiffReady on success or
    // fullDiffFailed on error / empty repo.
    void requestFullDiff();

    // Async fetch of `HEAD:<relPath>` as a raw blob, used by the buffer-diff
    // engine to feed xdl_diff. Returns the bytes via catFileBlobReady or an
    // error via catFileBlobFailed; both signals carry `relPath` so multiple
    // in-flight requests can be demultiplexed by the caller.
    void requestCatFileBlob(const QString &relPath);

signals:
    void stateChanged(GitController::State s);
    void statusUpdated();
    void numstatUpdated();
    void branchesUpdated();
    // Fired whenever ahead/behind vs upstream (or upstream-presence) changes.
    // Emitted after statusUpdated() so subscribers that depend on both can
    // assume the per-file model has already been refreshed.
    void aheadBehindChanged(int ahead, int behind, bool hasUpstream);
    void reposUpdated();
    void remoteOpProgress(const QString &line);
    void opSucceeded(const QString &humanName);
    // Fired in addition to opSucceeded after a successful Commit. Carries
    // OpKind affinity stably (no humanName string matching, which broke under
    // translations).
    void commitSucceeded();
    void errorOccurred(const GitError &err);
    void gitMissing();
    void dirtyTreePromptRequested(const QString &targetBranch);
    void diffReady(const QString &relPath, bool stagedSide, const QByteArray &diff);
    void diffFailed(const QString &relPath, bool stagedSide, const QString &message);
    void fullDiffReady(const QByteArray &diff);
    void fullDiffFailed(const QString &message);
    // HEAD-blob fetch result for the buffer-diff engine. `relPath` is the
    // request key; `blob` is the raw bytes of `HEAD:<relPath>`. Empty blob
    // is valid (empty file); the failure path emits catFileBlobFailed.
    void catFileBlobReady(const QString &relPath, const QByteArray &blob);
    void catFileBlobFailed(const QString &relPath, const QString &message);

private:
    enum class OpKind : std::uint8_t {
        Discover, Toplevel, SubmodulesList,
        HeadSym, HeadSha, Refs, Remotes, Status,
        IgnoredDirs,
        NumstatStaged, NumstatUnstaged,
        SubmoduleNumstat,
        DiffPath,
        DiffAllCached, DiffAllWorktree,
        CatFileBlob,
        Stage, Unstage, StageAll, UnstageAll,
        Commit,
        SwitchBranch, CreateBranch, RenameBranch, DeleteBranch, SetUpstream, ConfigTracking, Stash,
        Fetch, Pull, Push, ForcePush,
        Revert
    };
    struct Op {
        OpKind kind;
        QStringList argv;
        QByteArray stdinPayload;
        int timeoutMs = 30000;
        bool readErrAsProgress = false;
        QString humanName;
        QVariantMap meta;
    };

    QString m_workspaceRoot;
    State m_state = State::Idle;
    IGitProcessRunner *m_runner = nullptr;
    GitProcessRunner *m_realRunner = nullptr;     // owned when not test-injected
    GitRepoModel *m_repos;
    GitStatusModel *m_status;
    GitWatcher *m_watcher;
    QTimer *m_refreshDebounce;

    QString m_currentRepo;
    QString m_currentBranch;
    // Deferred selectRepo target — set when selectRepo() is called while a
    // queue is in flight; applied by runNext() after the queue drains. Avoids
    // silently dropping repo-switch clicks (combo box / submodule double-click)
    // that arrive mid-refresh, which previously left the changes panel showing
    // the prior repo's data despite the combo updating.
    QString m_pendingRepoSwitch;

    // Mirrors of GitStatusParser::Header from the most recent status fetch.
    int  m_ahead       = 0;
    int  m_behind      = 0;
    bool m_hasUpstream = false;
    QString m_detachedSha;
    QStringList m_localBranches;
    QStringList m_remoteBranches;
    QStringList m_remoteList;
    bool m_empty = false;

    QQueue<Op> m_queue;
    bool m_busy = false;
    Op m_current;
    bool m_refreshScheduled = false;

    void setState(State s);
    void enqueue(const Op &op);
    void runNext();
    void onRunFinished(int exit, const QByteArray &out, const QByteArray &err);

    void enqueueDiscovery();
    void enqueueFullRefresh();
    // Internal — apply a repo switch (mutate state + enqueue refresh).
    // selectRepo() defers to this when the controller is idle; runNext()
    // applies the deferred target once the queue drains.
    void applySelectRepo(const QString &cleanToplevel);

    void handleToplevelDone(int exit, const QByteArray &out, const QByteArray &err);
    void handleSubmodulesDone(int exit, const QByteArray &out);
    void handleHeadSymDone(const QByteArray &out);
    void handleHeadShaDone(int exit, const QByteArray &out);
    void handleRefsDone(const QByteArray &out);
    void handleRemotesDone(const QByteArray &out);
    void handleStatusDone(const QByteArray &out);
    void handleIgnoredDirsDone(const QByteArray &out);
    void handleNumstatDone(const QByteArray &out, bool stagedSide);
    void enqueueNumstatRefresh();
    // Submodule aggregate stats — one `git -C <subAbs> diff --numstat -z
    // --no-renames HEAD` spawn per unique submodule entry that has modified
    // content (sub field 'S.M.'). Pointer-only and untracked-only submodules
    // are skipped so we never spawn a process whose result would be empty.
    void enqueueSubmoduleStatsRefresh();
    void handleSubmoduleNumstatDone(const QByteArray &out, const QString &relPath);

    QByteArray m_lastForEachRefOut;
    QByteArray m_lastHeadSymOut;
    QByteArray m_lastHeadShaOut;
    QByteArray m_lastRemotesOut;
    bool m_lastHeadShaExitNonZero = false;

    void scheduleDebouncedRefresh();
};

#endif // GIT_CONTROLLER_H
