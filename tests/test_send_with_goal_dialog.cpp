#include <QtTest>

#include <QComboBox>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>

#include "AcpAgentRegistry.h"
#include "ApplicationSettings.h"
#include "GoalAgentSettings.h"
#include "GoalConfigWidget.h"
#include "GoalHttpJudge.h"
#include "SendWithGoalDialog.h"

class TestSendWithGoalDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void changedAgent_isRestoredAfterCancel();
    void changedAgent_isRestoredAfterEscape();
    void changedAgent_isRestoredAfterAccept();
    void removedStoredAgent_fallsBackAndNormalizesSetting();
    void customApi_isListedInCombo();
    void customApi_showsFieldsWhenSelected();
    void customApi_statusEmptyPartialInvalidIdeal();
    void customApi_loadingDisablesFields();

private:
    static QComboBox *agentCombo(SendWithGoalDialog &dialog);
    static QPushButton *dialogButton(SendWithGoalDialog &dialog, const QString &text);
    static QString storedAgentId(ApplicationSettings &settings);
    static void selectCodex(SendWithGoalDialog &dialog);

    QTemporaryDir m_settingsDir;
};

void TestSendWithGoalDialog::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("NotepadNextTest"));
    QCoreApplication::setApplicationName(QStringLiteral("NotepadNextTest_SendWithGoalDialog"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void TestSendWithGoalDialog::init()
{
    ApplicationSettings settings;
    settings.clear();
    settings.sync();
}

QComboBox *TestSendWithGoalDialog::agentCombo(SendWithGoalDialog &dialog)
{
    const auto combos = dialog.findChildren<QComboBox *>();
    for (QComboBox *combo : combos) {
        if (combo->findData(AcpAgentRegistry::builtinClaudeCodeId()) >= 0
            && combo->findData(AcpAgentRegistry::builtinCodexId()) >= 0) {
            return combo;
        }
    }
    return nullptr;
}

QPushButton *TestSendWithGoalDialog::dialogButton(SendWithGoalDialog &dialog, const QString &text)
{
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

QString TestSendWithGoalDialog::storedAgentId(ApplicationSettings &settings)
{
    const QString json = settings.get("Ai/GoalAgentSettings", QString());
    return GoalAgentSettings::fromJson(QJsonDocument::fromJson(json.toUtf8()).object()).agentId;
}

void TestSendWithGoalDialog::selectCodex(SendWithGoalDialog &dialog)
{
    QComboBox *combo = agentCombo(dialog);
    QVERIFY(combo);
    const int codexIndex = combo->findData(AcpAgentRegistry::builtinCodexId());
    QVERIFY(codexIndex >= 0);
    combo->setCurrentIndex(codexIndex);
}

void TestSendWithGoalDialog::changedAgent_isRestoredAfterCancel()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    selectCodex(first);
    QCOMPARE(storedAgentId(settings), AcpAgentRegistry::builtinCodexId());
    QVERIFY(dialogButton(first, QStringLiteral("Cancel")));
    QTest::mouseClick(dialogButton(first, QStringLiteral("Cancel")), Qt::LeftButton);
    QCOMPARE(first.result(), QDialog::Rejected);

    SendWithGoalDialog reopened(&registry, &settings);
    QCOMPARE(agentCombo(reopened)->currentData().toString(), AcpAgentRegistry::builtinCodexId());
}

void TestSendWithGoalDialog::changedAgent_isRestoredAfterEscape()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    selectCodex(first);
    QCOMPARE(storedAgentId(settings), AcpAgentRegistry::builtinCodexId());
    QTest::keyClick(&first, Qt::Key_Escape);
    QCOMPARE(first.result(), QDialog::Rejected);

    SendWithGoalDialog reopened(&registry, &settings);
    QCOMPARE(agentCombo(reopened)->currentData().toString(), AcpAgentRegistry::builtinCodexId());
}

void TestSendWithGoalDialog::changedAgent_isRestoredAfterAccept()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    selectCodex(first);
    QCOMPARE(storedAgentId(settings), AcpAgentRegistry::builtinCodexId());
    QVERIFY(dialogButton(first, QStringLiteral("Start Goal")));
    const auto criteria = first.findChildren<QPlainTextEdit *>();
    QVERIFY(!criteria.isEmpty());
    criteria.first()->setPlainText(QStringLiteral("Confirm selected agent"));
    QTest::mouseClick(dialogButton(first, QStringLiteral("Start Goal")), Qt::LeftButton);
    QCOMPARE(first.result(), QDialog::Accepted);

    SendWithGoalDialog reopened(&registry, &settings);
    QCOMPARE(agentCombo(reopened)->currentData().toString(), AcpAgentRegistry::builtinCodexId());
}

void TestSendWithGoalDialog::removedStoredAgent_fallsBackAndNormalizesSetting()
{
    ApplicationSettings settings;
    GoalAgentSettings goalSettings;
    goalSettings.agentId = QStringLiteral("removed-agent");
    settings.setValue(
        QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog dialog(&registry, &settings);
    QCOMPARE(agentCombo(dialog)->currentData().toString(), AcpAgentRegistry::builtinClaudeCodeId());
    QCOMPARE(storedAgentId(settings), AcpAgentRegistry::builtinClaudeCodeId());
}

void TestSendWithGoalDialog::customApi_isListedInCombo()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    SendWithGoalDialog dialog(&registry, &settings);
    QComboBox *combo = agentCombo(dialog);
    QVERIFY(combo);
    QVERIFY(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)) >= 0);
}

void TestSendWithGoalDialog::customApi_showsFieldsWhenSelected()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    SendWithGoalDialog dialog(&registry, &settings);
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    QVERIFY(fields->isHidden());
    QComboBox *combo = agentCombo(dialog);
    QVERIFY(combo);
    const int idx = combo->findData(QLatin1String(GoalHttpJudge::kAgentId));
    QVERIFY(idx >= 0);
    combo->setCurrentIndex(idx);
    QVERIFY(!fields->isHidden());
    QVERIFY(fields->findChild<QLineEdit *>());
    QCOMPARE(QString::fromLatin1(fields->metaObject()->className()),
             QStringLiteral("GoalCustomApiFields"));
}

void TestSendWithGoalDialog::customApi_statusEmptyPartialInvalidIdeal()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    SendWithGoalDialog dialog(&registry, &settings);
    QComboBox *combo = agentCombo(dialog);
    QVERIFY(combo);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));

    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    QLabel *status = fields->findChild<QLabel *>(QStringLiteral("customApiStatus"));
    QVERIFY(status);
    QVERIFY(!status->isHidden());
    QVERIFY(status->text().contains(QStringLiteral("Enter")));

    const auto edits = fields->findChildren<QLineEdit *>();
    QCOMPARE(edits.size(), 3);
    edits.at(0)->setText(QStringLiteral("not-a-url"));
    QVERIFY(status->text().contains(QLatin1String("http")));
    edits.at(0)->setText(QStringLiteral("https://api.anthropic.com"));
    edits.at(2)->clear();
    QVERIFY(!status->isHidden());
    QVERIFY(status->text().contains(QLatin1String("model"), Qt::CaseInsensitive));
    edits.at(2)->setText(QStringLiteral("claude-opus-5"));
    edits.at(1)->setText(QStringLiteral("sk-test-key"));
    QVERIFY(status->isHidden());
}

void TestSendWithGoalDialog::customApi_loadingDisablesFields()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    SendWithGoalDialog dialog(&registry, &settings);
    QComboBox *combo = agentCombo(dialog);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    auto *cfg = dialog.findChild<GoalConfigWidget *>();
    QVERIFY(cfg);
    cfg->setJudgeLoading(true);
    QLabel *status = fields->findChild<QLabel *>(QStringLiteral("customApiStatus"));
    QVERIFY(status);
    QCOMPARE(status->text(), QStringLiteral("Calling judge…"));
    QVERIFY(!status->isHidden());
    for (QLineEdit *edit : fields->findChildren<QLineEdit *>())
        QVERIFY(edit->isReadOnly());
    cfg->setJudgeLoading(false);
}

QTEST_MAIN(TestSendWithGoalDialog)

#include "test_send_with_goal_dialog.moc"
