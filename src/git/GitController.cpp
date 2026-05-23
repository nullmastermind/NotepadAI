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

#include "GitController.h"

#include "BranchRefParser.h"
#include "GitErrorClassifier.h"
#include "GitNumstatParser.h"
#include "GitProcessRunner.h"
#include "GitRepoDiscovery.h"
#include "GitRepoModel.h"
#include "GitStatusModel.h"
#include "GitStatusParser.h"
#include "GitWatcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <limits>

namespace {
constexpr int kTimeoutShort = 5000;
constexpr int kTimeoutNormal = 30000;
constexpr int kTimeoutStatus = 60000;
constexpr int kTimeoutRemote = 5 * 60 * 1000;

QString tr_(const char *s) { return QCoreApplication::translate("GitController", s); }
} // namespace

GitController::GitController(const QString &workspaceRoot, QObject *parent)
    : QObject(parent), m_workspaceRoot(QDir::cleanPath(workspaceRoot))
{
    m_repos = new GitRepoModel(this);
    m_status = new GitStatusModel(this);
    m_watcher = new GitWatcher(this);
    m_realRunner = new GitProcessRunner(this);
    m_runner = m_realRunner;

    m_refreshDebounce = new QTimer(this);
    m_refreshDebounce->setSingleShot(true);
    m_refreshDebounce->setInterval(200);
    connect(m_refreshDebounce, &QTimer::timeout, this, [this]() {
        m_refreshScheduled = false;
        refresh();
    });

    connect(m_realRunner, &GitProcessRunner::progressLine,
            this, &GitController::remoteOpProgress);
    connect(m_watcher, &GitWatcher::headChanged,
            this, &GitController::scheduleDebouncedRefresh);
    connect(m_watcher, &GitWatcher::indexChanged,
            this, &GitController::scheduleDebouncedRefresh);
    connect(m_watcher, &GitWatcher::refsChanged,
            this, &GitController::scheduleDebouncedRefresh);
    connect(m_watcher, &GitWatcher::workingTreeChanged,
            this, &GitController::scheduleDebouncedRefresh);
}

GitController::~GitController() = default;

void GitController::setRunnerForTesting(IGitProcessRunner *runner)
{
    m_runner = runner;
}

void GitController::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

bool GitController::hasConflicts() const
{
    return m_status && m_status->hasConflicts();
}

void GitController::scheduleDebouncedRefresh()
{
    if (m_refreshScheduled) return;
    m_refreshScheduled = true;
    m_refreshDebounce->start();
}

void GitController::initialize()
{
    if (!GitProcessRunner::gitAvailable()) {
        GitError e;
        e.kind = GitError::NotInstalled;
        e.humanMessage = tr_("Git is not installed or not on PATH.");
        e.hint = tr_("Install git and restart the application.");
        emit gitMissing();
        emit errorOccurred(e);
        return;
    }
    enqueueDiscovery();
}

void GitController::enqueueDiscovery()
{
    setState(State::Discovering);
    Op topl;
    topl.kind = OpKind::Toplevel;
    topl.argv = { QStringLiteral("-C"), m_workspaceRoot, QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel") };
    topl.timeoutMs = kTimeoutShort;
    topl.humanName = tr_("Detecting repository");
    enqueue(topl);
}

void GitController::enqueueFullRefresh()
{
    if (m_currentRepo.isEmpty()) return;
    setState(State::Refreshing);

    Op hsym;
    hsym.kind = OpKind::HeadSym;
    hsym.argv = { QStringLiteral("-C"), m_currentRepo,
                  QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("-q"), QStringLiteral("HEAD") };
    hsym.timeoutMs = kTimeoutShort;
    enqueue(hsym);

    Op hsha;
    hsha.kind = OpKind::HeadSha;
    hsha.argv = { QStringLiteral("-C"), m_currentRepo,
                  QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("HEAD") };
    hsha.timeoutMs = kTimeoutShort;
    enqueue(hsha);

    Op remotes;
    remotes.kind = OpKind::Remotes;
    remotes.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("remote") };
    remotes.timeoutMs = kTimeoutShort;
    enqueue(remotes);

    Op refs;
    refs.kind = OpKind::Refs;
    refs.argv = { QStringLiteral("-C"), m_currentRepo,
                  QStringLiteral("for-each-ref"),
                  QStringLiteral("--format=%(HEAD)%00%(refname:short)%00%(objecttype)%00%(upstream:short)%00"),
                  QStringLiteral("refs/heads"), QStringLiteral("refs/remotes") };
    refs.timeoutMs = kTimeoutShort * 2;
    enqueue(refs);

    Op st;
    st.kind = OpKind::Status;
    st.argv = { QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                QStringLiteral("-C"), m_currentRepo,
                QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                QStringLiteral("--branch"),
                QStringLiteral("-z"), QStringLiteral("--untracked-files=all"), QStringLiteral("--renames") };
    st.timeoutMs = kTimeoutStatus;
    enqueue(st);
}

void GitController::selectRepo(const QString &repoToplevel)
{
    const QString clean = QDir::cleanPath(repoToplevel);
    if (clean == m_currentRepo) {
        // Already on this repo. Cancel any deferred switch to a stale target.
        m_pendingRepoSwitch.clear();
        return;
    }
    if (m_busy) {
        // Queue draining will pick this up. Latest wins — clicking through
        // several submodules quickly only applies the final selection.
        m_pendingRepoSwitch = clean;
        return;
    }
    applySelectRepo(clean);
}

void GitController::applySelectRepo(const QString &cleanToplevel)
{
    m_currentRepo = cleanToplevel;
    m_watcher->setRepo(m_currentRepo);
    // Clear stale ahead/behind before the next status fetch fills it. Without
    // this the UI would briefly show counts from the prior repo.
    if (m_ahead != 0 || m_behind != 0 || m_hasUpstream) {
        m_ahead = 0;
        m_behind = 0;
        m_hasUpstream = false;
        emit aheadBehindChanged(0, 0, false);
    }
    enqueueFullRefresh();
}

void GitController::refresh()
{
    if (m_currentRepo.isEmpty()) return;
    if (m_busy) {
        m_refreshScheduled = true;
        return;
    }
    enqueueFullRefresh();
}

void GitController::stagePaths(const QStringList &relPaths)
{
    if (m_currentRepo.isEmpty() || relPaths.isEmpty()) return;

    // Chunk to keep argv under Windows 32k limit. ~120 chars/path average → 50 paths/chunk.
    constexpr int kChunk = 50;
    for (int i = 0; i < relPaths.size(); i += kChunk) {
        const QStringList chunk = relPaths.mid(i, kChunk);
        Op op;
        op.kind = OpKind::Stage;
        op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("add"), QStringLiteral("--") };
        op.argv.append(chunk);
        op.timeoutMs = kTimeoutNormal;
        op.humanName = tr_("Staging");
        enqueue(op);
    }
}

void GitController::unstagePaths(const QStringList &relPaths)
{
    if (m_currentRepo.isEmpty() || relPaths.isEmpty()) return;

    constexpr int kChunk = 50;
    for (int i = 0; i < relPaths.size(); i += kChunk) {
        const QStringList chunk = relPaths.mid(i, kChunk);
        Op op;
        op.kind = OpKind::Unstage;
        if (m_empty) {
            // No HEAD yet; use `rm --cached` to remove from index.
            op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("rm"),
                        QStringLiteral("--cached"), QStringLiteral("--"), };
        } else {
            op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("reset"),
                        QStringLiteral("-q"), QStringLiteral("HEAD"), QStringLiteral("--") };
        }
        op.argv.append(chunk);
        op.timeoutMs = kTimeoutNormal;
        op.humanName = tr_("Unstaging");
        enqueue(op);
    }
}

void GitController::stageAll()
{
    if (m_currentRepo.isEmpty()) return;
    Op op;
    op.kind = OpKind::StageAll;
    op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("add"), QStringLiteral("-A") };
    op.timeoutMs = kTimeoutStatus;
    op.humanName = tr_("Staging all");
    enqueue(op);
}

void GitController::unstageAll()
{
    if (m_currentRepo.isEmpty()) return;
    Op op;
    op.kind = OpKind::UnstageAll;
    if (m_empty) {
        // No HEAD: clear the index by listing then removing.
        // Best-effort one-shot via `git rm -r --cached -- .`
        op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("rm"),
                    QStringLiteral("-r"), QStringLiteral("--cached"), QStringLiteral("--"),
                    QStringLiteral(".") };
    } else {
        op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("reset"),
                    QStringLiteral("-q"), QStringLiteral("HEAD") };
    }
    op.timeoutMs = kTimeoutNormal;
    op.humanName = tr_("Unstaging all");
    enqueue(op);
}

void GitController::commit(const QString &message, bool amend, bool signoff, bool trackedOnly)
{
    if (m_currentRepo.isEmpty()) return;
    Op op;
    op.kind = OpKind::Commit;
    op.argv = { QStringLiteral("-c"), QStringLiteral("i18n.commitEncoding=UTF-8"),
                QStringLiteral("-C"), m_currentRepo,
                QStringLiteral("commit"), QStringLiteral("-F"), QStringLiteral("-") };
    if (amend)       op.argv.append(QStringLiteral("--amend"));
    if (signoff)     op.argv.append(QStringLiteral("--signoff"));
    if (trackedOnly) op.argv.append(QStringLiteral("-a"));

    QString normalised = message;
    normalised.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    op.stdinPayload = normalised.toUtf8();
    op.timeoutMs = kTimeoutStatus;
    op.humanName = amend ? tr_("Amending commit") : tr_("Committing");
    enqueue(op);
}

void GitController::switchBranch(const QString &name, BranchSwitchPolicy policy)
{
    if (m_currentRepo.isEmpty() || name.isEmpty()) return;

    if (policy == BranchSwitchPolicy::Cancel) return;

    // For Stash-and-switch, push stash first.
    if (policy == BranchSwitchPolicy::StashAndSwitch) {
        Op stash;
        stash.kind = OpKind::Stash;
        const QString msg = QStringLiteral("WIP on %1 before switching to %2")
            .arg(m_currentBranch.isEmpty() ? QStringLiteral("HEAD") : m_currentBranch, name);
        stash.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("stash"),
                       QStringLiteral("push"), QStringLiteral("-u"),
                       QStringLiteral("-m"), msg };
        stash.timeoutMs = kTimeoutStatus;
        stash.humanName = tr_("Stashing changes");
        enqueue(stash);
    }

    Op op;
    op.kind = OpKind::SwitchBranch;
    op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("checkout"), name };
    op.timeoutMs = kTimeoutNormal;
    op.humanName = tr_("Switching branch");
    op.meta.insert(QStringLiteral("branch"), name);
    enqueue(op);
}

void GitController::createBranch(const QString &name, const QString &base, bool checkout)
{
    if (m_currentRepo.isEmpty() || name.isEmpty()) return;
    Op op;
    op.kind = OpKind::CreateBranch;
    op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("checkout") };
    if (!checkout) op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("branch") };
    else op.argv.append(QStringLiteral("-b"));
    op.argv.append(name);
    if (!base.isEmpty()) op.argv.append(base);
    op.timeoutMs = kTimeoutNormal;
    op.humanName = tr_("Creating branch");
    enqueue(op);
}

void GitController::fetch(const QString &remote)
{
    if (m_currentRepo.isEmpty()) return;
    Op op;
    op.kind = OpKind::Fetch;
    op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("fetch"),
                QStringLiteral("--prune"), QStringLiteral("--progress") };
    if (!remote.isEmpty()) op.argv.append(remote);
    op.timeoutMs = kTimeoutRemote;
    op.readErrAsProgress = true;
    op.humanName = tr_("Fetching");
    enqueue(op);
}

void GitController::pull(bool rebase)
{
    if (m_currentRepo.isEmpty()) return;
    Op op;
    op.kind = OpKind::Pull;
    op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("pull"),
                QStringLiteral("--progress") };
    op.argv.append(rebase ? QStringLiteral("--rebase") : QStringLiteral("--ff-only"));
    op.timeoutMs = kTimeoutRemote;
    op.readErrAsProgress = true;
    op.humanName = rebase ? tr_("Pulling (rebase)") : tr_("Pulling");
    enqueue(op);
}

void GitController::push(const QString &remote, bool setUpstream)
{
    if (m_currentRepo.isEmpty()) return;
    Op op;
    op.kind = OpKind::Push;
    op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("push"),
                QStringLiteral("--progress") };
    if (setUpstream) op.argv.append(QStringLiteral("-u"));
    if (!remote.isEmpty()) {
        op.argv.append(remote);
        if (!m_currentBranch.isEmpty())
            op.argv.append(QStringLiteral("HEAD:%1").arg(m_currentBranch));
    }
    op.timeoutMs = kTimeoutRemote;
    op.readErrAsProgress = true;
    op.humanName = tr_("Pushing");
    enqueue(op);
}

void GitController::forcePush(const QString &remote)
{
    if (m_currentRepo.isEmpty()) return;
    Op op;
    op.kind = OpKind::ForcePush;
    op.argv = { QStringLiteral("-C"), m_currentRepo, QStringLiteral("push"),
                QStringLiteral("--force-with-lease"), QStringLiteral("--progress") };
    if (!remote.isEmpty()) op.argv.append(remote);
    op.timeoutMs = kTimeoutRemote;
    op.readErrAsProgress = true;
    op.humanName = tr_("Force pushing");
    enqueue(op);
}

void GitController::cancelCurrent()
{
    if (m_runner) m_runner->cancel();
}

void GitController::enqueue(const Op &op)
{
    m_queue.enqueue(op);
    if (!m_busy) runNext();
}

void GitController::runNext()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        if (m_state == State::Running || m_state == State::Refreshing) setState(State::Idle);
        // Apply a repo switch the user requested while we were busy. Done
        // before honoring m_refreshScheduled so a stale refresh of the old
        // repo doesn't race with the switch.
        if (!m_pendingRepoSwitch.isEmpty() && m_pendingRepoSwitch != m_currentRepo) {
            const QString target = m_pendingRepoSwitch;
            m_pendingRepoSwitch.clear();
            applySelectRepo(target);
            return;
        }
        m_pendingRepoSwitch.clear();
        if (m_refreshScheduled) {
            m_refreshScheduled = false;
            refresh();
        }
        return;
    }
    m_busy = true;
    m_current = m_queue.dequeue();

    if (m_current.kind != OpKind::HeadSym
        && m_current.kind != OpKind::HeadSha
        && m_current.kind != OpKind::Refs
        && m_current.kind != OpKind::Remotes
        && m_current.kind != OpKind::Status
        && m_current.kind != OpKind::Toplevel
        && m_current.kind != OpKind::SubmodulesList)
    {
        setState(State::Running);
    }

    m_runner->run(QString(), m_current.argv, m_current.stdinPayload,
                  m_current.timeoutMs, m_current.readErrAsProgress,
                  [this](int exit, const QByteArray &out, const QByteArray &err) {
                      onRunFinished(exit, out, err);
                  });
}

void GitController::handleToplevelDone(int exit, const QByteArray &out, const QByteArray &err)
{
    if (exit != 0) {
        GitError e = GitErrorClassifier::classify(exit, err, m_current.argv);
        // Force NotARepo for an explicit "no repo" case.
        if (e.kind == GitError::Unknown) {
            e.kind = GitError::NotARepo;
            e.humanMessage = tr_("This folder is not a Git repository.");
            e.hint = tr_("Run `git init` or open a folder that is one.");
        }
        m_currentRepo.clear();
        m_repos->setRepos({});
        setState(State::Error);
        emit reposUpdated();
        emit errorOccurred(e);
        return;
    }
    const QString toplevel = QDir::cleanPath(QString::fromUtf8(out).trimmed());
    if (toplevel.isEmpty()) return;

    // Build repos: root + submodules. Enqueue submodule discovery.
    GitRepoInfos infos;
    GitRepoInfo root;
    root.toplevel = toplevel;
    root.displayName = QFileInfo(toplevel).fileName();
    if (root.displayName.isEmpty()) root.displayName = toplevel;
    root.depth = 0;
    root.isSubmodule = false;
    infos.append(root);
    m_repos->setRepos(infos);
    emit reposUpdated();

    if (m_currentRepo.isEmpty()) m_currentRepo = toplevel;
    m_watcher->setRepo(m_currentRepo);

    Op sub;
    sub.kind = OpKind::SubmodulesList;
    sub.argv = { QStringLiteral("-C"), toplevel, QStringLiteral("submodule"),
                 QStringLiteral("status"), QStringLiteral("--recursive") };
    sub.timeoutMs = kTimeoutNormal;
    sub.meta.insert(QStringLiteral("rootToplevel"), toplevel);
    enqueue(sub);

    enqueueFullRefresh();
}

void GitController::handleSubmodulesDone(int exit, const QByteArray &out)
{
    Q_UNUSED(exit);
    const QString rootToplevel = m_current.meta.value(QStringLiteral("rootToplevel")).toString();
    auto subs = GitRepoDiscovery::parseSubmoduleStatus(out, rootToplevel);
    auto all = m_repos->repos();
    for (const auto &s : subs) {
        bool dup = false;
        for (const auto &existing : all)
            if (existing.toplevel == s.toplevel) { dup = true; break; }
        if (!dup) all.append(s);
    }
    m_repos->setRepos(all);
    emit reposUpdated();
}

void GitController::handleHeadSymDone(const QByteArray &out)
{
    m_lastHeadSymOut = out;
}

void GitController::handleHeadShaDone(int exit, const QByteArray &out)
{
    m_lastHeadShaOut = out;
    m_lastHeadShaExitNonZero = (exit != 0);
}

void GitController::handleRemotesDone(const QByteArray &out)
{
    m_lastRemotesOut = out;
}

void GitController::handleRefsDone(const QByteArray &out)
{
    m_lastForEachRefOut = out;
    // We now have everything: parse atomically.
    auto refs = BranchRefParser::parse(m_lastForEachRefOut, m_lastHeadSymOut,
                                       m_lastHeadShaExitNonZero ? QByteArray() : m_lastHeadShaOut,
                                       m_lastRemotesOut);
    m_localBranches = refs.local;
    m_remoteBranches = refs.remote;
    m_remoteList = refs.remotes;
    m_currentBranch = refs.currentLocal;
    m_detachedSha = refs.detachedShortSha;
    m_empty = refs.empty;
    emit branchesUpdated();
}

void GitController::handleStatusDone(const QByteArray &out)
{
    GitStatusParser::Header hdr;
    m_status->setEntries(GitStatusParser::parsePorcelainV2(out, &hdr));
    // Emit only when something actually changed — saves a UI repaint per refresh.
    const bool changed = (hdr.ahead != m_ahead) || (hdr.behind != m_behind)
                         || (hdr.hasUpstream != m_hasUpstream);
    m_ahead = hdr.ahead;
    m_behind = hdr.behind;
    m_hasUpstream = hdr.hasUpstream;
    emit statusUpdated();
    if (changed) emit aheadBehindChanged(m_ahead, m_behind, m_hasUpstream);
    enqueueNumstatRefresh();
}

void GitController::enqueueNumstatRefresh()
{
    if (m_currentRepo.isEmpty()) return;

    // Unstaged: worktree vs index (or worktree vs HEAD if empty repo)
    {
        Op op;
        op.kind = OpKind::NumstatUnstaged;
        op.argv = { QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                    QStringLiteral("-C"), m_currentRepo,
                    QStringLiteral("diff"), QStringLiteral("--numstat"), QStringLiteral("-z"),
                    QStringLiteral("--no-renames") };
        op.timeoutMs = kTimeoutStatus;
        enqueue(op);
    }
    // Staged: index vs HEAD (skip on empty repo — would error)
    if (!m_empty) {
        Op op;
        op.kind = OpKind::NumstatStaged;
        op.argv = { QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                    QStringLiteral("-C"), m_currentRepo,
                    QStringLiteral("diff"), QStringLiteral("--cached"),
                    QStringLiteral("--numstat"), QStringLiteral("-z"),
                    QStringLiteral("--no-renames") };
        op.timeoutMs = kTimeoutStatus;
        enqueue(op);
    }
}

void GitController::handleNumstatDone(const QByteArray &out, bool stagedSide)
{
    const auto stats = GitNumstatParser::parse(out);
    m_status->mergeNumstat(stats, stagedSide);
    // Emit only after the staged side completes (the second of the pair).
    // On empty repos we only run the unstaged op, so emit there too.
    if (stagedSide || m_empty) {
        emit numstatUpdated();
        // Once per-file numstat is merged into the model, fan out submodule
        // inner-diff probes (only for entries that actually have modified
        // content — porcelain v2 already told us which ones).
        enqueueSubmoduleStatsRefresh();
    }
}

void GitController::enqueueSubmoduleStatsRefresh()
{
    if (m_currentRepo.isEmpty()) return;
    const QStringList subs = m_status->modifiedSubmodulePaths();
    if (subs.isEmpty()) return;

    const QDir rootDir(m_currentRepo);
    for (const QString &relPath : subs) {
        const QString abs = QDir::cleanPath(rootDir.filePath(relPath));
        Op op;
        op.kind = OpKind::SubmoduleNumstat;
        op.argv = { QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                    QStringLiteral("-C"), abs,
                    QStringLiteral("diff"), QStringLiteral("--numstat"),
                    QStringLiteral("-z"), QStringLiteral("--no-renames"),
                    QStringLiteral("HEAD") };
        op.timeoutMs = kTimeoutStatus;
        op.meta.insert(QStringLiteral("subRelPath"), relPath);
        enqueue(op);
    }
}

void GitController::handleSubmoduleNumstatDone(const QByteArray &out, const QString &relPath)
{
    if (relPath.isEmpty()) return;
    const auto stats = GitNumstatParser::parse(out);
    qint64 added = 0;
    qint64 deleted = 0;
    for (auto it = stats.constBegin(); it != stats.constEnd(); ++it) {
        if (it->isBinary) continue;
        if (it->added   > 0) added   += it->added;
        if (it->deleted > 0) deleted += it->deleted;
    }
    // Clamp to qint32 — single submodule with >2B added lines is pathological.
    constexpr qint64 kMax = std::numeric_limits<qint32>::max();
    m_status->mergeSubmoduleStats(relPath,
                                  qint32(std::min(added,   kMax)),
                                  qint32(std::min(deleted, kMax)));
}

void GitController::requestDiff(const QString &relPath, bool stagedSide)
{
    if (m_currentRepo.isEmpty() || relPath.isEmpty()) return;

    Op op;
    op.kind = OpKind::DiffPath;
    op.argv = { QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                QStringLiteral("-C"), m_currentRepo,
                QStringLiteral("diff") };
    if (stagedSide) op.argv.append(QStringLiteral("--cached"));
    op.argv.append({ QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
                     QStringLiteral("--src-prefix=a/"), QStringLiteral("--dst-prefix=b/"),
                     QStringLiteral("--"), relPath });
    op.timeoutMs = kTimeoutNormal;
    QVariantMap meta;
    meta[QStringLiteral("relPath")] = relPath;
    meta[QStringLiteral("stagedSide")] = stagedSide;
    op.meta = meta;
    enqueue(op);
}

void GitController::requestFullDiff()
{
    if (m_currentRepo.isEmpty()) {
        emit fullDiffFailed(tr_("No repository selected."));
        return;
    }
    // Probe staged first; if empty, the success handler enqueues the worktree
    // diff as a fallback (matches the reference scenarios 11 + 12: staged wins
    // when present, worktree otherwise).
    Op op;
    op.kind = OpKind::DiffAllCached;
    op.argv = { QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                QStringLiteral("-C"), m_currentRepo,
                QStringLiteral("diff"), QStringLiteral("--cached"),
                QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
                QStringLiteral("--src-prefix=a/"), QStringLiteral("--dst-prefix=b/") };
    op.timeoutMs = kTimeoutNormal;
    op.humanName = tr_("Diff (staged)");
    enqueue(op);
}

void GitController::onRunFinished(int exit, const QByteArray &out, const QByteArray &err)
{
    const OpKind kind = m_current.kind;
    const QString humanName = m_current.humanName;

    auto popAndAdvance = [this]() { m_busy = false; runNext(); };

    // Cancelled sentinel (-2)
    if (exit == -2) {
        GitError e;
        e.kind = GitError::Cancelled;
        e.humanMessage = tr_("Operation cancelled.");
        e.details = QString::fromUtf8(err);
        setState(State::Error);
        emit errorOccurred(e);
        m_queue.clear();
        popAndAdvance();
        return;
    }

    // Timeout marker injected by GitProcessRunner::onTimeout
    if (err.contains("__GIT_TIMEOUT__")) {
        GitError e;
        e.kind = GitError::Timeout;
        e.humanMessage = tr_("Operation timed out.");
        e.hint = tr_("Network or process hung; check connection.");
        e.suggestedActions = { QStringLiteral("retry") };
        e.details = QString::fromUtf8(err).remove(QStringLiteral("__GIT_TIMEOUT__")).trimmed();
        setState(State::Error);
        emit errorOccurred(e);
        m_queue.clear();
        popAndAdvance();
        return;
    }

    // Special-case ops that are allowed to "fail" (HeadSym on detached, HeadSha on empty repo).
    if (kind == OpKind::HeadSym) {
        handleHeadSymDone(out);
        popAndAdvance();
        return;
    }
    if (kind == OpKind::HeadSha) {
        handleHeadShaDone(exit, out);
        popAndAdvance();
        return;
    }

    if (exit != 0) {
        GitError e = GitErrorClassifier::classify(exit, err, m_current.argv);
        if (kind == OpKind::Toplevel) {
            handleToplevelDone(exit, out, err);
            popAndAdvance();
            return;
        }
        // Numstat ops are best-effort — a failure (e.g. unborn HEAD edge cases,
        // missing object) shouldn't tip the controller into Error or block UI.
        if (kind == OpKind::NumstatStaged || kind == OpKind::NumstatUnstaged
            || kind == OpKind::SubmoduleNumstat) {
            popAndAdvance();
            return;
        }
        // Diff-path failures are surfaced to the requester only, never block.
        if (kind == OpKind::DiffPath) {
            emit diffFailed(m_current.meta.value(QStringLiteral("relPath")).toString(),
                            m_current.meta.value(QStringLiteral("stagedSide")).toBool(),
                            QString::fromUtf8(err));
            popAndAdvance();
            return;
        }
        // Full-diff failures: surface to AI generator only, do not block queue.
        if (kind == OpKind::DiffAllCached || kind == OpKind::DiffAllWorktree) {
            emit fullDiffFailed(QString::fromUtf8(err));
            popAndAdvance();
            return;
        }
        setState(State::Error);
        m_queue.clear();
        emit errorOccurred(e);
        popAndAdvance();
        return;
    }

    switch (kind) {
        case OpKind::Toplevel:        handleToplevelDone(exit, out, err); break;
        case OpKind::SubmodulesList:  handleSubmodulesDone(exit, out); break;
        case OpKind::Refs:            handleRefsDone(out); break;
        case OpKind::Remotes:         handleRemotesDone(out); break;
        case OpKind::Status:          handleStatusDone(out); break;
        case OpKind::NumstatStaged:   handleNumstatDone(out, true); break;
        case OpKind::NumstatUnstaged: handleNumstatDone(out, false); break;
        case OpKind::SubmoduleNumstat:
            handleSubmoduleNumstatDone(out,
                m_current.meta.value(QStringLiteral("subRelPath")).toString());
            break;
        case OpKind::DiffPath:
            emit diffReady(m_current.meta.value(QStringLiteral("relPath")).toString(),
                           m_current.meta.value(QStringLiteral("stagedSide")).toBool(),
                           out);
            break;
        case OpKind::DiffAllCached:
            if (!out.isEmpty()) {
                emit fullDiffReady(out);
            } else {
                // No staged diff — fall back to worktree diff.
                Op op;
                op.kind = OpKind::DiffAllWorktree;
                op.argv = { QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
                            QStringLiteral("-C"), m_currentRepo,
                            QStringLiteral("diff"),
                            QStringLiteral("--no-color"), QStringLiteral("--no-ext-diff"),
                            QStringLiteral("--src-prefix=a/"), QStringLiteral("--dst-prefix=b/") };
                op.timeoutMs = kTimeoutNormal;
                op.humanName = tr_("Diff (worktree)");
                enqueue(op);
            }
            break;
        case OpKind::DiffAllWorktree:
            if (!out.isEmpty()) emit fullDiffReady(out);
            else emit fullDiffFailed(tr_("No changes to commit."));
            break;
        case OpKind::Stage:
        case OpKind::Unstage:
        case OpKind::StageAll:
        case OpKind::UnstageAll:
        case OpKind::Commit:
        case OpKind::SwitchBranch:
        case OpKind::CreateBranch:
        case OpKind::Stash:
        case OpKind::Fetch:
        case OpKind::Pull:
        case OpKind::Push:
        case OpKind::ForcePush:
            if (kind == OpKind::Commit) emit commitSucceeded();
            if (!humanName.isEmpty()) emit opSucceeded(humanName);
            scheduleDebouncedRefresh();
            break;
        default: break;
    }
    popAndAdvance();
}
