/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "ForceRemovePath.h"
#include "GitErrorClassifier.h"

#ifdef Q_OS_WIN
#    include <windows.h>
#endif

class TestForceRemovePath : public QObject
{
    Q_OBJECT

private slots:
    void forceRemove_deletesNulFileCreatedViaExtendedPath();
    void forceRemove_deletesReadOnlyNul();
    void recoverFailedUntrackedDeletes_removesRemainingNul();
    void recoverFailedUntrackedDeletes_removesOrdinaryFile();
    void recoverFailedUntrackedDeletes_rejectsParentTraversal();
    void classify_failedToRemoveNul_isReservedName();
};

#ifdef Q_OS_WIN

static QString extendedWinPath(const QString &absPath)
{
    const QString native = QDir::toNativeSeparators(absPath);
    if (native.startsWith(QLatin1String("\\\\?\\")))
        return native;
    return QStringLiteral("\\\\?\\") + native;
}

static bool createExtendedFile(const QString &absPath)
{
    const QString ext = extendedWinPath(absPath);
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(ext.utf16()),
                           GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    CloseHandle(h);
    return true;
}

static bool existsExtended(const QString &absPath)
{
    const QString ext = extendedWinPath(absPath);
    const DWORD attr = GetFileAttributesW(reinterpret_cast<LPCWSTR>(ext.utf16()));
    return attr != INVALID_FILE_ATTRIBUTES;
}

#endif

void TestForceRemovePath::forceRemove_deletesNulFileCreatedViaExtendedPath()
{
#ifndef Q_OS_WIN
    QSKIP("Windows reserved-name delete");
#else
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString abs = QDir::toNativeSeparators(
        tmp.path() + QLatin1String("\\nul"));
    QVERIFY(createExtendedFile(abs));
    QVERIFY(existsExtended(abs));
    QVERIFY(ForceRemovePath::forceRemove(abs));
    QVERIFY(!existsExtended(abs));
#endif
}

void TestForceRemovePath::forceRemove_deletesReadOnlyNul()
{
#ifndef Q_OS_WIN
    QSKIP("Windows reserved-name delete");
#else
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString abs = QDir::toNativeSeparators(
        tmp.path() + QLatin1String("\\nul"));
    QVERIFY(createExtendedFile(abs));
    QVERIFY(SetFileAttributesW(reinterpret_cast<LPCWSTR>(extendedWinPath(abs).utf16()),
                               FILE_ATTRIBUTE_READONLY));
    QVERIFY(existsExtended(abs));
    QVERIFY(ForceRemovePath::forceRemove(abs));
    QVERIFY(!existsExtended(abs));
#endif
}

void TestForceRemovePath::recoverFailedUntrackedDeletes_removesRemainingNul()
{
#ifndef Q_OS_WIN
    QSKIP("Windows reserved-name delete");
#else
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString abs = QDir::toNativeSeparators(
        tmp.path() + QLatin1String("\\nul"));
    QVERIFY(createExtendedFile(abs));
    QVERIFY(existsExtended(abs));
    QVERIFY(ForceRemovePath::recoverFailedUntrackedDeletes(
        tmp.path(), {QStringLiteral("nul")}));
    QVERIFY(!existsExtended(abs));
#endif
}

void TestForceRemovePath::recoverFailedUntrackedDeletes_removesOrdinaryFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString abs = QDir(tmp.path()).filePath(QStringLiteral("gone.txt"));
    QFile file(abs);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("x"), qint64(1));
    file.close();
    QVERIFY(ForceRemovePath::recoverFailedUntrackedDeletes(
        tmp.path(), {QStringLiteral("gone.txt")}));
    QVERIFY(!QFileInfo::exists(abs));
}

void TestForceRemovePath::recoverFailedUntrackedDeletes_rejectsParentTraversal()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString repo = QDir(tmp.path()).filePath(QStringLiteral("repo"));
    QVERIFY(QDir().mkpath(repo));
    const QString outside = QDir(tmp.path()).filePath(QStringLiteral("keep.txt"));
    QFile file(outside);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(!ForceRemovePath::recoverFailedUntrackedDeletes(
        repo, {QStringLiteral("../keep.txt")}));
    QVERIFY(QFileInfo::exists(outside));
}

void TestForceRemovePath::classify_failedToRemoveNul_isReservedName()
{
    const GitError e = GitErrorClassifier::classify(
        1,
        QByteArrayLiteral("warning: failed to remove nul: Permission denied\n"),
        {QStringLiteral("-C"), QStringLiteral("D:/repo"),
         QStringLiteral("clean"), QStringLiteral("-fd"),
         QStringLiteral("--"), QStringLiteral("nul")});
    QCOMPARE(e.kind, GitError::ReservedName);
}

QTEST_APPLESS_MAIN(TestForceRemovePath)
#include "test_force_remove_path.moc"
