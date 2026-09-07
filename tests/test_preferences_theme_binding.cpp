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


// Exercises the Theme combo <-> ApplicationSettings binding used by
// PreferencesDialog.cpp: populate immediately, write only on apply().


#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include "ApplicationSettings.h"
#include "dialogs/PreferencesPendingEdits.h"


static void bindThemeCombo(QComboBox *combo, ApplicationSettings *settings, PreferencesPendingEdits *pending)
{
    combo->addItem(QStringLiteral("Follow System"), static_cast<int>(ApplicationSettings::System));
    combo->addItem(QStringLiteral("Light"),         static_cast<int>(ApplicationSettings::Light));
    combo->addItem(QStringLiteral("Dark"),          static_cast<int>(ApplicationSettings::Dark));
    {
        int themeIndex = combo->findData(static_cast<int>(settings->theme()));
        combo->setCurrentIndex(themeIndex == -1 ? 0 : themeIndex);
    }
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     combo, [=](int) {
        pending->markDirty();
    });
    pending->addApply([=]() {
        settings->setTheme(static_cast<ApplicationSettings::ThemeEnum>(
            combo->itemData(combo->currentIndex()).toInt()));
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
    void combo_changingComboDoesNotWriteUntilApply();
    void combo_applyWritesSetting();
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
    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindThemeCombo(&combo, &s, &pending);

    QCOMPARE(combo.count(), 3);
    QCOMPARE(combo.itemData(0).toInt(), static_cast<int>(ApplicationSettings::System));
    QCOMPARE(combo.itemData(1).toInt(), static_cast<int>(ApplicationSettings::Light));
    QCOMPARE(combo.itemData(2).toInt(), static_cast<int>(ApplicationSettings::Dark));
}

void TestPreferencesThemeBinding::combo_initialSelectionMatchesSetting()
{
    ApplicationSettings s;
    s.setTheme(ApplicationSettings::Dark);

    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindThemeCombo(&combo, &s, &pending);

    QCOMPARE(combo.currentData().toInt(), static_cast<int>(ApplicationSettings::Dark));
}

void TestPreferencesThemeBinding::combo_changingComboDoesNotWriteUntilApply()
{
    ApplicationSettings s;
    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindThemeCombo(&combo, &s, &pending);

    const auto before = s.theme();
    combo.setCurrentIndex(2);
    QCOMPARE(s.theme(), before);
    QVERIFY(pending.isDirty());
}

void TestPreferencesThemeBinding::combo_applyWritesSetting()
{
    ApplicationSettings s;
    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindThemeCombo(&combo, &s, &pending);

    combo.setCurrentIndex(2);
    pending.apply();
    QCOMPARE(s.theme(), ApplicationSettings::Dark);

    combo.setCurrentIndex(1);
    pending.apply();
    QCOMPARE(s.theme(), ApplicationSettings::Light);
}

void TestPreferencesThemeBinding::combo_initialFallsBackToFirstWhenSettingMissing()
{
    ApplicationSettings s;
    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindThemeCombo(&combo, &s, &pending);

    QCOMPARE(combo.currentIndex(), 0);
}

QTEST_MAIN(TestPreferencesThemeBinding)

#include "test_preferences_theme_binding.moc"
