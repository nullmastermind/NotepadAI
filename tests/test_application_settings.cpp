/*
 * This file is part of Notepad Next.
 * Copyright 2024 Justin Dailey
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
#include <QSignalSpy>
#include <QSettings>
#include <QTemporaryDir>

#include "ApplicationSettings.h"


class TestApplicationSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void theme_defaultIsSystem();
    void theme_setAndGetRoundTrip();
    void theme_emitsChangedSignal();
    void theme_persistsAcrossInstances();

private:
    QTemporaryDir tempDir;
};

void TestApplicationSettings::initTestCase()
{
    QVERIFY(tempDir.isValid());

    // Pin QSettings storage to the temp dir so tests don't touch the real user config.
    QCoreApplication::setOrganizationName("NotepadNextTest");
    QCoreApplication::setApplicationName("NotepadNextTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
}

void TestApplicationSettings::init()
{
    // Start each test from a clean slate.
    ApplicationSettings s;
    s.clear();
    s.sync();
}

void TestApplicationSettings::theme_defaultIsSystem()
{
    ApplicationSettings s;
    QCOMPARE(s.theme(), ApplicationSettings::System);
}

void TestApplicationSettings::theme_setAndGetRoundTrip()
{
    ApplicationSettings s;

    s.setTheme(ApplicationSettings::Dark);
    QCOMPARE(s.theme(), ApplicationSettings::Dark);

    s.setTheme(ApplicationSettings::Light);
    QCOMPARE(s.theme(), ApplicationSettings::Light);

    s.setTheme(ApplicationSettings::System);
    QCOMPARE(s.theme(), ApplicationSettings::System);
}

void TestApplicationSettings::theme_emitsChangedSignal()
{
    ApplicationSettings s;
    QSignalSpy spy(&s, &ApplicationSettings::themeChanged);
    QVERIFY(spy.isValid());

    s.setTheme(ApplicationSettings::Dark);

    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).value<ApplicationSettings::ThemeEnum>(), ApplicationSettings::Dark);
}

void TestApplicationSettings::theme_persistsAcrossInstances()
{
    {
        ApplicationSettings s;
        s.setTheme(ApplicationSettings::Dark);
        s.sync();
    }

    ApplicationSettings s2;
    QCOMPARE(s2.theme(), ApplicationSettings::Dark);
}

QTEST_MAIN(TestApplicationSettings)

#include "test_application_settings.moc"
