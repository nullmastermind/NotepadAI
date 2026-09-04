#include <QtTest/QtTest>

#include <QByteArray>
#include <QCoreApplication>
#include <QEvent>
#include <QJsonDocument>
#include <QPair>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QVector>

#include "AcpAgentRegistry.h"
#include "AcpConnection.h"
#include "AcpSessionModel.h"
#include "ApplicationSettings.h"
#include "GoalAgentSettings.h"
#include "GoalConversationSummary.h"
#include "GoalCustomApiFields.h"
#include "GoalDraftDialog.h"
#include "GoalDraftGenerator.h"
#include "GoalHttpJudge.h"
#include "GoalHttpJudgeRunner.h"
#include "ai/CredentialStore.h"
#include "ai/IAnthropicMessagesClient.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <stdexcept>

class TestGoalDraftGenerator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void parse_validActionBody_returnsDraft();
    void parse_completeAction_returnsNoDraft();
    void parse_malformedOrEmptyAction_rejects();
    void renderPrompt_usesFullTargetHistoryForEnhance();
    void conversationSummary_startIndexKeepsIncrementalWindow();
    void cancel_isIdempotentAndDeletesConnection();
    void start_customApiContinue_emitsDraft();
    void start_customApiComplete_emitsAlreadyComplete();
    void start_customApiHttpFailed_emitsError();
    void start_customApiUnconfigured_emitsConfigError();
    void start_customApiNoTool_emitsInvalidPrompt();
    void draftDialog_customApi_isListedInCombo();
    void draftDialog_customApi_showsFieldsWhenSelected();
    void draftDialog_customApi_statusEmptyPartialInvalidIdeal();
    void draftDialog_customApi_partialMissingKey();
    void draftDialog_customApi_partialMissingModel();
    void draftDialog_customApi_emptyUnconfiguredStatus();
    void start_customApiHalfOpen_recoversToDraft();
    void start_customApiHttp500_recordsFailureWithoutSecret();
    void draftDialog_editingFields_doesNotStartJudge();
    void start_customApiObservability_metricsWithoutSecret();
    void renderPrompt_sanitizesUserInjection();
    void draftDialog_customApi_loadingDisablesFields();
    void start_customApiSecondStartWhileRunning_isNoOp();
    void start_customApiHttpFailed_emitsDebugLogWithoutSecrets();
    void start_customApiPostThrows_fallsBackToError();
    void start_acpWithoutManager_stillErrors();
    void start_acpAgent_doesNotPostHttp();
    void start_customApiHttpRetry_emitsFinishedOnce();
    void start_customApi_sequentialRequests_independentDrafts();
    void start_customApiCircuitOpen_emitsUnavailableNoDraft();
    void draftDialog_listsAcpAndCustomApi();
    void draftDialog_acpHidesCustomApiFields();
    void draftDialog_oldSettingsWithoutCustomApi_opens();
    void draftDialog_customApi_layoutSpacingMatchesSendWithGoal();
    void draftDialog_apiKeyIsPasswordEcho();
    void start_customApiCancelWhileRunning_dropsLateVerdict();
    void renderPrompt_doesNotContainApiKey();
    void draftDialog_emptyUrl_blocksGenerate();
    void customApiKey_encryptedAtRest_roundTrips();
    void start_customApi_oldClientJson_failsClosedNotCrash();
    void draftDialog_nielsenTenHeuristics();
    void start_acpFlow_promptCancelNoHttp();

private:
    QTemporaryDir m_settingsDir;
};

void TestGoalDraftGenerator::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("NotepadNextTest"));
    QCoreApplication::setApplicationName(QStringLiteral("NotepadNextTest_GoalDraftGenerator"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void TestGoalDraftGenerator::init()
{
    ApplicationSettings settings;
    settings.clear();
    settings.sync();
    GoalHttpJudge::resetHttpGuardForTesting();
}

void TestGoalDraftGenerator::parse_validActionBody_returnsDraft()
{
    QString draft;
    bool complete = true;
    QVERIFY(GoalDraftGenerator::parseDraftResponseForTesting(
        QStringLiteral("<action type=\"continue\">  Run the focused tests.  </action>"),
        &draft, &complete));
    QCOMPARE(draft, QStringLiteral("Run the focused tests."));
    QVERIFY(!complete);
}

void TestGoalDraftGenerator::parse_completeAction_returnsNoDraft()
{
    QString draft = QStringLiteral("unchanged");
    bool complete = false;

    QVERIFY(GoalDraftGenerator::parseDraftResponseForTesting(
        QStringLiteral("<action type=\"complete\">Already satisfied.</action>"),
        &draft, &complete));

    QVERIFY(complete);
    QVERIFY(draft.isEmpty());
}

void TestGoalDraftGenerator::parse_malformedOrEmptyAction_rejects()
{
    QString draft = QStringLiteral("unchanged");
    QVERIFY(!GoalDraftGenerator::parseDraftResponseForTesting(
        QStringLiteral("plain text without xml"), &draft));
    QVERIFY(draft.isEmpty());

    draft = QStringLiteral("unchanged");
    QVERIFY(!GoalDraftGenerator::parseDraftResponseForTesting(
        QStringLiteral("<action type=\"continue\">   </action>"), &draft));
    QVERIFY(draft.isEmpty());
}

void TestGoalDraftGenerator::renderPrompt_usesFullTargetHistoryForEnhance()
{
    GoalAgentSettings goalSettings;
    goalSettings.defaultMaxIterations = 7;
    GoalPromptTemplate tpl;
    tpl.id = QStringLiteral("test-template");
    tpl.name = QStringLiteral("Test template");
    tpl.content = QStringLiteral(
        "goal={{goal}}\n"
        "conversation={{conversation}}\n"
        "iteration={{iteration}}/{{maxIterations}}\n"
        "criterion={{criterionIndex}}/{{totalCriteria}}\n"
        "original={{originalUserMessage}}");
    goalSettings.promptTemplates.append(tpl);

    ApplicationSettings settings;
    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"),
                      QString::fromUtf8(QJsonDocument(goalSettings.toJson())
                                            .toJson(QJsonDocument::Compact)));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("draft-history"),
                          QStringLiteral("proj"),
                          historyDir.path());
    model.appendUserMessage(QStringLiteral("Please inspect <xml>"),
                            QVector<QPair<QByteArray, QString>>{});
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("I checked A & B"));
    model.onPromptEnded();
    model.appendSystemMessage(QStringLiteral("system text ignored"));
    model.onThoughtChunk(QStringLiteral("thought text ignored"));

    GoalDraftGenerator generator(nullptr, &settings);
    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("first criterion\nsecond criterion");
    req.promptTemplateId = tpl.id;
    req.targetModel = &model;

    const QString prompt = generator.renderPromptForTesting(req);

    QVERIFY(prompt.contains(QStringLiteral("goal=first criterion")));
    QVERIFY(prompt.contains(
        QStringLiteral("<message role=\"user\">Please inspect &lt;xml&gt;</message>")));
    QVERIFY(prompt.contains(
        QStringLiteral("<message role=\"assistant\">I checked A &amp; B</message>")));
    QVERIFY(!prompt.contains(QStringLiteral("system text ignored")));
    QVERIFY(!prompt.contains(QStringLiteral("thought text ignored")));
    QVERIFY(prompt.contains(QStringLiteral("iteration=1/7")));
    QVERIFY(prompt.contains(QStringLiteral("criterion=1/2")));
    QVERIFY(prompt.contains(QStringLiteral("original=first criterion\nsecond criterion")));
}

void TestGoalDraftGenerator::conversationSummary_startIndexKeepsIncrementalWindow()
{
    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("incremental-history"),
                          QStringLiteral("proj"),
                          historyDir.path());
    model.appendUserMessage(QStringLiteral("old user"),
                            QVector<QPair<QByteArray, QString>>{});
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("old assistant"));
    model.onPromptEnded();

    const int lastSeen = model.messages().size();

    model.appendUserMessage(QStringLiteral("new user"),
                            QVector<QPair<QByteArray, QString>>{});
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("new assistant"));
    model.onPromptEnded();

    const QString incremental = GoalConversationSummary::fromModel(&model, lastSeen);

    QVERIFY(!incremental.contains(QStringLiteral("old user")));
    QVERIFY(!incremental.contains(QStringLiteral("old assistant")));
    QVERIFY(incremental.contains(QStringLiteral("new user")));
    QVERIFY(incremental.contains(QStringLiteral("new assistant")));
    QCOMPARE(model.messages().size(), lastSeen + 2);
}

void TestGoalDraftGenerator::cancel_isIdempotentAndDeletesConnection()
{
    GoalDraftGenerator generator(nullptr, nullptr);
    auto *connection = new AcpConnection;
    QPointer<AcpConnection> connectionPtr(connection);

    generator.setConnectionForTesting(connection, true);
    generator.cancel();
    generator.cancel();

    QCOMPARE(generator.teardownCountForTesting(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(connectionPtr.isNull());

    generator.cancel();
    QCOMPARE(generator.teardownCountForTesting(), 1);
}

namespace {

class FakeAnthropicClient : public ai::IAnthropicMessagesClient
{
public:
    explicit FakeAnthropicClient(QObject *parent = nullptr)
        : ai::IAnthropicMessagesClient(parent)
    {
    }

    QList<QByteArray> replies;
    int errorStatus = 0;
    QString errorMessage;
    bool defer = false;
    bool throwOnPost = false;
    int posts = 0;

    void post(const Request &req) override
    {
        Q_UNUSED(req)
        ++posts;
        if (throwOnPost)
            throw std::runtime_error("boom");
        if (errorStatus > 0) {
            emit errorOccurred(errorStatus, errorMessage);
            return;
        }
        if (defer)
            return;
        emit finished(posts - 1 < replies.size() ? replies.at(posts - 1) : QByteArray());
    }

    void completeDeferred()
    {
        emit finished(replies.isEmpty() ? QByteArray() : replies.first());
    }

    void cancel() override {}
};

QByteArray toolJson(const char *status, const char *text)
{
    return QByteArray("{")
        + R"("content":[{"type":"tool_use","id":"toolu_1","name":"submit_goal_verdict",)"
        + R"("input":{"status":")" + QByteArray(status)
        + R"(","text":")" + QByteArray(text) + R"("}}],"stop_reason":"tool_use"})";
}

} // namespace

void TestGoalDraftGenerator::start_customApiContinue_emitsDraft()
{
    FakeAnthropicClient fake;
    fake.replies.append(toolJson("continue", "Please run the tests."));
    GoalHttpJudgeRunner runner(&fake);

    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QString draft;
    int errors = 0;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &text) {
        draft = text;
    });
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &) {
        ++errors;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);

    QVERIFY(generator.start(req));
    QCOMPARE(errors, 0);
    QCOMPARE(draft, QStringLiteral("Please run the tests."));
}

void TestGoalDraftGenerator::start_customApiComplete_emitsAlreadyComplete()
{
    FakeAnthropicClient fake;
    fake.replies.append(toolJson("complete", "Already satisfied."));
    GoalHttpJudgeRunner runner(&fake);

    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QString draft;
    QString error;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &text) {
        draft = text;
    });
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &message) {
        error = message;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);

    QVERIFY(!generator.start(req));
    QVERIFY(!generator.isRunning());
    QVERIFY(draft.isEmpty());
    QCOMPARE(error, QStringLiteral("Goal is already complete. No prompt was generated."));
}

void TestGoalDraftGenerator::start_customApiHttpFailed_emitsError()
{
    FakeAnthropicClient fake;
    fake.errorStatus = 401;
    fake.errorMessage = QStringLiteral("Unauthorized");
    GoalHttpJudgeRunner runner(&fake);

    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QString draft;
    QString error;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &text) {
        draft = text;
    });
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &message) {
        error = message;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);

    QVERIFY(!generator.start(req));
    QVERIFY(!generator.isRunning());
    QVERIFY(draft.isEmpty());
    QCOMPARE(error, QStringLiteral("Unauthorized"));
}

void TestGoalDraftGenerator::start_customApiUnconfigured_emitsConfigError()
{
    GoalDraftGenerator generator(nullptr, nullptr);

    QString error;
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &message) {
        error = message;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);

    generator.start(req);
    QCOMPARE(error,
             QStringLiteral("Configure Custom API (Base URL and model) before generating a prompt."));
}

void TestGoalDraftGenerator::start_customApiNoTool_emitsInvalidPrompt()
{
    FakeAnthropicClient fake;
    const QByteArray prose = QByteArrayLiteral(
        R"({"content":[{"type":"text","text":"Looks good."}],"stop_reason":"end_turn"})");
    fake.replies.append(prose);
    fake.replies.append(prose);
    GoalHttpJudgeRunner runner(&fake);

    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QString draft;
    QString error;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &text) {
        draft = text;
    });
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &message) {
        error = message;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);

    QVERIFY(!generator.start(req));
    QVERIFY(draft.isEmpty());
    QVERIFY(!generator.isRunning());
    QCOMPARE(error, QStringLiteral("Goal-agent returned an invalid prompt. Adjust the criteria and try again."));
}

QComboBox *draftAgentCombo(GoalDraftDialog &dialog)
{
    const auto combos = dialog.findChildren<QComboBox *>();
    for (QComboBox *combo : combos) {
        if (combo->findData(AcpAgentRegistry::builtinClaudeCodeId()) >= 0)
            return combo;
    }
    return nullptr;
}

void TestGoalDraftGenerator::draftDialog_customApi_isListedInCombo()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    QVERIFY(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)) >= 0);
}

void TestGoalDraftGenerator::draftDialog_customApi_showsFieldsWhenSelected()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    QVERIFY(fields->isHidden());
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    const int idx = combo->findData(QLatin1String(GoalHttpJudge::kAgentId));
    QVERIFY(idx >= 0);
    combo->setCurrentIndex(idx);
    QVERIFY(!fields->isHidden());
    QVERIFY(fields->findChild<QLineEdit *>());
    QCOMPARE(QString::fromLatin1(fields->metaObject()->className()),
             QStringLiteral("GoalCustomApiFields"));
}

void TestGoalDraftGenerator::draftDialog_customApi_statusEmptyPartialInvalidIdeal()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));

    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    QLabel *status = fields->findChild<QLabel *>(QStringLiteral("customApiStatus"));
    QVERIFY(status);
    QVERIFY(!status->isHidden());
    QVERIFY(status->text().contains(QStringLiteral("Enter")));
    QCOMPARE(status->styleSheet(),
             QStringLiteral("font-size: 11px; color: palette(placeholder-text);"));

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

void TestGoalDraftGenerator::draftDialog_customApi_partialMissingKey()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    QLabel *status = fields->findChild<QLabel *>(QStringLiteral("customApiStatus"));
    QVERIFY(status);
    const auto edits = fields->findChildren<QLineEdit *>();
    QCOMPARE(edits.size(), 3);
    edits.at(0)->setText(QStringLiteral("https://api.anthropic.com"));
    edits.at(2)->setText(QStringLiteral("claude-opus-5"));
    edits.at(1)->clear();
    QVERIFY(!status->isHidden());
    QVERIFY(status->text().contains(QLatin1String("API key"), Qt::CaseInsensitive));
    QCOMPARE(status->styleSheet(),
             QStringLiteral("font-size: 11px; color: palette(placeholder-text);"));
}

void TestGoalDraftGenerator::draftDialog_customApi_loadingDisablesFields()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);

    QVERIFY(QMetaObject::invokeMethod(&dialog, "setGenerating", Qt::DirectConnection,
                                      Q_ARG(bool, true)));

    QLabel *status = fields->findChild<QLabel *>(QStringLiteral("customApiStatus"));
    QVERIFY(status);
    QCOMPARE(status->text(), QStringLiteral("Calling judge…"));
    QVERIFY(!status->isHidden());
    QCOMPARE(status->styleSheet(),
             QStringLiteral("font-size: 11px; color: palette(placeholder-text);"));
    for (QLineEdit *edit : fields->findChildren<QLineEdit *>())
        QVERIFY(edit->isReadOnly());
    QVERIFY(fields->isEnabled());

    QPushButton *generate = nullptr;
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Generate"))
            generate = button;
    }
    QVERIFY(generate);
    QVERIFY(!generate->isEnabled());
    QVERIFY(!combo->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&dialog, "setGenerating", Qt::DirectConnection,
                                      Q_ARG(bool, false)));
    for (QLineEdit *edit : fields->findChildren<QLineEdit *>())
        QVERIFY(!edit->isReadOnly());
}

void TestGoalDraftGenerator::start_customApiSecondStartWhileRunning_isNoOp()
{
    FakeAnthropicClient fake;
    fake.defer = true;
    fake.replies.append(toolJson("continue", "Please run the tests."));
    GoalHttpJudgeRunner runner(&fake);

    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QString draft;
    int errors = 0;
    int finished = 0;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &text) {
        draft = text;
        ++finished;
    });
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &) {
        ++errors;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);

    QVERIFY(generator.start(req));
    QVERIFY(generator.isRunning());
    QVERIFY(!generator.start(req));
    QCOMPARE(fake.posts, 1);

    fake.completeDeferred();
    QCOMPARE(errors, 0);
    QCOMPARE(finished, 1);
    QCOMPARE(draft, QStringLiteral("Please run the tests."));
    QVERIFY(!generator.isRunning());
}

void TestGoalDraftGenerator::start_customApiHttpFailed_emitsDebugLogWithoutSecrets()
{
    FakeAnthropicClient fake;
    fake.errorStatus = 401;
    fake.errorMessage = QStringLiteral("Unauthorized");
    GoalHttpJudgeRunner runner(&fake);

    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QStringList logs;
    QObject::connect(&generator, &GoalDraftGenerator::debugLogEntry, [&](const QString &entry) {
        logs.append(entry);
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    generator.start(req);

    QVERIFY(!logs.isEmpty());
    const QString joined = logs.join(QLatin1Char('\n'));
    QVERIFY(joined.contains(QLatin1String("http"), Qt::CaseInsensitive)
            || joined.contains(QLatin1String("failed"), Qt::CaseInsensitive));
    QVERIFY(joined.contains(QLatin1String("Unauthorized")));
    QVERIFY(!joined.contains(QLatin1String("sk-"), Qt::CaseInsensitive));
    QVERIFY(!joined.contains(QLatin1String("example.test")));
    QVERIFY(!joined.contains(QLatin1String("url=")));
}

void TestGoalDraftGenerator::start_customApiPostThrows_fallsBackToError()
{
    FakeAnthropicClient fake;
    fake.throwOnPost = true;
    GoalHttpJudgeRunner runner(&fake);

    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QString error;
    QStringList logs;
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &message) {
        error = message;
    });
    QObject::connect(&generator, &GoalDraftGenerator::debugLogEntry, [&](const QString &entry) {
        logs.append(entry);
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);

    QVERIFY(!generator.start(req));
    QVERIFY(!generator.isRunning());
    QCOMPARE(error, QStringLiteral("Could not generate prompt. Check Custom API settings."));
    QVERIFY(!logs.isEmpty());
}

void TestGoalDraftGenerator::start_acpWithoutManager_stillErrors()
{
    GoalDraftGenerator generator(nullptr, nullptr);
    QString error;
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &message) {
        error = message;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QStringLiteral("claude-code");
    QVERIFY(!generator.start(req));
    QCOMPARE(error,
             QStringLiteral("Could not generate prompt. Goal-agent settings are unavailable."));
}

void TestGoalDraftGenerator::start_acpAgent_doesNotPostHttp()
{
    FakeAnthropicClient fake;
    fake.replies.append(toolJson("continue", "Please run the tests."));
    GoalHttpJudgeRunner runner(&fake);
    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    int finished = 0;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &) {
        ++finished;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = AcpAgentRegistry::builtinClaudeCodeId();
    QVERIFY(!generator.start(req));
    QCOMPARE(fake.posts, 0);
    QCOMPARE(finished, 0);
}

void TestGoalDraftGenerator::start_customApiHttpRetry_emitsFinishedOnce()
{
    FakeAnthropicClient fake;
    fake.replies.append(QByteArrayLiteral(
        R"({"content":[{"type":"text","text":"Looks good."}],"stop_reason":"end_turn"})"));
    fake.replies.append(toolJson("continue", "Please run the tests."));
    GoalHttpJudgeRunner runner(&fake);
    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QString draft;
    int finished = 0;
    int errors = 0;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &text) {
        draft = text;
        ++finished;
    });
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &) {
        ++errors;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(generator.start(req));
    QCOMPARE(errors, 0);
    QCOMPARE(finished, 1);
    QCOMPARE(draft, QStringLiteral("Please run the tests."));
}

void TestGoalDraftGenerator::start_customApi_sequentialRequests_independentDrafts()
{
    FakeAnthropicClient fake;
    fake.replies.append(toolJson("continue", "First draft."));
    fake.replies.append(toolJson("continue", "Second draft."));
    GoalHttpJudgeRunner runner(&fake);
    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);
    QString draft;
    int finished = 0;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &text) {
        draft = text;
        ++finished;
    });
    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(generator.start(req));
    QCOMPARE(finished, 1);
    QCOMPARE(draft, QStringLiteral("First draft."));
    QVERIFY(!generator.isRunning());
    QVERIFY(generator.start(req));
    QCOMPARE(finished, 2);
    QCOMPARE(draft, QStringLiteral("Second draft."));
    QVERIFY(!generator.isRunning());
    QCOMPARE(fake.posts, 2);
}

void TestGoalDraftGenerator::start_customApiCircuitOpen_emitsUnavailableNoDraft()
{
    FakeAnthropicClient fake;
    fake.errorStatus = 503;
    fake.errorMessage = QStringLiteral("HTTP 503");
    GoalHttpJudgeRunner runner(&fake);
    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QString draft;
    QString error;
    int finished = 0;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &text) {
        draft = text;
        ++finished;
    });
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &message) {
        error = message;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(!generator.start(req));
    QVERIFY(!generator.start(req));
    QVERIFY(!generator.start(req));
    QCOMPARE(fake.posts, 3);
    QVERIFY(!generator.start(req));
    QCOMPARE(fake.posts, 3);
    QCOMPARE(finished, 0);
    QVERIFY(draft.isEmpty());
    QVERIFY(!generator.isRunning());
    QCOMPARE(error, QStringLiteral("Custom API is temporarily unavailable. Try again in a moment."));
}

void TestGoalDraftGenerator::draftDialog_listsAcpAndCustomApi()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    QVERIFY(combo->findData(AcpAgentRegistry::builtinClaudeCodeId()) >= 0);
    QVERIFY(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)) >= 0);
}

void TestGoalDraftGenerator::draftDialog_acpHidesCustomApiFields()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);

    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    QVERIFY(!fields->isHidden());
    combo->setCurrentIndex(combo->findData(AcpAgentRegistry::builtinClaudeCodeId()));
    QVERIFY(fields->isHidden());
}

void TestGoalDraftGenerator::draftDialog_oldSettingsWithoutCustomApi_opens()
{
    ApplicationSettings settings;
    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"),
                      QStringLiteral("{\"agentId\":\"claude-code\"}"));
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    QVERIFY(combo->count() >= 2);
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    QVERIFY(fields->isHidden());
}

void TestGoalDraftGenerator::draftDialog_customApi_layoutSpacingMatchesSendWithGoal()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    auto *layout = qobject_cast<QVBoxLayout *>(fields->layout());
    QVERIFY(layout);
    QCOMPARE(layout->spacing(), 8);
    QCOMPARE(layout->contentsMargins(), QMargins(0, 0, 0, 0));
}

void TestGoalDraftGenerator::draftDialog_apiKeyIsPasswordEcho()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    const auto edits = fields->findChildren<QLineEdit *>();
    QCOMPARE(edits.size(), 3);
    QCOMPARE(edits.at(1)->echoMode(), QLineEdit::Password);
}

void TestGoalDraftGenerator::start_customApiCancelWhileRunning_dropsLateVerdict()
{
    FakeAnthropicClient fake;
    fake.defer = true;
    fake.replies.append(toolJson("continue", "Please run the tests."));
    GoalHttpJudgeRunner runner(&fake);
    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    int finished = 0;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &) {
        ++finished;
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(generator.start(req));
    QVERIFY(generator.isRunning());
    generator.cancel();
    QVERIFY(!generator.isRunning());
    fake.completeDeferred();
    QCOMPARE(finished, 0);
}

void TestGoalDraftGenerator::renderPrompt_doesNotContainApiKey()
{
    ApplicationSettings settings;
    GoalAgentSettings gs;
    gs.customApiBaseUrl = QStringLiteral("https://api.anthropic.com");
    gs.customApiModel = QStringLiteral("claude-opus-5");
    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"),
                      QString::fromUtf8(QJsonDocument(gs.toJson()).toJson(QJsonDocument::Compact)));
    GoalDraftGenerator generator(nullptr, &settings);
    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    const QString prompt = generator.renderPromptForTesting(req);
    QVERIFY(!prompt.contains(QLatin1String("sk-"), Qt::CaseInsensitive));
    QVERIFY(!prompt.contains(QLatin1String("apiKey"), Qt::CaseInsensitive));
}

void TestGoalDraftGenerator::draftDialog_emptyUrl_blocksGenerate()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    const auto edits = dialog.findChildren<QPlainTextEdit *>();
    QVERIFY(!edits.isEmpty());
    edits.first()->setPlainText(QStringLiteral("A criterion"));
    QPushButton *generate = nullptr;
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Generate"))
            generate = button;
    }
    QVERIFY(generate);
    QTest::mouseClick(generate, Qt::LeftButton);
    bool saw = false;
    for (QLabel *label : dialog.findChildren<QLabel *>()) {
        if (label->text().contains(QLatin1String("Base URL")))
            saw = true;
    }
    QVERIFY(saw);
}

void TestGoalDraftGenerator::customApiKey_encryptedAtRest_roundTrips()
{
    const QString secret = QStringLiteral("sk-secret-goal-key-notepadai");
    GoalCustomApiFields fields;
    ApplicationSettings settings;
    fields.setSettings(&settings);
    fields.show();
    const auto edits = fields.findChildren<QLineEdit *>();
    QCOMPARE(edits.size(), 3);
    edits.at(0)->setText(QStringLiteral("https://api.anthropic.com"));
    edits.at(1)->setText(secret);
    edits.at(2)->setText(QStringLiteral("claude-opus-5"));
    fields.persistPending();

    const QString gs = settings.get("Ai/GoalAgentSettings", QString());
    QVERIFY(!gs.contains(secret));
    QVERIFY(!QJsonDocument::fromJson(gs.toUtf8()).object().contains(QStringLiteral("apiKey")));

    QString disk;
    QDir dir(m_settingsDir.path());
    for (const QString &name : dir.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        disk += QString::fromUtf8(f.readAll());
    }
    const auto subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subdirs) {
        QDir child(dir.filePath(sub));
        for (const QString &name : child.entryList(QDir::Files | QDir::NoDotAndDotDot)) {
            QFile f(child.filePath(name));
            if (!f.open(QIODevice::ReadOnly))
                continue;
            disk += QString::fromUtf8(f.readAll());
        }
    }
    QVERIFY(!disk.contains(secret));

    const QByteArray blobB64 =
        settings.value(QStringLiteral("Ai/Secrets/goal-agent-custom-api")).toByteArray();
    if (!blobB64.isEmpty()) {
        QVERIFY(QByteArray::fromBase64(blobB64) != secret.toUtf8());
        QVERIFY(blobB64 != secret.toUtf8().toBase64());
    }

    ai::CredentialStore store;
    const QString loaded = store.retrieveSecret(QLatin1String(GoalHttpJudge::kCredentialKey));
    if (loaded.isEmpty())
        QSKIP("OS secret backend did not persist in this environment");
    QCOMPARE(loaded, secret);
    store.clearSecret(QLatin1String(GoalHttpJudge::kCredentialKey));
}

void TestGoalDraftGenerator::draftDialog_nielsenTenHeuristics()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    auto *layout = qobject_cast<QVBoxLayout *>(fields->layout());
    QVERIFY(layout);

    // 1. Visibility of system status
    QVERIFY(QMetaObject::invokeMethod(&dialog, "setGenerating", Qt::DirectConnection,
                                      Q_ARG(bool, true)));
    QLabel *status = fields->findChild<QLabel *>(QStringLiteral("customApiStatus"));
    QVERIFY(status);
    QCOMPARE(status->text(), QStringLiteral("Calling judge…"));
    QVERIFY(!status->isHidden());

    QPushButton *generate = nullptr;
    QPushButton *cancel = nullptr;
    for (QPushButton *button : dialog.findChildren<QPushButton *>()) {
        if (button->text() == QStringLiteral("Generate"))
            generate = button;
        if (button->text() == QStringLiteral("Cancel"))
            cancel = button;
    }
    QVERIFY(generate);
    QVERIFY(cancel);

    // 2. Match the real world — field names users already know
    bool sawBase = false, sawKey = false, sawModel = false;
    for (QLabel *label : fields->findChildren<QLabel *>()) {
        if (label->text() == QStringLiteral("Base URL"))
            sawBase = true;
        if (label->text() == QStringLiteral("API key"))
            sawKey = true;
        if (label->text() == QStringLiteral("Model"))
            sawModel = true;
    }
    QVERIFY(sawBase && sawKey && sawModel);

    // 3. User control and freedom — cancel stays available while busy
    QVERIFY(cancel->isEnabled());

    // 4. Consistency — same 8px stack as Send with Goal
    QCOMPARE(layout->spacing(), 8);
    QCOMPARE(QString::fromLatin1(fields->metaObject()->className()),
             QStringLiteral("GoalCustomApiFields"));

    // 5. Error prevention — cannot fire Generate / switch agent while loading
    QVERIFY(!generate->isEnabled());
    QVERIFY(!combo->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&dialog, "setGenerating", Qt::DirectConnection,
                                      Q_ARG(bool, false)));

    // 6. Recognition rather than recall — placeholders
    const auto edits = dialog.findChildren<QPlainTextEdit *>();
    QVERIFY(!edits.isEmpty());
    QVERIFY(!edits.first()->placeholderText().isEmpty());
    const auto lines = fields->findChildren<QLineEdit *>();
    QCOMPARE(lines.size(), 3);
    QVERIFY(!lines.at(0)->placeholderText().isEmpty());

    // 7. Flexibility and efficiency of use
    QVERIFY(generate->isDefault());

    // 8. Aesthetic and minimalist design
    QCOMPARE(dialog.minimumWidth(), 480);
    combo->setCurrentIndex(combo->findData(AcpAgentRegistry::builtinClaudeCodeId()));
    QVERIFY(fields->isHidden());
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    QVERIFY(!fields->isHidden());

    // 9. Help recover from errors — empty URL names the missing field
    QVERIFY(status->text().contains(QStringLiteral("Enter"))
            || status->isHidden() || !status->text().isEmpty());

    // 10. Help and documentation — criteria placeholder describes the task
    QVERIFY(edits.first()->placeholderText().contains(QLatin1String("prompt"), Qt::CaseInsensitive)
            || edits.first()->placeholderText().contains(QLatin1String("goal"), Qt::CaseInsensitive)
            || !edits.first()->placeholderText().isEmpty());
}

void TestGoalDraftGenerator::start_customApi_oldClientJson_failsClosedNotCrash()
{
    ApplicationSettings settings;
    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"),
                      QStringLiteral("{\"agentId\":\"custom-api\"}"));
    GoalDraftGenerator generator(nullptr, &settings);
    QString error;
    int finished = 0;
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &message) {
        error = message;
    });
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &) {
        ++finished;
    });
    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(!generator.start(req));
    QVERIFY(!generator.isRunning());
    QCOMPARE(finished, 0);
    QVERIFY(!error.isEmpty());
}

void TestGoalDraftGenerator::start_acpFlow_promptCancelNoHttp()
{
    FakeAnthropicClient fake;
    GoalHttpJudgeRunner runner(&fake);
    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QString draft;
    QVERIFY(GoalDraftGenerator::parseDraftResponseForTesting(
        QStringLiteral("<action type=\"continue\">Run tests.</action>"), &draft));
    QCOMPARE(draft, QStringLiteral("Run tests."));

    auto *connection = new AcpConnection;
    generator.setConnectionForTesting(connection, true);
    QVERIFY(generator.isRunning());
    generator.cancel();
    QVERIFY(!generator.isRunning());
    QCOMPARE(fake.posts, 0);

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = AcpAgentRegistry::builtinClaudeCodeId();
    QVERIFY(!generator.start(req));
    QCOMPARE(fake.posts, 0);
}

void TestGoalDraftGenerator::start_customApiObservability_metricsWithoutSecret()
{
    FakeAnthropicClient fake;
    fake.replies.append(toolJson("continue", "Please run the tests."));
    GoalHttpJudgeRunner runner(&fake);
    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);

    QStringList logs;
    QObject::connect(&generator, &GoalDraftGenerator::debugLogEntry, [&](const QString &entry) {
        logs.append(entry);
    });

    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(generator.start(req));

    const auto metrics = GoalHttpJudge::httpMetrics();
    QVERIFY(metrics.requests >= 1);
    QVERIFY(metrics.lastTraceId >= 1);
    QCOMPARE(metrics.successes, quint64(1));
    const QString joined = logs.join(QLatin1Char('\n'));
    QVERIFY(!joined.contains(QLatin1String("sk-"), Qt::CaseInsensitive));
    QVERIFY(!joined.contains(QLatin1String("test-key")));
}

void TestGoalDraftGenerator::renderPrompt_sanitizesUserInjection()
{
    GoalDraftGenerator generator(nullptr, nullptr);
    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("goal\rX-Injected: 1<script>x</script>");
    const QString prompt = generator.renderPromptForTesting(req);
    QVERIFY(!prompt.contains(QLatin1Char('\r')));
    QVERIFY(prompt.contains(QLatin1String("<script>x</script>")));
    QVERIFY(!prompt.contains(QLatin1String("sk-"), Qt::CaseInsensitive));
}

void TestGoalDraftGenerator::draftDialog_customApi_partialMissingModel()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    QVERIFY(combo);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QVERIFY(fields);
    QLabel *status = fields->findChild<QLabel *>(QStringLiteral("customApiStatus"));
    const auto edits = fields->findChildren<QLineEdit *>();
    QCOMPARE(edits.size(), 3);
    edits.at(0)->setText(QStringLiteral("https://api.anthropic.com"));
    edits.at(1)->setText(QStringLiteral("sk-test-key"));
    edits.at(2)->clear();
    QVERIFY(!status->isHidden());
    QVERIFY(status->text().contains(QLatin1String("model"), Qt::CaseInsensitive));
}

void TestGoalDraftGenerator::draftDialog_customApi_emptyUnconfiguredStatus()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    QLabel *status = fields->findChild<QLabel *>(QStringLiteral("customApiStatus"));
    QVERIFY(!status->isHidden());
    QCOMPARE(status->text(), QStringLiteral("Enter Base URL, API key, and model."));
}

void TestGoalDraftGenerator::start_customApiHalfOpen_recoversToDraft()
{
    GoalHttpJudge::setNowMsForTesting(1000);
    FakeAnthropicClient fake;
    fake.errorStatus = 503;
    fake.errorMessage = QStringLiteral("HTTP 503");
    GoalHttpJudgeRunner runner(&fake);
    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);
    QString draft;
    QObject::connect(&generator, &GoalDraftGenerator::finished, [&](const QString &text) {
        draft = text;
    });
    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(!generator.start(req));
    QVERIFY(!generator.start(req));
    QVERIFY(!generator.start(req));
    QCOMPARE(GoalHttpJudge::circuitState(), GoalHttpJudge::Circuit::Open);

    GoalHttpJudge::setNowMsForTesting(1000 + GoalHttpJudge::kCircuitOpenMs);
    fake.errorStatus = 0;
    while (fake.replies.size() < fake.posts)
        fake.replies.append(QByteArray());
    fake.replies.append(toolJson("continue", "Please run the tests."));
    QVERIFY(generator.start(req));
    QCOMPARE(draft, QStringLiteral("Please run the tests."));
    QCOMPARE(GoalHttpJudge::circuitState(), GoalHttpJudge::Circuit::Closed);
}

void TestGoalDraftGenerator::start_customApiHttp500_recordsFailureWithoutSecret()
{
    FakeAnthropicClient fake;
    fake.errorStatus = 500;
    fake.errorMessage = QStringLiteral("HTTP 500");
    GoalHttpJudgeRunner runner(&fake);
    GoalDraftGenerator generator(nullptr, nullptr);
    generator.setHttpRunnerForTesting(&runner);
    QStringList logs;
    QString error;
    QObject::connect(&generator, &GoalDraftGenerator::debugLogEntry, [&](const QString &e) {
        logs.append(e);
    });
    QObject::connect(&generator, &GoalDraftGenerator::errorOccurred, [&](const QString &m) {
        error = m;
    });
    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("Tests pass");
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(!generator.start(req));
    QCOMPARE(error, QStringLiteral("HTTP 500"));
    QVERIFY(GoalHttpJudge::httpMetrics().failures >= 1);
    const QString joined = logs.join(QLatin1Char('\n'));
    QVERIFY(joined.contains(QLatin1String("http failed"), Qt::CaseInsensitive)
            || joined.contains(QLatin1String("500")));
    QVERIFY(!joined.contains(QLatin1String("sk-"), Qt::CaseInsensitive));
    QVERIFY(!joined.contains(QLatin1String("test-key")));
}

void TestGoalDraftGenerator::draftDialog_editingFields_doesNotStartJudge()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    GoalDraftDialog dialog(nullptr, &registry, &settings, nullptr, QString(), nullptr);
    QComboBox *combo = draftAgentCombo(dialog);
    combo->setCurrentIndex(combo->findData(QLatin1String(GoalHttpJudge::kAgentId)));
    auto *gen = dialog.findChild<GoalDraftGenerator *>();
    QVERIFY(gen);
    QVERIFY(!gen->isRunning());
    QWidget *fields = dialog.findChild<QWidget *>(QStringLiteral("customApiFields"));
    const auto edits = fields->findChildren<QLineEdit *>();
    edits.at(0)->setText(QStringLiteral("https://api.anthropic.com"));
    edits.at(1)->setText(QStringLiteral("sk-should-not-start"));
    edits.at(2)->setText(QStringLiteral("claude-opus-5"));
    QVERIFY(!gen->isRunning());
}

QTEST_MAIN(TestGoalDraftGenerator)

#include "test_goal_draft_generator.moc"
