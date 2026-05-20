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
#include <QString>

#include "StylesheetResource.h"


class TestStylesheetResource : public QObject
{
    Q_OBJECT

private slots:
    void dark_returnsDarkCss();
    void light_returnsLightCss();
    void resourceFormat_isQrcPath();
    void darkAndLight_areDifferent();
};

void TestStylesheetResource::dark_returnsDarkCss()
{
    QCOMPARE(chooseStylesheetResource(true), QStringLiteral(":/stylesheets/npp_dark.css"));
}

void TestStylesheetResource::light_returnsLightCss()
{
    QCOMPARE(chooseStylesheetResource(false), QStringLiteral(":/stylesheets/npp.css"));
}

void TestStylesheetResource::resourceFormat_isQrcPath()
{
    QVERIFY(chooseStylesheetResource(true).startsWith(QStringLiteral(":/")));
    QVERIFY(chooseStylesheetResource(false).startsWith(QStringLiteral(":/")));
}

void TestStylesheetResource::darkAndLight_areDifferent()
{
    QVERIFY(chooseStylesheetResource(true) != chooseStylesheetResource(false));
}

QTEST_APPLESS_MAIN(TestStylesheetResource)

#include "test_stylesheet_resource.moc"
