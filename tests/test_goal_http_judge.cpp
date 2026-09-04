#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "GoalAgentSettings.h"
#include "GoalHttpJudge.h"
#include "GoalHttpJudgeRunner.h"

class TestGoalHttpJudge : public QObject
{
    Q_OBJECT

private slots:
    void messagesUrl_appendsV1MessagesToBareHost();
    void messagesUrl_doesNotDoubleWhenAlreadyComplete();
    void messagesUrl_stripsTrailingSlashBeforeAppend();
    void parseResponse_readsContinueToolUse();
    void parseResponse_readsCompleteToolUse();
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
    void isUsableEndpointUrl_rejectsNonHttp();
    void apiKeyPlaceholder_emptyVsStored();
    void correctionPrompt_namesTheVerdictTool();
    void judgePrompt_namesTheToolAndCriterion_notXmlAction();
    void buildRequestBody_includesModelToolAndUserPrompt();
    void settings_roundTripsCustomApiFields();
    void settings_oldJsonWithoutCustomApiFields_doesNotCrash();
    void isCustomApiAgent_matchesAgentId();
    void runner_continueTool_emitsVerdict();
    void runner_noToolThenComplete_retriesOnce();
    void runner_noToolTwice_assumesAchieved();
};

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
}

void TestGoalHttpJudge::judgePrompt_namesTheToolAndCriterion_notXmlAction()
{
    const QString prompt = GoalHttpJudge::judgePrompt(
        QStringLiteral("All tests pass"),
        QStringLiteral("agent: I added a test"),
        1, 10, 1, 2,
        QStringLiteral("Fix the flaky test"));
    QVERIFY(prompt.contains(QLatin1String("submit_goal_verdict")));
    QVERIFY(prompt.contains(QLatin1String("All tests pass")));
    QVERIFY(prompt.contains(QLatin1String("Fix the flaky test")));
    QVERIFY(!prompt.contains(QLatin1String("<action")));
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
}

QTEST_MAIN(TestGoalHttpJudge)
#include "test_goal_http_judge.moc"
