/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "ZipPath.h"

class TestZipPath : public QObject
{
    Q_OBJECT

private slots:
    void normalize_convertsBackslashes();
    void prefix_stripsSingleWrapper();
    void prefix_keepsMixedTops();
    void prefix_emptyOnTopLevelFile();
    void validate_rejectsParentTraversal();
    void validate_rejectsAbsoluteUnix();
    void validate_rejectsWindowsDrive();
    void validate_rejectsReservedDevice();
    void validate_rejectsTrailingDotOnWindows();
    void validate_acceptsNormalRelative();
    void skip_gitAndNodeModules();
    void safeDest_acceptsInside();
    void safeDest_rejectsCleanPathEscape();
    void safeDest_rejectsExistingSymlink();
};

void TestZipPath::normalize_convertsBackslashes()
{
    QCOMPARE(ZipPath::normalizeZipEntryName(QStringLiteral("src\\foo.cpp")),
             QStringLiteral("src/foo.cpp"));
}

void TestZipPath::prefix_stripsSingleWrapper()
{
    const QStringList names{
        QStringLiteral("myproject/src/a.cpp"),
        QStringLiteral("myproject/README.md"),
    };
    QCOMPARE(ZipPath::detectCommonPrefix(names), QStringLiteral("myproject/"));
}

void TestZipPath::prefix_keepsMixedTops()
{
    const QStringList names{
        QStringLiteral("src/a.cpp"),
        QStringLiteral("README.md"),
    };
    QCOMPARE(ZipPath::detectCommonPrefix(names), QString());
}

void TestZipPath::prefix_emptyOnTopLevelFile()
{
    const QStringList names{QStringLiteral("only.txt")};
    QCOMPARE(ZipPath::detectCommonPrefix(names), QString());
}

void TestZipPath::validate_rejectsParentTraversal()
{
    QVERIFY(!ZipPath::validateExtractRelPath(QStringLiteral("../../etc/passwd"),
                                             ZipPath::DestOs::Posix).isEmpty());
}

void TestZipPath::validate_rejectsAbsoluteUnix()
{
    QVERIFY(!ZipPath::validateExtractRelPath(QStringLiteral("/tmp/evil"),
                                             ZipPath::DestOs::Posix).isEmpty());
}

void TestZipPath::validate_rejectsWindowsDrive()
{
    QVERIFY(!ZipPath::validateExtractRelPath(QStringLiteral("C:/Windows/evil"),
                                             ZipPath::DestOs::Windows).isEmpty());
}

void TestZipPath::validate_rejectsReservedDevice()
{
    QVERIFY(!ZipPath::validateExtractRelPath(QStringLiteral("CON.txt"),
                                             ZipPath::DestOs::Windows).isEmpty());
    QVERIFY(!ZipPath::validateExtractRelPath(QStringLiteral("nul"),
                                             ZipPath::DestOs::Windows).isEmpty());
}

void TestZipPath::validate_rejectsTrailingDotOnWindows()
{
    QVERIFY(!ZipPath::validateExtractRelPath(QStringLiteral("foo."),
                                             ZipPath::DestOs::Windows).isEmpty());
}

void TestZipPath::validate_acceptsNormalRelative()
{
    QCOMPARE(ZipPath::validateExtractRelPath(QStringLiteral("src/foo.cpp"),
                                             ZipPath::DestOs::Windows),
             QString());
}

void TestZipPath::skip_gitAndNodeModules()
{
    QVERIFY(ZipPath::isHardSkippedDirName(QStringLiteral(".git")));
    QVERIFY(ZipPath::isHardSkippedDirName(QStringLiteral("node_modules")));
    QVERIFY(ZipPath::isHardSkippedDirName(QStringLiteral("build-debug")));
    QVERIFY(ZipPath::isHardSkippedDirName(QStringLiteral("build-release")));
    QVERIFY(ZipPath::isHardSkippedDirName(QStringLiteral(".cpm-cache")));
    QVERIFY(!ZipPath::isHardSkippedDirName(QStringLiteral("src")));
    QVERIFY(!ZipPath::isHardSkippedDirName(QStringLiteral("build")));
}

void TestZipPath::safeDest_acceptsInside()
{
    QTemporaryDir tmp;
    QVERIFY(ZipPath::isSafeExtractDest(tmp.path(), tmp.path() + QStringLiteral("/src/a.cpp")));
}

void TestZipPath::safeDest_rejectsCleanPathEscape()
{
    QTemporaryDir tmp;
    const QString dest = QDir::cleanPath(tmp.path() + QStringLiteral("/../outside.txt"));
    QVERIFY(!ZipPath::isSafeExtractDest(tmp.path(), dest));
}

void TestZipPath::safeDest_rejectsExistingSymlink()
{
    QTemporaryDir tmp;
    const QString link = tmp.path() + QStringLiteral("/link");
    if (!QFile::link(tmp.path(), link))
        QSKIP("symlink creation not permitted");
    if (!QFileInfo(link).isSymLink())
        QSKIP("QFile::link did not produce a real symlink");
    QVERIFY(!ZipPath::isSafeExtractDest(tmp.path(), link));
    QVERIFY(!ZipPath::isSafeExtractDest(tmp.path(), link + QStringLiteral("/x.txt")));
}

QTEST_APPLESS_MAIN(TestZipPath)
#include "test_zip_path.moc"
