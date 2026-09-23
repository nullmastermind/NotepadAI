#include <QtTest>

#include <QCheckBox>
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
#include "GoalAgentConfigDialog.h"

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
    void autoCompact_defaultsUnchecked();
    void autoCompact_isRestoredAfterCancel();
    void autoCompact_isRestoredAfterAccept();
    void useNativeGoal_defaultsChecked();
    void useNativeGoal_uncheckedIsRestored();
    void useNativeGoal_goalResultFollowsCheck();
    void useNativeGoal_missingKeyStaysChecked();
    void changedTemplate_isRestoredAfterCancel();
    void changedTemplate_isRestoredAfterAccept();
    void removedStoredTemplate_fallsBackToDefault();
    void missingTemplateKey_openDoesNotMutateSettings();
    void switchedBackToDefault_isRestored();
    void corruptJson_openLeavesBlob();
    void remember_preservesUnknownKey_andIsIdempotent();
    void remember_rejectsNullEmptyAndCorrupt();
    void failedStart_keepsSelectedTemplate();
    void untouchedDefault_startWritesOnce();
    void scheduledTaskDialog_doesNotClobberSendTemplate();

private:
    static QComboBox *agentCombo(SendWithGoalDialog &dialog);
    static QComboBox *templateCombo(SendWithGoalDialog &dialog);
    static QPushButton *dialogButton(SendWithGoalDialog &dialog, const QString &text);
    static QString storedAgentId(ApplicationSettings &settings);
    static QString storedPromptTemplateId(ApplicationSettings &settings);
    static void selectCodex(SendWithGoalDialog &dialog);
    static void seedClassifyTemplate(ApplicationSettings &settings);
    static void selectClassify(SendWithGoalDialog &dialog);

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

QComboBox *TestSendWithGoalDialog::templateCombo(SendWithGoalDialog &dialog)
{
    for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
        if (combo->findData(QLatin1String(GoalAgentSettings::kDefaultTemplateId)) >= 0
            && combo->findData(AcpAgentRegistry::builtinClaudeCodeId()) < 0) {
            return combo;
        }
    }
    return nullptr;
}

QString TestSendWithGoalDialog::storedPromptTemplateId(ApplicationSettings &settings)
{
    const QString json = settings.get("Ai/GoalAgentSettings", QString());
    return GoalAgentSettings::fromJson(QJsonDocument::fromJson(json.toUtf8()).object()).promptTemplateId;
}

void TestSendWithGoalDialog::seedClassifyTemplate(ApplicationSettings &settings)
{
    GoalAgentSettings goalSettings;
    GoalPromptTemplate tpl;
    tpl.id = QStringLiteral("tpl-classify");
    tpl.name = QStringLiteral("classify");
    tpl.content = QStringLiteral("classify {{goal}}");
    goalSettings.promptTemplates.append(tpl);
    settings.setValue(
        QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
}

void TestSendWithGoalDialog::selectClassify(SendWithGoalDialog &dialog)
{
    QComboBox *combo = templateCombo(dialog);
    QVERIFY(combo);
    const int idx = combo->findData(QStringLiteral("tpl-classify"));
    QVERIFY(idx >= 0);
    combo->setCurrentIndex(idx);
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

void TestSendWithGoalDialog::autoCompact_defaultsUnchecked()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    SendWithGoalDialog dialog(&registry, &settings);
    auto *check = dialog.findChild<QCheckBox *>(QStringLiteral("autoCompactCheck"));
    QVERIFY(check);
    QVERIFY(!check->isChecked());
    QCOMPARE(dialog.goalResult().autoCompact, false);
}

void TestSendWithGoalDialog::autoCompact_isRestoredAfterCancel()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    auto *check = first.findChild<QCheckBox *>(QStringLiteral("autoCompactCheck"));
    QVERIFY(check);
    check->setChecked(true);
    const GoalAgentSettings stored = GoalAgentSettings::fromJson(
        QJsonDocument::fromJson(settings.get("Ai/GoalAgentSettings", QString()).toUtf8()).object());
    QVERIFY(stored.autoCompact);
    QVERIFY(dialogButton(first, QStringLiteral("Cancel")));
    QTest::mouseClick(dialogButton(first, QStringLiteral("Cancel")), Qt::LeftButton);
    QCOMPARE(first.result(), QDialog::Rejected);

    SendWithGoalDialog reopened(&registry, &settings);
    auto *reopenedCheck = reopened.findChild<QCheckBox *>(QStringLiteral("autoCompactCheck"));
    QVERIFY(reopenedCheck);
    QVERIFY(reopenedCheck->isChecked());
    QCOMPARE(reopened.goalResult().autoCompact, true);
}

void TestSendWithGoalDialog::autoCompact_isRestoredAfterAccept()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    auto *check = first.findChild<QCheckBox *>(QStringLiteral("autoCompactCheck"));
    QVERIFY(check);
    check->setChecked(true);
    const auto criteria = first.findChildren<QPlainTextEdit *>();
    QVERIFY(!criteria.isEmpty());
    criteria.first()->setPlainText(QStringLiteral("Confirm auto compact"));
    QVERIFY(dialogButton(first, QStringLiteral("Start Goal")));
    QTest::mouseClick(dialogButton(first, QStringLiteral("Start Goal")), Qt::LeftButton);
    QCOMPARE(first.result(), QDialog::Accepted);
    QCOMPARE(first.goalResult().autoCompact, true);

    SendWithGoalDialog reopened(&registry, &settings);
    auto *reopenedCheck = reopened.findChild<QCheckBox *>(QStringLiteral("autoCompactCheck"));
    QVERIFY(reopenedCheck);
    QVERIFY(reopenedCheck->isChecked());
}

void TestSendWithGoalDialog::useNativeGoal_defaultsChecked()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    SendWithGoalDialog dialog(&registry, &settings);
    auto *check = dialog.findChild<QCheckBox *>(QStringLiteral("useNativeGoalCheck"));
    QVERIFY(check);
    QVERIFY(check->isChecked());
}

void TestSendWithGoalDialog::useNativeGoal_uncheckedIsRestored()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    auto *check = first.findChild<QCheckBox *>(QStringLiteral("useNativeGoalCheck"));
    QVERIFY(check);
    check->setChecked(false);

    SendWithGoalDialog reopened(&registry, &settings);
    auto *reopenedCheck = reopened.findChild<QCheckBox *>(QStringLiteral("useNativeGoalCheck"));
    QVERIFY(reopenedCheck);
    QVERIFY(!reopenedCheck->isChecked());
}

void TestSendWithGoalDialog::useNativeGoal_goalResultFollowsCheck()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    SendWithGoalDialog dialog(&registry, &settings);
    auto *check = dialog.findChild<QCheckBox *>(QStringLiteral("useNativeGoalCheck"));
    QVERIFY(check);
    check->setChecked(false);
    QCOMPARE(dialog.goalResult().useNativeGoal, false);
    check->setChecked(true);
    QCOMPARE(dialog.goalResult().useNativeGoal, true);
}

void TestSendWithGoalDialog::useNativeGoal_missingKeyStaysChecked()
{
    ApplicationSettings settings;
    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"),
                      QStringLiteral("{\"autoCompact\":true}"));
    AcpAgentRegistry registry(&settings);
    SendWithGoalDialog dialog(&registry, &settings);
    auto *check = dialog.findChild<QCheckBox *>(QStringLiteral("useNativeGoalCheck"));
    QVERIFY(check);
    QVERIFY(check->isChecked());
    QCOMPARE(dialog.goalResult().useNativeGoal, true);
}

void TestSendWithGoalDialog::changedTemplate_isRestoredAfterCancel()
{
    ApplicationSettings settings;
    seedClassifyTemplate(settings);
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    selectClassify(first);
    QCOMPARE(storedPromptTemplateId(settings), QStringLiteral("tpl-classify"));
    QVERIFY(dialogButton(first, QStringLiteral("Cancel")));
    QTest::mouseClick(dialogButton(first, QStringLiteral("Cancel")), Qt::LeftButton);
    QCOMPARE(first.result(), QDialog::Rejected);

    SendWithGoalDialog reopened(&registry, &settings);
    QCOMPARE(templateCombo(reopened)->currentData().toString(), QStringLiteral("tpl-classify"));
}

void TestSendWithGoalDialog::changedTemplate_isRestoredAfterAccept()
{
    ApplicationSettings settings;
    seedClassifyTemplate(settings);
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    selectClassify(first);
    QCOMPARE(storedPromptTemplateId(settings), QStringLiteral("tpl-classify"));
    const auto criteria = first.findChildren<QPlainTextEdit *>();
    QVERIFY(!criteria.isEmpty());
    criteria.first()->setPlainText(QStringLiteral("Confirm selected template"));
    QVERIFY(dialogButton(first, QStringLiteral("Start Goal")));
    QTest::mouseClick(dialogButton(first, QStringLiteral("Start Goal")), Qt::LeftButton);
    QCOMPARE(first.result(), QDialog::Accepted);
    QCOMPARE(first.goalResult().promptTemplateId, QStringLiteral("tpl-classify"));

    SendWithGoalDialog reopened(&registry, &settings);
    QCOMPARE(templateCombo(reopened)->currentData().toString(), QStringLiteral("tpl-classify"));
}

void TestSendWithGoalDialog::removedStoredTemplate_fallsBackToDefault()
{
    ApplicationSettings settings;
    GoalAgentSettings goalSettings;
    goalSettings.promptTemplateId = QStringLiteral("removed-template");
    settings.setValue(
        QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog dialog(&registry, &settings);
    QCOMPARE(templateCombo(dialog)->currentData().toString(),
             QString::fromLatin1(GoalAgentSettings::kDefaultTemplateId));
    QCOMPARE(storedPromptTemplateId(settings),
             QString::fromLatin1(GoalAgentSettings::kDefaultTemplateId));
}

void TestSendWithGoalDialog::missingTemplateKey_openDoesNotMutateSettings()
{
    ApplicationSettings settings;
    const QString original = QStringLiteral("{\"futureFlag\":true}");
    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"), original);
    AcpAgentRegistry registry(&settings);

    const QString before = settings.get("Ai/GoalAgentSettings", QString());
    SendWithGoalDialog dialog(&registry, &settings);
    QCOMPARE(templateCombo(dialog)->currentData().toString(),
             QString::fromLatin1(GoalAgentSettings::kDefaultTemplateId));
    QCOMPARE(settings.get("Ai/GoalAgentSettings", QString()), before);
    QVERIFY(!before.contains(QStringLiteral("promptTemplateId")));
}

void TestSendWithGoalDialog::switchedBackToDefault_isRestored()
{
    ApplicationSettings settings;
    seedClassifyTemplate(settings);
    GoalAgentSettings seeded = GoalAgentSettings::fromJson(
        QJsonDocument::fromJson(settings.get("Ai/GoalAgentSettings", QString()).toUtf8()).object());
    seeded.promptTemplateId = QStringLiteral("tpl-classify");
    settings.setValue(
        QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(seeded.toJson()).toJson(QJsonDocument::Compact)));
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    QComboBox *combo = templateCombo(first);
    QVERIFY(combo);
    const int defaultIdx = combo->findData(QLatin1String(GoalAgentSettings::kDefaultTemplateId));
    QVERIFY(defaultIdx >= 0);
    combo->setCurrentIndex(defaultIdx);
    QCOMPARE(storedPromptTemplateId(settings),
             QString::fromLatin1(GoalAgentSettings::kDefaultTemplateId));
    QTest::mouseClick(dialogButton(first, QStringLiteral("Cancel")), Qt::LeftButton);

    SendWithGoalDialog reopened(&registry, &settings);
    QCOMPARE(templateCombo(reopened)->currentData().toString(),
             QString::fromLatin1(GoalAgentSettings::kDefaultTemplateId));
}

void TestSendWithGoalDialog::corruptJson_openLeavesBlob()
{
    ApplicationSettings settings;
    const QString corrupt = QStringLiteral("not-json");
    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"), corrupt);
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog dialog(&registry, &settings);
    QCOMPARE(templateCombo(dialog)->currentData().toString(),
             QString::fromLatin1(GoalAgentSettings::kDefaultTemplateId));
    QCOMPARE(settings.get("Ai/GoalAgentSettings", QString()), corrupt);
    QVERIFY(!GoalAgentSettings::rememberPromptTemplateId(
        &settings, QStringLiteral("tpl-classify")));
    QCOMPARE(settings.get("Ai/GoalAgentSettings", QString()), corrupt);
}

void TestSendWithGoalDialog::remember_preservesUnknownKey_andIsIdempotent()
{
    ApplicationSettings settings;
    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"),
                      QStringLiteral("{\"agentId\":\"keep-me\",\"futureFlag\":true}"));

    QVERIFY(GoalAgentSettings::rememberPromptTemplateId(
        &settings, QStringLiteral("tpl-classify")));
    const QString once = settings.get("Ai/GoalAgentSettings", QString());
    const QJsonObject obj = QJsonDocument::fromJson(once.toUtf8()).object();
    QCOMPARE(obj.value(QStringLiteral("agentId")).toString(), QStringLiteral("keep-me"));
    QCOMPARE(obj.value(QStringLiteral("futureFlag")).toBool(), true);
    QCOMPARE(obj.value(QStringLiteral("promptTemplateId")).toString(), QStringLiteral("tpl-classify"));

    QVERIFY(!GoalAgentSettings::rememberPromptTemplateId(
        &settings, QStringLiteral("tpl-classify")));
    QCOMPARE(settings.get("Ai/GoalAgentSettings", QString()), once);
}

void TestSendWithGoalDialog::remember_rejectsNullEmptyAndCorrupt()
{
    QVERIFY(!GoalAgentSettings::rememberPromptTemplateId(nullptr, QStringLiteral("tpl-classify")));

    ApplicationSettings settings;
    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"),
                      QStringLiteral("{\"agentId\":\"keep-me\"}"));
    const QString before = settings.get("Ai/GoalAgentSettings", QString());
    QVERIFY(!GoalAgentSettings::rememberPromptTemplateId(&settings, QStringLiteral("  ")));
    QCOMPARE(settings.get("Ai/GoalAgentSettings", QString()), before);

    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"), QStringLiteral("[1,2]"));
    QVERIFY(!GoalAgentSettings::rememberPromptTemplateId(&settings, QStringLiteral("tpl-classify")));
    QCOMPARE(settings.get("Ai/GoalAgentSettings", QString()), QStringLiteral("[1,2]"));
}

void TestSendWithGoalDialog::failedStart_keepsSelectedTemplate()
{
    ApplicationSettings settings;
    seedClassifyTemplate(settings);
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog dialog(&registry, &settings);
    selectClassify(dialog);
    QCOMPARE(storedPromptTemplateId(settings), QStringLiteral("tpl-classify"));
    QTest::mouseClick(dialogButton(dialog, QStringLiteral("Start Goal")), Qt::LeftButton);
    QCOMPARE(dialog.result(), QDialog::Rejected);
    QCOMPARE(storedPromptTemplateId(settings), QStringLiteral("tpl-classify"));
}

void TestSendWithGoalDialog::untouchedDefault_startWritesOnce()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);

    SendWithGoalDialog first(&registry, &settings);
    const auto criteria = first.findChildren<QPlainTextEdit *>();
    QVERIFY(!criteria.isEmpty());
    criteria.first()->setPlainText(QStringLiteral("Save default template"));
    QTest::mouseClick(dialogButton(first, QStringLiteral("Start Goal")), Qt::LeftButton);
    QCOMPARE(first.result(), QDialog::Accepted);
    QCOMPARE(storedPromptTemplateId(settings),
             QString::fromLatin1(GoalAgentSettings::kDefaultTemplateId));
    const QString afterStart = settings.get("Ai/GoalAgentSettings", QString());

    SendWithGoalDialog reopened(&registry, &settings);
    QCOMPARE(templateCombo(reopened)->currentData().toString(),
             QString::fromLatin1(GoalAgentSettings::kDefaultTemplateId));
    QCOMPARE(settings.get("Ai/GoalAgentSettings", QString()), afterStart);
}

void TestSendWithGoalDialog::scheduledTaskDialog_doesNotClobberSendTemplate()
{
    ApplicationSettings settings;
    seedClassifyTemplate(settings);
    GoalAgentSettings seeded = GoalAgentSettings::fromJson(
        QJsonDocument::fromJson(settings.get("Ai/GoalAgentSettings", QString()).toUtf8()).object());
    GoalPromptTemplate other;
    other.id = QStringLiteral("tpl-other");
    other.name = QStringLiteral("other");
    other.content = QStringLiteral("other");
    seeded.promptTemplates.append(other);
    seeded.promptTemplateId = QStringLiteral("tpl-classify");
    settings.setValue(
        QStringLiteral("Ai/GoalAgentSettings"),
        QString::fromUtf8(QJsonDocument(seeded.toJson()).toJson(QJsonDocument::Compact)));
    const QString before = settings.get("Ai/GoalAgentSettings", QString());
    AcpAgentRegistry registry(&settings);

    ScheduledTaskGoalConfig task;
    task.promptTemplateId = QStringLiteral("tpl-other");
    task.agentId = AcpAgentRegistry::builtinClaudeCodeId();
    task.criteriaList = QStringList{QStringLiteral("task criterion")};
    GoalAgentConfigDialog dialog(task, &registry, &settings);
    QComboBox *tplCombo = nullptr;
    for (QComboBox *combo : dialog.findChildren<QComboBox *>()) {
        if (combo->findData(QStringLiteral("tpl-other")) >= 0)
            tplCombo = combo;
    }
    QVERIFY(tplCombo);
    QCOMPARE(tplCombo->currentData().toString(), QStringLiteral("tpl-other"));
    QCOMPARE(dialog.goalConfig().promptTemplateId, QStringLiteral("tpl-other"));
    QCOMPARE(settings.get("Ai/GoalAgentSettings", QString()), before);

    tplCombo->setCurrentIndex(tplCombo->findData(QStringLiteral("tpl-classify")));
    QCOMPARE(storedPromptTemplateId(settings), QStringLiteral("tpl-classify"));
    QCOMPARE(settings.get("Ai/GoalAgentSettings", QString()), before);

    ScheduledTaskGoalConfig missing = task;
    missing.promptTemplateId = QStringLiteral("gone");
    GoalAgentConfigDialog missingDialog(missing, &registry, &settings);
    QComboBox *missingCombo = nullptr;
    for (QComboBox *combo : missingDialog.findChildren<QComboBox *>()) {
        if (combo->findData(QLatin1String(GoalAgentSettings::kDefaultTemplateId)) >= 0
            && combo->findData(AcpAgentRegistry::builtinClaudeCodeId()) < 0) {
            missingCombo = combo;
        }
    }
    QVERIFY(missingCombo);
    QCOMPARE(missingCombo->currentData().toString(),
             QString::fromLatin1(GoalAgentSettings::kDefaultTemplateId));
    QCOMPARE(missingDialog.goalConfig().promptTemplateId,
             QString::fromLatin1(GoalAgentSettings::kDefaultTemplateId));
    QCOMPARE(storedPromptTemplateId(settings), QStringLiteral("tpl-classify"));
}

QTEST_MAIN(TestSendWithGoalDialog)

#include "test_send_with_goal_dialog.moc"
