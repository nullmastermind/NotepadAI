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

#include "ApplicationSettings.h"
#include "ThemeResolver.h"


class TestThemeResolver : public QObject
{
    Q_OBJECT

private slots:
    void light_alwaysReturnsFalse();
    void dark_alwaysReturnsTrue();
    void system_followsColorScheme();
};

void TestThemeResolver::light_alwaysReturnsFalse()
{
    QCOMPARE(resolveThemeIsDark(ApplicationSettings::Light, Qt::ColorScheme::Light),   false);
    QCOMPARE(resolveThemeIsDark(ApplicationSettings::Light, Qt::ColorScheme::Dark),    false);
    QCOMPARE(resolveThemeIsDark(ApplicationSettings::Light, Qt::ColorScheme::Unknown), false);
}

void TestThemeResolver::dark_alwaysReturnsTrue()
{
    QCOMPARE(resolveThemeIsDark(ApplicationSettings::Dark, Qt::ColorScheme::Light),   true);
    QCOMPARE(resolveThemeIsDark(ApplicationSettings::Dark, Qt::ColorScheme::Dark),    true);
    QCOMPARE(resolveThemeIsDark(ApplicationSettings::Dark, Qt::ColorScheme::Unknown), true);
}

void TestThemeResolver::system_followsColorScheme()
{
    QCOMPARE(resolveThemeIsDark(ApplicationSettings::System, Qt::ColorScheme::Dark),    true);
    QCOMPARE(resolveThemeIsDark(ApplicationSettings::System, Qt::ColorScheme::Light),   false);
    // Unknown is the platform telling us it can't say. We treat it as light.
    QCOMPARE(resolveThemeIsDark(ApplicationSettings::System, Qt::ColorScheme::Unknown), false);
}

QTEST_APPLESS_MAIN(TestThemeResolver)

#include "test_theme_resolver.moc"
