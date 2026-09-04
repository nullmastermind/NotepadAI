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

#include "GoalDraftGenerator.h"

#include "AcpAgentDefinition.h"
#include "AcpAgentManager.h"
#include "AcpAgentRegistry.h"
#include "AcpConnection.h"
#include "AcpErrorClassifier.h"
#include "ApplicationSettings.h"
#include "GoalActionParser.h"
#include "GoalAgentSettings.h"
#include "GoalConversationSummary.h"
#include "GoalHttpJudge.h"
#include "GoalHttpJudgeRunner.h"
#include "GoalHttpJudgeSession.h"
#include "GoalPromptRenderer.h"
#include "remote/ExecutionContext.h"

#include <QDir>
#include <QJsonDocument>
#include <QStringList>
#include <QUrl>
#include <exception>

namespace {

constexpr qsizetype kMaxResponseChars = 256 * 1024;

QStringList criteriaLines(const QString &criteria)
{
    const QStringList rawLines = criteria.split(QLatin1Char('\n'));
    QStringList lines;
    lines.reserve(rawLines.size());
    for (const auto &raw : rawLines) {
        const QString line = raw.trimmed();
        if (!line.isEmpty())
            lines.append(line);
    }
    return lines;
}

} // namespace

GoalDraftGenerator::GoalDraftGenerator(AcpAgentManager *manager,
                                       ApplicationSettings *settings,
                                       QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_settings(settings)
{
}

GoalDraftGenerator::~GoalDraftGenerator()
{
    cancel();
}

bool GoalDraftGenerator::start(const Request &request)
{
    if (m_running)
        return false;

    const QString criteria = request.criteria.trimmed();
    if (criteria.isEmpty()) {
        emit errorOccurred(tr("Enter criteria before generating a prompt."));
        return false;
    }

    const QString prompt = renderPrompt(request);
    if (prompt.trimmed().isEmpty()) {
        emit errorOccurred(tr("Could not generate prompt. Check the selected template."));
        return false;
    }

    if (GoalHttpJudge::isCustomApiAgent(request.agentId))
        return startHttp(prompt);

    if (!m_manager || !m_manager->registry()) {
        emit errorOccurred(tr("Could not generate prompt. Goal-agent settings are unavailable."));
        return false;
    }

    const AcpAgentDefinition agent = m_manager->registry()->agent(request.agentId);
    if (agent.id.isEmpty()) {
        emit errorOccurred(tr("Select a goal-agent before generating a prompt."));
        return false;
    }

    m_responseBuffer.clear();
    m_running = true;
    m_teardownDone = false;

    auto *conn = new AcpConnection(this);
    m_connection = conn;

    connect(conn, &AcpConnection::messageChunk,
            this, &GoalDraftGenerator::onMessageChunk);
    connect(conn, &AcpConnection::promptEnded,
            this, &GoalDraftGenerator::onPromptEnded);
    connect(conn, &AcpConnection::agentExited,
            this, &GoalDraftGenerator::onAgentExited);
    connect(conn, &AcpConnection::errorOccurred,
            this, [this](AcpErrorClassifier::AcpErrorKind, const QString &friendly) {
        finishWithError(friendly);
    });
    connect(conn, &AcpConnection::debugLogAppended,
            this, [this](const QString &line) {
        emit debugLogEntry(QStringLiteral("[draft] %1").arg(line));
    });

    const auto &builder = m_manager->remoteChannelBuilder();
    if (request.executionContext && request.executionContext->isRemote() && builder) {
        conn->setRemoteSpawn(request.executionContext, builder);
    }

    const QString cwd = request.workingDirectory.isEmpty()
        ? QDir::currentPath()
        : request.workingDirectory;
    conn->spawn(agent, cwd);
    if (!m_connection)
        return false;

    m_connection->sendPrompt(prompt, {});
    return true;
}

void GoalDraftGenerator::cancel()
{
    if (!m_running && m_connection.isNull())
        return;

    finishAndTeardown();
}

bool GoalDraftGenerator::parseDraftResponseForTesting(const QString &response, QString *draft,
                                                      bool *complete)
{
    return parseDraftResponse(response, draft, complete);
}

QString GoalDraftGenerator::renderPromptForTesting(const Request &request) const
{
    return renderPrompt(request);
}

void GoalDraftGenerator::setConnectionForTesting(AcpConnection *connection, bool running)
{
    finishAndTeardown();
    m_connection = connection;
    if (m_connection) {
        m_connection->setParent(this);
    }
    m_responseBuffer.clear();
    m_running = running;
    m_teardownDone = false;
}

void GoalDraftGenerator::setHttpRunnerForTesting(GoalHttpJudgeRunner *runner)
{
    m_httpRunner = runner;
}

bool GoalDraftGenerator::startHttp(const QString &prompt)
{
    m_responseBuffer.clear();
    m_running = true;
    m_teardownDone = false;
    m_httpSyncFailed = false;

    try {
        if (m_httpRunner) {
            connect(m_httpRunner, &GoalHttpJudgeRunner::verdict,
                    this, &GoalDraftGenerator::handleHttpVerdict, Qt::UniqueConnection);
            connect(m_httpRunner, &GoalHttpJudgeRunner::failed,
                    this, &GoalDraftGenerator::handleHttpFailed, Qt::UniqueConnection);
            connect(m_httpRunner, &GoalHttpJudgeRunner::assumedAchieved,
                    this, [this](const QString &) {
                        handleHttpFailed(QStringLiteral("invalid_judge_response"));
                    });
            m_httpRunner->evaluate(QUrl(QStringLiteral("https://example.test/v1/messages")),
                                   QStringLiteral("test-key"),
                                   QStringLiteral("test-model"),
                                   prompt);
            return !m_httpSyncFailed;
        }

        ensureHttpSession();
        if (!m_httpSession) {
            finishWithError(tr("Could not generate prompt. Check Custom API settings."));
            return false;
        }
        m_httpSession->evaluate(m_settings, prompt);
        return !m_httpSyncFailed;
    } catch (const std::exception &ex) {
        emit debugLogEntry(QStringLiteral("[draft] http exception: %1")
                               .arg(QString::fromUtf8(ex.what())));
        if (m_running) {
            finishWithError(tr("Could not generate prompt. Check Custom API settings."));
        }
        return false;
    } catch (...) {
        emit debugLogEntry(QStringLiteral("[draft] http exception"));
        if (m_running) {
            finishWithError(tr("Could not generate prompt. Check Custom API settings."));
        }
        return false;
    }
}

void GoalDraftGenerator::ensureHttpSession()
{
    if (m_httpSession)
        return;

    m_httpSession = new GoalHttpJudgeSession(this);
    connect(m_httpSession, &GoalHttpJudgeSession::verdict,
            this, &GoalDraftGenerator::handleHttpVerdict);
    connect(m_httpSession, &GoalHttpJudgeSession::failed,
            this, &GoalDraftGenerator::handleHttpFailed);
    connect(m_httpSession, &GoalHttpJudgeSession::assumedAchieved,
            this, [this](const QString &) {
                handleHttpFailed(QStringLiteral("invalid_judge_response"));
            });
}

void GoalDraftGenerator::handleHttpVerdict(const GoalAction &action)
{
    if (!m_running)
        return;

    if (action.type == GoalAction::Complete) {
        finishWithError(tr("Goal is already complete. No prompt was generated."));
        return;
    }

    const QString text = GoalHttpJudge::sanitizePlainText(action.text.trimmed());
    if (text.isEmpty()) {
        finishWithError(tr("Goal-agent returned an invalid prompt. Adjust the criteria and try again."));
        return;
    }

    finishAndTeardown();
    emit finished(text);
}

void GoalDraftGenerator::handleHttpFailed(const QString &message)
{
    if (!m_running)
        return;

    m_httpSyncFailed = true;
    emit debugLogEntry(QStringLiteral("[draft] http failed: %1").arg(message));

    QString userMessage = message;
    if (message == QLatin1String("custom_api_not_configured")) {
        userMessage = tr("Configure Custom API (Base URL and model) before generating a prompt.");
    } else if (message == QLatin1String("custom_api_key_missing")
               || message == QLatin1String("custom_api_key_invalid")) {
        userMessage = tr("Enter an API key for Custom API.");
    } else if (message == QLatin1String(GoalHttpJudge::kUnavailableReason)) {
        userMessage = tr("Custom API is temporarily unavailable. Try again in a moment.");
    } else if (message == QLatin1String("invalid_judge_response")) {
        userMessage = tr("Goal-agent returned an invalid prompt. Adjust the criteria and try again.");
    } else if (message.isEmpty()) {
        userMessage = tr("Could not generate prompt. Check Custom API settings.");
    }
    finishWithError(userMessage);
}

void GoalDraftGenerator::onMessageChunk(const QString &chunk)
{
    if (!m_running)
        return;

    if (m_responseBuffer.size() + chunk.size() > kMaxResponseChars) {
        finishWithError(tr("Goal-agent response was too large. Adjust the criteria and try again."));
        return;
    }
    m_responseBuffer.append(chunk);
}

void GoalDraftGenerator::onPromptEnded()
{
    if (!m_running)
        return;

    QString draft;
    bool complete = false;
    if (!parseDraftResponse(m_responseBuffer, &draft, &complete)) {
        finishWithError(tr("Goal-agent returned an invalid prompt. Adjust the criteria and try again."));
        return;
    }
    if (complete) {
        finishWithError(tr("Goal is already complete. No prompt was generated."));
        return;
    }

    finishAndTeardown();
    emit finished(draft);
}

void GoalDraftGenerator::onAgentExited(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitCode)
    Q_UNUSED(exitStatus)
    if (!m_running)
        return;

    finishWithError(tr("Goal-agent exited before generating a prompt."));
}

bool GoalDraftGenerator::parseDraftResponse(const QString &response, QString *draft,
                                            bool *complete)
{
    if (complete)
        *complete = false;

    GoalAction action;
    GoalActionParser::ParseError parseError = GoalActionParser::NoError;
    if (!GoalActionParser::parse(response, &action, &parseError)) {
        Q_UNUSED(parseError)
        if (draft)
            draft->clear();
        return false;
    }

    if (action.type == GoalAction::Complete) {
        if (draft)
            draft->clear();
        if (complete)
            *complete = true;
        return true;
    }

    const QString text = GoalHttpJudge::sanitizePlainText(action.text.trimmed());
    if (text.isEmpty()) {
        if (draft)
            draft->clear();
        return false;
    }
    if (draft)
        *draft = text;
    return true;
}

QString GoalDraftGenerator::renderPrompt(const Request &request) const
{
    GoalAgentSettings goalSettings;
    if (m_settings) {
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
        }
    }

    const GoalPromptTemplate *tpl = goalSettings.findTemplate(request.promptTemplateId);
    if (!tpl)
        tpl = &goalSettings.defaultTemplate();

    const QString sanitized = GoalHttpJudge::sanitizePlainText(request.criteria);
    const QStringList criteria = criteriaLines(sanitized);
    if (criteria.isEmpty())
        return QString();

    return GoalPromptRenderer::renderJudgePrompt(
        tpl->content,
        criteria.first(),
        GoalConversationSummary::fromModel(request.targetModel, 0),
        1,
        goalSettings.defaultMaxIterations,
        1,
        criteria.size(),
        sanitized.trimmed());
}

void GoalDraftGenerator::finishWithError(const QString &message)
{
    m_httpSyncFailed = true;
    finishAndTeardown();
    emit errorOccurred(message);
}

void GoalDraftGenerator::finishAndTeardown()
{
    if (m_teardownDone)
        return;

    m_teardownDone = true;
    ++m_teardownCount;
    m_running = false;
    m_responseBuffer.clear();

    if (m_httpRunner) {
        disconnect(m_httpRunner, nullptr, this, nullptr);
        m_httpRunner->cancel();
    }

    if (m_httpSession) {
        disconnect(m_httpSession, nullptr, this, nullptr);
        m_httpSession->cancel();
        m_httpSession->deleteLater();
        m_httpSession = nullptr;
    }

    AcpConnection *conn = m_connection.data();
    m_connection = nullptr;
    if (!conn)
        return;

    disconnect(conn, nullptr, this, nullptr);
    conn->cancelPrompt();
    conn->deleteLater();
}
