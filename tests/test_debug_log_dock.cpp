/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// Delete Permanently on a folder QFileSystemModel is watching makes Qt's
// watcher thread emit qErrnoWarning ("FindNextChangeNotification failed …
// Access is denied"). DebugLogDock's message handler used to call
// QPlainTextEdit::appendPlainText on that thread. The document is not
// thread-safe; the crash reports die in HarfBuzz with RCX == 0. The handler
// must not touch the widget until the GUI thread processes events.

#include <QtTest>

#include <QPlainTextEdit>
#include <QThread>

#include <atomic>

#include "DebugLogDock.h"
#include "DebugManager.h"

class TestDebugLogDock : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void warningFromOtherThread_doesNotAppendUntilGuiProcessesEvents();
    void warningOnGuiThread_doesNotAppendUntilEventsProcessed();

private:
    QString logText(DebugLogDock &dock) const;
};

void TestDebugLogDock::init()
{
    DebugManager::manageDebugOutput();
}

QString TestDebugLogDock::logText(DebugLogDock &dock) const
{
    auto *edit = dock.findChild<QPlainTextEdit *>(QStringLiteral("txtDebugOutput"));
    return edit ? edit->toPlainText() : QString();
}

void TestDebugLogDock::warningFromOtherThread_doesNotAppendUntilGuiProcessesEvents()
{
    DebugLogDock dock;
    auto *edit = dock.findChild<QPlainTextEdit *>(QStringLiteral("txtDebugOutput"));
    QVERIFY(edit);

    const QString token = QStringLiteral("watcher-thread-warning-9f3c");
    std::atomic<bool> emitted{false};
    QThread *worker = QThread::create([&token, &emitted]() {
        qWarning("%s", qUtf8Printable(token));
        emitted.store(true, std::memory_order_release);
    });
    worker->start();
    QVERIFY(worker->wait(5000));
    delete worker;
    QVERIFY(emitted.load(std::memory_order_acquire));

    // Synchronous appendPlainText on the watcher thread would already have
    // written the token (or crashed). Queued delivery must leave the document
    // unchanged until the GUI thread runs the posted event.
    QVERIFY2(!logText(dock).contains(token),
             "log widget was mutated on the thread that emitted the warning");

    QCoreApplication::processEvents();
    QVERIFY(logText(dock).contains(token));
}

void TestDebugLogDock::warningOnGuiThread_doesNotAppendUntilEventsProcessed()
{
    DebugLogDock dock;
    auto *edit = dock.findChild<QPlainTextEdit *>(QStringLiteral("txtDebugOutput"));
    QVERIFY(edit);

    // A warning emitted while the log document is already laying out
    // (QTextCursor::setPosition "position out of range") used to re-enter
    // appendPlainText on the same stack and crash in insertBlock / HarfBuzz.
    const QString token = QStringLiteral("gui-thread-warning-9f3c");
    qWarning("%s", qUtf8Printable(token));
    QVERIFY2(!logText(dock).contains(token),
             "log widget was mutated inside the warning, before events were processed");

    QCoreApplication::processEvents();
    QVERIFY(logText(dock).contains(token));
}

QTEST_MAIN(TestDebugLogDock)
#include "test_debug_log_dock.moc"
