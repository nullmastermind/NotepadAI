/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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
#include <QCoreApplication>
#include <QFont>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "ApplicationSettings.h"


class TestApplicationSettingsTerminal : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void shellCommand_defaultIsNonEmpty();
    void shellCommand_setAndGetRoundTrip();
    void shellCommand_emitsChangedSignal();
    void shellCommand_emitsOnEverySet();

    void terminalFont_defaultParsesAsValidQFont();
    void terminalFont_setAndGetRoundTrip();
    void terminalFont_emitsChangedSignal();
    void terminalFont_emitsOnEverySet();

private:
    QTemporaryDir tempDir;
};

void TestApplicationSettingsTerminal::initTestCase()
{
    QVERIFY(tempDir.isValid());

    QCoreApplication::setOrganizationName("NotepadNextTest");
    QCoreApplication::setApplicationName("NotepadNextTestTerminal");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
}

void TestApplicationSettingsTerminal::init()
{
    ApplicationSettings s;
    s.clear();
    s.sync();
}

void TestApplicationSettingsTerminal::shellCommand_defaultIsNonEmpty()
{
    ApplicationSettings s;
    QVERIFY(!s.shellCommand().isEmpty());
}

void TestApplicationSettingsTerminal::shellCommand_setAndGetRoundTrip()
{
    ApplicationSettings s;
    s.setShellCommand(QStringLiteral("test_value"));
    QCOMPARE(s.shellCommand(), QStringLiteral("test_value"));
}

void TestApplicationSettingsTerminal::shellCommand_emitsChangedSignal()
{
    ApplicationSettings s;
    QSignalSpy spy(&s, &ApplicationSettings::shellCommandChanged);
    QVERIFY(spy.isValid());

    s.setShellCommand(QStringLiteral("test_value"));

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QStringLiteral("test_value"));
}

void TestApplicationSettingsTerminal::shellCommand_emitsOnEverySet()
{
    // CREATE_SETTING macro fires shellCommandChanged on every set, not only on change.
    ApplicationSettings s;
    s.setShellCommand(QStringLiteral("a"));

    QSignalSpy spy(&s, &ApplicationSettings::shellCommandChanged);
    s.setShellCommand(QStringLiteral("a"));
    QCOMPARE(spy.count(), 1);
}

void TestApplicationSettingsTerminal::terminalFont_defaultParsesAsValidQFont()
{
    ApplicationSettings s;
    const QString def = s.terminalFont();
    QVERIFY(!def.isEmpty());
    QFont f;
    QVERIFY(f.fromString(def));
}

void TestApplicationSettingsTerminal::terminalFont_setAndGetRoundTrip()
{
    ApplicationSettings s;
    QFont src(QStringLiteral("Courier New"), 12);
    const QString str = src.toString();

    s.setTerminalFont(str);
    QCOMPARE(s.terminalFont(), str);

    QFont round;
    QVERIFY(round.fromString(s.terminalFont()));
    QCOMPARE(round.family(), src.family());
    QCOMPARE(round.pointSize(), src.pointSize());
}

void TestApplicationSettingsTerminal::terminalFont_emitsChangedSignal()
{
    ApplicationSettings s;
    QSignalSpy spy(&s, &ApplicationSettings::terminalFontChanged);
    QVERIFY(spy.isValid());

    const QString val = QFont(QStringLiteral("Courier New"), 14).toString();
    s.setTerminalFont(val);

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), val);
}

void TestApplicationSettingsTerminal::terminalFont_emitsOnEverySet()
{
    // CREATE_SETTING macro fires terminalFontChanged on every set, not only on change.
    ApplicationSettings s;
    const QString val = QFont(QStringLiteral("Courier New"), 14).toString();
    s.setTerminalFont(val);

    QSignalSpy spy(&s, &ApplicationSettings::terminalFontChanged);
    s.setTerminalFont(val);
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestApplicationSettingsTerminal)

#include "test_application_settings_terminal.moc"
