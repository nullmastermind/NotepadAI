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
#include <QDialog>

#include "control/ControlEvents.h"

using control::ControlEvents;

class TestControlEvents : public QObject
{
    Q_OBJECT

private slots:
    void nothingHappensWithoutListeners();
    void subscribersGetNumberedFrames();
    void windowsOpeningAndClosingAreReported();
    void aWaiterIsDroppedOnceSatisfied();
};

void TestControlEvents::nothingHappensWithoutListeners()
{
    ControlEvents hub;
    QSignalSpy frames(&hub, &ControlEvents::frameReady);
    QVERIFY(!control::events::wanted());
    control::events::publish(QStringLiteral("chat.turnEnded"), {{QStringLiteral("id"), QStringLiteral("chat:a")}});
    QDialog dialog;
    dialog.show();
    QCOMPARE(frames.size(), 0);
}

void TestControlEvents::subscribersGetNumberedFrames()
{
    ControlEvents hub;
    QSignalSpy frames(&hub, &ControlEvents::frameReady);
    hub.setSubscribers(1);
    QVERIFY(control::events::wanted());
    control::events::publish(QStringLiteral("terminal.taskExited"), {{QStringLiteral("exitCode"), 2}});
    control::events::publish(QStringLiteral("terminal.taskExited"), {{QStringLiteral("exitCode"), 0}});
    QCOMPARE(frames.size(), 2);
    const QByteArray first = frames.at(0).at(0).toByteArray();
    QVERIFY(first.startsWith("event: terminal.taskExited\ndata: {"));
    QVERIFY(first.contains("\"exitCode\":2"));
    QVERIFY(first.contains("\"seq\":1"));
    QVERIFY(frames.at(1).at(0).toByteArray().contains("\"seq\":2"));

    hub.setSubscribers(0);
    QVERIFY(!control::events::wanted());
    control::events::publish(QStringLiteral("terminal.taskExited"), {});
    QCOMPARE(frames.size(), 2);
}

void TestControlEvents::windowsOpeningAndClosingAreReported()
{
    ControlEvents hub;
    hub.setSubscribers(1);
    QSignalSpy frames(&hub, &ControlEvents::frameReady);
    QDialog dialog;
    dialog.setObjectName(QStringLiteral("SettingsDialog"));
    dialog.setWindowTitle(QStringLiteral("Settings"));
    dialog.show();
    QTRY_VERIFY(!frames.isEmpty());
    const QByteArray opened = frames.at(0).at(0).toByteArray();
    QVERIFY(opened.startsWith("event: window.opened\n"));
    QVERIFY(opened.contains("\"id\":\"SettingsDialog\""));
    QVERIFY(opened.contains("\"title\":\"Settings\""));

    dialog.hide();
    QVERIFY(frames.last().at(0).toByteArray().startsWith("event: window.closed\n"));
}

void TestControlEvents::aWaiterIsDroppedOnceSatisfied()
{
    ControlEvents hub;
    int calls = 0;
    QJsonObject seen;
    hub.addWaiter([&](const QString &name, const QJsonObject &data) {
        ++calls;
        if (name != QLatin1String("chat.turnEnded"))
            return false;
        seen = data;
        return true;
    });
    // A pending wait is a listener: events flow while it waits.
    QVERIFY(control::events::wanted());
    control::events::publish(QStringLiteral("focus.changed"), {});
    control::events::publish(QStringLiteral("chat.turnEnded"), {{QStringLiteral("id"), QStringLiteral("chat:a")}});
    control::events::publish(QStringLiteral("chat.turnEnded"), {{QStringLiteral("id"), QStringLiteral("chat:b")}});
    QCOMPARE(calls, 2);
    QCOMPARE(seen.value(QStringLiteral("id")).toString(), QStringLiteral("chat:a"));
    QVERIFY(!control::events::wanted());
}

QTEST_MAIN(TestControlEvents)
#include "test_control_events.moc"
