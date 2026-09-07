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
// PreferencesDialog.cpp: populate immediately, write only on apply().


#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include "ApplicationSettings.h"
#include "dialogs/PreferencesPendingEdits.h"


static void bindAiFormatCombo(QComboBox *combo, ApplicationSettings *settings, PreferencesPendingEdits *pending)
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
                     combo, [=](int) {
        pending->markDirty();
    });
    pending->addApply([=]() {
        settings->setCommitMessageApiFormat(static_cast<ApplicationSettings::AiApiFormatEnum>(
            combo->itemData(combo->currentIndex()).toInt()));
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
    void combo_changingComboDoesNotWriteUntilApply();
    void combo_applyWritesSetting();

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
    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindAiFormatCombo(&combo, &s, &pending);
    QCOMPARE(combo.count(), 2);
    QCOMPARE(combo.itemData(0).toInt(), static_cast<int>(ApplicationSettings::OpenAiCompatible));
    QCOMPARE(combo.itemData(1).toInt(), static_cast<int>(ApplicationSettings::Anthropic));
    QCOMPARE(combo.itemText(0), QStringLiteral("OpenAI-compatible"));
    QCOMPARE(combo.itemText(1), QStringLiteral("Anthropic"));
}

void TestPreferencesAiFormatBinding::combo_defaultIsOpenAiCompatible()
{
    ApplicationSettings s;
    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindAiFormatCombo(&combo, &s, &pending);
    QCOMPARE(combo.currentIndex(), 0);
    QCOMPARE(s.commitMessageApiFormat(), ApplicationSettings::OpenAiCompatible);
}

void TestPreferencesAiFormatBinding::combo_changingFormatLeavesUrlSetting()
{
    ApplicationSettings s;
    s.setCommitMessageProviderUrl(QStringLiteral("https://keep.example/v1"));
    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindAiFormatCombo(&combo, &s, &pending);
    combo.setCurrentIndex(1);
    pending.apply();
    QCOMPARE(s.commitMessageApiFormat(), ApplicationSettings::Anthropic);
    QCOMPARE(s.commitMessageProviderUrl(), QStringLiteral("https://keep.example/v1"));
}

void TestPreferencesAiFormatBinding::combo_initialSelectionMatchesSetting()
{
    ApplicationSettings s;
    s.setCommitMessageApiFormat(ApplicationSettings::Anthropic);
    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindAiFormatCombo(&combo, &s, &pending);
    QCOMPARE(combo.currentData().toInt(), static_cast<int>(ApplicationSettings::Anthropic));
}

void TestPreferencesAiFormatBinding::combo_changingComboDoesNotWriteUntilApply()
{
    ApplicationSettings s;
    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindAiFormatCombo(&combo, &s, &pending);
    combo.setCurrentIndex(1);
    QCOMPARE(s.commitMessageApiFormat(), ApplicationSettings::OpenAiCompatible);
}

void TestPreferencesAiFormatBinding::combo_applyWritesSetting()
{
    ApplicationSettings s;
    PreferencesPendingEdits pending(&s);
    QComboBox combo;
    bindAiFormatCombo(&combo, &s, &pending);
    combo.setCurrentIndex(1);
    pending.apply();
    QCOMPARE(s.commitMessageApiFormat(), ApplicationSettings::Anthropic);
}

QTEST_MAIN(TestPreferencesAiFormatBinding)

#include "test_preferences_ai_format_binding.moc"
