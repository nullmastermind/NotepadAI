/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
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
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include "ApplicationSettings.h"
#include "dialogs/PreferencesPendingEdits.h"


class TestPreferencesPendingEdits : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void checkBox_toggleDoesNotWriteSetting();
    void checkBox_applyWritesSetting();
    void checkBox_toggleMarksDirty();
    void apply_clearsDirty();
    void mapCheckBox_loadDoesNotMarkDirty();
    void checkBox_revertRestoresWidgetAndClearsDirty();

private:
    QTemporaryDir tempDir;
};

void TestPreferencesPendingEdits::initTestCase()
{
    QVERIFY(tempDir.isValid());
    QCoreApplication::setOrganizationName("NotepadNextTest");
    QCoreApplication::setApplicationName("NotepadNextTestPendingEdits");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
}

void TestPreferencesPendingEdits::init()
{
    ApplicationSettings s;
    s.clear();
    s.sync();
}

void TestPreferencesPendingEdits::checkBox_toggleDoesNotWriteSetting()
{
    ApplicationSettings s;
    s.setShowLineNumbers(true);

    QCheckBox cb;
    PreferencesPendingEdits pending(&s);
    pending.mapCheckBox(&cb, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers);

    QCOMPARE(cb.isChecked(), true);
    cb.setChecked(false);
    QCOMPARE(s.showLineNumbers(), true);
}

void TestPreferencesPendingEdits::checkBox_applyWritesSetting()
{
    ApplicationSettings s;
    s.setShowLineNumbers(true);

    QCheckBox cb;
    PreferencesPendingEdits pending(&s);
    pending.mapCheckBox(&cb, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers);

    cb.setChecked(false);
    pending.apply();
    QCOMPARE(s.showLineNumbers(), false);
}

void TestPreferencesPendingEdits::checkBox_toggleMarksDirty()
{
    ApplicationSettings s;
    s.setShowLineNumbers(true);

    QCheckBox cb;
    PreferencesPendingEdits pending(&s);
    pending.mapCheckBox(&cb, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers);

    QVERIFY(!pending.isDirty());
    cb.setChecked(false);
    QVERIFY(pending.isDirty());
}

void TestPreferencesPendingEdits::apply_clearsDirty()
{
    ApplicationSettings s;
    s.setShowLineNumbers(true);

    QCheckBox cb;
    PreferencesPendingEdits pending(&s);
    pending.mapCheckBox(&cb, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers);

    cb.setChecked(false);
    pending.apply();
    QVERIFY(!pending.isDirty());
}

void TestPreferencesPendingEdits::mapCheckBox_loadDoesNotMarkDirty()
{
    ApplicationSettings s;
    s.setShowLineNumbers(true);

    QCheckBox cb;
    PreferencesPendingEdits pending(&s);
    pending.mapCheckBox(&cb, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers);

    QVERIFY(!pending.isDirty());
}

void TestPreferencesPendingEdits::checkBox_revertRestoresWidgetAndClearsDirty()
{
    ApplicationSettings s;
    s.setShowLineNumbers(true);

    QCheckBox cb;
    PreferencesPendingEdits pending(&s);
    pending.mapCheckBox(&cb, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers);

    cb.setChecked(false);
    pending.revert();
    QCOMPARE(cb.isChecked(), true);
    QCOMPARE(s.showLineNumbers(), true);
    QVERIFY(!pending.isDirty());
}

QTEST_MAIN(TestPreferencesPendingEdits)

#include "test_preferences_pending_edits.moc"
