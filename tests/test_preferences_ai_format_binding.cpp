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


// Exercises the AI format combo <-> ApplicationSettings binding from
// PreferencesDialog.cpp. We don't construct the full dialog because it pulls in
// Scintilla and SingleApplication; instead we replicate the combo statements
// against a freestanding QComboBox + ApplicationSettings.


#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include "ApplicationSettings.h"


static void bindAiFormatCombo(QComboBox *combo, ApplicationSettings *settings)
{
    combo->addItem(QStringLiteral("OpenAI-compatible"),
                   static_cast<int>(ApplicationSettings::OpenAiCompatible));
    combo->addItem(QStringLiteral("Anthropic"),
                   static_cast<int>(ApplicationSettings::Anthropic));
    {
        const int idx = combo->findData(static_cast<int>(settings->commitMessageApiFormat()));
        combo->setCurrentIndex(idx == -1 ? 0 : idx);
    }
    QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     combo, [=](int index) {
        settings->setCommitMessageApiFormat(static_cast<ApplicationSettings::AiApiFormatEnum>(
            combo->itemData(index).toInt()));
    });
    QObject::connect(settings, &ApplicationSettings::commitMessageApiFormatChanged,
                     combo, [=](ApplicationSettings::AiApiFormatEnum f) {
        const int idx = combo->findData(static_cast<int>(f));
        if (idx != -1) combo->setCurrentIndex(idx);
    });
}


class TestPreferencesAiFormatBinding : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void combo_hasTwoOptionsInExpectedOrder();
    void combo_defaultIsOpenAiCompatible();
    void combo_changingFormatLeavesUrlSetting();
    void combo_initialSelectionMatchesSetting();
    void combo_changingComboUpdatesSetting();
    void combo_changingSettingUpdatesCombo();

private:
    QTemporaryDir tempDir;
};

void TestPreferencesAiFormatBinding::initTestCase()
{
    QVERIFY(tempDir.isValid());
    QCoreApplication::setOrganizationName("NotepadNextTest");
    QCoreApplication::setApplicationName("NotepadNextTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
}

void TestPreferencesAiFormatBinding::init()
{
    ApplicationSettings s;
    s.clear();
    s.sync();
}

void TestPreferencesAiFormatBinding::combo_hasTwoOptionsInExpectedOrder()
{
    ApplicationSettings s;
    QComboBox combo;
    bindAiFormatCombo(&combo, &s);
    QCOMPARE(combo.count(), 2);
    QCOMPARE(combo.itemData(0).toInt(), static_cast<int>(ApplicationSettings::OpenAiCompatible));
    QCOMPARE(combo.itemData(1).toInt(), static_cast<int>(ApplicationSettings::Anthropic));
    QCOMPARE(combo.itemText(0), QStringLiteral("OpenAI-compatible"));
    QCOMPARE(combo.itemText(1), QStringLiteral("Anthropic"));
}

void TestPreferencesAiFormatBinding::combo_defaultIsOpenAiCompatible()
{
    ApplicationSettings s;
    QComboBox combo;
    bindAiFormatCombo(&combo, &s);
    QCOMPARE(combo.currentIndex(), 0);
    QCOMPARE(s.commitMessageApiFormat(), ApplicationSettings::OpenAiCompatible);
}

void TestPreferencesAiFormatBinding::combo_changingFormatLeavesUrlSetting()
{
    ApplicationSettings s;
    s.setCommitMessageProviderUrl(QStringLiteral("https://keep.example/v1"));
    QComboBox combo;
    bindAiFormatCombo(&combo, &s);
    combo.setCurrentIndex(1);
    QCOMPARE(s.commitMessageApiFormat(), ApplicationSettings::Anthropic);
    QCOMPARE(s.commitMessageProviderUrl(), QStringLiteral("https://keep.example/v1"));
}

void TestPreferencesAiFormatBinding::combo_initialSelectionMatchesSetting()
{
    ApplicationSettings s;
    s.setCommitMessageApiFormat(ApplicationSettings::Anthropic);
    QComboBox combo;
    bindAiFormatCombo(&combo, &s);
    QCOMPARE(combo.currentData().toInt(), static_cast<int>(ApplicationSettings::Anthropic));
}

void TestPreferencesAiFormatBinding::combo_changingComboUpdatesSetting()
{
    ApplicationSettings s;
    QComboBox combo;
    bindAiFormatCombo(&combo, &s);
    combo.setCurrentIndex(1);
    QCOMPARE(s.commitMessageApiFormat(), ApplicationSettings::Anthropic);
}

void TestPreferencesAiFormatBinding::combo_changingSettingUpdatesCombo()
{
    ApplicationSettings s;
    QComboBox combo;
    bindAiFormatCombo(&combo, &s);
    s.setCommitMessageApiFormat(ApplicationSettings::Anthropic);
    QCOMPARE(combo.currentData().toInt(), static_cast<int>(ApplicationSettings::Anthropic));
}

QTEST_MAIN(TestPreferencesAiFormatBinding)

#include "test_preferences_ai_format_binding.moc"
