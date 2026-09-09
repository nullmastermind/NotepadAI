/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "GitignoreMatcher.h"
#include "ZipIgnoreWalk.h"

class TestZipIgnoreWalk : public QObject
{
    Q_OBJECT

private slots:
    void skipsGitAndNodeModules();
    void skipsBuildDebugWithoutGitignore();
    void skipsCpmCache();
    void keepsFileNamedBuildRs();
    void skipsDirectorySymlink();
    void honorsGitignoreDist();
    void honorsNestedLogIgnore();
    void emptyOrIgnoredOnly_yieldsZero();
    void nestedStar_doesNotIgnoreSiblings();
    void nestedStar_walkKeepsSiblingsAndSubmoduleFiles();
    void leadingSlash_matchesOnlyRelativeToGitignoreDir();
    void nestedNegation_doesNotUnignoreSibling();
    void submoduleGitignore_doesNotLeakToSibling();
    void walkShape_ruffCacheAndSubmodule();
    void skipsGitfile();
    void doubleStar_matchesAtAnyDepth();
    void commentOnlyGitignore_doesNotIgnore();
};

static void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

void TestZipIgnoreWalk::skipsGitAndNodeModules()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/keep.txt"), "k");
    writeFile(root + QStringLiteral("/.git/HEAD"), "ref");
    writeFile(root + QStringLiteral("/node_modules/pkg/index.js"), "js");
    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.front().entryName, QStringLiteral("keep.txt"));
}

void TestZipIgnoreWalk::skipsBuildDebugWithoutGitignore()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/src/a.cpp"), "a");
    writeFile(root + QStringLiteral("/build-debug/foo.obj"), "obj");
    writeFile(root + QStringLiteral("/build/out.exe"), "exe");
    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    QStringList names;
    for (const auto &f : files)
        names.append(f.entryName);
    QVERIFY(names.contains(QStringLiteral("src/a.cpp")));
    QVERIFY(!names.contains(QStringLiteral("build-debug/foo.obj")));
    QVERIFY(names.contains(QStringLiteral("build/out.exe")));
}

void TestZipIgnoreWalk::skipsCpmCache()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/keep.txt"), "k");
    writeFile(root + QStringLiteral("/.cpm-cache/x.bin"), "x");
    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.front().entryName, QStringLiteral("keep.txt"));
}

void TestZipIgnoreWalk::keepsFileNamedBuildRs()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/build.rs"), "fn");
    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    QCOMPARE(files.size(), 1);
    QCOMPARE(files.front().entryName, QStringLiteral("build.rs"));
}

void TestZipIgnoreWalk::skipsDirectorySymlink()
{
    QTemporaryDir tmp;
    QTemporaryDir outside;
    writeFile(outside.path() + QStringLiteral("/secret.txt"), "s");
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/keep.txt"), "k");
    const QString link = root + QStringLiteral("/linkdir");
    if (!QFile::link(outside.path(), link))
        QSKIP("symlink creation not permitted");
    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    QStringList names;
    for (const auto &f : files)
        names.append(f.entryName);
    QVERIFY(names.contains(QStringLiteral("keep.txt")));
    QVERIFY(!names.contains(QStringLiteral("linkdir/secret.txt")));
}

void TestZipIgnoreWalk::honorsGitignoreDist()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/.gitignore"), "dist/\n");
    writeFile(root + QStringLiteral("/src/a.cpp"), "a");
    writeFile(root + QStringLiteral("/dist/out.js"), "x");
    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    QStringList names;
    for (const auto &f : files)
        names.append(f.entryName);
    QVERIFY(names.contains(QStringLiteral("src/a.cpp")));
    QVERIFY(!names.contains(QStringLiteral("dist/out.js")));
}

void TestZipIgnoreWalk::honorsNestedLogIgnore()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/sub/.gitignore"), "*.log\n");
    writeFile(root + QStringLiteral("/sub/ok.txt"), "ok");
    writeFile(root + QStringLiteral("/sub/n.log"), "log");
    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    QStringList names;
    for (const auto &f : files)
        names.append(f.entryName);
    QVERIFY(names.contains(QStringLiteral("sub/ok.txt")));
    QVERIFY(!names.contains(QStringLiteral("sub/n.log")));
}

void TestZipIgnoreWalk::emptyOrIgnoredOnly_yieldsZero()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/.gitignore"), "*\n");
    writeFile(root + QStringLiteral("/secret.bin"), "x");
    QCOMPARE(ZipIgnoreWalk::walkLocalFolder(root, root).size(), 0);
}

void TestZipIgnoreWalk::nestedStar_doesNotIgnoreSiblings()
{
    remote::GitignoreMatcher m;
    m.addRules(QStringLiteral(".ruff_cache"), QStringLiteral("*\n"));
    QVERIFY(m.isIgnored(QStringLiteral(".ruff_cache/x.bin"), false));
    QVERIFY(!m.isIgnored(QStringLiteral("CLAUDE.md"), false));
    QVERIFY(!m.isIgnored(QStringLiteral("package.json"), false));
    QVERIFY(!m.isIgnored(QStringLiteral("context-engine-rs"), true));
}

void TestZipIgnoreWalk::nestedStar_walkKeepsSiblingsAndSubmoduleFiles()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/.ruff_cache/.gitignore"), "*\n");
    writeFile(root + QStringLiteral("/.ruff_cache/x.bin"), "x");
    writeFile(root + QStringLiteral("/.agents/keep.md"), "k");
    writeFile(root + QStringLiteral("/CLAUDE.md"), "c");
    writeFile(root + QStringLiteral("/package.json"), "{}");
    writeFile(root + QStringLiteral("/context-engine-rs/src/lib.rs"), "rs");

    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    QStringList names;
    for (const auto &f : files)
        names.append(f.entryName);

    QVERIFY(names.contains(QStringLiteral(".agents/keep.md")));
    QVERIFY(names.contains(QStringLiteral("CLAUDE.md")));
    QVERIFY(names.contains(QStringLiteral("package.json")));
    QVERIFY(names.contains(QStringLiteral("context-engine-rs/src/lib.rs")));
    QVERIFY(!names.contains(QStringLiteral(".ruff_cache/x.bin")));
}

void TestZipIgnoreWalk::leadingSlash_matchesOnlyRelativeToGitignoreDir()
{
    remote::GitignoreMatcher nested;
    nested.addRules(QStringLiteral("context-engine-rs"), QStringLiteral("/target\n"));
    QVERIFY(nested.isIgnored(QStringLiteral("context-engine-rs/target"), true));
    QVERIFY(!nested.isIgnored(QStringLiteral("context-engine-rs/src/lib.rs"), false));
    QVERIFY(!nested.isIgnored(QStringLiteral("target"), true));
    QVERIFY(!nested.isIgnored(QStringLiteral("other/target"), true));

    remote::GitignoreMatcher root;
    root.addRules(QString(), QStringLiteral("/target\n"));
    QVERIFY(root.isIgnored(QStringLiteral("target"), true));
    QVERIFY(!root.isIgnored(QStringLiteral("context-engine-rs/target"), true));
}

void TestZipIgnoreWalk::nestedNegation_doesNotUnignoreSibling()
{
    remote::GitignoreMatcher m;
    m.addRules(QString(), QStringLiteral("keep.txt\n"));
    m.addRules(QStringLiteral(".ruff_cache"), QStringLiteral("*\n!keep.txt\n"));
    QVERIFY(m.isIgnored(QStringLiteral("keep.txt"), false));
    QVERIFY(!m.isIgnored(QStringLiteral(".ruff_cache/keep.txt"), false));
    QVERIFY(m.isIgnored(QStringLiteral(".ruff_cache/x.bin"), false));
}

void TestZipIgnoreWalk::submoduleGitignore_doesNotLeakToSibling()
{
    remote::GitignoreMatcher m;
    m.addRules(QStringLiteral("context-engine-rs"), QStringLiteral("/target\n"));
    m.addRules(QStringLiteral("context-engine-admin"), QStringLiteral("*.db\n"));
    QVERIFY(m.isIgnored(QStringLiteral("context-engine-rs/target"), true));
    QVERIFY(!m.isIgnored(QStringLiteral("context-engine-admin/target"), true));
    QVERIFY(m.isIgnored(QStringLiteral("context-engine-admin/foo.db"), false));
    QVERIFY(!m.isIgnored(QStringLiteral("context-engine-rs/foo.db"), false));
}

void TestZipIgnoreWalk::walkShape_ruffCacheAndSubmodule()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/.ruff_cache/.gitignore"), "*\n");
    writeFile(root + QStringLiteral("/.ruff_cache/CACHEDIR.TAG"), "tag");
    writeFile(root + QStringLiteral("/.opencode/.gitignore"),
              "package.json\nbun.lock\n.gitignore\n");
    writeFile(root + QStringLiteral("/.opencode/skills/x.md"), "md");
    writeFile(root + QStringLiteral("/.opencode/package.json"), "{}");
    writeFile(root + QStringLiteral("/CLAUDE.md"), "c");
    writeFile(root + QStringLiteral("/package.json"), "{}");
    writeFile(root + QStringLiteral("/bun.lock"), "lock");
    writeFile(root + QStringLiteral("/context-engine-rs/.gitignore"), "/target\n");
    writeFile(root + QStringLiteral("/context-engine-rs/src/lib.rs"), "rs");
    writeFile(root + QStringLiteral("/context-engine-rs/target/foo.rlib"), "rl");
    writeFile(root + QStringLiteral("/context-engine-admin/src/main.rs"), "rs");

    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    QStringList names;
    for (const auto &f : files)
        names.append(f.entryName);

    QVERIFY(names.contains(QStringLiteral("CLAUDE.md")));
    QVERIFY(names.contains(QStringLiteral("package.json")));
    QVERIFY(names.contains(QStringLiteral("bun.lock")));
    QVERIFY(names.contains(QStringLiteral("context-engine-rs/src/lib.rs")));
    QVERIFY(names.contains(QStringLiteral("context-engine-admin/src/main.rs")));
    QVERIFY(names.contains(QStringLiteral(".opencode/skills/x.md")));
    QVERIFY(!names.contains(QStringLiteral(".ruff_cache/CACHEDIR.TAG")));
    QVERIFY(!names.contains(QStringLiteral("context-engine-rs/target/foo.rlib")));
    QVERIFY(!names.contains(QStringLiteral(".opencode/package.json")));
}

void TestZipIgnoreWalk::skipsGitfile()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/keep.txt"), "k");
    writeFile(root + QStringLiteral("/sub/.git"), "gitdir: ../.git/modules/sub\n");
    writeFile(root + QStringLiteral("/sub/src.cpp"), "s");
    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    QStringList names;
    for (const auto &f : files)
        names.append(f.entryName);
    QVERIFY(names.contains(QStringLiteral("keep.txt")));
    QVERIFY(names.contains(QStringLiteral("sub/src.cpp")));
    QVERIFY(!names.contains(QStringLiteral("sub/.git")));
}

void TestZipIgnoreWalk::doubleStar_matchesAtAnyDepth()
{
    remote::GitignoreMatcher m;
    m.addRules(QString(), QStringLiteral("**/*.log\n"));
    QVERIFY(m.isIgnored(QStringLiteral("n.log"), false));
    QVERIFY(m.isIgnored(QStringLiteral("sub/n.log"), false));
    QVERIFY(m.isIgnored(QStringLiteral("a/b/n.log"), false));
    QVERIFY(!m.isIgnored(QStringLiteral("n.txt"), false));
}

void TestZipIgnoreWalk::commentOnlyGitignore_doesNotIgnore()
{
    remote::GitignoreMatcher m;
    m.addRules(QString(), QStringLiteral("# only a comment\n\n"));
    QVERIFY(m.isEmpty());
    QVERIFY(!m.isIgnored(QStringLiteral("keep.txt"), false));
}

QTEST_APPLESS_MAIN(TestZipIgnoreWalk)
#include "test_zip_ignore_walk.moc"
