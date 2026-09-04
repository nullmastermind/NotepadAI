#include "GoalHttpJudgeRunner.h"

#include "GoalHttpJudge.h"

#include <QtGlobal>

GoalHttpJudgeRunner::GoalHttpJudgeRunner(ai::IAnthropicMessagesClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    if (m_client) {
        connect(m_client, &ai::IAnthropicMessagesClient::finished,
                this, &GoalHttpJudgeRunner::onFinished);
        connect(m_client, &ai::IAnthropicMessagesClient::errorOccurred,
                this, &GoalHttpJudgeRunner::onError);
    }
}

void GoalHttpJudgeRunner::evaluate(const QUrl &url,
                                   const QString &apiKey,
                                   const QString &model,
                                   const QString &userPrompt)
{
    if (!m_client) {
        emit failed(QStringLiteral("no_http_client"));
        return;
    }
    m_url = url;
    m_apiKey = apiKey;
    m_model = model;
    m_originalPrompt = userPrompt;
    m_retried = false;
    m_busy = true;
    postPrompt(userPrompt);
}

void GoalHttpJudgeRunner::cancel()
{
    m_busy = false;
    if (m_client)
        m_client->cancel();
}

void GoalHttpJudgeRunner::postPrompt(const QString &userPrompt)
{
    ai::IAnthropicMessagesClient::Request req;
    req.url = m_url;
    req.apiKey = m_apiKey;
    req.body = GoalHttpJudge::buildRequestBody(m_model, userPrompt);
    m_client->post(req);
}

void GoalHttpJudgeRunner::onFinished(const QByteArray &body)
{
    if (!m_busy)
        return;

    GoalAction action;
    GoalHttpJudge::ParseError err = GoalHttpJudge::InvalidJson;
    const bool ok = GoalHttpJudge::parseResponse(body, &action, &err);
    const GoalHttpJudge::Decision decision = GoalHttpJudge::decide(
        ok ? GoalHttpJudge::NoError : err, action.type, m_retried);

    switch (decision) {
    case GoalHttpJudge::ApplyContinue:
    case GoalHttpJudge::ApplyComplete:
        m_busy = false;
        emit verdict(action);
        break;
    case GoalHttpJudge::RetryOnce: {
        m_retried = true;
        ai::IAnthropicMessagesClient::Request req;
        req.url = m_url;
        req.apiKey = m_apiKey;
        req.body = GoalHttpJudge::buildRetryBody(
            m_model,
            m_originalPrompt,
            GoalHttpJudge::assistantText(body),
            GoalHttpJudge::correctionPrompt());
        m_client->post(req);
        break;
    }
    case GoalHttpJudge::AssumeAchieved:
        m_busy = false;
        emit assumedAchieved(QStringLiteral(
            "Goal assumed achieved (model did not call submit_goal_verdict)."));
        break;
    case GoalHttpJudge::FailClosed:
        m_busy = false;
        qWarning("notepadai.goal.http: invalid_judge_response (empty-content or malformed)");
        emit failed(QStringLiteral("invalid_judge_response"));
        break;
    }
}

void GoalHttpJudgeRunner::onError(int httpStatus, const QString &message)
{
    Q_UNUSED(httpStatus)
    if (!m_busy)
        return;
    m_busy = false;
    qWarning("notepadai.goal.http: HTTP %d %s", httpStatus, qUtf8Printable(message));
    emit failed(message.isEmpty() ? QStringLiteral("http_error") : message);
}
