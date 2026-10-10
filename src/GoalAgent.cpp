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
#include "GoalHttpJudge.h"
#include "GoalHttpJudgeSession.h"
#include "GoalPromptRenderer.h"
#include "PiAcpJudgeConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonObject>
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
    m_autoCompact = req.autoCompact;
    m_prefixGoal = req.prefixGoal;
    m_originalUserMessage = req.originalUserMessage;
    m_currentCriterionIndex = 0;
    m_lastActionText.clear();
    m_judgeResponseBuffer.clear();
    m_awaitingJudgeResponse = false;
    m_judgeVerdictApplied = false;
    m_correctionAttempted = false;
    m_awaitingAuthoring = false;
    m_restartedSinceLastEval = false;
    m_restartingTarget = false;
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

    if (req.attachToExistingConversation && m_originalUserMessage.isEmpty()) {
        m_originalUserMessage = GoalConversationSummary::latestDeveloperText(
            m_targetModel->messages());
    }

    m_developerRequests = GoalConversationSummary::developerRequestsXml(
        m_targetModel->messages(), m_originalUserMessage);

    // Subscribe to target's promptEnded signal.
    connect(m_targetConnection, &AcpConnection::promptEnded,
            this, &GoalAgent::onTargetPromptEnded);
    connect(m_targetConnection, &QObject::destroyed,
            this, &GoalAgent::onTargetDestroyed);

    if (GoalHttpJudge::isCustomApiAgent(m_agentId)) {
        ensureHttpJudge();
    } else {
        spawnJudgeForCriterion(0);
        if (m_status != Idle) {
            logDebug(QStringLiteral("start: spawnJudge failed (status=%1)").arg(m_status));
            return false;
        }
    }

    setStatus(Active);
    if (req.attachToExistingConversation) {
        // Include the already-sent turn in the first judge prompt. When the
        // target is still streaming, wait for promptEnded instead of judging
        // a partial reply.
        m_lastSeenTargetMessageCount = 0;
        logDebug(QStringLiteral("start: OK, %1 criteria, agent=%2, maxIter=%3, attach")
                     .arg(m_criteria.size()).arg(m_agentId).arg(m_maxIterations));
        if (m_prefixGoal)
            sendFirstCriterionGoal();
        else if (!m_targetModel->isProcessing())
            evaluateCurrentCriterion();
    } else {
        m_lastSeenTargetMessageCount = m_targetModel->messages().size();
        logDebug(QStringLiteral("start: OK, %1 criteria, agent=%2, maxIter=%3")
                     .arg(m_criteria.size()).arg(m_agentId).arg(m_maxIterations));
    }
    return true;
}

GoalAgent::LaunchAction GoalAgent::launchAction(bool hasComposer, bool sessionHasHistory, bool processing)
{
    if (hasComposer && !processing)
        return LaunchAction::Send;
    if (sessionHasHistory || processing)
        return LaunchAction::Attach;
    return LaunchAction::NeedComposer;
}

QStringList GoalAgent::nativeGoalCommands(const QStringList &criteria)
{
    QStringList commands;
    commands.reserve(criteria.size());
    for (const QString &criterion : criteria) {
        const QString row = criterion.trimmed();
        if (!row.isEmpty())
            commands.append(QStringLiteral("/goal ") + row);
    }
    return commands;
}

QString GoalAgent::nativeGoalWireText(const QString &goalCommand, bool injectWorktree)
{
    if (!injectWorktree)
        return goalCommand;
    QString instruction = nativeGoalWorktreeInstruction();
    if (goalCommand.isEmpty())
        return instruction;
    return goalCommand + QLatin1String("\n\n") + instruction;
}

bool GoalAgent::isNativeGoalSlash(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (!trimmed.startsWith(QLatin1String("/goal")))
        return false;
    return trimmed.size() == 5 || trimmed.at(5).isSpace();
}

bool GoalAgent::nativeGoalUsesSidePrompt(const QString &text, bool agentAdvertisesGoal)
{
    return agentAdvertisesGoal && isNativeGoalSlash(text);
}


QString GoalAgent::prefixGoalMessage(const QString &text)
{
    if (text.isEmpty() || isNativeGoalSlash(text))
        return text;
    return QStringLiteral("/goal ") + text;
}

void GoalAgent::setTargetSession(AcpConnection *conn, AcpSessionModel *model)
{
    m_targetConnection = conn;
    m_targetModel = model;
}

void GoalAgent::sendFirstCriterionGoal()
{
    if (m_status != Active || !m_prefixGoal)
        return;
    if (m_criteria.isEmpty() || !m_targetConnection)
        return;
    const QString sent = prefixGoalMessage(m_criteria[0].text);
    const bool processing = m_targetModel && m_targetModel->isProcessing();
    const bool advertises = m_targetModel && m_targetModel->agentAdvertisesGoalCommand();
    if (mustQueueGoalFollowUp(processing, advertises, sent)) {
        m_deferredPrefixGoal = true;
        emit goalFollowUpQueued(sent);
        return;
    }
    if (m_targetModel)
        m_targetModel->appendUserMessage(sent, {}, /*fromGoalAgent=*/true);
    m_targetConnection->sendSidePrompt(wireTextForTarget(sent));
}

bool GoalAgent::mustQueueGoalFollowUp(bool processing, bool agentAdvertisesGoal,
                                      const QString &text)
{
    return processing && !text.isEmpty()
        && !nativeGoalUsesSidePrompt(text, agentAdvertisesGoal);
}

void GoalAgent::noteDeferredGoalDispatched()
{
    m_deferredPrefixGoal = false;
}

void GoalAgent::noteDeferredGoalDropped()
{
    if (!m_deferredPrefixGoal)
        return;
    m_deferredPrefixGoal = false;
    if (m_status != Active || m_awaitingJudgeResponse || m_awaitingAuthoring)
        return;
    if (m_targetModel && m_targetModel->isProcessing())
        return;
    evaluateCurrentCriterion();
}

void GoalAgent::stop()
{
    if (m_status != Active)
        return;
    logDebug(QStringLiteral("stop: user requested"));
    // Tear down the supervisor only: HTTP judge (API) and/or the spawned ACP
    // judge connection (Agent). Do not cancelPrompt() the target session —
    // the user's ACP agent keeps its in-flight turn. Composer Cancel is the
    // path that stops both (AcpSessionView::onCancelClicked).
    destroyJudgeConnection();
    if (m_httpSession)
        m_httpSession->cancel();
    markTerminal(Cancelled, QStringLiteral("user_stop"));
}

void GoalAgent::markTerminal(Status s, const QString &reason)
{
    cancelJudgePromptTimeout();
    logDebug(QStringLiteral("markTerminal: %1, reason=%2").arg(s).arg(reason));
    if (m_targetConnection) {
        disconnect(m_targetConnection, &AcpConnection::promptEnded,
                   this, &GoalAgent::onTargetPromptEnded);
    }
    setStatus(s);
    if (s == Achieved)
        maybeSendAutoCompact();
}

void GoalAgent::maybeSendAutoCompact()
{
    if (!m_autoCompact)
        return;
    sendAutoCompactTo(m_targetConnection, m_targetModel);
}

void GoalAgent::sendAutoCompactTo(AcpConnection *conn, AcpSessionModel *model)
{
    if (!conn)
        return;
    if (model)
        model->appendUserMessage(QStringLiteral("/compact"), {}, /*fromGoalAgent=*/true);
    conn->sendPrompt(QStringLiteral("/compact"), {});
}

void GoalAgent::destroyJudgeConnection()
{
    collapseGoalJudgeTranscript();
    m_judgeToolNames.clear();
    m_judgeToolTitles.clear();
    if (m_judgeConnection) {
        logDebug(QStringLiteral("destroyJudgeConnection: tearing down judge"));
        disconnect(m_judgeConnection, nullptr, this, nullptr);
        m_judgeConnection->deleteLater();
        m_judgeConnection = nullptr;
    }
    m_piJudgeAgentDir.reset();
}

void GoalAgent::attachVerdictMcp(AcpConnection *conn)
{
    if (!conn)
        return;
    if (m_targetConnection && m_targetConnection->executionContext()
        && m_targetConnection->executionContext()->isRemote())
        return;
    const QString exe = QCoreApplication::applicationFilePath();
    if (exe.isEmpty())
        return;
    QJsonObject server;
    server.insert(QStringLiteral("name"), QStringLiteral("goal-verdict"));
    server.insert(QStringLiteral("command"), exe);
    server.insert(QStringLiteral("args"),
                  QJsonArray{QStringLiteral("--goal-verdict-mcp")});
    // ACP McpServerStdio requires env as EnvVariable[] — omit it and Zod
    // agents (pi-acp, claude-agent-acp) reject session/new.
    server.insert(QStringLiteral("env"), QJsonArray{});
    conn->setMcpServers(QJsonArray{server});
}

void GoalAgent::stagePiAcpJudgeDir(AcpAgentDefinition *agent)
{
    if (!agent || !PiAcpJudgeConfig::isPiAcpAgent(*agent))
        return;
    if (m_targetConnection && m_targetConnection->executionContext()
        && m_targetConnection->executionContext()->isRemote())
        return;
    m_piJudgeAgentDir = std::make_unique<QTemporaryDir>();
    if (!m_piJudgeAgentDir->isValid()) {
        m_piJudgeAgentDir.reset();
        return;
    }
    const QString exe = QCoreApplication::applicationFilePath();
    if (!PiAcpJudgeConfig::stageJudgeAgentDir(PiAcpJudgeConfig::sourceAgentDir(*agent),
                                              m_piJudgeAgentDir->path(), exe)) {
        m_piJudgeAgentDir.reset();
        return;
    }
    agent->env.insert(QLatin1String(PiAcpJudgeConfig::kEnvAgentDir), m_piJudgeAgentDir->path());
}

void GoalAgent::considerJudgeVerdict(const QString &name, const QString &title,
                                     const QJsonObject &rawInput)
{
    if (!m_awaitingJudgeResponse || m_judgeVerdictApplied)
        return;
    GoalAction action;
    if (!GoalActionParser::parseToolCall(name, title, rawInput, &action))
        return;
    m_judgeVerdictApplied = true;
    m_awaitingJudgeResponse = false;
    cancelJudgePromptTimeout();
    // Tool result does not end the judge turn. Cancel leftover generation
    // before applyJudgeAction, which may session/prompt the same connection
    // (authoring). A later evaluate must not queue behind that turn either.
    if (m_judgeConnection)
        m_judgeConnection->cancelPrompt();
    applyJudgeAction(action);
}

void GoalAgent::handleJudgeToolCall(const AcpProtocol::AcpToolCall &tc)
{
    if (!m_awaitingJudgeResponse && !m_awaitingAuthoring)
        return;
    if (!tc.id.isEmpty()) {
        if (!tc.name.isEmpty())
            m_judgeToolNames.insert(tc.id, tc.name);
        if (!tc.title.isEmpty())
            m_judgeToolTitles.insert(tc.id, tc.title);
    }
    if (m_targetModel)
        m_targetModel->upsertGoalJudgeToolCall(tc);
    considerJudgeVerdict(tc.name, tc.title, tc.rawInput);
}

void GoalAgent::handleJudgeToolCallUpdate(const AcpProtocol::AcpToolCallUpdate &update)
{
    if (!m_awaitingJudgeResponse && !m_awaitingAuthoring)
        return;
    if (!update.id.isEmpty()) {
        if (update.name && !update.name->isEmpty())
            m_judgeToolNames.insert(update.id, *update.name);
        if (update.title && !update.title->isEmpty())
            m_judgeToolTitles.insert(update.id, *update.title);
    }
    if (m_targetModel)
        m_targetModel->applyGoalJudgeToolCallUpdate(update);
    // ACP tool_call_update omits unchanged fields. Name is only required on
    // the first report; arguments often arrive later in rawInput.
    const QString name = (update.name && !update.name->isEmpty())
        ? *update.name
        : m_judgeToolNames.value(update.id);
    const QString title = (update.title && !update.title->isEmpty())
        ? *update.title
        : m_judgeToolTitles.value(update.id);
    considerJudgeVerdict(name, title, update.rawInput.value_or(QJsonObject{}));
}

void GoalAgent::configureHeadlessJudge(AcpConnection *conn)
{
    if (!conn)
        return;
    // Same reason as AcpAgentManager::runHeadlessPrompt: this connection has
    // no permission UI. Manual policy parks the request in
    // m_pendingPermissions and the judge turn never ends.
    conn->setAutoApprovePolicyProvider([]() {
        return QStringLiteral("allowAll");
    });
}

void GoalAgent::beginGoalJudgeTranscript(const QString &headline)
{
    if (!m_targetModel)
        return;
    m_targetModel->beginGoalJudgeTurn(headline);
}

void GoalAgent::finishGoalJudgeTranscript()
{
    if (m_targetModel)
        m_targetModel->closeGoalJudgeStreaming();
}

void GoalAgent::collapseGoalJudgeTranscript()
{
    finishGoalJudgeTranscript();
    if (m_targetModel)
        m_targetModel->removeGoalJudgeTurn();
}

void GoalAgent::armJudgePromptTimeout()
{
    if (m_judgePromptTimeoutMs <= 0)
        return;
    if (!m_judgeTimeoutTimer) {
        m_judgeTimeoutTimer = new QTimer(this);
        m_judgeTimeoutTimer->setSingleShot(true);
        connect(m_judgeTimeoutTimer, &QTimer::timeout, this, &GoalAgent::onJudgePromptTimedOut);
    }
    m_judgeTimeoutTimer->start(m_judgePromptTimeoutMs);
}

void GoalAgent::cancelJudgePromptTimeout()
{
    if (m_judgeTimeoutTimer)
        m_judgeTimeoutTimer->stop();
}

void GoalAgent::onJudgePromptTimedOut()
{
    if (m_status != Active)
        return;
    if (!m_awaitingJudgeResponse && !m_awaitingAuthoring)
        return;
    logDebug(QStringLiteral("onJudgePromptTimedOut: judge did not end the turn"));
    m_awaitingJudgeResponse = false;
    m_awaitingAuthoring = false;
    finishGoalJudgeTranscript();
    m_lastActionText = tr("Goal agent did not return a verdict.");
    destroyJudgeConnection();
    markTerminal(Failed, QStringLiteral("judge_timeout"));
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
    configureHeadlessJudge(conn);
    attachVerdictMcp(conn);

    connect(conn, &AcpConnection::messageChunk,
            this, &GoalAgent::onJudgeMessageChunk);
    connect(conn, &AcpConnection::thoughtChunk, this, [this](const QString &chunk) {
        if (!m_awaitingJudgeResponse && !m_awaitingAuthoring)
            return;
        if (m_targetModel)
            m_targetModel->appendGoalJudgeThoughtChunk(chunk);
    });
    connect(conn, &AcpConnection::toolCallReceived, this, &GoalAgent::handleJudgeToolCall);
    connect(conn, &AcpConnection::toolCallUpdated, this, &GoalAgent::handleJudgeToolCallUpdate);
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
    } else {
        stagePiAcpJudgeDir(&agent);
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
    if (m_deferredPrefixGoal) {
        logDebug(QStringLiteral("onTargetPromptEnded: skipped (deferred /goal still queued)"));
        return;
    }
    logDebug(QStringLiteral("onTargetPromptEnded: evaluating criterion %1").arg(m_currentCriterionIndex));
    evaluateCurrentCriterion();
}

void GoalAgent::onTargetDestroyed()
{
    m_targetConnection = nullptr;
    m_targetModel = nullptr;
    if (m_restartingTarget)
        return;
    if (m_status != Active)
        return;
    logDebug(QStringLiteral("onTargetDestroyed: target session terminated, target=%1")
                 .arg(m_targetSessionId));
    destroyJudgeConnection();
    setStatus(Failed);
}

void GoalAgent::evaluateCurrentCriterion()
{
    m_restartedSinceLastEval = false;
    if (GoalHttpJudge::isCustomApiAgent(m_agentId)) {
        evaluateViaHttp();
        return;
    }

    if (!m_judgeConnection) {
        logDebug(QStringLiteral("evaluateCurrentCriterion: no judge connection"));
        markTerminal(Failed, QStringLiteral("goal_agent_exited"));
        return;
    }

    m_awaitingJudgeResponse = true;
    m_judgeVerdictApplied = false;
    m_judgeToolNames.clear();
    m_judgeToolTitles.clear();
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
        m_originalUserMessage,
        m_developerRequests);

    logDebug(QStringLiteral("evaluateCurrentCriterion: sending judge prompt (%1 chars)")
                 .arg(prompt.size()));
    beginGoalJudgeTranscript(tr("Goal · judging %1/%2")
                                 .arg(m_currentCriterionIndex + 1)
                                 .arg(m_criteria.size()));
    m_judgeConnection->sendPrompt(prompt, {});
    armJudgePromptTimeout();
}

void GoalAgent::onJudgeMessageChunk(const QString &chunk)
{
    if (m_awaitingAuthoring) {
        m_authoringBuffer.append(chunk);
    } else if (m_awaitingJudgeResponse) {
        m_judgeResponseBuffer.append(chunk);
        if (m_judgeResponseBuffer.size() == chunk.size()) {
            logDebug(QStringLiteral("onJudgeMessageChunk: first chunk (%1 chars)").arg(chunk.size()));
        }
    } else {
        return;
    }
    if (!m_targetModel)
        return;
    const QString &raw = m_awaitingAuthoring ? m_authoringBuffer : m_judgeResponseBuffer;
    const QString shown = GoalActionParser::displayText(raw);
    if (!shown.isEmpty())
        m_targetModel->replaceGoalJudgeText(shown);
}

void GoalAgent::onJudgePromptEnded()
{
    if (!m_awaitingJudgeResponse)
        return;
    cancelJudgePromptTimeout();
    m_awaitingJudgeResponse = false;
    finishGoalJudgeTranscript();
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
            beginGoalJudgeTranscript(tr("Goal · judging %1/%2")
                                         .arg(m_currentCriterionIndex + 1)
                                         .arg(m_criteria.size()));
            m_judgeConnection->sendPrompt(GoalActionParser::correctionPrompt(), {});
            armJudgePromptTimeout();
            return;
        }
        markTerminal(Failed, QStringLiteral("parse_failure_2x"));
        destroyJudgeConnection();
        return;
    }

    applyJudgeAction(action);
}

void GoalAgent::applyJudgeAction(const GoalAction &action)
{
    if (m_status != Active)
        return;

    collapseGoalJudgeTranscript();
    m_lastActionText = action.text;
    QString typeName = QStringLiteral("continue");
    if (action.type == GoalAction::Complete)
        typeName = QStringLiteral("complete");
    else if (action.type == GoalAction::Restart)
        typeName = QStringLiteral("restart");
    logDebug(QStringLiteral("processJudgeResponse: action=%1, text=%2")
                 .arg(typeName, action.text.left(100)));
    emit actionEmitted(typeName, action.text);

    if (action.type == GoalAction::Complete) {
        if (GoalActionParser::isUnmetComplete(action.text)) {
            if (GoalActionParser::isMaxIterationsUnmet(action.text)
                && m_currentCriterionIndex + 1 < m_criteria.size()) {
                skipToNextCriterionAfterMaxIter();
                return;
            }
            logDebug(QStringLiteral("applyJudgeAction: unmet complete, handing back"));
            destroyJudgeConnection();
            markTerminal(Cancelled, action.text.left(200));
            return;
        }
        advanceToNextCriterion(action.text);
        return;
    }

    if (action.type == GoalAction::Restart) {
        if (m_restartedSinceLastEval) {
            logDebug(QStringLiteral("applyJudgeAction: duplicate restart skipped"));
            return;
        }
        if (action.text.trimmed().isEmpty()) {
            markTerminal(Failed, QStringLiteral("restart_empty_prompt"));
            return;
        }
        if (!m_sessionRestarter) {
            markTerminal(Failed, QStringLiteral("restart_failed"));
            return;
        }

        auto &crit = m_criteria[m_currentCriterionIndex];
        crit.iteration++;
        if (crit.iteration >= m_maxIterations)
            emit iterationChanged(m_currentCriterionIndex, crit.iteration);
        if (consumeIfMaxIterations())
            return;

        restartWatchedSession(action.text);
        if (m_status != Active)
            return;
        // After restarter → dock rebind → view.clearGoalStatus(). Paint the
        // banner only now so it is not wiped.
        emit iterationChanged(m_currentCriterionIndex, crit.iteration);
        return;
    }

    auto &crit = m_criteria[m_currentCriterionIndex];
    crit.iteration++;
    emit iterationChanged(m_currentCriterionIndex, crit.iteration);

    if (consumeIfMaxIterations())
        return;

    if (m_targetConnection) {
        logDebug(QStringLiteral("processJudgeResponse: forwarding continue to target (%1 chars)")
                     .arg(action.text.size()));
        sendPromptToTarget(action.text);
    }
}

bool GoalAgent::consumeIfMaxIterations()
{
    if (m_criteria[m_currentCriterionIndex].iteration < m_maxIterations)
        return false;
    if (m_currentCriterionIndex + 1 >= m_criteria.size()) {
        destroyJudgeConnection();
        markTerminal(Cancelled, QStringLiteral("max_iter"));
        return true;
    }
    skipToNextCriterionAfterMaxIter();
    return true;
}

void GoalAgent::skipToNextCriterionAfterMaxIter()
{
    auto &crit = m_criteria[m_currentCriterionIndex];
    crit.status = Archived;
    crit.verdict = QStringLiteral("max_iter");
    logDebug(QStringLiteral("skipToNextCriterionAfterMaxIter: criterion %1 exhausted")
                 .arg(m_currentCriterionIndex));

    m_currentCriterionIndex++;
    auto &next = m_criteria[m_currentCriterionIndex];
    next.status = CriterionActive;
    next.iteration = 0;
    emit criterionAdvanced(m_currentCriterionIndex);

    destroyJudgeConnection();
    if (GoalHttpJudge::isCustomApiAgent(m_agentId)) {
        ensureHttpJudge();
    } else {
        spawnJudgeForCriterion(m_currentCriterionIndex);
        if (m_status != Active)
            return;
    }

    if (m_targetConnection)
        sendPromptToTarget(next.text);
}

void GoalAgent::setSessionRestarter(std::function<RestartedSession(const QString &oldSessionId)> fn)
{
    m_sessionRestarter = std::move(fn);
}

void GoalAgent::setTargetPromptDecorator(std::function<QString(const QString &)> fn)
{
    m_targetPromptDecorator = std::move(fn);
}

QString GoalAgent::wireTextForTarget(const QString &displayText) const
{
    if (!m_targetPromptDecorator)
        return displayText;
    if (displayText.trimmed() == QLatin1String("/compact"))
        return displayText;
    return m_targetPromptDecorator(displayText);
}

void GoalAgent::sendPromptToTarget(const QString &displayText)
{
    if (!m_targetConnection)
        return;
    const QString sent = m_prefixGoal ? prefixGoalMessage(displayText) : displayText;
    if (m_targetModel)
        m_targetModel->appendUserMessage(sent, {}, /*fromGoalAgent=*/true);
    m_targetConnection->sendPrompt(wireTextForTarget(sent), {});
    m_lastSeenTargetMessageCount = m_targetModel
        ? m_targetModel->messages().size() : 0;
}

void GoalAgent::restartWatchedSession(const QString &prompt)
{
    if (!m_sessionRestarter) {
        logDebug(QStringLiteral("restartWatchedSession: no restarter"));
        markTerminal(Failed, QStringLiteral("restart_failed"));
        return;
    }

    m_restartingTarget = true;
    if (m_targetConnection) {
        disconnect(m_targetConnection, &AcpConnection::promptEnded,
                   this, &GoalAgent::onTargetPromptEnded);
        disconnect(m_targetConnection, &QObject::destroyed,
                   this, &GoalAgent::onTargetDestroyed);
    }

    const QString oldId = m_targetSessionId;
    const RestartedSession restarted = m_sessionRestarter(oldId);
    if (restarted.sessionId.isEmpty() || !restarted.connection) {
        m_restartingTarget = false;
        markTerminal(Failed, QStringLiteral("restart_failed"));
        return;
    }

    m_targetSessionId = restarted.sessionId;
    setTargetSession(restarted.connection, restarted.model);
    m_lastSeenTargetMessageCount = 0;
    m_restartedSinceLastEval = true;

    connect(m_targetConnection, &AcpConnection::promptEnded,
            this, &GoalAgent::onTargetPromptEnded);
    connect(m_targetConnection, &QObject::destroyed,
            this, &GoalAgent::onTargetDestroyed);

    logDebug(QStringLiteral("restartWatchedSession: %1 -> %2, forwarding prompt (%3 chars)")
                 .arg(oldId, restarted.sessionId)
                 .arg(prompt.size()));
    sendPromptToTarget(prompt);
    m_restartingTarget = false;
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

    disconnect(m_judgeConnection, &AcpConnection::promptEnded,
               this, &GoalAgent::onJudgePromptEnded);
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
    beginGoalJudgeTranscript(tr("Goal · authoring next criterion"));
    m_judgeConnection->sendPrompt(authoringPrompt, {});
    armJudgePromptTimeout();
}

void GoalAgent::onAuthoringPromptEnded()
{
    if (!m_awaitingAuthoring)
        return;
    cancelJudgePromptTimeout();
    m_awaitingAuthoring = false;
    finishGoalJudgeTranscript();

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

    // HTTP custom-API judge has no ACP process — do not look it up in the registry.
    destroyJudgeConnection();
    if (GoalHttpJudge::isCustomApiAgent(m_agentId)) {
        ensureHttpJudge();
    } else {
        spawnJudgeForCriterion(m_currentCriterionIndex);
        if (m_status != Active)
            return;
    }

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
        sendPromptToTarget(handoff);
    }
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

void GoalAgent::ensureHttpJudge()
{
    if (m_httpSession)
        return;
    m_httpSession = new GoalHttpJudgeSession(this);
    connect(m_httpSession, &GoalHttpJudgeSession::busyChanged,
            this, &GoalAgent::httpJudgeBusyChanged);
    connect(m_httpSession, &GoalHttpJudgeSession::verdict,
            this, &GoalAgent::onHttpVerdict);
    connect(m_httpSession, &GoalHttpJudgeSession::assumedAchieved,
            this, &GoalAgent::onHttpAssumedAchieved);
    connect(m_httpSession, &GoalHttpJudgeSession::failed,
            this, &GoalAgent::onHttpFailed);
}

void GoalAgent::evaluateViaHttp()
{
    ensureHttpJudge();

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
    const QString prompt = GoalHttpJudge::judgePrompt(
        crit.text,
        conversation,
        crit.iteration + 1,
        m_maxIterations,
        m_currentCriterionIndex + 1,
        m_criteria.size(),
        m_originalUserMessage,
        tpl->content,
        m_developerRequests);

    m_awaitingJudgeResponse = true;
    logDebug(QStringLiteral("evaluateViaHttp: prompt=%1 chars").arg(prompt.size()));
    beginGoalJudgeTranscript(tr("Goal · judging %1/%2")
                                 .arg(m_currentCriterionIndex + 1)
                                 .arg(m_criteria.size()));
    if (m_targetModel)
        m_targetModel->appendGoalJudgeChunk(tr("Goal is judging…"));
    m_httpSession->evaluate(m_appSettings, prompt);
}

void GoalAgent::onHttpVerdict(const GoalAction &action)
{
    if (m_status != Active)
        return;
    m_awaitingJudgeResponse = false;
    if (m_targetModel)
        m_targetModel->replaceGoalJudgeText(action.text);
    applyJudgeAction(action);
}

void GoalAgent::onHttpAssumedAchieved(const QString &reason)
{
    if (m_status != Active)
        return;
    m_awaitingJudgeResponse = false;
    logDebug(QStringLiteral("onHttpAssumedAchieved: %1").arg(reason.left(120)));
    if (m_targetModel)
        m_targetModel->replaceGoalJudgeText(reason);
    GoalAction action;
    action.type = GoalAction::Complete;
    action.text = reason;
    applyJudgeAction(action);
}

void GoalAgent::onHttpFailed(const QString &message)
{
    if (m_status != Active)
        return;
    m_awaitingJudgeResponse = false;
    QString userMessage = message;
    if (message == QLatin1String(GoalHttpJudge::kUnavailableReason)) {
        userMessage = tr("Custom API is temporarily unavailable. Try again in a moment.");
    } else if (message == QLatin1String("custom_api_key_invalid")
               || message == QLatin1String("custom_api_key_missing")) {
        userMessage = tr("Enter an API key for Custom API.");
    } else if (message == QLatin1String("custom_api_not_configured")) {
        userMessage = tr("Configure Custom API (Base URL and model) before generating a prompt.");
    }
    m_lastActionText = userMessage;
    logDebug(QStringLiteral("onHttpFailed: %1").arg(message));
    collapseGoalJudgeTranscript();
    markTerminal(Failed, userMessage);
}
