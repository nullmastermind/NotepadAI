/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// git.* (docs/adr/0007). Commands run on the workspace's GitController, the
// one its Git tab uses, so the tab shows each step. Questions the tab would
// ask (dirty tree, diverged pull, rejected push) are arguments instead.

#include "Registrar.h"

#include "NotepadNextApplication.h"
#include "ai/CommitMessageGenerator.h"
#include "dialogs/MainWindow.h"
#include "docks/FolderAsWorkspaceDock.h"
#include "git/GitController.h"
#include "git/GitError.h"
#include "git/GitOperationManager.h"
#include "git/GitProcessRunner.h"
#include "git/GitRepoModel.h"
#include "git/GitRunnerFactory.h"
#include "git/GitStatusModel.h"
#include "git/GitTabWidget.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>

namespace control::commands {

namespace {

constexpr int kNetworkTimeoutMs = 10 * 60 * 1000;
constexpr int kLocalTimeoutMs = 2 * 60 * 1000;
// The longest a command waits for the status refresh after its operation.
constexpr int kStatusAfterOpMs = 5000;

struct GitTarget
{
    FolderAsWorkspaceDock *dock = nullptr;
    GitTabWidget *tab = nullptr;
    GitController *git = nullptr;
};

bool gitTarget(const CommandContext &ctx, MainWindow *w, GitTarget *t)
{
    t->dock = Registrar::workspaceArg(ctx, w);
    if (!t->dock)
        return false;
    t->git = t->dock->gitController();
    t->tab = t->dock->gitTabWidget();
    if (!t->git || !t->tab) {
        ctx.fail(409, QStringLiteral("git is not set up for this workspace"));
        return false;
    }
    if (t->git->currentRepo().isEmpty()) {
        ctx.fail(409, t->git->state() == GitController::State::Discovering
                          ? QStringLiteral("git is still looking for the repository; try again in a moment")
                          : QStringLiteral("this workspace is not in a git repository"));
        return false;
    }
    return true;
}

QString sectionName(GitStatusEntry::Section s)
{
    switch (s) {
    case GitStatusEntry::Conflicts:
        return QStringLiteral("conflicts");
    case GitStatusEntry::Staged:
        return QStringLiteral("staged");
    case GitStatusEntry::Untracked:
        return QStringLiteral("untracked");
    default:
        return QStringLiteral("changed");
    }
}

QString changeName(GitStatusEntry::Change c)
{
    switch (c) {
    case GitStatusEntry::Added:
        return QStringLiteral("added");
    case GitStatusEntry::Deleted:
        return QStringLiteral("deleted");
    case GitStatusEntry::Renamed:
        return QStringLiteral("renamed");
    case GitStatusEntry::Copied:
        return QStringLiteral("copied");
    case GitStatusEntry::TypeChanged:
        return QStringLiteral("typechange");
    case GitStatusEntry::Untracked_:
        return QStringLiteral("untracked");
    case GitStatusEntry::Unmerged:
        return QStringLiteral("unmerged");
    default:
        return QStringLiteral("modified");
    }
}

QString operationName(GitOperationManager::OperationState s)
{
    switch (s) {
    case GitOperationManager::OperationState::MergeRunning:
        return QStringLiteral("merging");
    case GitOperationManager::OperationState::MergeConflicted:
        return QStringLiteral("merge-conflicted");
    case GitOperationManager::OperationState::RebaseRunning:
        return QStringLiteral("rebasing");
    case GitOperationManager::OperationState::RebaseSuspended:
        return QStringLiteral("rebase-stopped");
    case GitOperationManager::OperationState::RebaseSuspendedEdit:
        return QStringLiteral("rebase-edit");
    default:
        return QString();
    }
}

QJsonObject statusJson(GitController *git, GitOperationManager *ops, int limit = 2000)
{
    QJsonObject o{{QStringLiteral("repo"), QDir::fromNativeSeparators(git->currentRepo())}};
    if (!git->currentBranch().isEmpty())
        o.insert(QStringLiteral("branch"), git->currentBranch());
    else if (!git->detachedShortSha().isEmpty())
        o.insert(QStringLiteral("detached"), git->detachedShortSha());
    if (git->hasUpstream()) {
        o.insert(QStringLiteral("ahead"), git->ahead());
        o.insert(QStringLiteral("behind"), git->behind());
    } else {
        o.insert(QStringLiteral("upstream"), false);
    }
    if (ops) {
        if (const QString op = operationName(ops->state(git->currentRepo())); !op.isEmpty())
            o.insert(QStringLiteral("operation"), op);
    }
    if (const QString running = git->runningOperationName(); !running.isEmpty())
        o.insert(QStringLiteral("running"), running);
    QJsonArray files;
    const GitStatusEntries entries = git->statusModel()->allEntries();
    for (const GitStatusEntry &e : entries) {
        if (files.size() >= limit)
            break;
        QJsonObject f{{QStringLiteral("path"), e.relPath},
                      {QStringLiteral("section"), sectionName(e.section)},
                      {QStringLiteral("change"), changeName(e.change)}};
        if (!e.origRelPath.isEmpty())
            f.insert(QStringLiteral("from"), e.origRelPath);
        if (e.addedLines >= 0)
            f.insert(QStringLiteral("added"), e.addedLines);
        if (e.deletedLines >= 0)
            f.insert(QStringLiteral("deleted"), e.deletedLines);
        files.append(f);
    }
    o.insert(QStringLiteral("files"), files);
    if (entries.size() > limit)
        o.insert(QStringLiteral("truncated"), true);
    return o;
}

// Paths as the repo knows them: relative, forward slashes. Absolute paths
// inside the repo are made relative.
QStringList repoPaths(GitController *git, const QStringList &paths)
{
    const QString repo = QDir::cleanPath(QDir::fromNativeSeparators(git->currentRepo()));
    QStringList out;
    for (const QString &p : paths) {
        QString path = QDir::fromNativeSeparators(p);
        if (!QDir::isRelativePath(path)) {
            path = QDir::cleanPath(path);
            if (path.startsWith(repo + QLatin1Char('/'), Qt::CaseInsensitive))
                path = path.mid(repo.size() + 1);
        }
        out.append(path);
    }
    return out;
}

// What a question the Git tab would ask means for the command: the error to
// answer with (needs an argument), or empty when the command answers it itself.
struct Prompts
{
    std::function<QString(GitController *, const QString &target)> dirtyTree;
    std::function<QString(GitController *)> diverged;
    std::function<QString(GitController *)> pushRejected;
};

// Runs `start` on the controller and answers once its queue has drained:
// with the status after a success, the error otherwise.
void runOp(const CommandContext &ctx, GitTarget t, GitOperationManager *ops, const std::function<void()> &start,
           const Prompts &prompts = {}, int timeoutMs = kLocalTimeoutMs)
{
    struct State
    {
        bool succeeded = false;
        bool done = false;
        bool awaitingStatus = false;
        QStringList steps;
    };
    auto state = std::make_shared<State>();
    auto *guard = new QObject(t.git);
    QPointer<GitTabWidget> tab(t.tab);
    QPointer<GitController> git(t.git);
    tab->setPromptsAnsweredByCaller(true);
    auto finish = [state, guard, tab] {
        state->done = true;
        if (tab)
            tab->setPromptsAnsweredByCaller(false);
        guard->deleteLater();
    };
    auto fail = [ctx, finish, state](int status, const QString &error, const QJsonObject &extra = {}) {
        if (state->done || state->awaitingStatus)
            return;
        finish();
        ctx.fail(status, error, extra);
    };

    QObject::connect(t.git, &GitController::opSucceeded, guard, [state](const QString &name) {
        state->succeeded = true;
        state->steps.append(name);
    });
    // Idle after a success: the status refresh that follows the operation may
    // still be coalesced behind it, so answer with the status it produces.
    QObject::connect(t.git, &GitController::stateChanged, guard,
                     [ctx, state, finish, git, ops, guard](GitController::State s) {
                         if (state->done || state->awaitingStatus || !state->succeeded
                             || s != GitController::State::Idle || !git)
                             return;
                         state->awaitingStatus = true;
                         auto answer = [ctx, state, finish, git, ops] {
                             if (state->done)
                                 return;
                             finish();
                             QJsonObject o = git ? statusJson(git, ops) : QJsonObject{};
                             o.insert(QStringLiteral("done"), QJsonArray::fromStringList(state->steps));
                             ctx.ok(o);
                         };
                         QObject::connect(git, &GitController::statusRefreshed, guard, answer);
                         QTimer::singleShot(kStatusAfterOpMs, guard, answer);
                         git->refresh();
                     });
    QObject::connect(t.git, &GitController::errorOccurred, guard, [fail](const GitError &err) {
        QString text = err.humanMessage;
        if (!err.hint.isEmpty())
            text += QStringLiteral(" ") + err.hint;
        QJsonObject extra;
        if (!err.details.isEmpty())
            extra.insert(QStringLiteral("details"), err.details.left(4000));
        fail(422, text, extra);
    });
    QObject::connect(t.git, &GitController::gitMissing, guard,
                     [fail] { fail(422, QStringLiteral("git is not installed or not on PATH")); });
    QObject::connect(t.git, &GitController::dirtyTreePromptRequested, guard,
                     [fail, prompts, git](const QString &target) {
                         const QString error = prompts.dirtyTree && git ? prompts.dirtyTree(git, target)
                                                                        : QStringLiteral("the working tree has changes");
                         if (!error.isEmpty())
                             fail(409, error, {{QStringLiteral("needs"), QStringLiteral("dirty")}});
                     });
    QObject::connect(t.git, &GitController::pullDivergedPromptRequested, guard, [fail, prompts, git] {
        const QString error = prompts.diverged && git ? prompts.diverged(git)
                                                      : QStringLiteral("the branches have diverged");
        if (!error.isEmpty())
            fail(409, error, {{QStringLiteral("needs"), QStringLiteral("strategy")}});
    });
    QObject::connect(t.git, &GitController::pushRejectedPromptRequested, guard, [fail, prompts, git] {
        const QString error = prompts.pushRejected && git
                                  ? prompts.pushRejected(git)
                                  : QStringLiteral("the remote has commits you do not have; run git.pull first, then "
                                                   "git.push");
        if (!error.isEmpty())
            fail(409, error);
    });
    QObject::connect(t.git, &GitController::pullConflicted, guard, [fail] {
        fail(409, QStringLiteral("the pull stopped on conflicts; git.status lists them, git.resolve settles them"),
             {{QStringLiteral("conflicts"), true}});
    });
    QTimer::singleShot(timeoutMs, guard, [fail, timeoutMs] {
        fail(504, QStringLiteral("git did not finish within %1 s").arg(timeoutMs / 1000));
    });
    start();
}

// Answers after the next status refresh (resolving conflicts, operations).
void answerAfterRefresh(const CommandContext &ctx, GitController *git, GitOperationManager *ops)
{
    auto *guard = new QObject(git);
    auto answer = [ctx, guard, git = QPointer<GitController>(git), ops] {
        guard->deleteLater();
        ctx.ok(git ? statusJson(git, ops) : QJsonObject());
    };
    QObject::connect(git, &GitController::statusRefreshed, guard, answer, Qt::SingleShotConnection);
    QTimer::singleShot(30000, guard, answer);
}

// Answers when a merge or rebase settles (or stops on conflicts).
void awaitOperation(const CommandContext &ctx, GitController *git, GitOperationManager *ops)
{
    const QString repo = git->currentRepo();
    auto *guard = new QObject(git);
    QPointer<GitController> g(git);
    auto done = [ctx, guard, g, ops](int status, const QString &error, const QString &outcome) {
        if (!guard->parent())
            return;
        guard->setParent(nullptr);
        guard->deleteLater();
        QJsonObject o = g ? statusJson(g, ops) : QJsonObject();
        o.insert(QStringLiteral("outcome"), outcome);
        if (status == 200)
            ctx.ok(o);
        else
            ctx.fail(status, error, o);
    };
    auto mine = [repo](const QString &r) { return r == repo; };
    using Ops = GitOperationManager;
    QObject::connect(ops, &Ops::mergeCompleted, guard, [=](const QString &r) {
        if (mine(r))
            done(200, QString(), QStringLiteral("merged"));
    });
    QObject::connect(ops, &Ops::mergeConflicted, guard, [=](const QString &r, const ConflictEntries &) {
        if (mine(r))
            done(409, QStringLiteral("the merge stopped on conflicts; resolve them with git.resolve, then "
                                     "git.operation {action: \"commit\"}"),
                 QStringLiteral("conflicts"));
    });
    QObject::connect(ops, &Ops::mergeFailed, guard, [=](const QString &r, const QString &error) {
        if (mine(r))
            done(422, error, QStringLiteral("failed"));
    });
    QObject::connect(ops, &Ops::rebaseCompleted, guard, [=](const QString &r) {
        if (mine(r))
            done(200, QString(), QStringLiteral("rebased"));
    });
    QObject::connect(ops, &Ops::rebaseAborted, guard, [=](const QString &r) {
        if (mine(r))
            done(200, QString(), QStringLiteral("aborted"));
    });
    QObject::connect(ops, &Ops::rebaseConflicted, guard, [=](const QString &r, const ConflictEntries &) {
        if (mine(r))
            done(409, QStringLiteral("the rebase stopped on conflicts; resolve them with git.resolve, then "
                                     "git.operation {action: \"continue\"}"),
                 QStringLiteral("conflicts"));
    });
    QObject::connect(ops, &Ops::rebaseSuspended, guard, [=](const QString &r, const QString &reason) {
        if (mine(r))
            done(409, reason.isEmpty() ? QStringLiteral("the rebase stopped") : reason, QStringLiteral("stopped"));
    });
    QObject::connect(ops, &Ops::rebaseFailed, guard, [=](const QString &r, const QString &error) {
        if (mine(r))
            done(422, error, QStringLiteral("failed"));
    });
    QObject::connect(ops, &Ops::operationStateChanged, guard, [=](const QString &r, Ops::OperationState s) {
        // Aborting a merge reports only the state change.
        if (mine(r) && s == Ops::OperationState::Idle)
            QTimer::singleShot(200, guard, [=] { done(200, QString(), QStringLiteral("idle")); });
    });
    QTimer::singleShot(kNetworkTimeoutMs, guard,
                       [=] { done(504, QStringLiteral("the operation did not settle within 10 minutes"), QString()); });
}

QList<Arg> pathsArgs(const QString &verb)
{
    return {Registrar::optional(QStringLiteral("paths"), ArgType::Array,
                                QStringLiteral("files to %1 (relative to the repository, or absolute)").arg(verb)),
            Registrar::optional(QStringLiteral("all"), ArgType::Bool, QStringLiteral("%1 everything").arg(verb)),
            Registrar::workspaceArgSpec()};
}

// Runs one read-only git command in `cwd` on the workspace's runner (local or
// SSH) and hands back its result; the runner is freed after.
void runGit(GitController *git, const QString &cwd, const QStringList &argv, int timeoutMs,
            std::function<void(int exit, const QByteArray &out, const QByteArray &err)> done)
{
    IGitProcessRunner *runner = GitRunnerFactory::createForRepo(git->runnerScope(), git);
    runner->run(cwd, argv, {}, timeoutMs, false,
                [runner, done = std::move(done)](int exit, const QByteArray &out, const QByteArray &err) {
                    runner->asQObject()->deleteLater();
                    done(exit, out, err);
                });
}

QString cleanRepoPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

// The repository the workspace's Git tab lists under `path` (case-insensitive
// on Windows), or null.
const GitRepoInfo *repoInfo(GitController *git, const QString &path)
{
    const QString want = cleanRepoPath(path);
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    GitRepoModel *model = git->repoModel();
    for (int i = 0; model && i < model->rowCount(); ++i) {
        const GitRepoInfo *info = model->infoAt(i);
        if (info && cleanRepoPath(info->toplevel).compare(want, cs) == 0)
            return info;
    }
    return nullptr;
}

QString repoKind(const GitRepoInfo &info)
{
    if (info.isWorktree)
        return QStringLiteral("worktree");
    if (info.isSubmodule)
        return QStringLiteral("submodule");
    return QStringLiteral("main");
}

// `git worktree list --porcelain` blocks: worktree / HEAD / branch|detached /
// bare / locked [reason] / prunable [reason].
QJsonArray parseWorktrees(const QByteArray &out, const QString &currentRepo)
{
    QJsonArray list;
    QJsonObject wt;
    auto flush = [&] {
        if (!wt.isEmpty())
            list.append(wt);
        wt = QJsonObject();
    };
    for (const QByteArray &raw : out.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) {
            flush();
            continue;
        }
        const QString key = line.section(QLatin1Char(' '), 0, 0);
        const QString value = line.section(QLatin1Char(' '), 1);
        if (key == QLatin1String("worktree")) {
            flush();
            const QString path = cleanRepoPath(value);
            wt.insert(QStringLiteral("path"), path);
            wt.insert(QStringLiteral("current"), path.compare(cleanRepoPath(currentRepo), Qt::CaseInsensitive) == 0);
            wt.insert(QStringLiteral("main"), list.isEmpty());
        } else if (key == QLatin1String("HEAD")) {
            wt.insert(QStringLiteral("head"), value);
        } else if (key == QLatin1String("branch")) {
            QString branch = value;
            if (branch.startsWith(QLatin1String("refs/heads/")))
                branch.remove(0, 11);
            wt.insert(QStringLiteral("branch"), branch);
        } else if (key == QLatin1String("detached")) {
            wt.insert(QStringLiteral("detached"), true);
        } else if (key == QLatin1String("bare")) {
            wt.insert(QStringLiteral("bare"), true);
        } else if (key == QLatin1String("locked") || key == QLatin1String("prunable")) {
            wt.insert(key, value.isEmpty() ? QJsonValue(true) : QJsonValue(value));
        }
    }
    flush();
    return list;
}

// The commit an interactive rebase is on when git opens the message editor:
// the last command in <git dir>/rebase-merge/done ("reword <sha> ...").
QString rebaseCurrentSha(const QString &gitDir)
{
    QFile done(gitDir + QStringLiteral("/rebase-merge/done"));
    if (!done.open(QIODevice::ReadOnly))
        return QString();
    const QList<QByteArray> lines = done.readAll().split('\n');
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const QList<QByteArray> parts = it->trimmed().split(' ');
        if (parts.size() >= 2 && !parts.first().startsWith('#'))
            return QString::fromLatin1(parts.at(1));
    }
    return QString();
}

bool writeWhole(const QString &path, const QByteArray &data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return f.commit();
}

const QStringList &rebaseActions()
{
    static const QStringList actions{QStringLiteral("pick"),   QStringLiteral("reword"), QStringLiteral("edit"),
                                     QStringLiteral("squash"), QStringLiteral("fixup"),  QStringLiteral("drop")};
    return actions;
}

} // namespace

void Registrar::git()
{
    const Arg ws = workspaceArgSpec();

    add(QStringLiteral("git.status"),
        QStringLiteral("Read the workspace repository's branch, ahead/behind, merge/rebase state and changed files"),
        false, {ws, optional(QStringLiteral("limit"), ArgType::Int, QStringLiteral("at most this many files; default 2000"))},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            ctx.ok(statusJson(t.git, w->m_gitOpMgr, int(std::max<qint64>(1, ctx.integer(QStringLiteral("limit"), 2000)))));
        });

    add(QStringLiteral("git.refresh"), QStringLiteral("Re-read the repository's status"), true, {ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            answerAfterRefresh(ctx, t.git, w->m_gitOpMgr);
            t.git->refresh(true);
        });

    add(QStringLiteral("git.stage"), QStringLiteral("Stage files (or everything)"), true, pathsArgs(QStringLiteral("stage")),
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QStringList paths = repoPaths(t.git, ctx.strings(QStringLiteral("paths")));
            if (paths.isEmpty() && !ctx.flag(QStringLiteral("all"))) {
                ctx.fail(400, QStringLiteral("pass `paths` or all: true"));
                return;
            }
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr, [t, paths] { paths.isEmpty() ? t.git->stageAll() : t.git->stagePaths(paths); });
        });

    add(QStringLiteral("git.unstage"), QStringLiteral("Unstage files (or everything); their changes stay"), true,
        pathsArgs(QStringLiteral("unstage")), [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QStringList paths = repoPaths(t.git, ctx.strings(QStringLiteral("paths")));
            if (paths.isEmpty() && !ctx.flag(QStringLiteral("all"))) {
                ctx.fail(400, QStringLiteral("pass `paths` or all: true"));
                return;
            }
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr,
                  [t, paths] { paths.isEmpty() ? t.git->unstageAll() : t.git->unstagePaths(paths); });
        });

    add(QStringLiteral("git.discard"),
        QStringLiteral("Throw away changes to files: tracked files go back to their committed text, untracked files are "
                       "deleted"),
        true,
        {required(QStringLiteral("paths"), ArgType::Array,
                  QStringLiteral("files (relative to the repository, or absolute)")),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QStringList paths = repoPaths(t.git, ctx.strings(QStringLiteral("paths")));
            if (paths.isEmpty()) {
                ctx.fail(400, QStringLiteral("`paths` is empty"));
                return;
            }
            const QStringList untrackedAll = t.git->statusModel()->relPathsInSection(GitStatusEntry::Untracked);
            QStringList tracked;
            QStringList untracked;
            for (const QString &p : paths)
                (untrackedAll.contains(p) ? untracked : tracked).append(p);
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr, [t, tracked, untracked] {
                if (!tracked.isEmpty())
                    t.git->revertPaths(tracked);
                if (!untracked.isEmpty())
                    t.git->deleteUntrackedPaths(untracked);
            });
        });

    add(QStringLiteral("git.commit"),
        QStringLiteral("Commit with a message, as the Commit button does (with nothing staged, everything is staged "
                       "first)"),
        true,
        {required(QStringLiteral("message"), ArgType::String, QStringLiteral("commit message")),
         optional(QStringLiteral("amend"), ArgType::Bool, QStringLiteral("amend the last commit")),
         optional(QStringLiteral("signoff"), ArgType::Bool, QStringLiteral("add Signed-off-by")),
         optional(QStringLiteral("trackedOnly"), ArgType::Bool,
                  QStringLiteral("commit all tracked changes, staged or not (git commit -a)")),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString message = ctx.str(QStringLiteral("message"));
            if (message.trimmed().isEmpty()) {
                ctx.fail(400, QStringLiteral("the message is empty"));
                return;
            }
            const bool amend = ctx.flag(QStringLiteral("amend"));
            if (!amend && t.git->statusModel()->totalEntries() == 0) {
                ctx.fail(409, QStringLiteral("nothing to commit"));
                return;
            }
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr, [ctx, t, message, amend] {
                t.tab->commitWithMessage(message, amend, ctx.flag(QStringLiteral("signoff")),
                                         ctx.flag(QStringLiteral("trackedOnly")));
            });
        });

    add(QStringLiteral("git.branches"), QStringLiteral("List local and remote branches, remotes and the current branch"),
        false, {ws}, [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            ctx.ok({{QStringLiteral("current"), t.git->currentBranch()},
                    {QStringLiteral("local"), QJsonArray::fromStringList(t.git->branchesLocal())},
                    {QStringLiteral("remote"), QJsonArray::fromStringList(t.git->branchesRemote())},
                    {QStringLiteral("remotes"), QJsonArray::fromStringList(t.git->remotes())},
                    {QStringLiteral("main"), t.git->mainBranch()}});
        });

    add(QStringLiteral("git.switch"),
        QStringLiteral("Switch to a branch (a remote one gets a local tracking branch)"), true,
        {required(QStringLiteral("branch"), ArgType::String, QStringLiteral("branch name from git.branches")),
         oneOf(QStringLiteral("dirty"), {QStringLiteral("stash"), QStringLiteral("force")},
               QStringLiteral("with uncommitted changes: stash them first, or switch anyway (git carries them over or "
                              "refuses)")),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString branch = ctx.str(QStringLiteral("branch"));
            const QString dirty = ctx.str(QStringLiteral("dirty"));
            const auto policy = dirty == QLatin1String("stash")   ? GitController::BranchSwitchPolicy::StashAndSwitch
                                : dirty == QLatin1String("force") ? GitController::BranchSwitchPolicy::SwitchAnyway
                                                                  : GitController::BranchSwitchPolicy::Cancel;
            Prompts prompts;
            prompts.dirtyTree = [](GitController *, const QString &target) {
                return QStringLiteral("the working tree has uncommitted changes; pass dirty: \"stash\" or \"force\" to "
                                      "switch to %1")
                    .arg(target);
            };
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr, [t, branch, policy] { t.git->switchBranch(branch, policy); }, prompts);
        });

    add(QStringLiteral("git.createBranch"), QStringLiteral("Create a branch (and switch to it)"), true,
        {required(QStringLiteral("name"), ArgType::String, QStringLiteral("new branch name")),
         optional(QStringLiteral("base"), ArgType::String, QStringLiteral("start point; default the current HEAD")),
         optional(QStringLiteral("checkout"), ArgType::Bool, QStringLiteral("switch to it; default true")), ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString name = ctx.str(QStringLiteral("name")).trimmed();
            const QString base = ctx.str(QStringLiteral("base"));
            const bool checkout = ctx.flag(QStringLiteral("checkout"), true);
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr, [t, name, base, checkout] { t.git->createBranch(name, base, checkout); });
        });

    add(QStringLiteral("git.renameBranch"), QStringLiteral("Rename a local branch"), true,
        {required(QStringLiteral("from"), ArgType::String, QStringLiteral("current name")),
         required(QStringLiteral("to"), ArgType::String, QStringLiteral("new name")),
         optional(QStringLiteral("updateRemote"), ArgType::Bool,
                  QStringLiteral("also rename it on the remote; default false")),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString from = ctx.str(QStringLiteral("from"));
            const QString to = ctx.str(QStringLiteral("to")).trimmed();
            const bool remote = ctx.flag(QStringLiteral("updateRemote"));
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr, [t, from, to, remote] { t.git->renameBranch(from, to, remote); },
                  {}, remote ? kNetworkTimeoutMs : kLocalTimeoutMs);
        });

    add(QStringLiteral("git.deleteBranch"), QStringLiteral("Delete a local branch"), true,
        {required(QStringLiteral("name"), ArgType::String, QStringLiteral("branch name")),
         optional(QStringLiteral("force"), ArgType::Bool, QStringLiteral("delete even if it is not merged")), ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString name = ctx.str(QStringLiteral("name"));
            if (name == t.git->currentBranch()) {
                ctx.fail(409, QStringLiteral("%1 is the current branch; switch away first").arg(name));
                return;
            }
            const bool force = ctx.flag(QStringLiteral("force"));
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr, [t, name, force] { t.git->deleteBranch(name, force); });
        });

    add(QStringLiteral("git.fetch"), QStringLiteral("Fetch from a remote (default: all of them)"), true,
        {optional(QStringLiteral("remote"), ArgType::String, QStringLiteral("remote name")), ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString remote = ctx.str(QStringLiteral("remote"));
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr, [t, remote] { t.git->fetch(remote); }, {}, kNetworkTimeoutMs);
        });

    add(QStringLiteral("git.pull"),
        QStringLiteral("Pull the current branch's upstream (fast-forward unless `strategy` says how to reconcile)"), true,
        {oneOf(QStringLiteral("strategy"), {QStringLiteral("ff"), QStringLiteral("merge"), QStringLiteral("rebase")},
               QStringLiteral("ff (default; stops when the branches diverged), merge or rebase")),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            if (!t.git->hasUpstream()) {
                ctx.fail(409, QStringLiteral("the current branch has no upstream; git.push {setUpstream: true} sets one"));
                return;
            }
            const QString strategy = ctx.str(QStringLiteral("strategy"), QStringLiteral("ff"));
            Prompts prompts;
            prompts.diverged = [](GitController *) {
                return QStringLiteral("the branches have diverged; pass strategy: \"merge\" or \"rebase\"");
            };
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr,
                  [t, strategy] {
                      if (strategy == QLatin1String("merge"))
                          t.git->pullMerge();
                      else
                          t.git->pull(strategy == QLatin1String("rebase"));
                  },
                  prompts, kNetworkTimeoutMs);
        });

    add(QStringLiteral("git.push"), QStringLiteral("Push the current branch"), true,
        {optional(QStringLiteral("remote"), ArgType::String, QStringLiteral("remote name; default the upstream's")),
         optional(QStringLiteral("setUpstream"), ArgType::Bool,
                  QStringLiteral("make the pushed branch the upstream (needed the first time)")),
         optional(QStringLiteral("force"), ArgType::Bool,
                  QStringLiteral("force-push with lease (overwrites the remote branch)")),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            if (!t.git->hasRemote()) {
                ctx.fail(409, QStringLiteral("the repository has no remote"));
                return;
            }
            const QString remote = ctx.str(QStringLiteral("remote"));
            const bool setUpstream = ctx.flag(QStringLiteral("setUpstream")) || !t.git->hasUpstream();
            const bool force = ctx.flag(QStringLiteral("force"));
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr,
                  [t, remote, setUpstream, force] {
                      if (force)
                          t.git->forcePush(remote);
                      else
                          t.git->push(remote, setUpstream);
                  },
                  {}, kNetworkTimeoutMs);
        });

    add(QStringLiteral("git.merge"), QStringLiteral("Merge a branch into the current one"), true,
        {required(QStringLiteral("branch"), ArgType::String, QStringLiteral("branch to merge")), ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            awaitOperation(ctx, t.git, w->m_gitOpMgr);
            w->m_gitOpMgr->startMerge(t.git, ctx.str(QStringLiteral("branch")));
        });

    add(QStringLiteral("git.rebase"), QStringLiteral("Rebase the current branch onto another"), true,
        {required(QStringLiteral("onto"), ArgType::String, QStringLiteral("branch to rebase onto")), ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            awaitOperation(ctx, t.git, w->m_gitOpMgr);
            w->m_gitOpMgr->startRebase(t.git, ctx.str(QStringLiteral("onto")));
        });

    add(QStringLiteral("git.operation"),
        QStringLiteral("Continue, skip or abort a rebase, or commit or abort a merge in progress"), true,
        {oneOf(QStringLiteral("action"),
               {QStringLiteral("continue"), QStringLiteral("skip"), QStringLiteral("abort"), QStringLiteral("commit")},
               QStringLiteral("continue / skip (rebase), commit (merge), abort (either)"), true),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            GitOperationManager *ops = w->m_gitOpMgr;
            using S = GitOperationManager::OperationState;
            const S state = ops->state(t.git->currentRepo());
            const bool merging = state == S::MergeRunning || state == S::MergeConflicted;
            const bool rebasing = state == S::RebaseRunning || state == S::RebaseSuspended || state == S::RebaseSuspendedEdit;
            const QString action = ctx.str(QStringLiteral("action"));
            if (!merging && !rebasing) {
                ctx.fail(409, QStringLiteral("no merge or rebase is in progress"));
                return;
            }
            if (merging && (action == QLatin1String("continue") || action == QLatin1String("skip"))) {
                ctx.fail(409, QStringLiteral("a merge is in progress: use commit or abort"));
                return;
            }
            if (rebasing && action == QLatin1String("commit")) {
                ctx.fail(409, QStringLiteral("a rebase is in progress: use continue, skip or abort"));
                return;
            }
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            awaitOperation(ctx, t.git, ops);
            if (merging)
                action == QLatin1String("abort") ? ops->abortMerge(t.git) : ops->commitMerge(t.git);
            else if (action == QLatin1String("continue"))
                ops->continueRebase(t.git);
            else if (action == QLatin1String("skip"))
                ops->skipRebase(t.git);
            else
                ops->abortRebase(t.git);
        });

    add(QStringLiteral("git.resolve"),
        QStringLiteral("Settle conflicted files: take ours, take theirs, or mark them resolved after editing"), true,
        {required(QStringLiteral("paths"), ArgType::Array, QStringLiteral("conflicted files")),
         oneOf(QStringLiteral("take"), {QStringLiteral("ours"), QStringLiteral("theirs"), QStringLiteral("edited")},
               QStringLiteral("ours, theirs, or edited (the file as it is now, conflict markers removed)"), true),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QStringList paths = repoPaths(t.git, ctx.strings(QStringLiteral("paths")));
            if (paths.isEmpty()) {
                ctx.fail(400, QStringLiteral("`paths` is empty"));
                return;
            }
            const QString take = ctx.str(QStringLiteral("take"));
            GitOperationManager *ops = w->m_gitOpMgr;
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            answerAfterRefresh(ctx, t.git, ops);
            if (take == QLatin1String("ours"))
                ops->acceptOurs(t.git, paths);
            else if (take == QLatin1String("theirs"))
                ops->acceptTheirs(t.git, paths);
            else
                for (const QString &p : paths)
                    ops->resolveFile(t.git, p);
        });

    add(QStringLiteral("git.diff"),
        QStringLiteral("Read a unified diff: one file (working tree or staged side), or the whole repository"), false,
        {optional(QStringLiteral("path"), ArgType::String,
                  QStringLiteral("file; default the whole repository (staged changes if any, else the working tree)")),
         optional(QStringLiteral("staged"), ArgType::Bool, QStringLiteral("with `path`: the staged side")),
         optional(QStringLiteral("maxChars"), ArgType::Int, QStringLiteral("cut after this many characters; default 200000")),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const qint64 maxChars = std::max<qint64>(1, ctx.integer(QStringLiteral("maxChars"), 200000));
            auto *guard = new QObject(t.git);
            auto answer = [ctx, guard, maxChars](const QByteArray &diff) {
                guard->deleteLater();
                QString text = QString::fromUtf8(diff);
                const bool truncated = text.size() > maxChars;
                if (truncated)
                    text.truncate(int(maxChars));
                QJsonObject o{{QStringLiteral("diff"), text}};
                if (truncated)
                    o.insert(QStringLiteral("truncated"), true);
                ctx.ok(o);
            };
            auto failed = [ctx, guard](const QString &error) {
                guard->deleteLater();
                ctx.fail(422, error);
            };
            QTimer::singleShot(60000, guard, [failed] { failed(QStringLiteral("git did not answer within 60 s")); });
            if (!ctx.has(QStringLiteral("path"))) {
                // Nothing tracked changed (untracked files are not in a diff):
                // an empty diff, not the Commit panel's "No changes" error.
                GitController *git = t.git;
                QObject::connect(git, &GitController::fullDiffReady, guard, answer);
                QObject::connect(git, &GitController::fullDiffFailed, guard, [answer, failed, git](const QString &error) {
                    for (const GitStatusEntry &e : git->statusModel()->allEntries()) {
                        if (e.section != GitStatusEntry::Untracked) {
                            failed(error);
                            return;
                        }
                    }
                    answer({});
                });
                t.git->requestFullDiff();
                return;
            }
            const QString path = repoPaths(t.git, {ctx.str(QStringLiteral("path"))}).constFirst();
            const bool staged = ctx.flag(QStringLiteral("staged"));
            static quint64 nextToken = 0x0C0'0000'0000ULL;
            const quint64 token = ++nextToken;
            QString orig;
            for (const GitStatusEntry &e : t.git->statusModel()->allEntries()) {
                if (e.relPath == path && e.stagedSide == staged)
                    orig = e.origRelPath;
            }
            QObject::connect(t.git, &GitController::diffReady, guard,
                             [answer, token](const QString &, bool, const QByteArray &diff, quint64 tk) {
                                 if (tk == token)
                                     answer(diff);
                             });
            QObject::connect(t.git, &GitController::diffFailed, guard,
                             [failed, token](const QString &, bool, const QString &message, quint64 tk) {
                                 if (tk == token)
                                     failed(message);
                             });
            t.git->requestDiff(path, staged, orig, false, token);
        });

    add(QStringLiteral("git.log"), QStringLiteral("Read recent commits of the current branch (or a ref)"), false,
        {optional(QStringLiteral("limit"), ArgType::Int, QStringLiteral("how many; default 30")),
         optional(QStringLiteral("ref"), ArgType::String, QStringLiteral("branch, tag or sha; default HEAD")),
         optional(QStringLiteral("path"), ArgType::String, QStringLiteral("only commits touching this path")), ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString repo = t.git->currentRepo();
            const qint64 limit = std::clamp<qint64>(ctx.integer(QStringLiteral("limit"), 30), 1, 1000);
            QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("log"), QStringLiteral("-n"), QString::number(limit),
                             QStringLiteral("--format=%H%x1f%h%x1f%an%x1f%aI%x1f%s%x1e")};
            const QString ref = ctx.str(QStringLiteral("ref"));
            if (!ref.isEmpty()) {
                if (ref.startsWith(QLatin1Char('-'))) {
                    ctx.fail(400, QStringLiteral("`ref` cannot start with '-'"));
                    return;
                }
                argv.append(ref);
            }
            if (ctx.has(QStringLiteral("path"))) {
                argv.append(QStringLiteral("--"));
                argv.append(repoPaths(t.git, {ctx.str(QStringLiteral("path"))}));
            }
            IGitProcessRunner *runner = GitRunnerFactory::createForRepo(t.git->runnerScope(), t.git);
            runner->run(repo, argv, {}, 30000, false, [ctx, runner](int exit, const QByteArray &out, const QByteArray &err) {
                runner->asQObject()->deleteLater();
                if (exit != 0) {
                    ctx.fail(422, QString::fromUtf8(err).trimmed());
                    return;
                }
                QJsonArray commits;
                for (const QByteArray &record : out.split('\x1e')) {
                    const QList<QByteArray> f = record.trimmed().split('\x1f');
                    if (f.size() < 5)
                        continue;
                    commits.append(QJsonObject{{QStringLiteral("sha"), QString::fromLatin1(f[0])},
                                               {QStringLiteral("short"), QString::fromLatin1(f[1])},
                                               {QStringLiteral("author"), QString::fromUtf8(f[2])},
                                               {QStringLiteral("date"), QString::fromLatin1(f[3])},
                                               {QStringLiteral("subject"), QString::fromUtf8(f[4])}});
                }
                ctx.ok({{QStringLiteral("commits"), commits}});
            });
        });

    add(QStringLiteral("git.showDiff"), QStringLiteral("Open a changed file's diff in a diff tab"), true,
        {required(QStringLiteral("path"), ArgType::String, QStringLiteral("changed file from git.status")),
         optional(QStringLiteral("staged"), ArgType::Bool, QStringLiteral("show the staged side")), ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString path = repoPaths(t.git, {ctx.str(QStringLiteral("path"))}).constFirst();
            const bool staged = ctx.flag(QStringLiteral("staged"));
            const GitStatusEntries entries = t.git->statusModel()->allEntries();
            const auto it = std::find_if(entries.cbegin(), entries.cend(), [&](const GitStatusEntry &e) {
                return e.relPath == path && (e.section == GitStatusEntry::Staged) == staged;
            });
            if (it == entries.cend()) {
                ctx.fail(404, QStringLiteral("%1 has no %2 changes; git.status lists the changed files")
                                  .arg(path, staged ? QStringLiteral("staged") : QStringLiteral("unstaged")));
                return;
            }
            t.dock->showGitDiffPreview(*it);
            ctx.ok({{QStringLiteral("path"), path}});
        });

    add(QStringLiteral("git.showCommit"), QStringLiteral("Open a commit's details and diff in a tab"), true,
        {required(QStringLiteral("sha"), ArgType::String, QStringLiteral("commit sha (from git.log)")), ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString sha = ctx.str(QStringLiteral("sha")).trimmed();
            static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{4,64}$"));
            if (!hex.match(sha).hasMatch()) {
                ctx.fail(400, QStringLiteral("'%1' is not a commit sha").arg(sha));
                return;
            }
            t.dock->showGitCommitDetail(sha.toLatin1());
            ctx.ok({{QStringLiteral("sha"), sha}});
        });

    add(QStringLiteral("git.generateMessage"),
        QStringLiteral("Generate a commit message with AI into the commit box, as its AI button does (from the staged "
                       "changes, else all); answers with the message"),
        true, {ws}, [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            ai::CommitMessageGenerator *gen = w->app->getCommitMessageGenerator();
            if (!gen) {
                ctx.fail(409, QStringLiteral("AI commit messages are not available in this build"));
                return;
            }
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            QString why;
            if (!t.tab->generateCommitMessage(&why)) {
                if (why.contains(QLatin1String("URL")) || why.contains(QLatin1String("model")))
                    why += QStringLiteral(" (settings.set commitMessageProviderUrl and commitMessageModel)");
                else if (why.contains(QLatin1String("API key")))
                    why += QStringLiteral(" (the user enters the key there; ask them)");
                ctx.fail(409, why);
                return;
            }
            struct State
            {
                bool started = false;
                bool done = false;
            };
            auto state = std::make_shared<State>();
            auto *guard = new QObject(t.tab);
            QPointer<GitTabWidget> tab(t.tab);
            auto finish = [ctx, state, guard](int status, const QString &error, const QString &message) {
                if (state->done)
                    return;
                state->done = true;
                guard->deleteLater();
                if (status == 200)
                    ctx.ok({{QStringLiteral("message"), message}});
                else
                    ctx.fail(status, error);
            };
            using GenState = ai::CommitMessageGenerator::State;
            QObject::connect(gen, &ai::CommitMessageGenerator::stateChanged, guard, [state, finish, tab](GenState s) {
                if (s == GenState::Authenticating || s == GenState::Streaming) {
                    state->started = true;
                } else if (s == GenState::Idle && state->started) {
                    const QString message = tab ? tab->composerMessage() : QString();
                    if (message.trimmed().isEmpty())
                        finish(422, QStringLiteral("the generation was cancelled"), QString());
                    else
                        finish(200, QString(), message);
                }
            });
            QObject::connect(gen, &ai::CommitMessageGenerator::errorOccurred, guard,
                             [finish](const QString &error) { finish(422, error, QString()); });
            QObject::connect(t.git, &GitController::fullDiffFailed, guard,
                             [finish](const QString &error) { finish(422, error, QString()); });
            QTimer::singleShot(kLocalTimeoutMs, guard, [finish] {
                finish(504, QStringLiteral("the AI did not answer within 2 minutes"), QString());
            });
        });

    add(QStringLiteral("git.setUpstream"),
        QStringLiteral("Set the remote branch a local branch tracks (pull, push and ahead/behind use it)"), true,
        {optional(QStringLiteral("branch"), ArgType::String, QStringLiteral("local branch; default the current one")),
         optional(QStringLiteral("remote"), ArgType::String,
                  QStringLiteral("remote name; default origin (or the only remote)")),
         optional(QStringLiteral("remoteBranch"), ArgType::String,
                  QStringLiteral("branch on the remote; default the same name as `branch`")),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString branch = ctx.str(QStringLiteral("branch"), t.git->currentBranch());
            if (branch.isEmpty()) {
                ctx.fail(409, QStringLiteral("HEAD is detached; pass `branch`"),
                         {{QStringLiteral("needs"), QStringLiteral("branch")}});
                return;
            }
            if (!t.git->branchesLocal().contains(branch)) {
                ctx.fail(404, QStringLiteral("no local branch '%1'; git.branches lists them").arg(branch));
                return;
            }
            const QStringList remotes = t.git->remotes();
            if (remotes.isEmpty()) {
                ctx.fail(409, QStringLiteral("the repository has no remote"));
                return;
            }
            QString remote = ctx.str(QStringLiteral("remote"));
            if (remote.isEmpty())
                remote = remotes.size() == 1 ? remotes.constFirst() : QStringLiteral("origin");
            if (!remotes.contains(remote)) {
                ctx.fail(404, QStringLiteral("no remote '%1'; the remotes are: %2")
                                  .arg(remote, remotes.join(QStringLiteral(", "))));
                return;
            }
            const QString ref = remote + QLatin1Char('/') + ctx.str(QStringLiteral("remoteBranch"), branch);
            if (!t.git->branchesRemote().contains(ref)) {
                ctx.fail(404, QStringLiteral("%1 does not exist (as last fetched); git.fetch first, or git.push "
                                             "{setUpstream: true} creates it")
                                  .arg(ref));
                return;
            }
            const QString local = branch == t.git->currentBranch() ? QString() : branch;
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            runOp(ctx, t, w->m_gitOpMgr, [t, ref, local] { t.git->setUpstream(ref, local); });
        });

    add(QStringLiteral("git.rebaseInteractive"),
        QStringLiteral("Rebase the current branch onto another with an edited todo list (pick, reword, edit, squash, "
                       "fixup, drop), as the interactive rebase dialog does; local workspaces only"),
        true,
        {required(QStringLiteral("onto"), ArgType::String, QStringLiteral("branch or commit to rebase onto")),
         optional(QStringLiteral("todo"), ArgType::Array,
                  QStringLiteral("[{action: pick|reword|edit|squash|fixup|drop, sha}] oldest first; without it the "
                                 "answer is 409 with the default list (every commit picked) to edit")),
         optional(QStringLiteral("messages"), ArgType::Object,
                  QStringLiteral("{sha: message} for reword and squash commits; others keep their message")),
         ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            GitOperationManager *ops = w->m_gitOpMgr;
            const QString repo = t.git->currentRepo();
            if (GitRunnerFactory::isRemotePath(t.git->runnerScope()) || GitRunnerFactory::isRemotePath(repo)) {
                ctx.fail(409, QStringLiteral("interactive rebase runs only in local workspaces; git.rebase works here"));
                return;
            }
            if (ops->state(repo) != GitOperationManager::OperationState::Idle) {
                ctx.fail(409, QStringLiteral("a merge or rebase is already in progress; git.operation continues or "
                                             "aborts it"));
                return;
            }
            if (ops->hasEditorScript()) {
                ctx.fail(409, QStringLiteral("another interactive rebase is running"));
                return;
            }
            const QString onto = ctx.str(QStringLiteral("onto")).trimmed();
            if (onto.isEmpty() || onto.startsWith(QLatin1Char('-'))) {
                ctx.fail(400, QStringLiteral("`onto` must be a branch or commit"));
                return;
            }
            if (!ctx.has(QStringLiteral("todo"))) {
                runGit(t.git, repo,
                       {QStringLiteral("-C"), repo, QStringLiteral("log"), QStringLiteral("--reverse"),
                        QStringLiteral("--format=%H%x1f%s%x1e"), onto + QStringLiteral("..HEAD")},
                       30000, [ctx, onto](int exit, const QByteArray &out, const QByteArray &err) {
                           if (exit != 0) {
                               ctx.fail(422, QString::fromUtf8(err).trimmed());
                               return;
                           }
                           QJsonArray todo;
                           for (const QByteArray &record : out.split('\x1e')) {
                               const QList<QByteArray> f = record.trimmed().split('\x1f');
                               if (f.size() < 2)
                                   continue;
                               todo.append(QJsonObject{{QStringLiteral("action"), QStringLiteral("pick")},
                                                       {QStringLiteral("sha"), QString::fromLatin1(f[0])},
                                                       {QStringLiteral("subject"), QString::fromUtf8(f[1])}});
                           }
                           if (todo.isEmpty()) {
                               ctx.fail(409, QStringLiteral("HEAD has no commits after %1: nothing to rebase").arg(onto));
                               return;
                           }
                           ctx.fail(409,
                                    QStringLiteral("pass `todo`: the list below picks every commit after %1 (oldest "
                                                   "first); change actions, reorder or drop entries")
                                        .arg(onto),
                                    {{QStringLiteral("needs"), QStringLiteral("todo")}, {QStringLiteral("todo"), todo}});
                       });
                return;
            }
            static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{4,64}$"));
            QByteArray todoText;
            const QJsonArray todo = ctx.array(QStringLiteral("todo"));
            for (const QJsonValue &v : todo) {
                const QJsonObject item = v.toObject();
                const QString action = item.value(QStringLiteral("action")).toString(QStringLiteral("pick"));
                const QString sha = item.value(QStringLiteral("sha")).toString().trimmed();
                if (!rebaseActions().contains(action)) {
                    ctx.fail(400, QStringLiteral("todo action '%1' is not one of %2")
                                      .arg(action, rebaseActions().join(QStringLiteral(", "))));
                    return;
                }
                if (!hex.match(sha).hasMatch()) {
                    ctx.fail(400, QStringLiteral("todo sha '%1' is not a commit sha").arg(sha));
                    return;
                }
                todoText += action.toLatin1() + ' ' + sha.toLatin1() + '\n';
            }
            if (todoText.isEmpty()) {
                ctx.fail(400, QStringLiteral("`todo` is empty; drop commits with action drop instead"));
                return;
            }
            const QJsonObject messages = ctx.object(QStringLiteral("messages"));
            ops->setEditorScript([todoText, messages](const QString &file, bool isTodoFile) {
                if (isTodoFile)
                    return writeWhole(file, todoText);
                // A reword or squash message: the commit git is on, by sha prefix.
                const QString sha = rebaseCurrentSha(QFileInfo(file).absolutePath());
                for (auto it = messages.constBegin(); !sha.isEmpty() && it != messages.constEnd(); ++it) {
                    if (sha.startsWith(it.key(), Qt::CaseInsensitive) || it.key().startsWith(sha, Qt::CaseInsensitive))
                        return writeWhole(file, it.value().toString().toUtf8() + '\n');
                }
                return true; // the message as git proposes it
            });
            // The script lives until the rebase is over: continuing after a
            // conflict stop can open the message editor too.
            auto *scriptGuard = new QObject(ops);
            QObject::connect(ops, &GitOperationManager::operationStateChanged, scriptGuard,
                             [ops, scriptGuard, repo](const QString &r, GitOperationManager::OperationState s) {
                                 if (r != repo || s != GitOperationManager::OperationState::Idle)
                                     return;
                                 ops->setEditorScript({});
                                 scriptGuard->deleteLater();
                             });
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            awaitOperation(ctx, t.git, ops);
            ops->startInteractiveRebase(t.git, onto);
        });

    add(QStringLiteral("git.worktrees"),
        QStringLiteral("List the repository's worktrees: path, branch, HEAD, which is current, locked/prunable"), false,
        {ws}, [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString main = t.git->mainWorktree().isEmpty() ? t.git->currentRepo() : t.git->mainWorktree();
            const QString current = t.git->currentRepo();
            runGit(t.git, main,
                   {QStringLiteral("-C"), main, QStringLiteral("worktree"), QStringLiteral("list"),
                    QStringLiteral("--porcelain")},
                   30000, [ctx, current](int exit, const QByteArray &out, const QByteArray &err) {
                       if (exit != 0) {
                           ctx.fail(422, QString::fromUtf8(err).trimmed());
                           return;
                       }
                       ctx.ok({{QStringLiteral("worktrees"), parseWorktrees(out, current)}});
                   });
        });

    // Removing (optionally after merging its branch into the checkout that owns
    // it), as the worktree menu does; a dirty worktree needs `force`.
    auto worktreeRemoval = [this, ws](const QString &name, const QString &summary, bool mergeFirst) {
        add(name, summary, true,
            {required(QStringLiteral("path"), ArgType::String, QStringLiteral("worktree path from git.worktrees")),
             optional(QStringLiteral("force"), ArgType::Bool,
                      QStringLiteral("remove it even with uncommitted changes (they are lost)")),
             ws},
            [w = w, mergeFirst](const CommandContext &ctx) {
                GitTarget t;
                if (!gitTarget(ctx, w, &t))
                    return;
                const GitRepoInfo *info = repoInfo(t.git, ctx.str(QStringLiteral("path")));
                if (!info || !info->isWorktree) {
                    ctx.fail(404, QStringLiteral("'%1' is not a linked worktree of this repository; git.worktrees "
                                                 "lists them")
                                      .arg(ctx.str(QStringLiteral("path"))));
                    return;
                }
                if (mergeFirst && info->branch.isEmpty()) {
                    ctx.fail(409, QStringLiteral("the worktree is detached: it has no branch to merge"));
                    return;
                }
                const QString path = info->toplevel;
                const QString branch = info->branch;
                const bool force = ctx.flag(QStringLiteral("force"));
                auto start = [ctx, t, w, path, branch, force, mergeFirst] {
                    t.dock->showGitTab();
                    ctx.reveal(t.tab);
                    runOp(ctx, t, w->m_gitOpMgr, [t, path, branch, force, mergeFirst] {
                        if (mergeFirst)
                            t.git->mergeAndRemoveWorktree(path, branch, force);
                        else
                            t.git->removeWorktree(path, force);
                    });
                };
                if (force) {
                    start();
                    return;
                }
                runGit(t.git, path,
                       {QStringLiteral("-C"), path, QStringLiteral("status"), QStringLiteral("--porcelain")}, 30000,
                       [ctx, start](int exit, const QByteArray &out, const QByteArray &err) {
                           if (exit != 0) {
                               ctx.fail(422, QString::fromUtf8(err).trimmed());
                               return;
                           }
                           if (!out.trimmed().isEmpty()) {
                               ctx.fail(409, QStringLiteral("the worktree has uncommitted changes; pass force: true to "
                                                            "remove it anyway (they are lost)"),
                                        {{QStringLiteral("needs"), QStringLiteral("force")}});
                               return;
                           }
                           start();
                       });
            });
    };
    worktreeRemoval(QStringLiteral("git.removeWorktree"), QStringLiteral("Remove a linked worktree and its working copy"),
                    false);
    worktreeRemoval(QStringLiteral("git.mergeWorktree"),
                    QStringLiteral("Merge a linked worktree's branch into the checkout that owns it, then remove the "
                                   "worktree"),
                    true);

    add(QStringLiteral("git.repos"),
        QStringLiteral("List the repositories the workspace's Git tab can act on (main, submodules, worktrees) and "
                       "which is current"),
        false, {ws}, [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const QString current = cleanRepoPath(t.git->currentRepo());
            QJsonArray repos;
            GitRepoModel *model = t.git->repoModel();
            for (int i = 0; model && i < model->rowCount(); ++i) {
                const GitRepoInfo *info = model->infoAt(i);
                if (!info)
                    continue;
                QJsonObject o{{QStringLiteral("path"), cleanRepoPath(info->toplevel)},
                              {QStringLiteral("name"), info->displayName},
                              {QStringLiteral("kind"), repoKind(*info)},
                              {QStringLiteral("depth"), info->depth},
                              {QStringLiteral("current"),
                               cleanRepoPath(info->toplevel).compare(current, Qt::CaseInsensitive) == 0}};
                if (!info->branch.isEmpty())
                    o.insert(QStringLiteral("branch"), info->branch);
                repos.append(o);
            }
            ctx.ok({{QStringLiteral("repos"), repos}, {QStringLiteral("current"), current}});
        });

    add(QStringLiteral("git.selectRepo"),
        QStringLiteral("Make a repository (main, submodule or worktree) current in the Git tab, as picking it in its "
                       "repository box does; later git.* commands act on it"),
        true, {required(QStringLiteral("path"), ArgType::String, QStringLiteral("repository path from git.repos")), ws},
        [w = w](const CommandContext &ctx) {
            GitTarget t;
            if (!gitTarget(ctx, w, &t))
                return;
            const GitRepoInfo *info = repoInfo(t.git, ctx.str(QStringLiteral("path")));
            if (!info) {
                ctx.fail(404, QStringLiteral("the Git tab does not list '%1'; git.repos lists the repositories")
                                  .arg(ctx.str(QStringLiteral("path"))));
                return;
            }
            const QString target = cleanRepoPath(info->toplevel);
            t.dock->showGitTab();
            ctx.reveal(t.tab);
            if (!t.tab->selectRepository(target)) {
                ctx.fail(422, QStringLiteral("the Git tab could not select %1").arg(target));
                return;
            }
            GitController *git = t.git;
            GitOperationManager *ops = w->m_gitOpMgr;
            auto selected = [git, target] {
                return cleanRepoPath(git->currentRepo()).compare(target, Qt::CaseInsensitive) == 0;
            };
            if (selected() && git->state() == GitController::State::Idle) {
                ctx.ok(statusJson(git, ops));
                return;
            }
            // Selecting refreshes the new repository: answer with its status.
            auto *guard = new QObject(git);
            QPointer<GitController> g(git);
            auto answer = [ctx, guard, g, ops, selected, target](bool timedOut) {
                if (!guard->parent())
                    return;
                if (!timedOut && (!g || !selected()))
                    return;
                guard->setParent(nullptr);
                guard->deleteLater();
                if (g && selected())
                    ctx.ok(statusJson(g, ops));
                else
                    ctx.fail(504, QStringLiteral("%1 did not become current within 30 s").arg(target));
            };
            QObject::connect(git, &GitController::statusRefreshed, guard, [answer] { answer(false); });
            QTimer::singleShot(30000, guard, [answer] { answer(true); });
        });
}

} // namespace control::commands
