/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>

#include <atomic>

#include "ZipArchive.h"
#include "ZipIgnoreWalk.h"
#include "ZipPath.h"
#include "ZipUploadWipe.h"

class TestZipUploadWipe : public QObject
{
    Q_OBJECT

private slots:
    void orphan_walkedNotRetained();
    void orphan_skippedDestNotOrphan();
    void orphan_emptyWalk();
    void orphan_extraRetainIgnored();
    void orphan_sortsResult();
    void remove_deletesOrphanInside();
    void remove_refusesParentEscape();
    void remove_cancelStopsAfterFirst();
    void remove_prunesEmptyDir();
    void wipe_gitignoreAndHardSkipSurvive();
    void wipe_scopedToSelectedFolder();
    void wipe_idempotentSecondPass();
    void remoteDest_rejectsParentEscape();
    void extractWipe_successDeletesOrphan();
    void extractWipe_invalidZipDoesNotWipe();
    void extractWipe_extractFailDoesNotWipe();
    void extractWipe_cancelDuringExtractDoesNotWipe();
    void extractWipe_cancelDuringWipeKeepsExtract();
};

static void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

static QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

void TestZipUploadWipe::orphan_walkedNotRetained()
{
    const QStringList walked{QStringLiteral("old.cpp"), QStringLiteral("keep.cpp")};
    const QStringList retain{QStringLiteral("keep.cpp")};
    QCOMPARE(ZipUploadWipe::orphanRelPaths(walked, retain),
             QStringList{QStringLiteral("old.cpp")});
}

void TestZipUploadWipe::orphan_skippedDestNotOrphan()
{
    const QStringList walked{QStringLiteral("skip.cpp"), QStringLiteral("old.cpp")};
    const QStringList retain{QStringLiteral("skip.cpp")}; // Skip'd → retain, not extract
    QCOMPARE(ZipUploadWipe::orphanRelPaths(walked, retain),
             QStringList{QStringLiteral("old.cpp")});
}

void TestZipUploadWipe::orphan_emptyWalk()
{
    QCOMPARE(ZipUploadWipe::orphanRelPaths({}, {QStringLiteral("a.cpp")}), QStringList{});
}

void TestZipUploadWipe::orphan_extraRetainIgnored()
{
    const QStringList walked{QStringLiteral("only.cpp")};
    const QStringList retain{QStringLiteral("only.cpp"), QStringLiteral("ghost.cpp")};
    QCOMPARE(ZipUploadWipe::orphanRelPaths(walked, retain), QStringList{});
}

void TestZipUploadWipe::orphan_sortsResult()
{
    const QStringList walked{QStringLiteral("b.cpp"), QStringLiteral("a.cpp")};
    QCOMPARE(ZipUploadWipe::orphanRelPaths(walked, {}),
             (QStringList{QStringLiteral("a.cpp"), QStringLiteral("b.cpp")}));
}

void TestZipUploadWipe::remove_deletesOrphanInside()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/keep.cpp"), "keep");
    writeFile(root + QStringLiteral("/old.cpp"), "old");
    const QString err = ZipUploadWipe::removeLocalOrphans(
        root, {QStringLiteral("old.cpp")}, nullptr, {});
    QCOMPARE(err, QString());
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/keep.cpp")));
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/old.cpp")));
    QCOMPARE(readFile(root + QStringLiteral("/keep.cpp")), QByteArray("keep"));
}

void TestZipUploadWipe::remove_refusesParentEscape()
{
    QTemporaryDir tmp;
    QTemporaryDir outside;
    const QString victim = outside.filePath(QStringLiteral("secret.txt"));
    writeFile(victim, "secret");
    QString rel = QDir(tmp.path()).relativeFilePath(victim);
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QVERIFY(rel.startsWith(QLatin1String("..")));
    const QString err = ZipUploadWipe::removeLocalOrphans(tmp.path(), {rel}, nullptr, {});
    QCOMPARE(err, QString());
    QVERIFY(QFileInfo::exists(victim));
    QCOMPARE(readFile(victim), QByteArray("secret"));
}

void TestZipUploadWipe::remove_cancelStopsAfterFirst()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/a.cpp"), "a");
    writeFile(root + QStringLiteral("/b.cpp"), "b");
    writeFile(root + QStringLiteral("/c.cpp"), "c");
    std::atomic<bool> cancel{false};
    const QString err = ZipUploadWipe::removeLocalOrphans(
        root,
        {QStringLiteral("a.cpp"), QStringLiteral("b.cpp"), QStringLiteral("c.cpp")},
        &cancel,
        [&](int current, int, const QString &) {
            if (current >= 1)
                cancel.store(true);
        });
    QCOMPARE(err, QStringLiteral("cancelled"));
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/a.cpp")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/b.cpp")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/c.cpp")));
}

void TestZipUploadWipe::remove_prunesEmptyDir()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/gone/a.cpp"), "a");
    writeFile(root + QStringLiteral("/stay/b.cpp"), "b");
    const QString err = ZipUploadWipe::removeLocalOrphans(
        root, {QStringLiteral("gone/a.cpp")}, nullptr, {});
    QCOMPARE(err, QString());
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/gone/a.cpp")));
    QVERIFY(!QDir(root + QStringLiteral("/gone")).exists());
    QVERIFY(QDir(root).exists());
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/stay/b.cpp")));
}

void TestZipUploadWipe::wipe_gitignoreAndHardSkipSurvive()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/.gitignore"), "dist/\n");
    writeFile(root + QStringLiteral("/keep.cpp"), "k");
    writeFile(root + QStringLiteral("/old.cpp"), "o");
    writeFile(root + QStringLiteral("/dist/out.js"), "js");
    writeFile(root + QStringLiteral("/.git/HEAD"), "ref");
    writeFile(root + QStringLiteral("/node_modules/pkg/index.js"), "js");
    writeFile(root + QStringLiteral("/build-debug/foo.obj"), "obj");
    writeFile(root + QStringLiteral("/.cpm-cache/x.bin"), "x");
    const QString err = ZipUploadWipe::wipeLocalFolder(
        root, root, {QStringLiteral("keep.cpp")}, nullptr, {});
    QCOMPARE(err, QString());
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/keep.cpp")));
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/old.cpp")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/dist/out.js")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/.git/HEAD")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/node_modules/pkg/index.js")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/build-debug/foo.obj")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/.cpm-cache/x.bin")));
}

void TestZipUploadWipe::wipe_scopedToSelectedFolder()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/README.md"), "r");
    writeFile(root + QStringLiteral("/src/keep.cpp"), "k");
    writeFile(root + QStringLiteral("/src/old.cpp"), "o");
    const QString err = ZipUploadWipe::wipeLocalFolder(
        root, root + QStringLiteral("/src"), {QStringLiteral("keep.cpp")}, nullptr, {});
    QCOMPARE(err, QString());
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/README.md")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/src/keep.cpp")));
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/src/old.cpp")));
}

void TestZipUploadWipe::wipe_idempotentSecondPass()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/keep.cpp"), "k");
    writeFile(root + QStringLiteral("/old.cpp"), "o");
    const QStringList retain{QStringLiteral("keep.cpp")};
    QCOMPARE(ZipUploadWipe::wipeLocalFolder(root, root, retain, nullptr, {}), QString());
    QCOMPARE(ZipUploadWipe::wipeLocalFolder(root, root, retain, nullptr, {}), QString());
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/keep.cpp")));
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/old.cpp")));
    QCOMPARE(readFile(root + QStringLiteral("/keep.cpp")), QByteArray("k"));
}

static QString packZip(const QString &zipPath, const QStringList &entryNames,
                       const QList<QByteArray> &bodies, const QString &scratch)
{
    ZipWriter w;
    if (!w.open(zipPath))
        return w.errorString();
    for (int i = 0; i < entryNames.size(); ++i) {
        const QString src = scratch + QLatin1Char('/') + entryNames.at(i);
        writeFile(src, bodies.at(i));
        if (!w.addFile(src, entryNames.at(i)))
            return w.errorString();
    }
    if (!w.finish())
        return w.errorString();
    return {};
}

void TestZipUploadWipe::remoteDest_rejectsParentEscape()
{
    QVERIFY(ZipUploadWipe::isSafeRemoteDest(QStringLiteral("/proj"),
                                            QStringLiteral("/proj/src/a.cpp")));
    QVERIFY(!ZipUploadWipe::isSafeRemoteDest(QStringLiteral("/proj"),
                                             QStringLiteral("/proj/../etc/passwd")));
    QVERIFY(!ZipUploadWipe::isSafeRemoteDest(QStringLiteral("/proj"),
                                             QStringLiteral("/etc/passwd")));
}

void TestZipUploadWipe::extractWipe_successDeletesOrphan()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/old.cpp"), "old");
    const QString zip = tmp.filePath(QStringLiteral("in.zip"));
    QCOMPARE(packZip(zip, {QStringLiteral("keep.cpp")}, {QByteArray("keep")},
                     tmp.filePath(QStringLiteral("scratch"))),
             QString());
    const QList<ZipUploadWipe::ExtractFile> items{{QStringLiteral("keep.cpp"),
                                                   QStringLiteral("keep.cpp")}};
    const QString err = ZipUploadWipe::extractThenWipeLocal(
        zip, root, root, items, {QStringLiteral("keep.cpp")}, nullptr, {});
    QCOMPARE(err, QString());
    QCOMPARE(readFile(root + QStringLiteral("/keep.cpp")), QByteArray("keep"));
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/old.cpp")));
}

void TestZipUploadWipe::extractWipe_invalidZipDoesNotWipe()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/old.cpp"), "old");
    writeFile(tmp.filePath(QStringLiteral("bad.zip")), "not-a-zip");
    const QList<ZipUploadWipe::ExtractFile> items{{QStringLiteral("keep.cpp"),
                                                   QStringLiteral("keep.cpp")}};
    const QString err = ZipUploadWipe::extractThenWipeLocal(
        tmp.filePath(QStringLiteral("bad.zip")), root, root, items,
        {QStringLiteral("keep.cpp")}, nullptr, {});
    QVERIFY(!err.isEmpty());
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/old.cpp")));
}

void TestZipUploadWipe::extractWipe_extractFailDoesNotWipe()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/old.cpp"), "old");
    writeFile(root + QStringLiteral("/nested"), "i-am-a-file");
    const QString zip = tmp.filePath(QStringLiteral("in.zip"));
    QCOMPARE(packZip(zip,
                     {QStringLiteral("keep.cpp"), QStringLiteral("nested/a.cpp")},
                     {QByteArray("keep"), QByteArray("a")},
                     tmp.filePath(QStringLiteral("scratch"))),
             QString());
    const QList<ZipUploadWipe::ExtractFile> items{
        {QStringLiteral("keep.cpp"), QStringLiteral("keep.cpp")},
        {QStringLiteral("nested/a.cpp"), QStringLiteral("nested/a.cpp")},
    };
    const QString err = ZipUploadWipe::extractThenWipeLocal(
        zip, root, root, items,
        {QStringLiteral("keep.cpp"), QStringLiteral("nested/a.cpp")}, nullptr, {});
    QVERIFY(!err.isEmpty());
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/old.cpp")));
}

void TestZipUploadWipe::extractWipe_cancelDuringExtractDoesNotWipe()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/old.cpp"), "old");
    const QString zip = tmp.filePath(QStringLiteral("in.zip"));
    QCOMPARE(packZip(zip, {QStringLiteral("a.cpp"), QStringLiteral("b.cpp")},
                     {QByteArray("A"), QByteArray("B")},
                     tmp.filePath(QStringLiteral("scratch"))),
             QString());
    const QList<ZipUploadWipe::ExtractFile> items{
        {QStringLiteral("a.cpp"), QStringLiteral("a.cpp")},
        {QStringLiteral("b.cpp"), QStringLiteral("b.cpp")},
    };
    std::atomic<bool> cancel{false};
    const QString err = ZipUploadWipe::extractThenWipeLocal(
        zip, root, root, items, {QStringLiteral("a.cpp"), QStringLiteral("b.cpp")}, &cancel,
        [&](int current, int, const QString &) {
            if (current >= 1)
                cancel.store(true);
        });
    QCOMPARE(err, QStringLiteral("cancelled"));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/old.cpp")));
}

void TestZipUploadWipe::extractWipe_cancelDuringWipeKeepsExtract()
{
    QTemporaryDir tmp;
    const QString root = tmp.path();
    writeFile(root + QStringLiteral("/old.cpp"), "old");
    writeFile(root + QStringLiteral("/extra.cpp"), "extra");
    const QString zip = tmp.filePath(QStringLiteral("in.zip"));
    QCOMPARE(packZip(zip, {QStringLiteral("keep.cpp")}, {QByteArray("keep")},
                     tmp.filePath(QStringLiteral("scratch"))),
             QString());
    const QList<ZipUploadWipe::ExtractFile> items{{QStringLiteral("keep.cpp"),
                                                   QStringLiteral("keep.cpp")}};
    std::atomic<bool> cancel{false};
    int calls = 0;
    const QString err = ZipUploadWipe::extractThenWipeLocal(
        zip, root, root, items, {QStringLiteral("keep.cpp")}, &cancel,
        [&](int, int, const QString &) {
            ++calls;
            if (calls >= 2)
                cancel.store(true);
        });
    QCOMPARE(err, QStringLiteral("cancelled"));
    QCOMPARE(readFile(root + QStringLiteral("/keep.cpp")), QByteArray("keep"));
    const int remaining = int(QFileInfo::exists(root + QStringLiteral("/old.cpp")))
        + int(QFileInfo::exists(root + QStringLiteral("/extra.cpp")));
    QCOMPARE(remaining, 1);
}

QTEST_APPLESS_MAIN(TestZipUploadWipe)
#include "test_zip_upload_wipe.moc"
