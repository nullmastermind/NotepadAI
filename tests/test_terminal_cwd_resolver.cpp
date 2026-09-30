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
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "TerminalCwdResolver.h"
#include "remote/ExecutionContext.h"

#include <QList>

using CwdSpace = TerminalCwdResolver::CwdSpace;

class MockRemoteContext : public remote::ExecutionContext
{
public:
    bool isRemote() const override { return true; }
    QString displayName() const override { return QStringLiteral("mock"); }
    State state() const override { return State::Connected; }
    IPtyProcess *createPty(QObject *) override { return nullptr; }
    IGitProcessRunner *createGitRunner(QObject *) override { return nullptr; }
    void exec(const QString &, const QStringList &, const QByteArray &, int, ExecCallback) override {}
    remote::IFileSystemBackend *fsBackend() override { return nullptr; }
    QString resolveCwd(const QString &requested) const override { return requested; }
};


class TestTerminalCwdResolver : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void workspaceOnly_canOpenInWorkspace_isTrue();
    void workspaceOnly_canOpenInFolder_fallsBackToWorkspace();
    void workspaceOnly_resolveFolder_returnsWorkspace();

    void fileOnly_canOpenInWorkspace_isFalse();
    void fileOnly_canOpenInFolder_isTrue();
    void fileOnly_resolveFolder_returnsParent();

    void fileAndWorkspace_resolveFolder_prefersFileParent();

    void neither_allDisabled();

    void untitledBufferWithWorkspace_resolveFolder_fallsBack();

    void emptyPaths_disabled();
    void nonExistingWorkspace_disabled();
    void nonExistingFileParentWithWorkspace_fallsBack();

    void sshWorkspaceRoot_allDisabled();
    void sshFileWithLocalWorkspace_canOpenInFolder_usesWorkspace();
    void sshFileWithSshWorkspace_allDisabled();
    void remoteContext_sameProfileFile_resolveFolder_returnsFileParent();
    void remoteContext_crossProfileFile_resolveFolder_fallsBackToWorkspace();
    void cwdBelongsToWorkspace_exactRoot_isTrue();
    void cwdBelongsToWorkspace_descendant_isTrue();
    void cwdBelongsToWorkspace_siblingPrefix_isFalse();
    void cwdBelongsToWorkspace_empty_isFalse();
    void cwdBelongsToWorkspace_localCwdIgnoresSshWorkspace();
    void cwdBelongsToWorkspace_remoteCwdMatchesSshRemotePath();
    void cwdBelongsToWorkspace_remoteCwdIgnoresLocalWorkspace();
    void matchingWorkspace_nestedRoots_picksLongest();
    void matchingWorkspace_unmatched_isEmpty();
    void visibleTerminalIndices_filtersToActiveWorkspace();
    void visibleTerminalIndices_anyUnmatched_disablesFilter();
    void visibleTerminalIndices_emptyActive_showsAll();
    void closeTerminalIndices_onlyOwnedByClosing();
    void closeTerminalIndices_nestedKeepsChildOwned();
    void closeTerminalIndices_trailingSlashMatches();
    void closeTerminalIndices_emptyClosing_none();
    void closeTerminalIndices_addsClosingRootIfMissing();
    void closeTerminalIndices_remoteSshUri();
    void visibleTerminalIndices_trailingSlashActive_filters();
    void terminalTabWanted_anyUnmatched_showsEveryTab();
    void terminalTabWanted_filtered_onlyActive();
    void applyFilterTwoPass_matchesVisibleIndices();



private:
    QTemporaryDir tmp;
    QString workspacePath;
    QString filePath;
    QString fileParent;
};

void TestTerminalCwdResolver::initTestCase()
{
    QVERIFY(tmp.isValid());
    QDir root(tmp.path());

    QVERIFY(root.mkpath(QStringLiteral("workspace")));
    workspacePath = root.absoluteFilePath(QStringLiteral("workspace"));

    QVERIFY(root.mkpath(QStringLiteral("project/src")));
    fileParent = root.absoluteFilePath(QStringLiteral("project/src"));
    filePath = fileParent + QStringLiteral("/main.cpp");

    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("int main(){}\n");
    f.close();
}

void TestTerminalCwdResolver::cleanupTestCase()
{
}

void TestTerminalCwdResolver::workspaceOnly_canOpenInWorkspace_isTrue()
{
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(workspacePath), true);
    QCOMPARE(TerminalCwdResolver::resolveWorkspace(workspacePath), QDir::cleanPath(workspacePath));
}

void TestTerminalCwdResolver::workspaceOnly_canOpenInFolder_fallsBackToWorkspace()
{
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(QString(), false, workspacePath), true);
}

void TestTerminalCwdResolver::workspaceOnly_resolveFolder_returnsWorkspace()
{
    QCOMPARE(TerminalCwdResolver::resolveFolder(QString(), false, workspacePath), QDir::cleanPath(workspacePath));
}

void TestTerminalCwdResolver::fileOnly_canOpenInWorkspace_isFalse()
{
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(QString()), false);
    QCOMPARE(TerminalCwdResolver::resolveWorkspace(QString()), QString());
}

void TestTerminalCwdResolver::fileOnly_canOpenInFolder_isTrue()
{
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(filePath, true, QString()), true);
}

void TestTerminalCwdResolver::fileOnly_resolveFolder_returnsParent()
{
    QCOMPARE(TerminalCwdResolver::resolveFolder(filePath, true, QString()), QDir::cleanPath(fileParent));
}

void TestTerminalCwdResolver::fileAndWorkspace_resolveFolder_prefersFileParent()
{
    QCOMPARE(TerminalCwdResolver::resolveFolder(filePath, true, workspacePath), QDir::cleanPath(fileParent));
}

void TestTerminalCwdResolver::neither_allDisabled()
{
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(QString()), false);
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(QString(), false, QString()), false);
    QCOMPARE(TerminalCwdResolver::resolveFolder(QString(), false, QString()), QString());
}

void TestTerminalCwdResolver::untitledBufferWithWorkspace_resolveFolder_fallsBack()
{
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(QString(), false, workspacePath), true);
    QCOMPARE(TerminalCwdResolver::resolveFolder(QString(), false, workspacePath), QDir::cleanPath(workspacePath));
}

void TestTerminalCwdResolver::emptyPaths_disabled()
{
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(QStringLiteral("")), false);
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(QStringLiteral(""), true, QStringLiteral("")), false);
}

void TestTerminalCwdResolver::nonExistingWorkspace_disabled()
{
    const QString bogus = tmp.path() + QStringLiteral("/does/not/exist");
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(bogus), false);
    QCOMPARE(TerminalCwdResolver::resolveWorkspace(bogus), QString());
}

void TestTerminalCwdResolver::nonExistingFileParentWithWorkspace_fallsBack()
{
    const QString bogusFile = tmp.path() + QStringLiteral("/does/not/exist/file.txt");
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(bogusFile, true, workspacePath), true);
    QCOMPARE(TerminalCwdResolver::resolveFolder(bogusFile, true, workspacePath), QDir::cleanPath(workspacePath));
}

void TestTerminalCwdResolver::sshWorkspaceRoot_allDisabled()
{
    const QString sshRoot = QStringLiteral("ssh://myprofile/root/project");
    QCOMPARE(TerminalCwdResolver::canOpenInWorkspace(sshRoot), false);
    QCOMPARE(TerminalCwdResolver::resolveWorkspace(sshRoot), QString());
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(QString(), false, sshRoot), false);
    QCOMPARE(TerminalCwdResolver::resolveFolder(QString(), false, sshRoot), QString());
}

void TestTerminalCwdResolver::sshFileWithLocalWorkspace_canOpenInFolder_usesWorkspace()
{
    // Remote file open + local workspace → fall back to local workspace for the terminal.
    const QString sshFile = QStringLiteral("ssh://myprofile/root/project/main.cpp");
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(sshFile, true, workspacePath), true);
    QCOMPARE(TerminalCwdResolver::resolveFolder(sshFile, true, workspacePath), QDir::cleanPath(workspacePath));
}

void TestTerminalCwdResolver::sshFileWithSshWorkspace_allDisabled()
{
    const QString sshFile = QStringLiteral("ssh://myprofile/root/project/main.cpp");
    const QString sshRoot = QStringLiteral("ssh://myprofile/root/project");
    QCOMPARE(TerminalCwdResolver::canOpenInFolder(sshFile, true, sshRoot), false);
    QCOMPARE(TerminalCwdResolver::resolveFolder(sshFile, true, sshRoot), QString());
}

void TestTerminalCwdResolver::remoteContext_sameProfileFile_resolveFolder_returnsFileParent()
{
    MockRemoteContext ctx;
    const QString sshFile = QStringLiteral("ssh://profileA/root/project/src/main.cpp");
    const QString sshRoot = QStringLiteral("ssh://profileA/root/project");
    QCOMPARE(TerminalCwdResolver::resolveFolderForContext(&ctx, sshFile, true, sshRoot), QStringLiteral("/root/project/src"));
    QCOMPARE(TerminalCwdResolver::canOpenInFolderForContext(&ctx, sshFile, true, sshRoot), true);
}

void TestTerminalCwdResolver::remoteContext_crossProfileFile_resolveFolder_fallsBackToWorkspace()
{
    MockRemoteContext ctx;
    const QString sshFile = QStringLiteral("ssh://profileB/other/project/src/main.cpp");
    const QString sshRoot = QStringLiteral("ssh://profileA/root/project");
    QCOMPARE(TerminalCwdResolver::resolveFolderForContext(&ctx, sshFile, true, sshRoot), QStringLiteral("/root/project"));
    QCOMPARE(TerminalCwdResolver::canOpenInFolderForContext(&ctx, sshFile, true, sshRoot), true);
}

void TestTerminalCwdResolver::cwdBelongsToWorkspace_exactRoot_isTrue()
{
    QCOMPARE(TerminalCwdResolver::cwdBelongsToWorkspace(
                 QStringLiteral("/ws/a"), CwdSpace::Local, QStringLiteral("/ws/a")),
             true);
}

void TestTerminalCwdResolver::cwdBelongsToWorkspace_descendant_isTrue()
{
    QCOMPARE(TerminalCwdResolver::cwdBelongsToWorkspace(
                 QStringLiteral("/ws/a/src"), CwdSpace::Local, QStringLiteral("/ws/a")),
             true);
}

void TestTerminalCwdResolver::cwdBelongsToWorkspace_siblingPrefix_isFalse()
{
    QCOMPARE(TerminalCwdResolver::cwdBelongsToWorkspace(
                 QStringLiteral("/ws/ab"), CwdSpace::Local, QStringLiteral("/ws/a")),
             false);
}

void TestTerminalCwdResolver::cwdBelongsToWorkspace_empty_isFalse()
{
    QCOMPARE(TerminalCwdResolver::cwdBelongsToWorkspace(
                 QString(), CwdSpace::Local, QStringLiteral("/ws/a")),
             false);
    QCOMPARE(TerminalCwdResolver::cwdBelongsToWorkspace(
                 QStringLiteral("/ws/a"), CwdSpace::Local, QString()),
             false);
}

void TestTerminalCwdResolver::cwdBelongsToWorkspace_localCwdIgnoresSshWorkspace()
{
    QCOMPARE(TerminalCwdResolver::cwdBelongsToWorkspace(
                 QStringLiteral("/opt/app"), CwdSpace::Local,
                 QStringLiteral("ssh://prod/opt/app")),
             false);
}

void TestTerminalCwdResolver::cwdBelongsToWorkspace_remoteCwdMatchesSshRemotePath()
{
    QCOMPARE(TerminalCwdResolver::cwdBelongsToWorkspace(
                 QStringLiteral("/opt/app"), CwdSpace::Remote,
                 QStringLiteral("ssh://prod/opt/app")),
             true);
    QCOMPARE(TerminalCwdResolver::cwdBelongsToWorkspace(
                 QStringLiteral("/opt/app/src"), CwdSpace::Remote,
                 QStringLiteral("ssh://prod/opt/app")),
             true);
}

void TestTerminalCwdResolver::cwdBelongsToWorkspace_remoteCwdIgnoresLocalWorkspace()
{
    QCOMPARE(TerminalCwdResolver::cwdBelongsToWorkspace(
                 QStringLiteral("/opt/app"), CwdSpace::Remote, QStringLiteral("/opt/app")),
             false);
}

void TestTerminalCwdResolver::matchingWorkspace_nestedRoots_picksLongest()
{
    const QStringList roots{QStringLiteral("/ws"), QStringLiteral("/ws/nested")};
    QCOMPARE(TerminalCwdResolver::matchingWorkspace(
                 QStringLiteral("/ws/nested/src"), CwdSpace::Local, roots),
             QStringLiteral("/ws/nested"));
}

void TestTerminalCwdResolver::matchingWorkspace_unmatched_isEmpty()
{
    const QStringList roots{QStringLiteral("/ws/a"), QStringLiteral("/ws/b")};
    QCOMPARE(TerminalCwdResolver::matchingWorkspace(
                 QStringLiteral("/orphan"), CwdSpace::Local, roots),
             QString());
}


void TestTerminalCwdResolver::visibleTerminalIndices_filtersToActiveWorkspace()
{
    const QString wsA = QStringLiteral("/ws/a");
    const QString wsB = QStringLiteral("/ws/b");
    const QStringList cwds{
        QStringLiteral("/ws/a"),
        QStringLiteral("/ws/a/src"),
        QStringLiteral("/ws/b"),
        QStringLiteral("/ws/b/pkg"),
    };
    const QList<CwdSpace> spaces{CwdSpace::Local, CwdSpace::Local, CwdSpace::Local, CwdSpace::Local};
    const QStringList roots{wsA, wsB};
    QCOMPARE(TerminalCwdResolver::visibleTerminalIndices(cwds, spaces, roots, wsA),
             (QList<int>{0, 1}));
    QCOMPARE(TerminalCwdResolver::visibleTerminalIndices(cwds, spaces, roots, wsB),
             (QList<int>{2, 3}));
}

void TestTerminalCwdResolver::visibleTerminalIndices_anyUnmatched_disablesFilter()
{
    const QString wsA = QStringLiteral("/ws/a");
    const QString wsB = QStringLiteral("/ws/b");
    const QStringList cwds{
        QStringLiteral("/ws/a/t"),
        QStringLiteral("/ws/b/t"),
        QStringLiteral("/orphan"),
    };
    const QList<CwdSpace> spaces{CwdSpace::Local, CwdSpace::Local, CwdSpace::Local};
    const QStringList roots{wsA, wsB};
    QCOMPARE(TerminalCwdResolver::visibleTerminalIndices(cwds, spaces, roots, wsA),
             (QList<int>{0, 1, 2}));
}

void TestTerminalCwdResolver::visibleTerminalIndices_emptyActive_showsAll()
{
    const QStringList cwds{QStringLiteral("/ws/a/t"), QStringLiteral("/ws/b/t")};
    const QList<CwdSpace> spaces{CwdSpace::Local, CwdSpace::Local};
    const QStringList roots{QStringLiteral("/ws/a"), QStringLiteral("/ws/b")};
    QCOMPARE(TerminalCwdResolver::visibleTerminalIndices(cwds, spaces, roots, QString()),
             (QList<int>{0, 1}));
}

void TestTerminalCwdResolver::closeTerminalIndices_onlyOwnedByClosing()
{
    const QStringList cwds{
        QStringLiteral("/ws/a"),
        QStringLiteral("/ws/a/src"),
        QStringLiteral("/ws/b"),
        QStringLiteral("/orphan"),
    };
    const QList<CwdSpace> spaces{CwdSpace::Local, CwdSpace::Local, CwdSpace::Local, CwdSpace::Local};
    const QStringList roots{QStringLiteral("/ws/a"), QStringLiteral("/ws/b")};
    QCOMPARE(TerminalCwdResolver::closeTerminalIndices(
                 cwds, spaces, roots, QStringLiteral("/ws/a")),
             (QList<int>{0, 1}));
}

void TestTerminalCwdResolver::closeTerminalIndices_nestedKeepsChildOwned()
{
    const QStringList cwds{
        QStringLiteral("/ws/src"),
        QStringLiteral("/ws/nested/src"),
    };
    const QList<CwdSpace> spaces{CwdSpace::Local, CwdSpace::Local};
    const QStringList roots{QStringLiteral("/ws"), QStringLiteral("/ws/nested")};
    QCOMPARE(TerminalCwdResolver::closeTerminalIndices(
                 cwds, spaces, roots, QStringLiteral("/ws")),
             (QList<int>{0}));
    QCOMPARE(TerminalCwdResolver::closeTerminalIndices(
                 cwds, spaces, roots, QStringLiteral("/ws/nested")),
             (QList<int>{1}));
}

void TestTerminalCwdResolver::closeTerminalIndices_trailingSlashMatches()
{
    const QStringList cwds{QStringLiteral("/ws/a/src")};
    const QList<CwdSpace> spaces{CwdSpace::Local};
    const QStringList roots{QStringLiteral("/ws/a")};
    QCOMPARE(TerminalCwdResolver::closeTerminalIndices(
                 cwds, spaces, roots, QStringLiteral("/ws/a/")),
             (QList<int>{0}));
}

void TestTerminalCwdResolver::closeTerminalIndices_emptyClosing_none()
{
    const QStringList cwds{QStringLiteral("/ws/a/t")};
    const QList<CwdSpace> spaces{CwdSpace::Local};
    const QStringList roots{QStringLiteral("/ws/a")};
    QCOMPARE(TerminalCwdResolver::closeTerminalIndices(cwds, spaces, roots, QString()),
             QList<int>());
}

void TestTerminalCwdResolver::closeTerminalIndices_addsClosingRootIfMissing()
{
    const QStringList cwds{QStringLiteral("/ws/a/src"), QStringLiteral("/ws/b/src")};
    const QList<CwdSpace> spaces{CwdSpace::Local, CwdSpace::Local};
    const QStringList roots{QStringLiteral("/ws/b")};
    QCOMPARE(TerminalCwdResolver::closeTerminalIndices(
                 cwds, spaces, roots, QStringLiteral("/ws/a")),
             (QList<int>{0}));
}

void TestTerminalCwdResolver::closeTerminalIndices_remoteSshUri()
{
    const QStringList cwds{QStringLiteral("/opt/app/src"), QStringLiteral("/opt/other")};
    const QList<CwdSpace> spaces{CwdSpace::Remote, CwdSpace::Remote};
    const QStringList roots{QStringLiteral("ssh://prod/opt/app")};
    QCOMPARE(TerminalCwdResolver::closeTerminalIndices(
                 cwds, spaces, roots, QStringLiteral("ssh://prod/opt/app/")),
             (QList<int>{0}));
}

void TestTerminalCwdResolver::visibleTerminalIndices_trailingSlashActive_filters()
{
    const QStringList cwds{QStringLiteral("/ws/a/t"), QStringLiteral("/ws/b/t")};
    const QList<CwdSpace> spaces{CwdSpace::Local, CwdSpace::Local};
    const QStringList roots{QStringLiteral("/ws/a"), QStringLiteral("/ws/b")};
    QCOMPARE(TerminalCwdResolver::visibleTerminalIndices(
                 cwds, spaces, roots, QStringLiteral("/ws/a/")),
             (QList<int>{0}));
}

void TestTerminalCwdResolver::terminalTabWanted_anyUnmatched_showsEveryTab()
{
    const QStringList roots{QStringLiteral("/ws/a"), QStringLiteral("/ws/b")};
    const QString active = QStringLiteral("/ws/a");
    QCOMPARE(TerminalCwdResolver::terminalTabWanted(
                 QStringLiteral("/ws/a/t"), CwdSpace::Local, roots, active, true),
             true);
    QCOMPARE(TerminalCwdResolver::terminalTabWanted(
                 QStringLiteral("/ws/b/t"), CwdSpace::Local, roots, active, true),
             true);
    QCOMPARE(TerminalCwdResolver::terminalTabWanted(
                 QStringLiteral("/orphan"), CwdSpace::Local, roots, active, true),
             true);
}

void TestTerminalCwdResolver::terminalTabWanted_filtered_onlyActive()
{
    const QStringList roots{QStringLiteral("/ws/a"), QStringLiteral("/ws/b")};
    const QString active = QStringLiteral("/ws/a");
    QCOMPARE(TerminalCwdResolver::terminalTabWanted(
                 QStringLiteral("/ws/a/t"), CwdSpace::Local, roots, active, false),
             true);
    QCOMPARE(TerminalCwdResolver::terminalTabWanted(
                 QStringLiteral("/ws/b/t"), CwdSpace::Local, roots, active, false),
             false);
    QCOMPARE(TerminalCwdResolver::terminalTabWanted(
                 QStringLiteral("/orphan"), CwdSpace::Local, roots, active, false),
             false);
}

void TestTerminalCwdResolver::applyFilterTwoPass_matchesVisibleIndices()
{
    const QStringList roots{QStringLiteral("/ws/a"), QStringLiteral("/ws/b")};
    const struct {
        QStringList cwds;
        QString active;
    } cases[] = {
        {{QStringLiteral("/ws/a/t"), QStringLiteral("/ws/b/t")}, QStringLiteral("/ws/a")},
        {{QStringLiteral("/ws/a/t"), QStringLiteral("/ws/b/t"), QStringLiteral("/orphan")},
         QStringLiteral("/ws/a")},
        {{QStringLiteral("/ws/a/t"), QStringLiteral("/ws/b/t")}, QString()},
    };
    for (const auto &c : cases) {
        QList<CwdSpace> spaces;
        spaces.reserve(c.cwds.size());
        for (int i = 0; i < c.cwds.size(); ++i)
            spaces.append(CwdSpace::Local);

        bool anyUnmatched = c.active.isEmpty() || roots.isEmpty();
        if (!anyUnmatched) {
            for (int i = 0; i < c.cwds.size(); ++i) {
                if (TerminalCwdResolver::matchingWorkspace(c.cwds.at(i), spaces.at(i), roots)
                        .isEmpty()) {
                    anyUnmatched = true;
                    break;
                }
            }
        }
        QList<int> wants;
        wants.reserve(c.cwds.size());
        for (int i = 0; i < c.cwds.size(); ++i) {
            if (TerminalCwdResolver::terminalTabWanted(
                    c.cwds.at(i), spaces.at(i), roots, c.active, anyUnmatched))
                wants.append(i);
        }
        QCOMPARE(wants, TerminalCwdResolver::visibleTerminalIndices(
                            c.cwds, spaces, roots, c.active));
    }
}



QTEST_APPLESS_MAIN(TestTerminalCwdResolver)
#include "test_terminal_cwd_resolver.moc"
