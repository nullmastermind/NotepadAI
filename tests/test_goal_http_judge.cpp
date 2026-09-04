#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "GoalAgentSettings.h"
#include "GoalHttpJudge.h"
#include "GoalHttpJudgeRunner.h"
#include "GoalHttpJudgeSession.h"

class TestGoalHttpJudge : public QObject
{
    Q_OBJECT

private slots:
    void messagesUrl_appendsV1MessagesToBareHost();
    void messagesUrl_doesNotDoubleWhenAlreadyComplete();
    void messagesUrl_stripsTrailingSlashBeforeAppend();
    void parseResponse_readsContinueToolUse();
    void parseResponse_readsCompleteToolUse();
    void parseResponse_readsContinueFromXmlText();
    void parseResponse_readsCompleteFromXmlText();
    void parseResponse_prefersToolUseOverXml();
    void parseResponse_noToolCall();
    void parseResponse_emptyContent_failsClosed();
    void decide_emptyContent_failsClosed();
    void runner_emptyContent_failsClosed();
    void formatFailureTrace_includesUrlAndModel_notKey();
    void decide_noToolFirstTime_retries();
    void decide_noToolAfterRetry_assumesAchieved();
    void decide_validContinue_appliesContinue();
    void decide_validComplete_appliesComplete();
    void decide_invalidJson_failsClosed();
    void runner_garbageBody_failsClosed();
    void runner_httpError_failsClosed();
    void sessionFail_emitsReasonWithoutUrlModelOrKey();
    void isUsableEndpointUrl_rejectsNonHttp();
    void apiKeyPlaceholder_emptyVsStored();
    void correctionPrompt_namesTheVerdictTool();
    void judgePrompt_requiresToolCall_hidesXml();
    void judgePrompt_usesProvidedTemplate();
    void buildRequestBody_includesModelToolAndUserPrompt();
    void settings_roundTripsCustomApiFields();
    void settings_oldJsonWithoutCustomApiFields_doesNotCrash();
    void settings_fromJson_unknownExtraFields_doesNotCrash();
    void isCustomApiAgent_matchesAgentId();
    void runner_continueTool_emitsVerdict();
    void runner_xmlText_emitsVerdict();
    void runner_noToolThenComplete_retriesOnce();
    void runner_noToolTwice_assumesAchieved();
    void init();
    void isSafeHeaderValue_rejectsCRLF();
    void isUsableEndpointUrl_rejectsUserinfoAndCrlf();
    void isTransportOutage_networkAnd5xx_not401();
    void circuit_threeTransportFailures_rejectsFourth();
    void circuit_authFailures_doNotOpen();
    void circuit_successResetsFailures();
    void circuit_openThenAdvance_allowsHalfOpenProbe();
    void settings_toJson_doesNotContainApiKey();
    void sanitizePlainText_stripsNulKeepsScriptLiteral();
    void httpMetrics_retryIncrements();
    void buildRequestBody_escapesUserQuotes();
    void observability_http500_metricsAndNoSecret();
};

void TestGoalHttpJudge::init()
{
    GoalHttpJudge::resetHttpGuardForTesting();
}

void TestGoalHttpJudge::messagesUrl_appendsV1MessagesToBareHost()
{
    QCOMPARE(GoalHttpJudge::messagesUrl(QStringLiteral("https://api.anthropic.com")),
             QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
}

void TestGoalHttpJudge::messagesUrl_doesNotDoubleWhenAlreadyComplete()
{
    QCOMPARE(GoalHttpJudge::messagesUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
             QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
}

void TestGoalHttpJudge::messagesUrl_stripsTrailingSlashBeforeAppend()
{
    QCOMPARE(GoalHttpJudge::messagesUrl(QStringLiteral("https://api.anthropic.com/")),
             QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
}

void TestGoalHttpJudge::parseResponse_readsContinueToolUse()
{
    const QByteArray json = QByteArrayLiteral(
        R"({"content":[{"type":"tool_use","id":"toolu_1","name":"submit_goal_verdict",)"
        R"("input":{"status":"continue","text":"Please run the tests."}}],)"
        R"("stop_reason":"tool_use"})");
    GoalAction action;
    GoalHttpJudge::ParseError err = GoalHttpJudge::InvalidJson;
    QVERIFY(GoalHttpJudge::parseResponse(json, &action, &err));
    QCOMPARE(err, GoalHttpJudge::NoError);
    QCOMPARE(action.type, GoalAction::Continue);
    QCOMPARE(action.text, QStringLiteral("Please run the tests."));
}

void TestGoalHttpJudge::parseResponse_readsCompleteToolUse()
{
    const QByteArray json = QByteArrayLiteral(
        R"({"content":[{"type":"tool_use","id":"toolu_1","name":"submit_goal_verdict",)"
        R"("input":{"status":"complete","text":"Tests passed."}}],)"
        R"("stop_reason":"tool_use"})");
    GoalAction action;
    QVERIFY(GoalHttpJudge::parseResponse(json, &action, nullptr));
    QCOMPARE(action.type, GoalAction::Complete);
    QCOMPARE(action.text, QStringLiteral("Tests passed."));
}

void TestGoalHttpJudge::parseResponse_readsContinueFromXmlText()
{
    const QByteArray json = QByteArrayLiteral(
        R"({"content":[{"type":"text","text":"<action type=\"continue\">Please run the tests.</action>"}],)"
        R"("stop_reason":"end_turn"})");
    GoalAction action;
    GoalHttpJudge::ParseError err = GoalHttpJudge::InvalidJson;
    QVERIFY(GoalHttpJudge::parseResponse(json, &action, &err));
    QCOMPARE(err, GoalHttpJudge::NoError);
    QCOMPARE(action.type, GoalAction::Continue);
    QCOMPARE(action.text, QStringLiteral("Please run the tests."));
}

void TestGoalHttpJudge::parseResponse_readsCompleteFromXmlText()
{
    const QByteArray json = QByteArrayLiteral(
        R"({"content":[{"type":"text","text":"<action type=\"complete\">Tests passed.</action>"}],)"
        R"("stop_reason":"end_turn"})");
    GoalAction action;
    QVERIFY(GoalHttpJudge::parseResponse(json, &action, nullptr));
    QCOMPARE(action.type, GoalAction::Complete);
    QCOMPARE(action.text, QStringLiteral("Tests passed."));
}

void TestGoalHttpJudge::parseResponse_prefersToolUseOverXml()
{
    const QByteArray json = QByteArrayLiteral(
        R"({"content":[)"
        R"({"type":"text","text":"<action type=\"complete\">Ignore me.</action>"},)"
        R"({"type":"tool_use","id":"toolu_1","name":"submit_goal_verdict",)"
        R"("input":{"status":"continue","text":"Please run the tests."}}],)"
        R"("stop_reason":"tool_use"})");
    GoalAction action;
    QVERIFY(GoalHttpJudge::parseResponse(json, &action, nullptr));
    QCOMPARE(action.type, GoalAction::Continue);
    QCOMPARE(action.text, QStringLiteral("Please run the tests."));
}

void TestGoalHttpJudge::parseResponse_noToolCall()
{
    const QByteArray json = QByteArrayLiteral(
        R"({"content":[{"type":"text","text":"Looks done to me."}],"stop_reason":"end_turn"})");
    GoalAction action;
    GoalHttpJudge::ParseError err = GoalHttpJudge::NoError;
    QVERIFY(!GoalHttpJudge::parseResponse(json, &action, &err));
    QCOMPARE(err, GoalHttpJudge::NoToolCall);
}

void TestGoalHttpJudge::parseResponse_emptyContent_failsClosed()
{
    GoalAction action;
    GoalHttpJudge::ParseError err = GoalHttpJudge::NoError;
    QVERIFY(!GoalHttpJudge::parseResponse(QByteArrayLiteral(R"({"content":[]})"), &action, &err));
    QCOMPARE(err, GoalHttpJudge::EmptyContent);
    QVERIFY(!GoalHttpJudge::parseResponse(QByteArrayLiteral(R"({})"), &action, &err));
    QCOMPARE(err, GoalHttpJudge::EmptyContent);
}

void TestGoalHttpJudge::decide_emptyContent_failsClosed()
{
    QCOMPARE(GoalHttpJudge::decide(GoalHttpJudge::EmptyContent, GoalAction::Continue, false),
             GoalHttpJudge::FailClosed);
    QCOMPARE(GoalHttpJudge::decide(GoalHttpJudge::EmptyContent, GoalAction::Continue, true),
             GoalHttpJudge::FailClosed);
}

void TestGoalHttpJudge::decide_noToolFirstTime_retries()
{
    QCOMPARE(GoalHttpJudge::decide(GoalHttpJudge::NoToolCall, GoalAction::Continue, false),
             GoalHttpJudge::RetryOnce);
}

void TestGoalHttpJudge::decide_noToolAfterRetry_assumesAchieved()
{
    QCOMPARE(GoalHttpJudge::decide(GoalHttpJudge::NoToolCall, GoalAction::Continue, true),
             GoalHttpJudge::AssumeAchieved);
}

void TestGoalHttpJudge::decide_validContinue_appliesContinue()
{
    QCOMPARE(GoalHttpJudge::decide(GoalHttpJudge::NoError, GoalAction::Continue, false),
             GoalHttpJudge::ApplyContinue);
}

void TestGoalHttpJudge::decide_validComplete_appliesComplete()
{
    QCOMPARE(GoalHttpJudge::decide(GoalHttpJudge::NoError, GoalAction::Complete, false),
             GoalHttpJudge::ApplyComplete);
}

void TestGoalHttpJudge::decide_invalidJson_failsClosed()
{
    QCOMPARE(GoalHttpJudge::decide(GoalHttpJudge::InvalidJson, GoalAction::Continue, false),
             GoalHttpJudge::FailClosed);
    QCOMPARE(GoalHttpJudge::decide(GoalHttpJudge::InvalidJson, GoalAction::Continue, true),
             GoalHttpJudge::FailClosed);
}

void TestGoalHttpJudge::isUsableEndpointUrl_rejectsNonHttp()
{
    QVERIFY(GoalHttpJudge::isUsableEndpointUrl(QStringLiteral("https://api.anthropic.com")));
    QVERIFY(!GoalHttpJudge::isUsableEndpointUrl(QStringLiteral("not-a-url")));
    QVERIFY(!GoalHttpJudge::isUsableEndpointUrl(QString()));
}

void TestGoalHttpJudge::apiKeyPlaceholder_emptyVsStored()
{
    QVERIFY(!GoalHttpJudge::apiKeyPlaceholder(false).contains(QLatin1String("keychain"),
                                                              Qt::CaseInsensitive));
    QVERIFY(GoalHttpJudge::apiKeyPlaceholder(true).contains(QLatin1String("keychain"),
                                                            Qt::CaseInsensitive));
}

void TestGoalHttpJudge::correctionPrompt_namesTheVerdictTool()
{
    const QString prompt = GoalHttpJudge::correctionPrompt();
    QVERIFY(prompt.contains(QLatin1String("submit_goal_verdict")));
    QVERIFY(prompt.contains(QLatin1String("MUST")));
    QVERIFY(!prompt.contains(QLatin1String("<action")));
}

void TestGoalHttpJudge::judgePrompt_requiresToolCall_hidesXml()
{
    const QString prompt = GoalHttpJudge::judgePrompt(
        QStringLiteral("All tests pass"),
        QStringLiteral("agent: I added a test"),
        1, 10, 1, 2,
        QStringLiteral("Fix the flaky test"));
    QVERIFY(prompt.contains(QLatin1String("You MUST call submit_goal_verdict")));
    QVERIFY(prompt.contains(QLatin1String("All tests pass")));
    QVERIFY(prompt.contains(QLatin1String("Fix the flaky test")));
    QVERIFY(!prompt.contains(QLatin1String("<action")));
}

void TestGoalHttpJudge::judgePrompt_usesProvidedTemplate()
{
    const QString prompt = GoalHttpJudge::judgePrompt(
        QStringLiteral("All tests pass"),
        QStringLiteral("agent: I added a test"),
        1, 10, 1, 2,
        QStringLiteral("Fix the flaky test"),
        QStringLiteral("CUSTOM {{goal}} {{originalUserMessage}}"));
    QVERIFY(prompt.contains(QLatin1String("CUSTOM All tests pass")));
    QVERIFY(prompt.contains(QLatin1String("Fix the flaky test")));
    QVERIFY(prompt.contains(QLatin1String("submit_goal_verdict")));
}

void TestGoalHttpJudge::buildRequestBody_includesModelToolAndUserPrompt()
{
    const QByteArray body = GoalHttpJudge::buildRequestBody(
        QStringLiteral("claude-opus-5"),
        QStringLiteral("Evaluate criterion 1"));
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    QCOMPARE(obj.value(QLatin1String("model")).toString(), QStringLiteral("claude-opus-5"));
    const QJsonArray messages = obj.value(QLatin1String("messages")).toArray();
    QVERIFY(!messages.isEmpty());
    QCOMPARE(messages.at(0).toObject().value(QLatin1String("content")).toString(),
             QStringLiteral("Evaluate criterion 1"));
    const QJsonArray tools = obj.value(QLatin1String("tools")).toArray();
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools.at(0).toObject().value(QLatin1String("name")).toString(),
             QStringLiteral("submit_goal_verdict"));
    QVERIFY(obj.value(QLatin1String("max_tokens")).toInt() > 0);
    const QString system = obj.value(QLatin1String("system")).toString();
    QVERIFY(system.contains(QLatin1String("MUST")));
    QVERIFY(system.contains(QLatin1String("submit_goal_verdict")));
    QVERIFY(system.contains(QLatin1String("<action")));
}

void TestGoalHttpJudge::settings_roundTripsCustomApiFields()
{
    GoalAgentSettings s;
    s.customApiBaseUrl = QStringLiteral("https://api.anthropic.com");
    s.customApiModel = QStringLiteral("claude-opus-5");
    const GoalAgentSettings back = GoalAgentSettings::fromJson(s.toJson());
    QCOMPARE(back.customApiBaseUrl, QStringLiteral("https://api.anthropic.com"));
    QCOMPARE(back.customApiModel, QStringLiteral("claude-opus-5"));
}

void TestGoalHttpJudge::settings_oldJsonWithoutCustomApiFields_doesNotCrash()
{
    const QJsonObject old{
        {QStringLiteral("agentId"), QStringLiteral("claude-code")},
        {QStringLiteral("defaultMaxIterations"), 10},
        {QStringLiteral("promptTemplates"), QJsonArray{}},
        {QStringLiteral("criteriaPresets"), QJsonArray{}},
    };
    const GoalAgentSettings s = GoalAgentSettings::fromJson(old);
    QCOMPARE(s.agentId, QStringLiteral("claude-code"));
    QCOMPARE(s.defaultMaxIterations, 10);
    QVERIFY(s.customApiBaseUrl.isEmpty());
    QVERIFY(s.customApiModel.isEmpty());
    QVERIFY(!s.promptTemplates.isEmpty());
    const QJsonObject out = s.toJson();
    QVERIFY(out.contains(QStringLiteral("customApiBaseUrl")));
    QVERIFY(out.contains(QStringLiteral("customApiModel")));
}

void TestGoalHttpJudge::settings_fromJson_unknownExtraFields_doesNotCrash()
{
    const QJsonObject future{
        {QStringLiteral("agentId"), QStringLiteral("claude-code")},
        {QStringLiteral("defaultMaxIterations"), 7},
        {QStringLiteral("unknownFutureFlag"), true},
        {QStringLiteral("legacyDbUrl"), QStringLiteral("postgres://old")},
        {QStringLiteral("promptTemplates"), QJsonArray{}},
    };
    const GoalAgentSettings s = GoalAgentSettings::fromJson(future);
    QCOMPARE(s.agentId, QStringLiteral("claude-code"));
    QCOMPARE(s.defaultMaxIterations, 7);
    QVERIFY(s.customApiBaseUrl.isEmpty());
    QVERIFY(!s.toJson().contains(QStringLiteral("unknownFutureFlag")));
    QVERIFY(!s.toJson().contains(QStringLiteral("legacyDbUrl")));
}

void TestGoalHttpJudge::isCustomApiAgent_matchesAgentId()
{
    QVERIFY(GoalHttpJudge::isCustomApiAgent(QLatin1String(GoalHttpJudge::kAgentId)));
    QVERIFY(!GoalHttpJudge::isCustomApiAgent(QStringLiteral("claude-code")));
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
    QList<Request> posts;
    int errorStatus = 0;
    QString errorMessage;

    void post(const Request &req) override
    {
        posts.append(req);
        if (errorStatus > 0) {
            emit errorOccurred(errorStatus, errorMessage);
            return;
        }
        const int idx = posts.size() - 1;
        emit finished(idx < replies.size() ? replies.at(idx) : QByteArray());
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

void TestGoalHttpJudge::runner_continueTool_emitsVerdict()
{
    FakeAnthropicClient fake;
    fake.replies.append(toolJson("continue", "Please run the tests."));
    GoalHttpJudgeRunner runner(&fake);
    GoalAction seen;
    int verdicts = 0;
    QObject::connect(&runner, &GoalHttpJudgeRunner::verdict, [&](const GoalAction &a) {
        seen = a;
        ++verdicts;
    });
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-test"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(verdicts, 1);
    QCOMPARE(fake.posts.size(), 1);
    QCOMPARE(seen.type, GoalAction::Continue);
    QCOMPARE(seen.text, QStringLiteral("Please run the tests."));
}

void TestGoalHttpJudge::runner_xmlText_emitsVerdict()
{
    FakeAnthropicClient fake;
    fake.replies.append(QByteArrayLiteral(
        R"({"content":[{"type":"text","text":"<action type=\"continue\">Please run the tests.</action>"}],)"
        R"("stop_reason":"end_turn"})"));
    GoalHttpJudgeRunner runner(&fake);
    GoalAction seen;
    int verdicts = 0;
    int retries = 0;
    QObject::connect(&runner, &GoalHttpJudgeRunner::verdict, [&](const GoalAction &a) {
        seen = a;
        ++verdicts;
    });
    QObject::connect(&runner, &GoalHttpJudgeRunner::assumedAchieved, [&](const QString &) {
        ++retries;
    });
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-test"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(verdicts, 1);
    QCOMPARE(retries, 0);
    QCOMPARE(fake.posts.size(), 1);
    QCOMPARE(seen.type, GoalAction::Continue);
    QCOMPARE(seen.text, QStringLiteral("Please run the tests."));
}

void TestGoalHttpJudge::runner_noToolThenComplete_retriesOnce()
{
    FakeAnthropicClient fake;
    fake.replies.append(QByteArrayLiteral(
        R"({"content":[{"type":"text","text":"Looks good."}],"stop_reason":"end_turn"})"));
    fake.replies.append(toolJson("complete", "Tests passed."));
    GoalHttpJudgeRunner runner(&fake);
    GoalAction seen;
    int verdicts = 0;
    QObject::connect(&runner, &GoalHttpJudgeRunner::verdict, [&](const GoalAction &a) {
        seen = a;
        ++verdicts;
    });
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-test"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(fake.posts.size(), 2);
    QCOMPARE(verdicts, 1);
    QCOMPARE(seen.type, GoalAction::Complete);
    const QJsonObject retryBody = QJsonDocument::fromJson(fake.posts.at(1).body).object();
    const QJsonArray messages = retryBody.value(QLatin1String("messages")).toArray();
    QVERIFY(messages.size() >= 2);
    QCOMPARE(messages.last().toObject().value(QLatin1String("content")).toString(),
             GoalHttpJudge::correctionPrompt());
}

void TestGoalHttpJudge::runner_noToolTwice_assumesAchieved()
{
    FakeAnthropicClient fake;
    const QByteArray prose = QByteArrayLiteral(
        R"({"content":[{"type":"text","text":"Done."}],"stop_reason":"end_turn"})");
    fake.replies.append(prose);
    fake.replies.append(prose);
    GoalHttpJudgeRunner runner(&fake);
    QString reason;
    int assumed = 0;
    int verdicts = 0;
    QObject::connect(&runner, &GoalHttpJudgeRunner::assumedAchieved, [&](const QString &r) {
        reason = r;
        ++assumed;
    });
    QObject::connect(&runner, &GoalHttpJudgeRunner::verdict, [&](const GoalAction &) {
        ++verdicts;
    });
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-test"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(fake.posts.size(), 2);
    QCOMPARE(assumed, 1);
    QCOMPARE(verdicts, 0);
    QVERIFY(!reason.isEmpty());
}

void TestGoalHttpJudge::runner_garbageBody_failsClosed()
{
    FakeAnthropicClient fake;
    fake.replies.append(QByteArrayLiteral("<html>Bad Gateway</html>"));
    GoalHttpJudgeRunner runner(&fake);
    int assumed = 0;
    int failed = 0;
    QObject::connect(&runner, &GoalHttpJudgeRunner::assumedAchieved, [&](const QString &) {
        ++assumed;
    });
    QObject::connect(&runner, &GoalHttpJudgeRunner::failed, [&](const QString &) {
        ++failed;
    });
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-test"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(assumed, 0);
    QCOMPARE(failed, 1);
    QCOMPARE(fake.posts.size(), 1);
}

void TestGoalHttpJudge::runner_emptyContent_failsClosed()
{
    FakeAnthropicClient fake;
    fake.replies.append(QByteArrayLiteral(R"({"content":[]})"));
    GoalHttpJudgeRunner runner(&fake);
    int assumed = 0;
    int failed = 0;
    QObject::connect(&runner, &GoalHttpJudgeRunner::assumedAchieved, [&](const QString &) {
        ++assumed;
    });
    QObject::connect(&runner, &GoalHttpJudgeRunner::failed, [&](const QString &) {
        ++failed;
    });
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-test"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(assumed, 0);
    QCOMPARE(failed, 1);
    QCOMPARE(fake.posts.size(), 1);
}

void TestGoalHttpJudge::formatFailureTrace_includesUrlAndModel_notKey()
{
    const QString trace = GoalHttpJudge::formatFailureTrace(
        QStringLiteral("Idle timeout"),
        QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
        QStringLiteral("claude-opus-5"));
    QVERIFY(trace.contains(QLatin1String("Idle timeout")));
    QVERIFY(trace.contains(QLatin1String("api.anthropic.com")));
    QVERIFY(trace.contains(QLatin1String("claude-opus-5")));
    QVERIFY(!trace.contains(QLatin1String("sk-"), Qt::CaseInsensitive));
}

void TestGoalHttpJudge::runner_httpError_failsClosed()
{
    FakeAnthropicClient fake;
    fake.errorStatus = 401;
    fake.errorMessage = QStringLiteral("Unauthorized");
    GoalHttpJudgeRunner runner(&fake);
    int assumed = 0;
    int failed = 0;
    QString failMsg;
    QObject::connect(&runner, &GoalHttpJudgeRunner::assumedAchieved, [&](const QString &) {
        ++assumed;
    });
    QObject::connect(&runner, &GoalHttpJudgeRunner::failed, [&](const QString &m) {
        failMsg = m;
        ++failed;
    });
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-test"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(assumed, 0);
    QCOMPARE(failed, 1);
    QVERIFY(!failMsg.contains(QLatin1String("sk-test")));
    QVERIFY(!failMsg.contains(QLatin1String("sk-"), Qt::CaseInsensitive));
    QVERIFY(!failMsg.contains(QLatin1String("url=")));
    QVERIFY(!failMsg.contains(QLatin1String("https://")));
    QVERIFY(!failMsg.contains(QLatin1String("claude-opus-5")));
}

void TestGoalHttpJudge::sessionFail_emitsReasonWithoutUrlModelOrKey()
{
    GoalHttpJudgeSession session;
    QString failMsg;
    QObject::connect(&session, &GoalHttpJudgeSession::failed, [&](const QString &m) {
        failMsg = m;
    });
    session.evaluate(nullptr, QStringLiteral("hi"));
    QCOMPARE(failMsg, QStringLiteral("custom_api_not_configured"));
    QVERIFY(!failMsg.contains(QLatin1String("url=")));
    QVERIFY(!failMsg.contains(QLatin1String("model=")));
    QVERIFY(!failMsg.contains(QLatin1String("http://")));
    QVERIFY(!failMsg.contains(QLatin1String("https://")));
    QVERIFY(!failMsg.contains(QLatin1String("sk-"), Qt::CaseInsensitive));
}

void TestGoalHttpJudge::isSafeHeaderValue_rejectsCRLF()
{
    QVERIFY(GoalHttpJudge::isSafeHeaderValue(QStringLiteral("sk-test")));
    QVERIFY(!GoalHttpJudge::isSafeHeaderValue(QStringLiteral("sk-test\r\nX-Injected: 1")));
    QVERIFY(!GoalHttpJudge::isSafeHeaderValue(QStringLiteral("sk-test\n")));
    QVERIFY(!GoalHttpJudge::isSafeHeaderValue(QStringLiteral("sk-test\r")));
}

void TestGoalHttpJudge::isUsableEndpointUrl_rejectsUserinfoAndCrlf()
{
    QVERIFY(GoalHttpJudge::isUsableEndpointUrl(QStringLiteral("https://api.anthropic.com")));
    QVERIFY(!GoalHttpJudge::isUsableEndpointUrl(
        QStringLiteral("https://user:pass@api.anthropic.com")));
    QVERIFY(!GoalHttpJudge::isUsableEndpointUrl(
        QStringLiteral("https://api.anthropic.com\r\nX-Injected: 1")));
    QVERIFY(!GoalHttpJudge::isUsableEndpointUrl(QStringLiteral("file:///tmp/x")));
}

void TestGoalHttpJudge::isTransportOutage_networkAnd5xx_not401()
{
    QVERIFY(GoalHttpJudge::isTransportOutage(0));
    QVERIFY(GoalHttpJudge::isTransportOutage(429));
    QVERIFY(GoalHttpJudge::isTransportOutage(503));
    QVERIFY(GoalHttpJudge::isTransportOutage(500));
    QVERIFY(!GoalHttpJudge::isTransportOutage(401));
    QVERIFY(!GoalHttpJudge::isTransportOutage(403));
    QVERIFY(!GoalHttpJudge::isTransportOutage(400));
}

static void fireTransportFail(GoalHttpJudgeRunner *runner, FakeAnthropicClient *fake)
{
    fake->errorStatus = 503;
    fake->errorMessage = QStringLiteral("HTTP 503");
    runner->evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                     QStringLiteral("sk-test"),
                     QStringLiteral("claude-opus-5"),
                     QStringLiteral("Evaluate criterion 1"));
}

void TestGoalHttpJudge::circuit_threeTransportFailures_rejectsFourth()
{
    FakeAnthropicClient fake;
    GoalHttpJudgeRunner runner(&fake);
    int failed = 0;
    QString last;
    QObject::connect(&runner, &GoalHttpJudgeRunner::failed, [&](const QString &m) {
        last = m;
        ++failed;
    });
    fireTransportFail(&runner, &fake);
    fireTransportFail(&runner, &fake);
    fireTransportFail(&runner, &fake);
    QCOMPARE(fake.posts.size(), 3);
    QCOMPARE(GoalHttpJudge::circuitState(), GoalHttpJudge::Circuit::Open);

    fireTransportFail(&runner, &fake);
    QCOMPARE(fake.posts.size(), 3);
    QCOMPARE(failed, 4);
    QCOMPARE(last, QLatin1String(GoalHttpJudge::kUnavailableReason));
    QCOMPARE(GoalHttpJudge::httpMetrics().rejected, quint64(1));
}

void TestGoalHttpJudge::circuit_authFailures_doNotOpen()
{
    FakeAnthropicClient fake;
    fake.errorStatus = 401;
    fake.errorMessage = QStringLiteral("Unauthorized");
    GoalHttpJudgeRunner runner(&fake);
    for (int i = 0; i < 5; ++i) {
        runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                        QStringLiteral("sk-test"),
                        QStringLiteral("claude-opus-5"),
                        QStringLiteral("Evaluate criterion 1"));
    }
    QCOMPARE(fake.posts.size(), 5);
    QCOMPARE(GoalHttpJudge::circuitState(), GoalHttpJudge::Circuit::Closed);
}

void TestGoalHttpJudge::circuit_successResetsFailures()
{
    FakeAnthropicClient fake;
    GoalHttpJudgeRunner runner(&fake);
    fireTransportFail(&runner, &fake);
    fireTransportFail(&runner, &fake);
    fake.errorStatus = 0;
    fake.errorMessage.clear();
    while (fake.replies.size() < fake.posts.size())
        fake.replies.append(QByteArray());
    fake.replies.append(toolJson("continue", "Please run the tests."));
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-test"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(GoalHttpJudge::circuitState(), GoalHttpJudge::Circuit::Closed);
    fireTransportFail(&runner, &fake);
    QCOMPARE(GoalHttpJudge::circuitState(), GoalHttpJudge::Circuit::Closed);
}

void TestGoalHttpJudge::circuit_openThenAdvance_allowsHalfOpenProbe()
{
    GoalHttpJudge::setNowMsForTesting(1000);
    FakeAnthropicClient fake;
    GoalHttpJudgeRunner runner(&fake);
    fireTransportFail(&runner, &fake);
    fireTransportFail(&runner, &fake);
    fireTransportFail(&runner, &fake);
    QCOMPARE(GoalHttpJudge::circuitState(), GoalHttpJudge::Circuit::Open);

    GoalHttpJudge::setNowMsForTesting(1000 + GoalHttpJudge::kCircuitOpenMs);
    fake.errorStatus = 0;
    while (fake.replies.size() < fake.posts.size())
        fake.replies.append(QByteArray());
    fake.replies.append(toolJson("continue", "Please run the tests."));
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-test"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(fake.posts.size(), 4);
    QCOMPARE(GoalHttpJudge::circuitState(), GoalHttpJudge::Circuit::Closed);
}

void TestGoalHttpJudge::settings_toJson_doesNotContainApiKey()
{
    GoalAgentSettings s;
    s.customApiBaseUrl = QStringLiteral("https://api.anthropic.com");
    s.customApiModel = QStringLiteral("claude-opus-5");
    const QJsonObject obj = s.toJson();
    QVERIFY(!obj.contains(QStringLiteral("apiKey")));
    QVERIFY(!obj.contains(QStringLiteral("api_key")));
    const QByteArray json = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    QVERIFY(!json.contains("sk-"));
}

void TestGoalHttpJudge::sanitizePlainText_stripsNulKeepsScriptLiteral()
{
    const QString raw = QStringLiteral("ok") + QChar(0) + QStringLiteral("<script>x</script>");
    QCOMPARE(GoalHttpJudge::sanitizePlainText(raw), QStringLiteral("ok<script>x</script>"));
    QVERIFY(GoalHttpJudge::sanitizePlainText(QStringLiteral("a\rb")).contains(QLatin1Char('a')));
    QVERIFY(!GoalHttpJudge::sanitizePlainText(QStringLiteral("a\rb")).contains(QLatin1Char('\r')));
}

void TestGoalHttpJudge::httpMetrics_retryIncrements()
{
    FakeAnthropicClient fake;
    fake.replies.append(QByteArrayLiteral(
        R"({"content":[{"type":"text","text":"Looks good."}],"stop_reason":"end_turn"})"));
    fake.replies.append(toolJson("continue", "Please run the tests."));
    GoalHttpJudgeRunner runner(&fake);
    int verdicts = 0;
    QObject::connect(&runner, &GoalHttpJudgeRunner::verdict, [&](const GoalAction &) {
        ++verdicts;
    });
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-test"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(verdicts, 1);
    QCOMPARE(GoalHttpJudge::httpMetrics().retries, quint64(1));
    QCOMPARE(GoalHttpJudge::httpMetrics().requests, quint64(1));
    QCOMPARE(GoalHttpJudge::httpMetrics().successes, quint64(1));
}

void TestGoalHttpJudge::buildRequestBody_escapesUserQuotes()
{
    const QByteArray body = GoalHttpJudge::buildRequestBody(
        QStringLiteral("claude-opus-5"),
        QStringLiteral("say \"hi\"\nand <script>x</script>"));
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    QVERIFY(doc.isObject());
    const QJsonArray messages = doc.object().value(QLatin1String("messages")).toArray();
    QVERIFY(!messages.isEmpty());
    QCOMPARE(messages.first().toObject().value(QLatin1String("content")).toString(),
             QStringLiteral("say \"hi\"\nand <script>x</script>"));
    QVERIFY(!body.contains("sk-"));
}

void TestGoalHttpJudge::observability_http500_metricsAndNoSecret()
{
    FakeAnthropicClient fake;
    fake.errorStatus = 500;
    fake.errorMessage = QStringLiteral("HTTP 500");
    GoalHttpJudgeRunner runner(&fake);
    QString failMsg;
    QObject::connect(&runner, &GoalHttpJudgeRunner::failed, [&](const QString &m) {
        failMsg = m;
    });
    runner.evaluate(QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")),
                    QStringLiteral("sk-observability-secret"),
                    QStringLiteral("claude-opus-5"),
                    QStringLiteral("Evaluate criterion 1"));
    QCOMPARE(failMsg, QStringLiteral("HTTP 500"));
    QVERIFY(!failMsg.contains(QLatin1String("sk-")));
    QVERIFY(GoalHttpJudge::httpMetrics().failures >= 1);
    QVERIFY(GoalHttpJudge::httpMetrics().lastTraceId >= 1);
    QVERIFY(GoalHttpJudge::httpMetrics().requests >= 1);
}

QTEST_MAIN(TestGoalHttpJudge)
#include "test_goal_http_judge.moc"
