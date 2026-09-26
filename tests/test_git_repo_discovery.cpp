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

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
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
    void submoduleGitdirPrimary_isNotListed();
    void submoduleWorktree_carriesParentToplevel();
    void replaceOwnedWorktrees_keepsOtherParents();
    void replaceOwnedWorktrees_insertsAfterOwner();
    void submoduleWorktree_nestsUnderOwner();
    void worktreesUnchanged_ignoresOtherParents();
    void ownerCwd_forSubmoduleWorktree_isParent();
    void isGitDirPath_detectsModulesGitdir();
    void isWorktreeRegistryPath_isRegistryNotEntry();
    void worktreeWatchDirs_areRegistryAndParent();
    void resolveGitDir_readsGitdirFile();
    void filteredWorktrees_emptyNeedle_allWorktrees();
    void filteredWorktrees_skipsNonWorktrees();
    void filteredWorktrees_matchesNameBranchPathParent();
    void filteredWorktrees_scalesPastMenuDump();
    void worktreeMergeTargetName_usesParentBasename();
    void comboLabel_worktree_hasNoIndent();
    void indentedLabel_prefixesFigureSpaces();
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

void TestGitRepoDiscovery::submoduleGitdirPrimary_isNotListed()
{
    // `git worktree list` inside a submodule reports the primary path as the
    // gitdir under .git/modules/, not the checkout. That path must not become
    // a combo row; the linked worktree must.
    const QByteArray out =
        "worktree C:/repo/.git/modules/admin\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/repo/.claude/worktrees/admin\n"
        "HEAD fedcba9876543210fedcba9876543210fedcba98\n"
        "branch refs/heads/worktree-admin\n";

    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(out, QStringLiteral("C:/repo/admin"));
    QCOMPARE(r.size(), 1);
    QCOMPARE(r.at(0).toplevel, QStringLiteral("C:/repo/.claude/worktrees/admin"));
    QVERIFY(r.at(0).isWorktree);
    QCOMPARE(r.at(0).displayName, QStringLiteral("admin"));
    QCOMPARE(r.at(0).branch, QStringLiteral("worktree-admin"));
}

void TestGitRepoDiscovery::submoduleWorktree_carriesParentToplevel()
{
    // Replace/remove must run `git -C <submodule checkout>`, not the workspace
    // root. The linked row has to remember which checkout owns it.
    const QByteArray out =
        "worktree C:/repo/.git/modules/admin\n"
        "HEAD 0123456789abcdef0123456789abcdef01234567\n"
        "branch refs/heads/master\n"
        "\n"
        "worktree C:/repo/.claude/worktrees/admin\n"
        "HEAD fedcba9876543210fedcba9876543210fedcba98\n"
        "branch refs/heads/worktree-admin\n";

    const GitRepoInfos r = GitRepoDiscovery::parseWorktreeList(out, QStringLiteral("C:/repo/admin"));
    QCOMPARE(r.size(), 1);
    QCOMPARE(r.at(0).parentToplevel, QStringLiteral("C:/repo/admin"));
}

void TestGitRepoDiscovery::replaceOwnedWorktrees_keepsOtherParents()
{
    // Applying a submodule's worktree list must not drop the root's linked
    // worktrees (the combo would lose try-worktree when admin's list arrives).
    GitRepoInfo root;
    root.toplevel = QStringLiteral("C:/repo");
    GitRepoInfo rootWt;
    rootWt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/try");
    rootWt.isWorktree = true;
    rootWt.parentToplevel = QStringLiteral("C:/repo");
    GitRepoInfo sub;
    sub.toplevel = QStringLiteral("C:/repo/admin");
    sub.isSubmodule = true;
    sub.depth = 1;
    GitRepoInfo subWt;
    subWt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/admin");
    subWt.isWorktree = true;
    subWt.parentToplevel = QStringLiteral("C:/repo/admin");

    const GitRepoInfos merged = GitRepoDiscovery::replaceOwnedWorktrees(
        { root, rootWt, sub }, QStringLiteral("C:/repo/admin"), { subWt });

    QCOMPARE(merged.size(), 4);
    QCOMPARE(merged.at(0).toplevel, QStringLiteral("C:/repo"));
    QCOMPARE(merged.at(1).toplevel, QStringLiteral("C:/repo/.claude/worktrees/try"));
    QVERIFY(merged.at(1).isWorktree);
    QCOMPARE(merged.at(2).toplevel, QStringLiteral("C:/repo/admin"));
    QCOMPARE(merged.at(3).toplevel, QStringLiteral("C:/repo/.claude/worktrees/admin"));
    QVERIFY(merged.at(3).isWorktree);
}

void TestGitRepoDiscovery::replaceOwnedWorktrees_insertsAfterOwner()
{
    // Combo order is the UX: a submodule worktree sits under that submodule,
    // not dumped after every other checkout.
    GitRepoInfo root;
    root.toplevel = QStringLiteral("C:/repo");
    GitRepoInfo admin;
    admin.toplevel = QStringLiteral("C:/repo/admin");
    admin.isSubmodule = true;
    admin.depth = 1;
    GitRepoInfo rs;
    rs.toplevel = QStringLiteral("C:/repo/rs");
    rs.isSubmodule = true;
    rs.depth = 1;
    GitRepoInfo adminWt;
    adminWt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/admin");
    adminWt.isWorktree = true;
    adminWt.parentToplevel = QStringLiteral("C:/repo/admin");

    const GitRepoInfos merged = GitRepoDiscovery::replaceOwnedWorktrees(
        { root, admin, rs }, QStringLiteral("C:/repo/admin"), { adminWt });
    QCOMPARE(merged.size(), 4);
    QCOMPARE(merged.at(0).toplevel, QStringLiteral("C:/repo"));
    QCOMPARE(merged.at(1).toplevel, QStringLiteral("C:/repo/admin"));
    QCOMPARE(merged.at(2).toplevel, QStringLiteral("C:/repo/.claude/worktrees/admin"));
    QVERIFY(merged.at(2).isWorktree);
    QCOMPARE(merged.at(3).toplevel, QStringLiteral("C:/repo/rs"));
}

void TestGitRepoDiscovery::submoduleWorktree_nestsUnderOwner()
{
    GitRepoInfo sub;
    sub.toplevel = QStringLiteral("C:/repo/admin");
    sub.isSubmodule = true;
    sub.depth = 1;
    GitRepoInfo subWt;
    subWt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/admin");
    subWt.isWorktree = true;
    subWt.depth = 1;
    subWt.parentToplevel = QStringLiteral("C:/repo/admin");

    const GitRepoInfos merged = GitRepoDiscovery::replaceOwnedWorktrees(
        { sub }, QStringLiteral("C:/repo/admin"), { subWt });
    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(1).depth, 2);
}

void TestGitRepoDiscovery::worktreesUnchanged_ignoresOtherParents()
{
    GitRepoInfo rootWt;
    rootWt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/try");
    rootWt.isWorktree = true;
    rootWt.parentToplevel = QStringLiteral("C:/repo");
    GitRepoInfo subWt;
    subWt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/admin");
    subWt.isWorktree = true;
    subWt.parentToplevel = QStringLiteral("C:/repo/admin");

    QVERIFY(GitRepoDiscovery::worktreesUnchanged(
        { rootWt, subWt }, { subWt }, QStringLiteral("C:/repo/admin")));
    QVERIFY(!GitRepoDiscovery::worktreesUnchanged(
        { rootWt }, { subWt }, QStringLiteral("C:/repo/admin")));
}

void TestGitRepoDiscovery::ownerCwd_forSubmoduleWorktree_isParent()
{
    GitRepoInfo wt;
    wt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/admin");
    wt.isWorktree = true;
    wt.parentToplevel = QStringLiteral("C:/repo/admin");

    QCOMPARE(GitRepoDiscovery::ownerCwdForWorktree(
                 { wt }, QStringLiteral("C:/repo/.claude/worktrees/admin"), QStringLiteral("C:/repo")),
             QStringLiteral("C:/repo/admin"));
    QCOMPARE(GitRepoDiscovery::ownerCwdForWorktree(
                 {}, QStringLiteral("C:/repo/.claude/worktrees/admin"), QStringLiteral("C:/repo")),
             QStringLiteral("C:/repo"));
}

void TestGitRepoDiscovery::isGitDirPath_detectsModulesGitdir()
{
    QVERIFY(GitRepoDiscovery::isGitDirPath(QStringLiteral("C:/repo/.git/modules/admin")));
    QVERIFY(!GitRepoDiscovery::isGitDirPath(QStringLiteral("C:/repo/.claude/worktrees/admin")));
    QVERIFY(!GitRepoDiscovery::isGitDirPath(QStringLiteral("C:/repo/admin")));
}

void TestGitRepoDiscovery::isWorktreeRegistryPath_isRegistryNotEntry()
{
    QVERIFY(GitRepoDiscovery::isWorktreeRegistryPath(QStringLiteral("C:/repo/.git/worktrees")));
    QVERIFY(GitRepoDiscovery::isWorktreeRegistryPath(
        QStringLiteral("C:/repo/.git/modules/admin/worktrees")));
    QVERIFY(!GitRepoDiscovery::isWorktreeRegistryPath(
        QStringLiteral("C:/repo/.git/worktrees/feat")));
}

void TestGitRepoDiscovery::worktreeWatchDirs_areRegistryAndParent()
{
    const QStringList dirs = GitRepoDiscovery::worktreeWatchDirs(QStringLiteral("C:/repo/.git"));
    QCOMPARE(dirs, (QStringList{
        QStringLiteral("C:/repo/.git/worktrees"),
        QStringLiteral("C:/repo/.git")}));
}

void TestGitRepoDiscovery::resolveGitDir_readsGitdirFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString checkout = tmp.path() + QStringLiteral("/admin");
    QVERIFY(QDir().mkpath(checkout));
    QFile f(checkout + QStringLiteral("/.git"));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("gitdir: ../.git/modules/admin\n");
    f.close();
    QCOMPARE(GitRepoDiscovery::resolveGitDir(checkout),
             QDir::cleanPath(tmp.path() + QStringLiteral("/.git/modules/admin")));

    const QString plain = tmp.path() + QStringLiteral("/plain");
    QVERIFY(QDir().mkpath(plain + QStringLiteral("/.git")));
    QCOMPARE(GitRepoDiscovery::resolveGitDir(plain),
             QDir::cleanPath(plain + QStringLiteral("/.git")));
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

void TestGitRepoDiscovery::filteredWorktrees_emptyNeedle_allWorktrees()
{
    GitRepoInfo root;
    root.toplevel = QStringLiteral("C:/repo");
    root.displayName = QStringLiteral("repo");

    GitRepoInfo wt;
    wt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/try");
    wt.displayName = QStringLiteral("try");
    wt.isWorktree = true;
    wt.parentToplevel = QStringLiteral("C:/repo");
    wt.branch = QStringLiteral("try");

    GitRepoInfo sub;
    sub.toplevel = QStringLiteral("C:/repo/admin");
    sub.displayName = QStringLiteral("admin");
    sub.isSubmodule = true;
    sub.depth = 1;

    const GitRepoInfos hit = GitRepoDiscovery::filteredWorktrees({ root, wt, sub }, {});
    QCOMPARE(hit.size(), 1);
    QCOMPARE(hit.at(0).toplevel, wt.toplevel);
}

void TestGitRepoDiscovery::filteredWorktrees_skipsNonWorktrees()
{
    GitRepoInfo sub;
    sub.toplevel = QStringLiteral("C:/repo/admin");
    sub.displayName = QStringLiteral("admin");
    sub.isSubmodule = true;

    GitRepoInfo wt;
    wt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/admin");
    wt.displayName = QStringLiteral("admin");
    wt.isWorktree = true;
    wt.parentToplevel = QStringLiteral("C:/repo/admin");

    const GitRepoInfos hit = GitRepoDiscovery::filteredWorktrees({ sub, wt }, QStringLiteral("admin"));
    QCOMPARE(hit.size(), 1);
    QCOMPARE(hit.at(0).toplevel, wt.toplevel);
}

void TestGitRepoDiscovery::filteredWorktrees_matchesNameBranchPathParent()
{
    GitRepoInfo wt;
    wt.toplevel = QStringLiteral("C:/repo/.claude/worktrees/try-worktree");
    wt.displayName = QStringLiteral("try-worktree");
    wt.isWorktree = true;
    wt.parentToplevel = QStringLiteral("C:/repo");
    wt.branch = QStringLiteral("feat/search");

    GitRepoInfo other;
    other.toplevel = QStringLiteral("C:/repo/.claude/worktrees/other");
    other.displayName = QStringLiteral("other");
    other.isWorktree = true;
    other.parentToplevel = QStringLiteral("C:/repo/admin");
    other.branch = QStringLiteral("other");

    QCOMPARE(GitRepoDiscovery::filteredWorktrees({ wt, other }, QStringLiteral("TRY-WORKTREE")).size(), 1);
    QCOMPARE(GitRepoDiscovery::filteredWorktrees({ wt, other }, QStringLiteral("feat/search")).size(), 1);
    QCOMPARE(GitRepoDiscovery::filteredWorktrees({ wt, other }, QStringLiteral("try-worktree")).at(0).toplevel,
             wt.toplevel);
    QCOMPARE(GitRepoDiscovery::filteredWorktrees({ wt, other }, QStringLiteral("admin")).size(), 1);
    QCOMPARE(GitRepoDiscovery::filteredWorktrees({ wt, other }, QStringLiteral("admin")).at(0).toplevel,
             other.toplevel);
    QCOMPARE(GitRepoDiscovery::filteredWorktrees({ wt, other }, QStringLiteral("nope")).size(), 0);
}

void TestGitRepoDiscovery::filteredWorktrees_scalesPastMenuDump()
{
    GitRepoInfos repos;
    GitRepoInfo root;
    root.toplevel = QStringLiteral("C:/repo");
    root.displayName = QStringLiteral("repo");
    repos.append(root);
    for (int i = 0; i < 200; ++i) {
        GitRepoInfo wt;
        wt.toplevel = QStringLiteral("C:/wt/feat-%1").arg(i);
        wt.displayName = QStringLiteral("feat-%1").arg(i);
        wt.isWorktree = true;
        wt.parentToplevel = QStringLiteral("C:/repo");
        wt.branch = QStringLiteral("feat-%1").arg(i);
        repos.append(wt);
    }
    const GitRepoInfos hit = GitRepoDiscovery::filteredWorktrees(repos, QStringLiteral("feat-199"));
    QCOMPARE(hit.size(), 1);
    QCOMPARE(hit.at(0).displayName, QStringLiteral("feat-199"));
    QCOMPARE(GitRepoDiscovery::filteredWorktrees(repos, {}).size(), 200);
}

void TestGitRepoDiscovery::worktreeMergeTargetName_usesParentBasename()
{
    GitRepoInfo wt;
    wt.isWorktree = true;
    wt.displayName = QStringLiteral("context-engine-admin");
    wt.parentToplevel = QStringLiteral("C:/repo/context-engine-admin");
    QCOMPARE(GitRepoDiscovery::worktreeMergeTargetName(wt, QStringLiteral("main")),
             QStringLiteral("context-engine-admin"));

    GitRepoInfo detached;
    detached.isWorktree = true;
    detached.displayName = QStringLiteral("orphan");
    QCOMPARE(GitRepoDiscovery::worktreeMergeTargetName(detached, QStringLiteral("master")),
             QStringLiteral("master"));
}

void TestGitRepoDiscovery::comboLabel_worktree_hasNoIndent()
{
    GitRepoInfo wt;
    wt.displayName = QStringLiteral("context-engine-admin");
    wt.depth = 2;
    wt.isWorktree = true;
    GitRepoModel model;
    model.setRepos({ wt });
    const QString label = model.data(model.index(0), Qt::DisplayRole).toString();
    QVERIFY(!label.startsWith(QChar(0x2007)));
    QCOMPARE(label, QStringLiteral("context-engine-admin (worktree)"));
}

void TestGitRepoDiscovery::indentedLabel_prefixesFigureSpaces()
{
    QCOMPARE(GitRepoModel::indentedLabel(QStringLiteral("feat (worktree)"), 0),
             QStringLiteral("feat (worktree)"));
    QCOMPARE(GitRepoModel::indentedLabel(QStringLiteral("feat (worktree)"), 2),
             QString(4, QChar(0x2007)) + QStringLiteral("feat (worktree)"));
}

QTEST_GUILESS_MAIN(TestGitRepoDiscovery)
#include "test_git_repo_discovery.moc"
