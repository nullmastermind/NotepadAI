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
#include "GoalPromptRenderer.h"
#include "remote/ExecutionContext.h"

#include <QDir>
#include <QJsonDocument>
#include <QStringList>

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
    if (!m_manager || !m_manager->registry()) {
        emit errorOccurred(tr("Could not generate prompt. Goal-agent settings are unavailable."));
        return false;
    }

    const AcpAgentDefinition agent = m_manager->registry()->agent(request.agentId);
    if (agent.id.isEmpty()) {
        emit errorOccurred(tr("Select a goal-agent before generating a prompt."));
        return false;
    }

    const QString prompt = renderPrompt(request);
    if (prompt.trimmed().isEmpty()) {
        emit errorOccurred(tr("Could not generate prompt. Check the selected template."));
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

    const QString text = action.text.trimmed();
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

    const QStringList criteria = criteriaLines(request.criteria);
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
        request.criteria.trimmed());
}

void GoalDraftGenerator::finishWithError(const QString &message)
{
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

    AcpConnection *conn = m_connection.data();
    m_connection = nullptr;
    if (!conn)
        return;

    disconnect(conn, nullptr, this, nullptr);
    conn->cancelPrompt();
    conn->deleteLater();
}
