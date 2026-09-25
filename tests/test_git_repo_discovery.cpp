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

#include <QtTest>

#include "GitErrorClassifier.h"
#include "GitRepoDiscovery.h"
#include "GitRepoModel.h"

class TestGitRepoDiscovery : public QObject
{
    Q_OBJECT

private slots:
    void claudeWorktree_isListed();
    void linkedWorktree_isListed();
    void empty_returnsEmpty();
    void multipleClaudeWorktrees_allListed();
    void crlf_isAccepted();
    void linkedWorktree_carriesBranch();
    void primaryListed_whenCurrentIsLinked();
    void detached_hasEmptyBranch();
    void fallbackRepo_switchesWhenCurrentMissing();
    void fallbackRepo_externalDelete_switchesToMain();
    void parseMainWorktreePath_isFirstRecord();
    void parseWorktrees_exposesMainBranch();
    void linkedWorktree_comboLabelDistinguishes();
    void removeArgv_runsFromMainCwd();
    void mergeArgv_targetsMainCwd();
    void nextRepo_afterRemovingCurrent_isMain();
    void nextRepo_afterMergeConflict_isMain();
    void nextRepo_dirtyRemove_staysOnCurrent();
    void classify_dirtyWorktreeRemove();
    void linkedWorktree_gitOpsCwdIsToplevel();
    void worktreeFailure_dirtyDoesNotSwitch();
    void worktreeRegistryDir_isGitRegistry();
    void worktreesUnchanged_detectsNewWorktree();
    void createdAfterStartup_listsOutsideAndClaudePorcelain();
    void classify_lockedWorktreeRemove_doesNotSwitch();
};

void TestGitRepoDiscovery::claudeWorktree_isListed()
{
    // A checkout under <root>/.claude/worktrees/ missing from the repo combo.
    const QByteArray out =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/repo/.claude/worktrees/feat\n"
        "HEAD fedcba9876543210fedcba9876543210fedcba98\n"
        "branch refs/heads/feat\n";

    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(out, QStringLiteral("C:/repo"));
    QCOMPARE(r.size(), 1);
    QCOMPARE(r.at(0).toplevel, QStringLiteral("C:/repo/.claude/worktrees/feat"));
    QCOMPARE(r.at(0).displayName, QStringLiteral("feat"));
    QCOMPARE(r.at(0).depth, 1);
    QVERIFY(!r.at(0).isSubmodule);
}

void TestGitRepoDiscovery::linkedWorktree_isListed()
{
    // Any linked worktree from `git worktree list` must appear, not only
    // checkouts under .claude/worktrees/.
    const QByteArray out =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/wt/other\n"
        "HEAD aaaabbbbccccddddeeeeffff0000111122223333\n"
        "branch refs/heads/other\n"
        "\n"
        "worktree C:/repo/.claude/worktrees/feat\n"
        "HEAD fedcba9876543210fedcba9876543210fedcba98\n"
        "branch refs/heads/feat\n";

    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(out, QStringLiteral("C:/repo"));
    QCOMPARE(r.size(), 2);
    QCOMPARE(r.at(0).toplevel, QStringLiteral("C:/wt/other"));
    QCOMPARE(r.at(0).displayName, QStringLiteral("other"));
    QVERIFY(r.at(0).isWorktree);
    QCOMPARE(r.at(1).displayName, QStringLiteral("feat"));
    QVERIFY(r.at(1).isWorktree);
}

void TestGitRepoDiscovery::empty_returnsEmpty()
{
    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(QByteArray(), QStringLiteral("C:/repo"));
    QCOMPARE(r.size(), 0);
}

void TestGitRepoDiscovery::multipleClaudeWorktrees_allListed()
{
    const QByteArray out =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/repo/.claude/worktrees/feat\n"
        "HEAD aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "branch refs/heads/feat\n"
        "\n"
        "worktree C:/repo/.claude/worktrees/fix\n"
        "HEAD bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
        "branch refs/heads/fix\n";

    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(out, QStringLiteral("C:/repo"));
    QCOMPARE(r.size(), 2);
    QCOMPARE(r.at(0).displayName, QStringLiteral("feat"));
    QCOMPARE(r.at(0).toplevel, QStringLiteral("C:/repo/.claude/worktrees/feat"));
    QCOMPARE(r.at(1).displayName, QStringLiteral("fix"));
    QCOMPARE(r.at(1).toplevel, QStringLiteral("C:/repo/.claude/worktrees/fix"));
}

void TestGitRepoDiscovery::crlf_isAccepted()
{
    const QByteArray out =
        "worktree C:/repo\r\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\r\n"
        "branch refs/heads/master\r\n"
        "\r\n"
        "worktree C:/repo/.claude/worktrees/feat\r\n"
        "HEAD fedcba9876543210fedcba9876543210fedcba98\r\n"
        "branch refs/heads/feat\r\n";

    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(out, QStringLiteral("C:/repo"));
    QCOMPARE(r.size(), 1);
    QCOMPARE(r.at(0).displayName, QStringLiteral("feat"));
    QCOMPARE(r.at(0).toplevel, QStringLiteral("C:/repo/.claude/worktrees/feat"));
}

void TestGitRepoDiscovery::linkedWorktree_carriesBranch()
{
    // Remove/merge actions need the worktree's branch; dropping it would
    // merge the wrong ref or disable the action incorrectly.
    const QByteArray out =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/wt/feat\n"
        "HEAD fedcba9876543210fedcba9876543210fedcba98\n"
        "branch refs/heads/feat\n";

    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(out, QStringLiteral("C:/repo"));
    QCOMPARE(r.size(), 1);
    QVERIFY(r.at(0).isWorktree);
    QCOMPARE(r.at(0).branch, QStringLiteral("feat"));
}

void TestGitRepoDiscovery::primaryListed_whenCurrentIsLinked()
{
    // Selecting a linked worktree must still offer the primary checkout so
    // Remove can switch back to it.
    const QByteArray out =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/wt/feat\n"
        "HEAD fedcba9876543210fedcba9876543210fedcba98\n"
        "branch refs/heads/feat\n";

    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(out, QStringLiteral("C:/wt/feat"));
    QCOMPARE(r.size(), 1);
    QCOMPARE(r.at(0).toplevel, QStringLiteral("C:/repo"));
    QVERIFY(!r.at(0).isWorktree);
    QCOMPARE(r.at(0).depth, 0);
    QCOMPARE(r.at(0).branch, QStringLiteral("master"));
}

void TestGitRepoDiscovery::detached_hasEmptyBranch()
{
    const QByteArray out =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/wt/det\n"
        "HEAD fedcba9876543210fedcba9876543210fedcba98\n"
        "detached\n";

    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(out, QStringLiteral("C:/repo"));
    QCOMPARE(r.size(), 1);
    QVERIFY(r.at(0).isWorktree);
    QVERIFY(r.at(0).branch.isEmpty());
}

void TestGitRepoDiscovery::fallbackRepo_switchesWhenCurrentMissing()
{
    GitRepoInfos repos;
    GitRepoInfo main;
    main.toplevel = QStringLiteral("C:/repo");
    repos.append(main);
    GitRepoInfo wt;
    wt.toplevel = QStringLiteral("C:/wt/feat");
    wt.isWorktree = true;
    repos.append(wt);

    QCOMPARE(GitRepoDiscovery::fallbackRepo(repos, QStringLiteral("C:/wt/feat"), QStringLiteral("C:/repo")),
             QStringLiteral("C:/wt/feat"));
    QCOMPARE(GitRepoDiscovery::fallbackRepo(repos, QStringLiteral("C:/wt/gone"), QStringLiteral("C:/repo")),
             QStringLiteral("C:/repo"));
    QCOMPARE(GitRepoDiscovery::fallbackRepo(repos, QStringLiteral("C:/repo"), QStringLiteral("C:/repo")),
             QStringLiteral("C:/repo"));
}

void TestGitRepoDiscovery::parseMainWorktreePath_isFirstRecord()
{
    const QByteArray out =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/wt/feat\n"
        "HEAD fedcba9876543210fedcba9876543210fedcba98\n"
        "branch refs/heads/feat\n";

    QCOMPARE(GitRepoDiscovery::parseMainWorktreePath(out), QStringLiteral("C:/repo"));
    QCOMPARE(GitRepoDiscovery::parseMainWorktreePath(QByteArray()), QString());
}

void TestGitRepoDiscovery::fallbackRepo_externalDelete_switchesToMain()
{
    GitRepoInfos repos;
    GitRepoInfo main;
    main.toplevel = QStringLiteral("C:/repo");
    repos.append(main);

    QCOMPARE(GitRepoDiscovery::fallbackRepo(repos, QStringLiteral("C:/wt/deleted"), QStringLiteral("C:/repo")),
             QStringLiteral("C:/repo"));
}

void TestGitRepoDiscovery::parseWorktrees_exposesMainBranch()
{
    const QByteArray out =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/wt/feat\n"
        "HEAD fedcba9876543210fedcba9876543210fedcba98\n"
        "branch refs/heads/feat\n";

    const auto parsed = GitRepoDiscovery::parseWorktrees(out, QStringLiteral("C:/repo"));
    QCOMPARE(parsed.mainToplevel, QStringLiteral("C:/repo"));
    QCOMPARE(parsed.mainBranch, QStringLiteral("master"));
    QCOMPARE(parsed.linked.size(), 1);
    QVERIFY(parsed.linked.at(0).isWorktree);
}

void TestGitRepoDiscovery::linkedWorktree_comboLabelDistinguishes()
{
    GitRepoInfo wt;
    wt.displayName = QStringLiteral("feat");
    wt.depth = 1;
    wt.isWorktree = true;
    GitRepoInfos repos;
    repos.append(wt);
    GitRepoModel model;
    model.setRepos(repos);
    const QString label = model.data(model.index(0), Qt::DisplayRole).toString();
    QVERIFY(label.contains(QStringLiteral("feat")));
    QVERIFY(label.contains(QStringLiteral("worktree")));
}

void TestGitRepoDiscovery::removeArgv_runsFromMainCwd()
{
    const QStringList argv = GitRepoDiscovery::worktreeRemoveArgv(
        QStringLiteral("C:/repo"), QStringLiteral("C:/wt/feat"), false);
    QCOMPARE(argv, (QStringList{
        QStringLiteral("-C"), QStringLiteral("C:/repo"),
        QStringLiteral("worktree"), QStringLiteral("remove"),
        QStringLiteral("C:/wt/feat")}));

    const QStringList forced = GitRepoDiscovery::worktreeRemoveArgv(
        QStringLiteral("C:/repo"), QStringLiteral("C:/wt/feat"), true);
    QVERIFY(forced.contains(QStringLiteral("--force")));
    QCOMPARE(forced.at(1), QStringLiteral("C:/repo"));
}

void TestGitRepoDiscovery::mergeArgv_targetsMainCwd()
{
    const QStringList argv = GitRepoDiscovery::worktreeMergeArgv(
        QStringLiteral("C:/repo"), QStringLiteral("feat"));
    QCOMPARE(argv, (QStringList{
        QStringLiteral("-C"), QStringLiteral("C:/repo"),
        QStringLiteral("merge"), QStringLiteral("--no-edit"),
        QStringLiteral("feat")}));
}

void TestGitRepoDiscovery::nextRepo_afterRemovingCurrent_isMain()
{
    QCOMPARE(GitRepoDiscovery::nextRepoAfterRemove(
                 QStringLiteral("C:/wt/feat"), QStringLiteral("C:/wt/feat"), QStringLiteral("C:/repo")),
             QStringLiteral("C:/repo"));
    QCOMPARE(GitRepoDiscovery::nextRepoAfterRemove(
                 QStringLiteral("C:/repo"), QStringLiteral("C:/wt/feat"), QStringLiteral("C:/repo")),
             QStringLiteral("C:/repo"));
}

void TestGitRepoDiscovery::nextRepo_afterMergeConflict_isMain()
{
    QCOMPARE(GitRepoDiscovery::nextRepoAfterMergeConflict(QStringLiteral("C:/repo")),
             QStringLiteral("C:/repo"));
}

void TestGitRepoDiscovery::nextRepo_dirtyRemove_staysOnCurrent()
{
    QCOMPARE(GitRepoDiscovery::nextRepoAfterFailedRemove(QStringLiteral("C:/wt/feat")),
             QStringLiteral("C:/wt/feat"));
}

void TestGitRepoDiscovery::classify_dirtyWorktreeRemove()
{
    const QByteArray err =
        "fatal: 'C:/wt/feat' contains modified or untracked files, use --force to delete it\n";
    const GitError e = GitErrorClassifier::classify(
        128, err, {QStringLiteral("-C"), QStringLiteral("C:/repo"),
                   QStringLiteral("worktree"), QStringLiteral("remove"),
                   QStringLiteral("C:/wt/feat")});
    QCOMPARE(e.kind, GitError::DirtyTree);
}

void TestGitRepoDiscovery::linkedWorktree_gitOpsCwdIsToplevel()
{
    // onRepoSelected → selectRepo(info->toplevel) → refresh/stage/commit -C that
    // path. isWorktree must not redirect ops to the primary checkout.
    const QByteArray out =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/wt/other\n"
        "HEAD aaaabbbbccccddddeeeeffff0000111122223333\n"
        "branch refs/heads/other\n";

    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(out, QStringLiteral("C:/repo"));
    QCOMPARE(r.size(), 1);
    QVERIFY(r.at(0).isWorktree);
    QCOMPARE(r.at(0).toplevel, QStringLiteral("C:/wt/other"));

    const QString cwd = r.at(0).toplevel;
    QCOMPARE(GitRepoDiscovery::statusArgv(cwd).at(2), QStringLiteral("-C"));
    QCOMPARE(GitRepoDiscovery::statusArgv(cwd).at(3), cwd);
    QCOMPARE(GitRepoDiscovery::stageArgv(cwd).at(0), QStringLiteral("-C"));
    QCOMPARE(GitRepoDiscovery::stageArgv(cwd).at(1), cwd);
    QCOMPARE(GitRepoDiscovery::commitArgv(cwd).at(2), QStringLiteral("-C"));
    QCOMPARE(GitRepoDiscovery::commitArgv(cwd).at(3), cwd);
    QCOMPARE(GitRepoDiscovery::diffArgv(cwd, false).at(2), QStringLiteral("-C"));
    QCOMPARE(GitRepoDiscovery::diffArgv(cwd, false).at(3), cwd);
    QCOMPARE(GitRepoDiscovery::diffArgv(cwd, true).at(2), QStringLiteral("-C"));
    QCOMPARE(GitRepoDiscovery::diffArgv(cwd, true).at(3), cwd);

    const QStringList remove = GitRepoDiscovery::worktreeRemoveArgv(
        QStringLiteral("C:/repo"), cwd, false);
    QCOMPARE(remove.at(1), QStringLiteral("C:/repo"));
    QCOMPARE(remove.last(), cwd);
}

void TestGitRepoDiscovery::worktreeFailure_dirtyDoesNotSwitch()
{
    QVERIFY(!GitRepoDiscovery::worktreeFailureSwitchesToMain(GitError::DirtyTree));
    QVERIFY(!GitRepoDiscovery::worktreeFailureSwitchesToMain(GitError::LockHeld));
    QVERIFY(GitRepoDiscovery::worktreeFailureSwitchesToMain(GitError::MergeConflict));
}

void TestGitRepoDiscovery::worktreeRegistryDir_isGitRegistry()
{
    QCOMPARE(GitRepoDiscovery::worktreeRegistryDir(QStringLiteral("C:/repo/.git")),
             QStringLiteral("C:/repo/.git/worktrees"));
    QCOMPARE(GitRepoDiscovery::worktreeRegistryDir(QStringLiteral("C:/repo/.git/worktrees/feat")),
             QStringLiteral("C:/repo/.git/worktrees"));
    QCOMPARE(GitRepoDiscovery::worktreeRegistryDir(QStringLiteral("C:/repo/.git/worktrees")),
             QStringLiteral("C:/repo/.git/worktrees"));
    QCOMPARE(GitRepoDiscovery::worktreeRegistryDir(QStringLiteral("D:/wt/other/.git")),
             QStringLiteral("D:/wt/other/.git/worktrees"));
}

void TestGitRepoDiscovery::worktreesUnchanged_detectsNewWorktree()
{
    GitRepoInfo root;
    root.toplevel = QStringLiteral("C:/repo");
    root.displayName = QStringLiteral("repo");

    GitRepoInfo wt;
    wt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/try-worktree");
    wt.displayName = QStringLiteral("try-worktree");
    wt.isWorktree = true;
    wt.branch = QStringLiteral("try-worktree");

    QVERIFY(GitRepoDiscovery::worktreesUnchanged({ root }, {}));
    QVERIFY(!GitRepoDiscovery::worktreesUnchanged({ root }, { wt }));
    QVERIFY(GitRepoDiscovery::worktreesUnchanged({ root, wt }, { wt }));
}

void TestGitRepoDiscovery::createdAfterStartup_listsOutsideAndClaudePorcelain()
{
    GitRepoInfo root;
    root.toplevel = QStringLiteral("C:/repo");
    root.displayName = QStringLiteral("repo");

    const QByteArray before =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n";
    const GitRepoInfos initial = GitRepoDiscovery::parseWorktreeList(before, QStringLiteral("C:/repo"));
    QCOMPARE(initial.size(), 0);
    QVERIFY(GitRepoDiscovery::worktreesUnchanged({ root }, initial));

    const QByteArray after =
        "worktree C:/repo\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree D:/wt/other\n"
        "HEAD aaaabbbbccccddddeeeeffff0000111122223333\n"
        "branch refs/heads/other\n"
        "\n"
        "worktree C:/repo/.claude/worktrees/try-worktree\n"
        "HEAD bbbbccccddddeeeeffff00001111222233334444\n"
        "branch refs/heads/try-worktree\n";
    const GitRepoInfos linked = GitRepoDiscovery::parseWorktreeList(after, QStringLiteral("C:/repo"));
    QCOMPARE(linked.size(), 2);
    QCOMPARE(linked.at(0).toplevel, QStringLiteral("D:/wt/other"));
    QVERIFY(linked.at(0).isWorktree);
    QCOMPARE(linked.at(1).toplevel, QStringLiteral("C:/repo/.claude/worktrees/try-worktree"));
    QVERIFY(linked.at(1).isWorktree);
    QVERIFY(!GitRepoDiscovery::worktreesUnchanged({ root }, linked));

    const QString outside = linked.at(0).toplevel;
    QCOMPARE(GitRepoDiscovery::statusArgv(outside).at(3), outside);
    QCOMPARE(GitRepoDiscovery::stageArgv(outside).at(1), outside);
    QCOMPARE(GitRepoDiscovery::commitArgv(outside).at(3), outside);
    QCOMPARE(GitRepoDiscovery::diffArgv(outside, false).at(3), outside);
    QCOMPARE(GitRepoDiscovery::diffArgv(outside, true).at(3), outside);

    const QString shot = linked.at(1).toplevel;
    QCOMPARE(GitRepoDiscovery::statusArgv(shot).at(3), shot);
    QCOMPARE(GitRepoDiscovery::worktreeRegistryDir(QStringLiteral("C:/repo/.git")),
             QStringLiteral("C:/repo/.git/worktrees"));
}

void TestGitRepoDiscovery::classify_lockedWorktreeRemove_doesNotSwitch()
{
    const QByteArray err =
        "fatal: 'C:/wt/feat' is locked, cannot be removed\n";
    const GitError e = GitErrorClassifier::classify(
        128, err, {QStringLiteral("-C"), QStringLiteral("C:/repo"),
                   QStringLiteral("worktree"), QStringLiteral("remove"),
                   QStringLiteral("C:/wt/feat")});
    QCOMPARE(e.kind, GitError::LockHeld);
    QCOMPARE(GitRepoDiscovery::nextRepoAfterFailedRemove(QStringLiteral("C:/wt/feat")),
             QStringLiteral("C:/wt/feat"));
    QVERIFY(!GitRepoDiscovery::worktreeFailureSwitchesToMain(e.kind));
}

QTEST_GUILESS_MAIN(TestGitRepoDiscovery)
#include "test_git_repo_discovery.moc"
