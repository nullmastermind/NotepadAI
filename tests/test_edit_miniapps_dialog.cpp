/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QtTest>

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>

#include "ApplicationSettings.h"
#include "EditMiniAppsDialog.h"
#include "MiniAppRegistry.h"

class TestEditMiniAppsDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void advancedOff_disablesTimeout_and_keepsItAcrossRowSwitch();
    void advancedOff_persistsTimeoutAndRestoresOnRecheck();
    void legacyJson_withoutFlag_treatsCustomTimeoutAsEnabled();
    void randomPort_isOutsideLegacy9222Range();

private:
    static QPushButton *buttonWithText(QWidget &root, const QString &text);
    static QGroupBox *groupByTitle(QWidget &root, const QString &title);
    static QLineEdit *editWithPlaceholder(QWidget &root, const QString &placeholder);
    static QSpinBox *timeoutSpin(QWidget &root);
    static QSpinBox *debugPortSpin(QWidget &root);

    QTemporaryDir m_tempDir;
};

void TestEditMiniAppsDialog::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("NotepadNextTest"));
    QCoreApplication::setApplicationName(QStringLiteral("EditMiniAppsDialogTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
}

void TestEditMiniAppsDialog::init()
{
    ApplicationSettings settings;
    settings.clear();
    settings.sync();
}

QPushButton *TestEditMiniAppsDialog::buttonWithText(QWidget &root, const QString &text)
{
    for (QPushButton *button : root.findChildren<QPushButton *>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

QGroupBox *TestEditMiniAppsDialog::groupByTitle(QWidget &root, const QString &title)
{
    for (QGroupBox *box : root.findChildren<QGroupBox *>()) {
        if (box->title() == title)
            return box;
    }
    return nullptr;
}

QLineEdit *TestEditMiniAppsDialog::editWithPlaceholder(QWidget &root, const QString &placeholder)
{
    for (QLineEdit *edit : root.findChildren<QLineEdit *>()) {
        if (edit->placeholderText() == placeholder)
            return edit;
    }
    return nullptr;
}

QSpinBox *TestEditMiniAppsDialog::timeoutSpin(QWidget &root)
{
    for (QSpinBox *spin : root.findChildren<QSpinBox *>()) {
        if (spin->minimum() == 5 && spin->maximum() == 300)
            return spin;
    }
    return nullptr;
}

QSpinBox *TestEditMiniAppsDialog::debugPortSpin(QWidget &root)
{
    for (QSpinBox *spin : root.findChildren<QSpinBox *>()) {
        if (spin->specialValueText() == QStringLiteral("Disabled"))
            return spin;
    }
    return nullptr;
}

void TestEditMiniAppsDialog::advancedOff_disablesTimeout_and_keepsItAcrossRowSwitch()
{
    ApplicationSettings settings;
    MiniAppRegistry registry(&settings);
    EditMiniAppsDialog dialog(&registry, QString());

    QPushButton *add = buttonWithText(dialog, QStringLiteral("+"));
    QVERIFY(add);
    add->click();

    QGroupBox *advanced = groupByTitle(dialog, QStringLiteral("Advanced"));
    QSpinBox *timeout = timeoutSpin(dialog);
    QLineEdit *health = editWithPlaceholder(dialog, QStringLiteral("(defaults to main URL)"));
    QVERIFY(advanced);
    QVERIFY(timeout);
    QVERIFY(health);

    // Advanced starts off, and the timeout field must not be editable in that state.
    QVERIFY(!advanced->isChecked());
    QVERIFY(!timeout->isEnabled());

    advanced->setChecked(true);
    QVERIFY(timeout->isEnabled());
    timeout->setValue(120);
    health->setText(QStringLiteral("https://g.ai/health"));
    advanced->setChecked(false);
    QCOMPARE(timeout->value(), 120);
    QVERIFY(!timeout->isEnabled());

    add->click();
    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    QCOMPARE(list->count(), 2);
    list->setCurrentRow(0);

    QVERIFY(!advanced->isChecked());
    QVERIFY(!timeout->isEnabled());
    QCOMPARE(timeout->value(), 120);
    QCOMPARE(health->text(), QStringLiteral("https://g.ai/health"));

    advanced->setChecked(true);
    QVERIFY(timeout->isEnabled());
    QCOMPARE(timeout->value(), 120);
    QCOMPARE(health->text(), QStringLiteral("https://g.ai/health"));
}

void TestEditMiniAppsDialog::advancedOff_persistsTimeoutAndRestoresOnRecheck()
{
    ApplicationSettings settings;
    MiniAppRegistry registry(&settings);
    {
        EditMiniAppsDialog dialog(&registry, QString());
        QPushButton *add = buttonWithText(dialog, QStringLiteral("+"));
        QVERIFY(add);
        add->click();

        editWithPlaceholder(dialog, QStringLiteral("Display name (required)"))
            ->setText(QStringLiteral("Google"));
        editWithPlaceholder(dialog, QStringLiteral("http://localhost:3000"))
            ->setText(QStringLiteral("https://g.ai"));

        QGroupBox *advanced = groupByTitle(dialog, QStringLiteral("Advanced"));
        QVERIFY(advanced);
        advanced->setChecked(true);
        timeoutSpin(dialog)->setValue(120);
        editWithPlaceholder(dialog, QStringLiteral("(defaults to main URL)"))
            ->setText(QStringLiteral("https://g.ai/health"));
        advanced->setChecked(false);

        auto *box = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(box);
        box->button(QDialogButtonBox::Ok)->click();
        QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    }

    const QList<MiniAppDefinition> saved = registry.globalApps();
    QCOMPARE(saved.size(), 1);
    QCOMPARE(saved[0].healthTimeoutMs, 120000);
    QCOMPARE(saved[0].healthCheckUrl, QStringLiteral("https://g.ai/health"));
    QVERIFY(!saved[0].advancedEnabled);
    QCOMPARE(saved[0].effectiveHealthTimeoutMs(), 60000);
    QCOMPARE(saved[0].effectiveHealthUrl(), QStringLiteral("https://g.ai"));

    EditMiniAppsDialog again(&registry, QString());
    QGroupBox *advanced = groupByTitle(again, QStringLiteral("Advanced"));
    QSpinBox *timeout = timeoutSpin(again);
    QLineEdit *health = editWithPlaceholder(again, QStringLiteral("(defaults to main URL)"));
    QVERIFY(advanced);
    QVERIFY(timeout);
    QVERIFY(health);
    QVERIFY(!advanced->isChecked());
    QCOMPARE(timeout->value(), 120);
    QCOMPARE(health->text(), QStringLiteral("https://g.ai/health"));

    advanced->setChecked(true);
    QVERIFY(timeout->isEnabled());
    QCOMPARE(timeout->value(), 120);
    QCOMPARE(health->text(), QStringLiteral("https://g.ai/health"));
}

void TestEditMiniAppsDialog::legacyJson_withoutFlag_treatsCustomTimeoutAsEnabled()
{
    ApplicationSettings settings;
    settings.setMiniAppsGlobalJson(QStringLiteral(
        R"([{"id":"a","name":"Old","url":"https://example.com","healthTimeoutMs":120000}])"));
    MiniAppRegistry registry(&settings);
    const QList<MiniAppDefinition> apps = registry.globalApps();
    QCOMPARE(apps.size(), 1);
    QVERIFY(apps[0].advancedEnabled);
    QCOMPARE(apps[0].healthTimeoutMs, 120000);
    QCOMPARE(apps[0].effectiveHealthTimeoutMs(), 120000);
}

void TestEditMiniAppsDialog::randomPort_isOutsideLegacy9222Range()
{
    ApplicationSettings settings;
    MiniAppRegistry registry(&settings);
    EditMiniAppsDialog dialog(&registry, QString());

    QPushButton *add = buttonWithText(dialog, QStringLiteral("+"));
    QVERIFY(add);
    add->click();

    QGroupBox *debug = groupByTitle(dialog, QStringLiteral("Debug"));
    QPushButton *random = buttonWithText(dialog, QStringLiteral("Random"));
    QSpinBox *port = debugPortSpin(dialog);
    QVERIFY(debug);
    QVERIFY(random);
    QVERIFY(port);

    debug->setChecked(true);
    QVERIFY(random->isEnabled());
    const int before = port->value();
    random->click();
    const int assigned = port->value();
    QVERIFY(assigned > 0);
    QVERIFY(assigned <= 65535);
    QVERIFY(assigned < 9222 || assigned > 9322);
    QVERIFY(assigned != before);
}

QTEST_MAIN(TestEditMiniAppsDialog)

#include "test_edit_miniapps_dialog.moc"
