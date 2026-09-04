#include "GoalHttpJudgeSession.h"

#include "ApplicationSettings.h"
#include "GoalAgentSettings.h"
#include "GoalHttpJudge.h"
#include "GoalHttpJudgeRunner.h"
#include "ai/AnthropicMessagesClient.h"
#include "ai/CredentialStore.h"

#include <QJsonDocument>
#include <QtGlobal>

GoalHttpJudgeSession::GoalHttpJudgeSession(QObject *parent)
    : QObject(parent)
    , m_client(new ai::AnthropicMessagesClient(this))
    , m_runner(new GoalHttpJudgeRunner(m_client, this))
{
    connect(m_runner, &GoalHttpJudgeRunner::verdict, this, [this](const GoalAction &action) {
        finishBusy();
        emit verdict(action);
    });
    connect(m_runner, &GoalHttpJudgeRunner::assumedAchieved, this, [this](const QString &reason) {
        finishBusy();
        emit assumedAchieved(reason);
    });
    connect(m_runner, &GoalHttpJudgeRunner::failed, this, [this](const QString &message) {
        fail(message);
    });
}

void GoalHttpJudgeSession::evaluate(ApplicationSettings *settings, const QString &prompt)
{
    GoalAgentSettings goalSettings;
    if (settings) {
        const QString settingsJson = settings->get("Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
        }
    }

    m_url = GoalHttpJudge::messagesUrl(goalSettings.customApiBaseUrl);
    m_model = goalSettings.customApiModel;

    if (goalSettings.customApiBaseUrl.trimmed().isEmpty()
        || m_model.trimmed().isEmpty()
        || !GoalHttpJudge::isUsableEndpointUrl(goalSettings.customApiBaseUrl)) {
        fail(QStringLiteral("custom_api_not_configured"));
        return;
    }

    ai::CredentialStore store;
    QString keyError;
    const QString apiKey = store.retrieveSecret(
        QLatin1String(GoalHttpJudge::kCredentialKey), &keyError);
    if (apiKey.isEmpty()) {
        fail(QStringLiteral("custom_api_key_missing"));
        return;
    }

    m_busy = true;
    emit busyChanged(true);
    m_runner->evaluate(m_url, apiKey, m_model, prompt);
}

void GoalHttpJudgeSession::cancel()
{
    if (m_runner)
        m_runner->cancel();
    finishBusy();
}

void GoalHttpJudgeSession::finishBusy()
{
    if (!m_busy)
        return;
    m_busy = false;
    emit busyChanged(false);
}

void GoalHttpJudgeSession::fail(const QString &reason)
{
    finishBusy();
    // Process log may include url/model for diagnosis; the failed() signal is
    // user-facing (transcript + debug dialog) and must not.
    qWarning("notepadai.goal.http: %s",
             qUtf8Printable(GoalHttpJudge::formatFailureTrace(reason, m_url, m_model)));
    emit failed(reason);
}
