#include "GoalAgent.h"

#include "AcpAgentManager.h"
#include "AcpAgentRegistry.h"
#include "AcpConnection.h"
#include "AcpProtocol.h"
#include "AcpSessionModel.h"
#include "ApplicationSettings.h"
#include "ExecutionContext.h"
#include "GoalActionParser.h"
#include "GoalAgentSettings.h"
#include "GoalConversationSummary.h"
#include "GoalPromptRenderer.h"

#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QUuid>

Q_LOGGING_CATEGORY(lcGoal, "notepadai.goal")

GoalAgent::GoalAgent(AcpAgentManager *manager,
                     ApplicationSettings *settings,
                     QObject *parent)
    : QObject(parent)
    , m_manager(manager)
    , m_appSettings(settings)
{
}

GoalAgent::~GoalAgent()
{
    if (m_status == Active)
        stop();
}

void GoalAgent::setStatus(Status s)
{
    if (m_status == s)
        return;
    m_status = s;
    logDebug(QStringLiteral("status → %1").arg(s));
    emit statusChanged(s);
}

void GoalAgent::logDebug(const QString &msg)
{
    const QString ts = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const QString entry = QStringLiteral("[%1] [goal] %2").arg(ts, msg);
    qCInfo(lcGoal) << msg;
    emit debugLogEntry(entry);
}

bool GoalAgent::start(const StartRequest &req)
{
    if (m_status == Active) {
        logDebug(QStringLiteral("start: already active for %1").arg(m_targetSessionId));
        return false;
    }

    if (req.successCriteriaList.isEmpty()) {
        logDebug(QStringLiteral("start: empty criteria list"));
        return false;
    }

    m_targetSessionId = req.targetSessionId;
    m_agentId = req.agentId;
    m_maxIterations = req.maxIterations;
    m_promptTemplateId = req.promptTemplateId;
    m_originalUserMessage = req.originalUserMessage;
    m_currentCriterionIndex = 0;
    m_lastActionText.clear();
    m_judgeResponseBuffer.clear();
    m_awaitingJudgeResponse = false;
    m_correctionAttempted = false;
    m_awaitingAuthoring = false;
    m_authoringBuffer.clear();
    m_authoringVerdict.clear();

    m_criteria.clear();
    for (const auto &text : req.successCriteriaList) {
        Criterion c;
        c.text = text.trimmed();
        c.status = (m_criteria.isEmpty()) ? CriterionActive : Pending;
        m_criteria.append(c);
    }

    if (!m_targetConnection || !m_targetModel) {
        logDebug(QStringLiteral("start: target connection/model not set"));
        return false;
    }

    // Subscribe to target's promptEnded signal.
    connect(m_targetConnection, &AcpConnection::promptEnded,
            this, &GoalAgent::onTargetPromptEnded);
    connect(m_targetConnection, &QObject::destroyed,
            this, &GoalAgent::onTargetDestroyed);

    // Spawn the judge agent for criterion 0.
    spawnJudgeForCriterion(0);

    if (m_status != Idle) {
        logDebug(QStringLiteral("start: spawnJudge failed (status=%1)").arg(m_status));
        return false;
    }

    setStatus(Active);
    m_lastSeenTargetMessageCount = m_targetModel->messages().size();
    logDebug(QStringLiteral("start: OK, %1 criteria, agent=%2, maxIter=%3")
                 .arg(m_criteria.size()).arg(m_agentId).arg(m_maxIterations));
    return true;
}

void GoalAgent::setTargetSession(AcpConnection *conn, AcpSessionModel *model)
{
    m_targetConnection = conn;
    m_targetModel = model;
}

void GoalAgent::stop()
{
    if (m_status != Active)
        return;
    logDebug(QStringLiteral("stop: user requested"));
    destroyJudgeConnection();
    if (m_targetConnection) {
        disconnect(m_targetConnection, &AcpConnection::promptEnded,
                   this, &GoalAgent::onTargetPromptEnded);
        m_targetConnection->cancelPrompt();
    }
    markTerminal(Cancelled, QStringLiteral("user_stop"));
}

void GoalAgent::markTerminal(Status s, const QString &reason)
{
    logDebug(QStringLiteral("markTerminal: %1, reason=%2").arg(s).arg(reason));
    if (m_targetConnection) {
        disconnect(m_targetConnection, &AcpConnection::promptEnded,
                   this, &GoalAgent::onTargetPromptEnded);
    }
    setStatus(s);
}

void GoalAgent::destroyJudgeConnection()
{
    if (m_judgeConnection) {
        logDebug(QStringLiteral("destroyJudgeConnection: tearing down judge"));
        disconnect(m_judgeConnection, nullptr, this, nullptr);
        m_judgeConnection->deleteLater();
        m_judgeConnection = nullptr;
    }
}

void GoalAgent::spawnJudgeForCriterion(int index)
{
    destroyJudgeConnection();

    AcpAgentDefinition agent = m_manager->registry()->agent(m_agentId);
    if (agent.id.isEmpty()) {
        logDebug(QStringLiteral("spawnJudge: agent not found: %1").arg(m_agentId));
        markTerminal(Failed, QStringLiteral("goal_agent_not_found"));
        return;
    }

    auto *conn = new AcpConnection(this);
    m_judgeConnection = conn;

    connect(conn, &AcpConnection::messageChunk,
            this, &GoalAgent::onJudgeMessageChunk);
    connect(conn, &AcpConnection::promptEnded,
            this, &GoalAgent::onJudgePromptEnded);
    connect(conn, &AcpConnection::agentExited,
            this, &GoalAgent::onJudgeExited);
    connect(conn, &AcpConnection::debugLogAppended,
            this, [this](const QString &line) {
        emit debugLogEntry(QStringLiteral("[judge] %1").arg(line));
    });

    QString cwd = m_targetConnection ? m_targetConnection->workingDirectory()
                                     : QDir::currentPath();
    if (m_targetConnection && m_targetConnection->executionContext()
        && m_targetConnection->executionContext()->isRemote() && m_manager) {
        conn->setRemoteSpawn(m_targetConnection->executionContext(),
                             m_manager->remoteChannelBuilder());
    }
    conn->spawn(agent, cwd);

    logDebug(QStringLiteral("spawnJudge: criterion %1, agent %2, cwd=%3")
                 .arg(index).arg(m_agentId, cwd));
}

void GoalAgent::onTargetPromptEnded()
{
    if (m_status != Active)
        return;
    if (m_awaitingJudgeResponse) {
        logDebug(QStringLiteral("onTargetPromptEnded: skipped (awaiting judge)"));
        return;
    }
    if (m_awaitingAuthoring) {
        logDebug(QStringLiteral("onTargetPromptEnded: skipped (awaiting authoring)"));
        return;
    }
    logDebug(QStringLiteral("onTargetPromptEnded: evaluating criterion %1").arg(m_currentCriterionIndex));
    evaluateCurrentCriterion();
}

void GoalAgent::onTargetDestroyed()
{
    m_targetConnection = nullptr;
    m_targetModel = nullptr;
    if (m_status != Active)
        return;
    logDebug(QStringLiteral("onTargetDestroyed: target session terminated, target=%1")
                 .arg(m_targetSessionId));
    destroyJudgeConnection();
    setStatus(Failed);
}

void GoalAgent::evaluateCurrentCriterion()
{
    if (!m_judgeConnection) {
        logDebug(QStringLiteral("evaluateCurrentCriterion: no judge connection"));
        markTerminal(Failed, QStringLiteral("goal_agent_exited"));
        return;
    }

    m_awaitingJudgeResponse = true;
    m_correctionAttempted = false;
    m_judgeResponseBuffer.clear();

    // Load settings to get the prompt template.
    const QString settingsJson = m_appSettings->get(
        "Ai/GoalAgentSettings", QString());
    GoalAgentSettings goalSettings;
    if (!settingsJson.isEmpty()) {
        goalSettings = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }

    const GoalPromptTemplate *tpl = goalSettings.findTemplate(m_promptTemplateId);
    if (!tpl)
        tpl = &goalSettings.defaultTemplate();

    const auto &crit = m_criteria[m_currentCriterionIndex];
    const QString conversation = buildConversationSummary();

    logDebug(QStringLiteral("evaluateCurrentCriterion: criterion=\"%1\", conversation=%2 chars, template=%3")
                 .arg(crit.text.left(60)).arg(conversation.size()).arg(m_promptTemplateId));

    const QString prompt = GoalPromptRenderer::renderJudgePrompt(
        tpl->content,
        crit.text,
        conversation,
        crit.iteration + 1,
        m_maxIterations,
        m_currentCriterionIndex + 1,
        m_criteria.size(),
        m_originalUserMessage);

    logDebug(QStringLiteral("evaluateCurrentCriterion: sending judge prompt (%1 chars)")
                 .arg(prompt.size()));
    m_judgeConnection->sendPrompt(prompt, {});
}

void GoalAgent::onJudgeMessageChunk(const QString &chunk)
{
    if (!m_awaitingJudgeResponse)
        return;
    m_judgeResponseBuffer.append(chunk);
    if (m_judgeResponseBuffer.size() == chunk.size()) {
        logDebug(QStringLiteral("onJudgeMessageChunk: first chunk (%1 chars)").arg(chunk.size()));
    }
}

void GoalAgent::onJudgePromptEnded()
{
    if (!m_awaitingJudgeResponse)
        return;
    m_awaitingJudgeResponse = false;
    logDebug(QStringLiteral("onJudgePromptEnded: response %1 chars, content=%2")
                 .arg(m_judgeResponseBuffer.size())
                 .arg(m_judgeResponseBuffer.left(300)));
    processJudgeResponse();
}

void GoalAgent::onJudgeExited(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)
    logDebug(QStringLiteral("onJudgeExited: code=%1").arg(exitCode));
    if (m_status != Active)
        return;
    if (m_awaitingAuthoring) {
        m_awaitingAuthoring = false;
        logDebug(QStringLiteral("onJudgeExited: during authoring, falling back to raw criterion"));
        const int nextIdx = m_currentCriterionIndex + 1;
        finalizeHandoff(m_authoringVerdict, m_criteria[nextIdx].text, /*authoringSucceeded=*/false);
        return;
    }
    markTerminal(Failed, QStringLiteral("goal_agent_exited"));
}

void GoalAgent::processJudgeResponse()
{
    GoalAction action;
    GoalActionParser::ParseError parseErr;
    if (!GoalActionParser::parse(m_judgeResponseBuffer, &action, &parseErr)) {
        logDebug(QStringLiteral("processJudgeResponse: parse failed (err=%1), correction=%2")
                     .arg(static_cast<int>(parseErr)).arg(m_correctionAttempted));
        if (!m_correctionAttempted && m_judgeConnection) {
            m_correctionAttempted = true;
            m_judgeResponseBuffer.clear();
            m_awaitingJudgeResponse = true;
            m_judgeConnection->sendPrompt(GoalActionParser::correctionPrompt(), {});
            return;
        }
        markTerminal(Failed, QStringLiteral("parse_failure_2x"));
        destroyJudgeConnection();
        return;
    }

    m_lastActionText = action.text;
    logDebug(QStringLiteral("processJudgeResponse: action=%1, text=%2")
                 .arg(action.type == GoalAction::Complete
                          ? QStringLiteral("complete")
                          : QStringLiteral("continue"),
                      action.text.left(100)));
    emit actionEmitted(action.type == GoalAction::Complete
                           ? QStringLiteral("complete")
                           : QStringLiteral("continue"),
                       action.text);

    if (action.type == GoalAction::Continue) {
        auto &crit = m_criteria[m_currentCriterionIndex];
        crit.iteration++;
        emit iterationChanged(m_currentCriterionIndex, crit.iteration);

        if (crit.iteration >= m_maxIterations) {
            destroyJudgeConnection();
            markTerminal(Cancelled, QStringLiteral("max_iter"));
            return;
        }

        // Forward the continue text to the target agent.
        if (m_targetConnection) {
            logDebug(QStringLiteral("processJudgeResponse: forwarding continue to target (%1 chars)")
                         .arg(action.text.size()));
            if (m_targetModel) {
                m_targetModel->appendUserMessage(action.text, {}, /*fromGoalAgent=*/true);
            }
            m_targetConnection->sendPrompt(action.text, {});
        }
        m_lastSeenTargetMessageCount = m_targetModel
            ? m_targetModel->messages().size() : 0;
    } else {
        // Complete
        advanceToNextCriterion(action.text);
    }
}

void GoalAgent::advanceToNextCriterion(const QString &verdict)
{
    auto &crit = m_criteria[m_currentCriterionIndex];
    crit.status = Archived;
    crit.verdict = verdict;

    bool isFinal = (m_currentCriterionIndex + 1) >= m_criteria.size();
    logDebug(QStringLiteral("advanceToNextCriterion: criterion %1 archived, isFinal=%2, verdict=%3")
                 .arg(m_currentCriterionIndex).arg(isFinal).arg(verdict.left(80)));
    if (isFinal) {
        destroyJudgeConnection();
        markTerminal(Achieved, QStringLiteral("complete: ") + verdict.left(200));
        return;
    }

    // Not final — begin the authoring step on the OLD judge (still alive).
    beginAuthoringStep(verdict);
}

void GoalAgent::beginAuthoringStep(const QString &verdict)
{
    if (!m_judgeConnection) {
        logDebug(QStringLiteral("beginAuthoringStep: no judge, falling back to raw criterion"));
        finalizeHandoff(verdict, m_criteria[m_currentCriterionIndex + 1].text, /*authoringSucceeded=*/false);
        return;
    }

    m_awaitingAuthoring = true;
    m_awaitingJudgeResponse = false;
    m_authoringBuffer.clear();
    m_authoringVerdict = verdict;

    // Disconnect the normal judge response handlers and wire authoring handlers.
    disconnect(m_judgeConnection, &AcpConnection::messageChunk,
               this, &GoalAgent::onJudgeMessageChunk);
    disconnect(m_judgeConnection, &AcpConnection::promptEnded,
               this, &GoalAgent::onJudgePromptEnded);
    connect(m_judgeConnection, &AcpConnection::messageChunk,
            this, &GoalAgent::onAuthoringChunk);
    connect(m_judgeConnection, &AcpConnection::promptEnded,
            this, &GoalAgent::onAuthoringPromptEnded);

    // Load settings for the authoring template.
    const QString settingsJson = m_appSettings->get(
        "Ai/GoalAgentSettings", QString());
    GoalAgentSettings goalSettings;
    if (!settingsJson.isEmpty()) {
        goalSettings = GoalAgentSettings::fromJson(
            QJsonDocument::fromJson(settingsJson.toUtf8()).object());
    }

    const int nextIdx = m_currentCriterionIndex + 1;
    const auto &nextCrit = m_criteria[nextIdx];
    const QString recentMsgs = collectRecentUserMessages(3, 500);

    const QString authoringPrompt = GoalPromptRenderer::renderHandoffAuthoring(
        goalSettings.handoffAuthoringTemplate,
        verdict,
        nextCrit.text,
        nextIdx + 1,
        m_criteria.size(),
        recentMsgs);

    logDebug(QStringLiteral("beginAuthoringStep: sending authoring prompt to old judge (%1 chars)")
                 .arg(authoringPrompt.size()));
    m_judgeConnection->sendPrompt(authoringPrompt, {});
}

void GoalAgent::onAuthoringChunk(const QString &chunk)
{
    if (!m_awaitingAuthoring)
        return;
    m_authoringBuffer.append(chunk);
}

void GoalAgent::onAuthoringPromptEnded()
{
    if (!m_awaitingAuthoring)
        return;
    m_awaitingAuthoring = false;

    QString authored = m_authoringBuffer.trimmed();
    logDebug(QStringLiteral("onAuthoringPromptEnded: authored %1 chars, content=%2")
                 .arg(authored.size()).arg(authored.left(200)));

    const int nextIdx = m_currentCriterionIndex + 1;
    if (authored.isEmpty()) {
        logDebug(QStringLiteral("onAuthoringPromptEnded: empty response, using raw criterion"));
        authored = m_criteria[nextIdx].text;
        finalizeHandoff(m_authoringVerdict, authored, /*authoringSucceeded=*/false);
    } else {
        finalizeHandoff(m_authoringVerdict, authored, /*authoringSucceeded=*/true);
    }
}

void GoalAgent::finalizeHandoff(const QString &verdict, const QString &authoredText,
                                bool authoringSucceeded)
{
    // Advance cursor.
    m_currentCriterionIndex++;
    m_criteria[m_currentCriterionIndex].status = CriterionActive;
    emit criterionAdvanced(m_currentCriterionIndex);

    // Destroy old judge, spawn new one.
    destroyJudgeConnection();
    spawnJudgeForCriterion(m_currentCriterionIndex);

    QString handoff;
    if (authoringSucceeded) {
        handoff = authoredText;
    } else {
        const QString settingsJson = m_appSettings->get(
            "Ai/GoalAgentSettings", QString());
        GoalAgentSettings goalSettings;
        if (!settingsJson.isEmpty()) {
            goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
        }

        handoff = GoalPromptRenderer::renderHandoff(
            goalSettings.handoffTemplate,
            verdict,
            authoredText,
            m_currentCriterionIndex + 1,
            m_criteria.size());
    }

    if (m_targetConnection) {
        logDebug(QStringLiteral("finalizeHandoff: sending handoff to target (%1 chars)")
                     .arg(handoff.size()));
        if (m_targetModel) {
            m_targetModel->appendUserMessage(handoff, {}, /*fromGoalAgent=*/true);
        }
        m_targetConnection->sendPrompt(handoff, {});
    }
    m_lastSeenTargetMessageCount = m_targetModel
        ? m_targetModel->messages().size() : 0;
}

QString GoalAgent::collectRecentUserMessages(int take, int perEntryCharCap)
{
    if (!m_targetModel)
        return QStringLiteral("(no recent user messages)");

    const auto &msgs = m_targetModel->messages();
    QStringList collected;
    for (int i = msgs.size() - 1; i >= 0 && collected.size() < take; --i) {
        const auto &msg = msgs[i];
        if (msg.role != QLatin1String("user"))
            continue;
        QString text;
        for (const auto &block : msg.content) {
            if (block.kind == AcpProtocol::AcpContentBlock::Kind::Text)
                text += block.text;
        }
        text = text.trimmed();
        if (text.isEmpty())
            continue;
        if (text.size() > perEntryCharCap) {
            text.truncate(perEntryCharCap);
            text.append(QChar(0x2026)); // …
        }
        collected.prepend(text);
    }

    if (collected.isEmpty())
        return QStringLiteral("(no recent user messages)");

    return collected.join(QStringLiteral("\n---\n"));
}

QString GoalAgent::buildConversationSummary()
{
    if (!m_targetModel)
        return QStringLiteral("<conversation />");

    const auto &msgs = m_targetModel->messages();
    const int startIdx = m_lastSeenTargetMessageCount;
    logDebug(QStringLiteral("buildConversationSummary: msgs=%1, startIdx=%2, new=%3")
                 .arg(msgs.size()).arg(startIdx).arg(msgs.size() - startIdx));
    const QString xml = GoalConversationSummary::fromModel(m_targetModel, startIdx);
    m_lastSeenTargetMessageCount = msgs.size();
    return xml;
}
