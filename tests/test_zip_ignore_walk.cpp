/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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

QTEST_APPLESS_MAIN(TestZipIgnoreWalk)
#include "test_zip_ignore_walk.moc"
