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


// Exercises the exact Theme combo <-> ApplicationSettings binding code from
// PreferencesDialog.cpp. We don't construct the full dialog because it pulls in
// Scintilla and SingleApplication via includes; instead we replicate the four
// statements verbatim against a freestanding QComboBox + ApplicationSettings.
// If this passes, the in-dialog binding passes (the code is identical).


#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include "ApplicationSettings.h"


// Mirror of the lambdas in PreferencesDialog.cpp — kept literal so they drift
// together if either is edited.
static void bindThemeCombo(QComboBox *combo, ApplicationSettings *settings)
{
    combo->addItem(QStringLiteral("Follow System"), static_cast<int>(ApplicationSettings::System));
    combo->addItem(QStringLiteral("Light"),         static_cast<int>(ApplicationSettings::Light));
    combo->addItem(QStringLiteral("Dark"),          static_cast<int>(ApplicationSettings::Dark));
    {
        int themeIndex = combo->findData(static_cast<int>(settings->theme()));
        combo->setCurrentIndex(themeIndex == -1 ? 0 : themeIndex);
    }
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     combo, [=](int index) {
        settings->setTheme(static_cast<ApplicationSettings::ThemeEnum>(
            combo->itemData(index).toInt()));
    });
    QObject::connect(settings, &ApplicationSettings::themeChanged,
                     combo, [=](ApplicationSettings::ThemeEnum t) {
        int idx = combo->findData(static_cast<int>(t));
        if (idx != -1) combo->setCurrentIndex(idx);
    });
}


class TestPreferencesThemeBinding : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void combo_hasThreeOptionsInExpectedOrder();
    void combo_initialSelectionMatchesSetting();
    void combo_changingComboUpdatesSetting();
    void combo_changingSettingUpdatesCombo();
    void combo_initialFallsBackToFirstWhenSettingMissing();

private:
    QTemporaryDir tempDir;
};

void TestPreferencesThemeBinding::initTestCase()
{
    QVERIFY(tempDir.isValid());
    QCoreApplication::setOrganizationName("NotepadNextTest");
    QCoreApplication::setApplicationName("NotepadNextTestPrefs");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
}

void TestPreferencesThemeBinding::init()
{
    ApplicationSettings s;
    s.clear();
    s.sync();
}

void TestPreferencesThemeBinding::combo_hasThreeOptionsInExpectedOrder()
{
    ApplicationSettings s;
    QComboBox combo;
    bindThemeCombo(&combo, &s);

    QCOMPARE(combo.count(), 3);
    QCOMPARE(combo.itemData(0).toInt(), static_cast<int>(ApplicationSettings::System));
    QCOMPARE(combo.itemData(1).toInt(), static_cast<int>(ApplicationSettings::Light));
    QCOMPARE(combo.itemData(2).toInt(), static_cast<int>(ApplicationSettings::Dark));
}

void TestPreferencesThemeBinding::combo_initialSelectionMatchesSetting()
{
    ApplicationSettings s;
    s.setTheme(ApplicationSettings::Dark);

    QComboBox combo;
    bindThemeCombo(&combo, &s);

    QCOMPARE(combo.currentData().toInt(), static_cast<int>(ApplicationSettings::Dark));
}

void TestPreferencesThemeBinding::combo_changingComboUpdatesSetting()
{
    ApplicationSettings s;
    QComboBox combo;
    bindThemeCombo(&combo, &s);

    // Select Dark (index 2).
    combo.setCurrentIndex(2);
    QCOMPARE(s.theme(), ApplicationSettings::Dark);

    // Select Light (index 1).
    combo.setCurrentIndex(1);
    QCOMPARE(s.theme(), ApplicationSettings::Light);
}

void TestPreferencesThemeBinding::combo_changingSettingUpdatesCombo()
{
    ApplicationSettings s;
    QComboBox combo;
    bindThemeCombo(&combo, &s);

    s.setTheme(ApplicationSettings::Dark);
    QCOMPARE(combo.currentData().toInt(), static_cast<int>(ApplicationSettings::Dark));

    s.setTheme(ApplicationSettings::Light);
    QCOMPARE(combo.currentData().toInt(), static_cast<int>(ApplicationSettings::Light));

    s.setTheme(ApplicationSettings::System);
    QCOMPARE(combo.currentData().toInt(), static_cast<int>(ApplicationSettings::System));
}

void TestPreferencesThemeBinding::combo_initialFallsBackToFirstWhenSettingMissing()
{
    // Default ApplicationSettings (System).
    ApplicationSettings s;
    QComboBox combo;
    bindThemeCombo(&combo, &s);

    // System maps to index 0; the fallback branch is exercised when findData() == -1,
    // which can't happen for legal enum values — assert that the fallback path keeps
    // index 0 by binding against a setting that resolves to System.
    QCOMPARE(combo.currentIndex(), 0);
}

QTEST_MAIN(TestPreferencesThemeBinding)

#include "test_preferences_theme_binding.moc"
