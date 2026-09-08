/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "ZipArchive.h"
#include "ZipPath.h"

class TestZipArchive : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip_packExtract();
    void roundTrip_zeroByteFile();
    void emptyList_openStillWorks();
    void reader_rejectsGarbage();
};

void TestZipArchive::roundTrip_packExtract()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString a = tmp.filePath(QStringLiteral("a.txt"));
    const QString b = tmp.filePath(QStringLiteral("sub/b.txt"));
    QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("sub")));
    {
        QFile fa(a);
        QVERIFY(fa.open(QIODevice::WriteOnly));
        fa.write("hello");
    }
    {
        QFile fb(b);
        QVERIFY(fb.open(QIODevice::WriteOnly));
        fb.write("world");
    }

    const QString zip = tmp.filePath(QStringLiteral("out.zip"));
    ZipWriter writer;
    QVERIFY(writer.open(zip));
    QVERIFY(writer.addFile(a, QStringLiteral("a.txt")));
    QVERIFY(writer.addFile(b, QStringLiteral("sub/b.txt")));
    QVERIFY(writer.finish());
    QVERIFY(QFileInfo::exists(zip));

    ZipReader reader;
    QVERIFY(reader.open(zip));
    const QStringList names = reader.fileEntries();
    QVERIFY(names.contains(QStringLiteral("a.txt")));
    QVERIFY(names.contains(QStringLiteral("sub/b.txt")));

    const QString outA = tmp.filePath(QStringLiteral("out/a.txt"));
    QVERIFY(QDir(tmp.path()).mkpath(QStringLiteral("out")));
    QVERIFY(reader.extractToFile(QStringLiteral("a.txt"), outA));
    QFile ra(outA);
    QVERIFY(ra.open(QIODevice::ReadOnly));
    QCOMPARE(ra.readAll(), QByteArray("hello"));

    QByteArray bytes;
    QVERIFY(reader.extractToBytes(QStringLiteral("sub/b.txt"), &bytes));
    QCOMPARE(bytes, QByteArray("world"));
}

void TestZipArchive::roundTrip_zeroByteFile()
{
    QTemporaryDir tmp;
    const QString empty = tmp.filePath(QStringLiteral("empty.dat"));
    QVERIFY(QFile(empty).open(QIODevice::WriteOnly));
    const QString zip = tmp.filePath(QStringLiteral("z.zip"));
    ZipWriter writer;
    QVERIFY(writer.open(zip));
    QVERIFY(writer.addFile(empty, QStringLiteral("empty.dat")));
    QVERIFY(writer.finish());
    ZipReader reader;
    QVERIFY(reader.open(zip));
    QByteArray bytes;
    QVERIFY(reader.extractToBytes(QStringLiteral("empty.dat"), &bytes));
    QVERIFY(bytes.isEmpty());
}

void TestZipArchive::emptyList_openStillWorks()
{
    QTemporaryDir tmp;
    const QString zip = tmp.filePath(QStringLiteral("empty.zip"));
    ZipWriter writer;
    QVERIFY(writer.open(zip));
    QVERIFY(writer.finish());
    ZipReader reader;
    QVERIFY(reader.open(zip));
    QVERIFY(reader.fileEntries().isEmpty());
}

void TestZipArchive::reader_rejectsGarbage()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("not.zip"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not a zip");
    f.close();
    ZipReader reader;
    QVERIFY(!reader.open(path));
}

QTEST_APPLESS_MAIN(TestZipArchive)
#include "test_zip_archive.moc"
