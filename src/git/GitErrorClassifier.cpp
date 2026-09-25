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

#include "GitErrorClassifier.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace {

bool rx(const QString &haystack, const char *pattern)
{
    static const QRegularExpression::PatternOption opts = QRegularExpression::CaseInsensitiveOption;
    QRegularExpression re(QString::fromLatin1(pattern), opts);
    return re.match(haystack).hasMatch();
}

GitError make(GitError::Kind k, const QString &human, const QString &hint,
              const QString &details, const QStringList &actions = {})
{
    GitError e;
    e.kind = k;
    e.humanMessage = human;
    e.hint = hint;
    e.details = details;
    e.suggestedActions = actions;
    return e;
}

} // namespace

GitError GitErrorClassifier::classify(int exitCode, const QByteArray &stderr_, const QStringList &argv)
{
    const QString lowered = QString::fromUtf8(stderr_).toLower();
    QString details = QString::fromUtf8(stderr_).trimmed();

    if (details.isEmpty()) {
        details = QStringLiteral("exit code %1\ncmd: git %2")
                      .arg(exitCode)
                      .arg(argv.join(QLatin1Char(' ')));
    }

    auto tr = [](const char *s) { return QCoreApplication::translate("GitError", s); };

    if (rx(lowered, R"(not a git repository)"))
        return make(GitError::NotARepo,
                    tr("This folder is not a Git repository."),
                    tr("Run `git init` or open a folder that is one."),
                    details, {QStringLiteral("init")});

    if (rx(lowered, R"(unknown revision|does not have any commits yet|ambiguous argument 'head')"))
        return make(GitError::EmptyRepo,
                    tr("Repository has no commits yet."),
                    tr("Stage files and make the first commit."),
                    details, {QStringLiteral("initial-commit")});

    if (rx(lowered, R"(has no upstream branch|set-upstream)"))
        return make(GitError::NoUpstream,
                    tr("Current branch has no upstream."),
                    tr("Push with --set-upstream to publish it."),
                    details, {QStringLiteral("set-upstream")});

    if (rx(lowered, R"(not possible to fast-forward)"))
        return make(GitError::PullDiverged,
                    tr("Branches have diverged — fast-forward not possible."),
                    tr("Merge or rebase to reconcile."),
                    details, {QStringLiteral("pull-merge"), QStringLiteral("pull-rebase")});

    if (rx(lowered, R"(non-fast-forward|rejected.*fetch first|tip of your current branch is behind|updates were rejected)"))
        return make(GitError::NonFastForward,
                    tr("Push rejected: remote has commits you don't."),
                    tr("Pull with rebase, then push."),
                    details, {QStringLiteral("pull-rebase"), QStringLiteral("force-push")});

    if (rx(lowered, R"(merge conflict|conflict.*automatic merge|fix conflicts|could not apply|conflicts? prevent)"))
        return make(GitError::MergeConflict,
                    tr("Merge produced conflicts."),
                    tr("Resolve conflicts then `git add` the files."),
                    details, {QStringLiteral("open-conflicts")});

    if (rx(lowered, R"(authentication failed|could not read (username|password)|permission denied \(publickey\)|invalid credentials|terminal prompts disabled|fatal: authentication)"))
        return make(GitError::AuthFailed,
                    tr("Authentication failed."),
                    tr("Check credentials in the system credential manager."),
                    details, {QStringLiteral("retry")});

    if (rx(lowered, R"(could not resolve host|failed to connect|connection (timed out|refused)|ssl_error|server certificate verification failed|operation timed out)"))
        return make(GitError::NetworkFailed,
                    tr("Network error contacting the remote."),
                    tr("Check your connection and try again."),
                    details, {QStringLiteral("retry")});

    if (rx(lowered, R"(index\.lock.*exists|unable to create.*lock|another git process)"))
        return make(GitError::LockHeld,
                    tr("Another Git operation is in progress."),
                    tr("Wait or remove `.git/index.lock` manually if no other process is running."),
                    details, {QStringLiteral("retry")});

    if (rx(lowered, R"(contains modified or untracked files|use --force to delete)"))
        return make(GitError::DirtyTree,
                    tr("Worktree has uncommitted changes — retry with Force, or commit or stash first."),
                    QString(),
                    details, {QStringLiteral("retry")});

    if (rx(lowered, R"(is locked|locked, cannot be removed)"))
        return make(GitError::LockHeld,
                    tr("Worktree is locked — unlock it, or retry with Force."),
                    QString(),
                    details, {QStringLiteral("retry")});

    return make(GitError::Unknown,
                tr("Git operation failed."),
                QString(),
                details, {QStringLiteral("retry")});
}
