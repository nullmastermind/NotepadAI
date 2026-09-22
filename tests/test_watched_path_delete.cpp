/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// Deleting a directory QFileSystemModel is watching, without dropping the
// watch first, makes Qt's watcher thread emit
// "FindNextChangeNotification failed … (Access is denied.)".
// WatchedPathDelete::remove must not produce that warning.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>

#include "WatchedPathDelete.h"

namespace {

std::atomic<int> g_offThreadWarnings{0};
std::atomic<unsigned long long> g_guiTid{0};

void watcherWarningHandler(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    if (!msg.contains(QLatin1String("FindNextChangeNotification")))
        return;
    const auto tid = static_cast<unsigned long long>(
        reinterpret_cast<uintptr_t>(QThread::currentThreadId()));
    if (tid != g_guiTid.load(std::memory_order_relaxed))
        g_offThreadWarnings.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

class TestWatchedPathDelete : public QObject
{
    Q_OBJECT

private slots:
    void remove_watchedDirectory_noOffThreadWarning();
};

void TestWatchedPathDelete::remove_watchedDirectory_noOffThreadWarning()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString watched = QDir(tmp.path()).filePath(QStringLiteral("watched"));
    QVERIFY(QDir().mkpath(watched));
    QFile file(QDir(watched).filePath(QStringLiteral("a.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("x"), qint64(1));
    file.close();

    QFileSystemModel model;
    model.setRootPath(watched);
    QVERIFY(model.index(watched).isValid());

    g_offThreadWarnings.store(0);
    g_guiTid.store(static_cast<unsigned long long>(
        reinterpret_cast<uintptr_t>(QThread::currentThreadId())));
    const QtMessageHandler previous = qInstallMessageHandler(watcherWarningHandler);

    const bool removed = WatchedPathDelete::remove(&model, watched, true);
    QTest::qWait(1500);

    qInstallMessageHandler(previous);

    QVERIFY(removed);
    QVERIFY(!QFileInfo::exists(watched));
    QCOMPARE(g_offThreadWarnings.load(), 0);
}

QTEST_MAIN(TestWatchedPathDelete)
#include "test_watched_path_delete.moc"
