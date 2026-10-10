/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "AcpAgentDefinition.h"
#include "AcpConnection.h"
#include "AcpSessionModel.h"
#include "ApplicationSettings.h"
#include "GoalActionParser.h"
#include "GoalAgent.h"
#include "GoalAgentSettings.h"
#include "GoalHttpJudge.h"
#include "AcpProtocol.h"
#include "PiAcpJudgeConfig.h"
#include "GoalVerdictMcp.h"
#include "IAcpProcessChannel.h"
#include "AcpPromptQueue.h"


// Records JSON-RPC bytes AcpConnection would send on a live transport.
// start() marks the channel running but does not emit started(), so the
// initialize handshake is skipped and only explicit outbound calls appear.
class RecordingChannel : public IAcpProcessChannel
{
public:
    QByteArray written;

    explicit RecordingChannel(QObject *parent = nullptr)
        : IAcpProcessChannel(parent)
    {
    }

    void start() override { m_running = true; }
    void write(const QByteArray &bytes) override { written.append(bytes); }
    void kill() override { m_running = false; }
    bool isRunning() const override { return m_running; }

    void pushStdout(const QByteArray &chunk) { emit readyReadStdout(chunk); }

    int sessionPromptCount() const
    {
        int n = 0;
        qsizetype from = 0;
        const QByteArray needle("\"method\":\"session/prompt\"");
        while (true) {
            const qsizetype i = written.indexOf(needle, from);
            if (i < 0)
                return n;
            ++n;
            from = i + needle.size();
        }
    }

    bool wroteSessionCancel() const
    {
        return written.contains("session/cancel");
    }

private:
    bool m_running = false;
};

QByteArray rpcResultFrame(int id, const QJsonObject &result = {{QStringLiteral("stopReason"), QStringLiteral("end_turn")}})
{
    QJsonObject obj;
    obj.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("result"), result);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

QByteArray rpcErrorFrame(int id, const QJsonObject &error)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("error"), error);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

QByteArray sessionUpdateFrame(const QString &kind, const QJsonObject &extra = {})
{
    QJsonObject update = extra;
    update.insert(QStringLiteral("sessionUpdate"), kind);
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), QStringLiteral("s1"));
    params.insert(QStringLiteral("update"), update);
    QJsonObject obj;
    obj.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    obj.insert(QStringLiteral("method"), QStringLiteral("session/update"));
    obj.insert(QStringLiteral("params"), params);
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

class TestGoalAgent : public QObject
{
    Q_OBJECT

private slots:
    void stop_doesNotCancelTargetAcpPrompt();
    void restartAction_doesNotCompleteGoal();
    void restartAction_rebindsAndSendsPrompt();
    void restartAction_emitsIterationChangedAfterRestarter();
    void restartAction_restarterFailure_failsGoal();
    void restartAction_whenNotActive_isNoOp();
    void restartAction_duplicateBeforeNextEval_isIgnored();
    void restartAction_oldConnectionDestroyedDuringRestart_staysActive();
    void continueAction_stillForwardsToTarget();
    void continueAction_withPrefixGoal_prefixesMessageToTarget();
    void sendFirstCriterionGoal_withPrefixGoal_sendsImmediatelyWithoutJudge();
    void sendFirstCriterionGoal_withoutPrefixGoal_isNoOp();
    void start_attachWithPrefixGoal_sendsFirstCriterionWithoutJudging();
    void continueAction_atMaxIterations_advancesToNextCriterionAndResetsTurns();
    void continueAction_atMaxIterations_onLastCriterion_cancels();
    void continueAction_appliesTargetPromptDecorator_hidesFromTranscript();
    void restartAction_appliesTargetPromptDecorator_hidesFromTranscript();
    void completeAction_stillAchievesSingleCriterion();
    void completeAction_withAutoCompact_sendsCompactToTarget();
    void completeAction_withAutoCompact_skipsTargetPromptDecorator();
    void completeAction_withoutAutoCompact_doesNotSendCompact();
    void completeUnmetPrefix_stopsWithoutAchieving();
    void completeAction_maxIterationsReached_advancesToNextCriterion();
    void completeAction_maxIterationsReached_onLastCriterion_cancels();
    void builtinPrompt_matchesGoalAgentSpec();
    void parseToolCall_readsStatusAndText();
    void verdictToolCall_appliesContinueWithoutXml();
    void verdictToolCall_cancelsLeftoverJudgePrompt();
    void verdictToolCall_completeCancelsBeforeAuthoringPrompt();
    void verdictToolCallUpdate_appliesWhenNameOnlyOnCreate();
    void verdictMcp_listsSubmitTool();
    void verdictMcp_callResult_tellsJudgeToStop();
    void verdictMcp_newlineDiscoverAndInitialize();
    void verdictMcp_sessionNewIncludesEnvArray();
    void piAcp_isDetectedFromCommandOrNpxArg();
    void piAcp_sourceAgentDir_prefersAcpEnvOverHome();
    void piAcp_stageJudgeDir_mergesVerdictAndSkipsSessions();
    void piAcp_stageJudgeDir_restoresLastSessionModel();
    void stop_withAutoCompact_doesNotSendCompact();
    void start_attachToExistingConversation_whenIdle_evaluatesImmediately();
    void start_attachToExistingConversation_whenProcessing_waitsForPromptEnded();
    void start_attachToExistingConversation_usesLastUserMessageAsOriginal();
    void start_attach_ignoresRequestsBeforeGoalAchieved();
    void start_snapshotsDeveloperRequestsBeforeLaterUserMessage();
    void launchAction_emptyIdleNoHistory_needsComposer();
    void launchAction_emptyWithHistory_attaches();
    void launchAction_emptyWhileProcessing_attaches();
    void launchAction_composerIdle_sends();
    void launchAction_composerWhileProcessing_attaches();
    void nativeGoalCommand_prefixesCriterion();
    void nativeGoalCommands_oneSlashPerRow();
    void nativeGoalWireText_appendsWorktreeInstruction();
    void nativeGoalCommand_skipsBlankRows();
    void isNativeGoalSlash_matchesGoalCommandOnly();
    void prefixGoalMessage_prefixesUnlessAlreadyGoal();
    void nativeGoalUsesSidePrompt_onlyWhenAgentAdvertisesGoal();
    void sendFirstCriterionGoal_turnInFlight_withoutAdvertisedGoal_queuesInsteadOfSending();
    void deferredGoal_turnEnd_doesNotJudgeBeforeDispatch();
    void mustQueueGoalFollowUp_onlyWhenBusyAndAgentOmitsGoal();
    void sendFirstCriterionGoal_turnInFlight_advertisedGoal_sidePrompts();
    void deferredGoal_afterDispatch_turnEndJudges();

    void sidePrompt_whileTurnInFlight_doesNotEndTurn();
    void sidePrompts_twoGoalsWhileTurnInFlight_bothGoOutImmediately();
    void userPromptResult_whileGoalOutstanding_doesNotEndTurn();
    void promptEnded_waitsIdleAfterRpcResult();
    void promptEnded_chunkAfterRpcKeepsTurnOpen();
    void promptEnded_availableCommandsDoesNotKeepTurnOpen();
    void streamingChunk_whileIdle_startsTurn();
    void sessionBusy_doesNotFail_retriesPrompt();
    void sessionBusy_followUpAfterRpc_doesNotFireUntilIdle();

    void judgeConnection_permissionRequest_isAutoApproved();
    void judgePrompt_neverEnds_failsClosed();
    void judgeMessageChunk_mirrorsOntoTargetModel();
    void actionDisplayText_stripsActionTags();
    void continueAction_dropsJudgeBubblesKeepsResult();
};

void TestGoalAgent::stop_doesNotCancelTargetAcpPrompt()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));
    QCOMPARE(goal.status(), GoalAgent::Active);

    goal.stop();

    QCOMPARE(goal.status(), GoalAgent::Cancelled);
    QVERIFY(!channel->wroteSessionCancel());
}

void TestGoalAgent::restartAction_doesNotCompleteGoal()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));
    QCOMPARE(goal.status(), GoalAgent::Active);

    QSignalSpy spy(&goal, &GoalAgent::actionEmitted);
    GoalAction action;
    action.type = GoalAction::Restart;
    action.text = QStringLiteral("Continue from the last failing test.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Failed);
    QCOMPARE(goal.criteria().at(0).iteration, 0);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("restart"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Continue from the last failing test."));
}

void TestGoalAgent::restartAction_rebindsAndSendsPrompt()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection oldTarget;
    auto *oldChannel = new RecordingChannel(&oldTarget);
    oldTarget.attachChannelForTest(oldChannel);
    oldChannel->start();

    AcpConnection newTarget;
    auto *newChannel = new RecordingChannel(&newTarget);
    newTarget.attachChannelForTest(newChannel);
    newChannel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel oldModel(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    AcpSessionModel newModel(QStringLiteral("s2"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&oldTarget, &oldModel);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    int restarterCalls = 0;
    QString capturedOld;
    goal.setSessionRestarter([&](const QString &oldId) {
        capturedOld = oldId;
        ++restarterCalls;
        GoalAgent::RestartedSession out;
        out.sessionId = QStringLiteral("s2");
        out.connection = &newTarget;
        out.model = &newModel;
        return out;
    });

    GoalAction action;
    action.type = GoalAction::Restart;
    action.text = QStringLiteral("Continue from the last failing test.");
    goal.applyJudgeAction(action);

    QCOMPARE(restarterCalls, 1);
    QCOMPARE(capturedOld, QStringLiteral("s1"));
    QCOMPARE(goal.targetSessionId(), QStringLiteral("s2"));
    QCOMPARE(goal.status(), GoalAgent::Active);
    QCOMPARE(newModel.messages().size(), 1);
    QCOMPARE(newModel.messages().at(0).role, QStringLiteral("user"));
    QCOMPARE(newModel.messages().at(0).content.first().text,
             QStringLiteral("Continue from the last failing test."));
    QVERIFY(!oldChannel->wroteSessionCancel());
}

void TestGoalAgent::restartAction_emitsIterationChangedAfterRestarter()
{
    // View rebind (inside restartSession) calls clearGoalStatus(). The dock
    // paints the banner on iterationChanged — that signal must fire AFTER
    // restarter/rebind or the banner stays gone.
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection oldTarget;
    auto *oldChannel = new RecordingChannel(&oldTarget);
    oldTarget.attachChannelForTest(oldChannel);
    oldChannel->start();

    AcpConnection newTarget;
    auto *newChannel = new RecordingChannel(&newTarget);
    newTarget.attachChannelForTest(newChannel);
    newChannel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel oldModel(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    AcpSessionModel newModel(QStringLiteral("s2"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&oldTarget, &oldModel);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    QStringList order;
    QObject::connect(&goal, &GoalAgent::iterationChanged, [&](int, int) {
        order.append(QStringLiteral("iter"));
    });
    goal.setSessionRestarter([&](const QString &) {
        order.append(QStringLiteral("rebind"));
        GoalAgent::RestartedSession out;
        out.sessionId = QStringLiteral("s2");
        out.connection = &newTarget;
        out.model = &newModel;
        return out;
    });

    GoalAction action;
    action.type = GoalAction::Restart;
    action.text = QStringLiteral("Continue from the last failing test.");
    goal.applyJudgeAction(action);

    QCOMPARE(order, (QStringList{QStringLiteral("rebind"), QStringLiteral("iter")}));
}

void TestGoalAgent::restartAction_restarterFailure_failsGoal()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    goal.setSessionRestarter([](const QString &) {
        return GoalAgent::RestartedSession{};
    });

    GoalAction action;
    action.type = GoalAction::Restart;
    action.text = QStringLiteral("Resume after the crash.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Failed);
    QVERIFY(goal.status() != GoalAgent::Achieved);
}

void TestGoalAgent::restartAction_whenNotActive_isNoOp()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));
    goal.stop();
    QCOMPARE(goal.status(), GoalAgent::Cancelled);

    int restarterCalls = 0;
    goal.setSessionRestarter([&](const QString &) {
        ++restarterCalls;
        return GoalAgent::RestartedSession{};
    });

    GoalAction action;
    action.type = GoalAction::Restart;
    action.text = QStringLiteral("Should not run.");
    goal.applyJudgeAction(action);

    QCOMPARE(restarterCalls, 0);
    QCOMPARE(goal.status(), GoalAgent::Cancelled);
}

void TestGoalAgent::restartAction_duplicateBeforeNextEval_isIgnored()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection oldTarget;
    auto *oldChannel = new RecordingChannel(&oldTarget);
    oldTarget.attachChannelForTest(oldChannel);
    oldChannel->start();

    AcpConnection newTarget;
    auto *newChannel = new RecordingChannel(&newTarget);
    newTarget.attachChannelForTest(newChannel);
    newChannel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel oldModel(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    AcpSessionModel newModel(QStringLiteral("s2"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&oldTarget, &oldModel);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    int restarterCalls = 0;
    goal.setSessionRestarter([&](const QString &) {
        ++restarterCalls;
        GoalAgent::RestartedSession out;
        out.sessionId = QStringLiteral("s2");
        out.connection = &newTarget;
        out.model = &newModel;
        return out;
    });

    GoalAction action;
    action.type = GoalAction::Restart;
    action.text = QStringLiteral("Continue from the last failing test.");
    goal.applyJudgeAction(action);
    goal.applyJudgeAction(action);

    QCOMPARE(restarterCalls, 1);
    QCOMPARE(goal.criteria().at(0).iteration, 1);
    QCOMPARE(goal.status(), GoalAgent::Active);
    QCOMPARE(goal.targetSessionId(), QStringLiteral("s2"));
}

void TestGoalAgent::restartAction_oldConnectionDestroyedDuringRestart_staysActive()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    auto *oldTarget = new AcpConnection;
    auto *oldChannel = new RecordingChannel(oldTarget);
    oldTarget->attachChannelForTest(oldChannel);
    oldChannel->start();

    AcpConnection newTarget;
    auto *newChannel = new RecordingChannel(&newTarget);
    newTarget.attachChannelForTest(newChannel);
    newChannel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel oldModel(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    AcpSessionModel newModel(QStringLiteral("s2"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(oldTarget, &oldModel);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    goal.setSessionRestarter([&](const QString &) {
        delete oldTarget;
        oldTarget = nullptr;
        GoalAgent::RestartedSession out;
        out.sessionId = QStringLiteral("s2");
        out.connection = &newTarget;
        out.model = &newModel;
        return out;
    });

    GoalAction action;
    action.type = GoalAction::Restart;
    action.text = QStringLiteral("Continue from the last failing test.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Active);
    QCOMPARE(goal.targetSessionId(), QStringLiteral("s2"));
}

void TestGoalAgent::continueAction_stillForwardsToTarget()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    GoalAction action;
    action.type = GoalAction::Continue;
    action.text = QStringLiteral("Please run the tests.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Active);
    QCOMPARE(goal.criteria().at(0).iteration, 1);
    QCOMPARE(model.messages().last().role, QStringLiteral("user"));
}

void TestGoalAgent::continueAction_withPrefixGoal_prefixesMessageToTarget()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.prefixGoal = true;
    QVERIFY(goal.start(req));

    GoalAction action;
    action.type = GoalAction::Continue;
    action.text = QStringLiteral("Please run the tests.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Active);
    QCOMPARE(model.messages().last().fromGoalAgent, true);
    QCOMPARE(model.messages().last().content.first().text,
             QStringLiteral("/goal Please run the tests."));
}

void TestGoalAgent::sendFirstCriterionGoal_withPrefixGoal_sendsImmediatelyWithoutJudge()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("hi in Vietnamese"),
                                          QStringLiteral("hi in Japanese")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.prefixGoal = true;
    QVERIFY(goal.start(req));
    QCOMPARE(model.messages().size(), 0);

    target.sendPrompt(QStringLiteral("do the work"), {});
    goal.sendFirstCriterionGoal();

    QCOMPARE(model.messages().size(), 1);
    QCOMPARE(model.messages().last().fromGoalAgent, true);
    QCOMPARE(model.messages().last().content.first().text,
             QStringLiteral("/goal hi in Vietnamese"));
    QVERIFY(channel->written.contains("\"/goal hi in Vietnamese\""));
    QCOMPARE(goal.currentCriterionIndex(), 0);
    QVERIFY(!channel->written.contains("\"/goal hi in Japanese\""));
}

void TestGoalAgent::sendFirstCriterionGoal_turnInFlight_withoutAdvertisedGoal_queuesInsteadOfSending()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    connect(&target, &AcpConnection::promptStarted,
            &model, &AcpSessionModel::onPromptStarted);
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("hi in Vietnamese"),
                                          QStringLiteral("hi in Japanese")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.prefixGoal = true;
    QVERIFY(goal.start(req));

    QSignalSpy queued(&goal, &GoalAgent::goalFollowUpQueued);
    target.sendPrompt(QStringLiteral("do the work"), {});
    QVERIFY(model.isProcessing());
    QVERIFY(!model.agentAdvertisesGoalCommand());
    const int messagesBefore = model.messages().size();

    goal.sendFirstCriterionGoal();

    QCOMPARE(queued.count(), 1);
    QCOMPARE(queued.at(0).at(0).toString(), QStringLiteral("/goal hi in Vietnamese"));
    QVERIFY(!channel->written.contains("\"/goal hi in Vietnamese\""));
    QCOMPARE(model.messages().size(), messagesBefore);
    QVERIFY(!channel->written.contains("\"/goal hi in Japanese\""));
}

void TestGoalAgent::deferredGoal_turnEnd_doesNotJudgeBeforeDispatch()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    model.appendUserMessage(QStringLiteral("do the work"), {});
    model.onPromptStarted();
    goal.setTargetSession(&target, &model);

    QSignalSpy logs(&goal, &GoalAgent::debugLogEntry);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("hi in Vietnamese")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.prefixGoal = true;
    req.attachToExistingConversation = true;
    QVERIFY(goal.start(req));
    QVERIFY(model.isProcessing());
    QVERIFY(!channel->written.contains("\"/goal hi in Vietnamese\""));

    goal.onTargetPromptEnded();

    bool evaluated = false;
    for (const auto &row : logs) {
        const QString line = row.at(0).toString();
        if (line.contains(QLatin1String("evaluating criterion"))
            || line.contains(QLatin1String("evaluateViaHttp"))
            || line.contains(QLatin1String("evaluateCurrentCriterion")))
            evaluated = true;
    }
    QVERIFY(!evaluated);
}

void TestGoalAgent::mustQueueGoalFollowUp_onlyWhenBusyAndAgentOmitsGoal()
{
    QVERIFY(GoalAgent::mustQueueGoalFollowUp(true, false, QStringLiteral("/goal x")));
    QVERIFY(!GoalAgent::mustQueueGoalFollowUp(true, true, QStringLiteral("/goal x")));
    QVERIFY(!GoalAgent::mustQueueGoalFollowUp(false, false, QStringLiteral("/goal x")));
    QVERIFY(!GoalAgent::mustQueueGoalFollowUp(true, false, QString()));
    QVERIFY(GoalAgent::mustQueueGoalFollowUp(true, true, QStringLiteral("hello")));
}

void TestGoalAgent::sendFirstCriterionGoal_turnInFlight_advertisedGoal_sidePrompts()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    AcpProtocol::AcpCommandInfo agentGoal;
    agentGoal.name = QStringLiteral("/goal");
    model.onAvailableCommandsUpdated({agentGoal});
    QVERIFY(model.agentAdvertisesGoalCommand());
    connect(&target, &AcpConnection::promptStarted,
            &model, &AcpSessionModel::onPromptStarted);
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("hi in Vietnamese")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.prefixGoal = true;
    QVERIFY(goal.start(req));

    QSignalSpy queued(&goal, &GoalAgent::goalFollowUpQueued);
    target.sendPrompt(QStringLiteral("do the work"), {});
    QVERIFY(model.isProcessing());
    goal.sendFirstCriterionGoal();

    QCOMPARE(queued.count(), 0);
    QVERIFY(channel->written.contains("\"/goal hi in Vietnamese\""));
    QCOMPARE(model.messages().last().fromGoalAgent, true);
    QCOMPARE(model.messages().last().content.first().text,
             QStringLiteral("/goal hi in Vietnamese"));
}

void TestGoalAgent::deferredGoal_afterDispatch_turnEndJudges()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    model.appendUserMessage(QStringLiteral("do the work"), {});
    model.onPromptStarted();
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("hi in Vietnamese")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.prefixGoal = true;
    req.attachToExistingConversation = true;
    QVERIFY(goal.start(req));

    goal.noteDeferredGoalDispatched();
    QSignalSpy logs(&goal, &GoalAgent::debugLogEntry);
    goal.onTargetPromptEnded();

    bool evaluated = false;
    for (const auto &row : logs) {
        if (row.at(0).toString().contains(QLatin1String("evaluating criterion")))
            evaluated = true;
    }
    QVERIFY(evaluated);
}

void TestGoalAgent::sendFirstCriterionGoal_withoutPrefixGoal_isNoOp()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("hi in Vietnamese")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.prefixGoal = false;
    QVERIFY(goal.start(req));

    target.sendPrompt(QStringLiteral("do the work"), {});
    goal.sendFirstCriterionGoal();

    QCOMPARE(model.messages().size(), 0);
    QVERIFY(!channel->written.contains("/goal"));
}

void TestGoalAgent::start_attachWithPrefixGoal_sendsFirstCriterionWithoutJudging()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    model.appendUserMessage(QStringLiteral("do the work"), {});
    goal.setTargetSession(&target, &model);

    QSignalSpy logs(&goal, &GoalAgent::debugLogEntry);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("hi in Vietnamese"),
                                          QStringLiteral("hi in Japanese")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.prefixGoal = true;
    req.attachToExistingConversation = true;
    QVERIFY(goal.start(req));

    QCOMPARE(model.messages().last().fromGoalAgent, true);
    QCOMPARE(model.messages().last().content.first().text,
             QStringLiteral("/goal hi in Vietnamese"));
    QVERIFY(channel->written.contains("\"/goal hi in Vietnamese\""));
    QVERIFY(!channel->written.contains("\"/goal hi in Japanese\""));

    bool evaluated = false;
    for (const auto &row : logs) {
        if (row.at(0).toString().contains(QLatin1String("evaluateViaHttp")))
            evaluated = true;
    }
    QVERIFY(!evaluated);
}

void TestGoalAgent::continueAction_atMaxIterations_advancesToNextCriterionAndResetsTurns()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("first"), QStringLiteral("second")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.maxIterations = 1;
    QVERIFY(goal.start(req));

    GoalAction action;
    action.type = GoalAction::Continue;
    action.text = QStringLiteral("keep going");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Active);
    QCOMPARE(goal.currentCriterionIndex(), 1);
    QCOMPARE(goal.criteria().at(0).status, GoalAgent::Archived);
    QCOMPARE(goal.criteria().at(0).iteration, 1);
    QCOMPARE(goal.criteria().at(1).status, GoalAgent::CriterionActive);
    QCOMPARE(goal.criteria().at(1).iteration, 0);
    QCOMPARE(model.messages().size(), 1);
    QCOMPARE(model.messages().last().fromGoalAgent, true);
    QCOMPARE(model.messages().last().content.first().text, QStringLiteral("second"));
}

void TestGoalAgent::continueAction_atMaxIterations_onLastCriterion_cancels()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("only")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.maxIterations = 1;
    QVERIFY(goal.start(req));

    GoalAction action;
    action.type = GoalAction::Continue;
    action.text = QStringLiteral("keep going");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Cancelled);
    QCOMPARE(goal.currentCriterionIndex(), 0);
    QCOMPARE(goal.criteria().at(0).status, GoalAgent::CriterionActive);
    QCOMPARE(goal.criteria().at(0).iteration, 1);
    QCOMPARE(model.messages().size(), 0);
}

void TestGoalAgent::continueAction_appliesTargetPromptDecorator_hidesFromTranscript()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    int decorateCalls = 0;
    goal.setTargetPromptDecorator([&](const QString &text) {
        ++decorateCalls;
        return text + QLatin1String("\n\nWORKTREE");
    });

    GoalAction action;
    action.type = GoalAction::Continue;
    action.text = QStringLiteral("Please run the tests.");
    goal.applyJudgeAction(action);

    QCOMPARE(decorateCalls, 1);
    QCOMPARE(model.messages().last().content.first().text,
             QStringLiteral("Please run the tests."));
    QCOMPARE(goal.wireTextForTarget(action.text),
             QStringLiteral("Please run the tests.\n\nWORKTREE"));
}

void TestGoalAgent::restartAction_appliesTargetPromptDecorator_hidesFromTranscript()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection oldTarget;
    auto *oldChannel = new RecordingChannel(&oldTarget);
    oldTarget.attachChannelForTest(oldChannel);
    oldChannel->start();

    AcpConnection newTarget;
    auto *newChannel = new RecordingChannel(&newTarget);
    newTarget.attachChannelForTest(newChannel);
    newChannel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel oldModel(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    AcpSessionModel newModel(QStringLiteral("s2"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&oldTarget, &oldModel);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    goal.setSessionRestarter([&](const QString &) {
        GoalAgent::RestartedSession out;
        out.sessionId = QStringLiteral("s2");
        out.connection = &newTarget;
        out.model = &newModel;
        return out;
    });

    int decorateCalls = 0;
    goal.setTargetPromptDecorator([&](const QString &text) {
        ++decorateCalls;
        return text + QLatin1String("\n\nWORKTREE");
    });

    GoalAction action;
    action.type = GoalAction::Restart;
    action.text = QStringLiteral("Continue from the last failing test.");
    goal.applyJudgeAction(action);

    QCOMPARE(decorateCalls, 1);
    QCOMPARE(newModel.messages().at(0).content.first().text,
             QStringLiteral("Continue from the last failing test."));
}

void TestGoalAgent::completeAction_stillAchievesSingleCriterion()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    GoalAction action;
    action.type = GoalAction::Complete;
    action.text = QStringLiteral("Tests passed.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Achieved);
    QCOMPARE(model.messages().size(), 0);
}

void TestGoalAgent::completeAction_withAutoCompact_sendsCompactToTarget()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.autoCompact = true;
    QVERIFY(goal.start(req));

    GoalAction action;
    action.type = GoalAction::Complete;
    action.text = QStringLiteral("Tests passed.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Achieved);
    QCOMPARE(model.messages().size(), 1);
    QCOMPARE(model.messages().last().role, QStringLiteral("user"));
    QCOMPARE(model.messages().last().fromGoalAgent, true);
    QCOMPARE(model.messages().last().content.first().text, QStringLiteral("/compact"));
}

void TestGoalAgent::completeAction_withAutoCompact_skipsTargetPromptDecorator()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    int decorateCalls = 0;
    goal.setTargetPromptDecorator([&](const QString &text) {
        ++decorateCalls;
        return text + QLatin1String("\n\nWORKTREE");
    });

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.autoCompact = true;
    QVERIFY(goal.start(req));

    GoalAction action;
    action.type = GoalAction::Complete;
    action.text = QStringLiteral("Tests passed.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Achieved);
    QCOMPARE(decorateCalls, 0);
    QCOMPARE(model.messages().last().content.first().text, QStringLiteral("/compact"));
    QCOMPARE(goal.wireTextForTarget(QStringLiteral("/compact")),
             QStringLiteral("/compact"));
}

void TestGoalAgent::completeAction_withoutAutoCompact_doesNotSendCompact()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.autoCompact = false;
    QVERIFY(goal.start(req));

    GoalAction action;
    action.type = GoalAction::Complete;
    action.text = QStringLiteral("Tests passed.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Achieved);
    QCOMPARE(model.messages().size(), 0);
}

void TestGoalAgent::completeUnmetPrefix_stopsWithoutAchieving()
{
    const QStringList reasons = {
        QStringLiteral("need human-in-the-loop: merge the pull request"),
        QStringLiteral("Need human-in-the-loop: sign the release"),
    };
    for (const QString &reason : reasons) {
        ApplicationSettings settings;
        GoalAgent goal(nullptr, &settings);

        AcpConnection target;
        auto *channel = new RecordingChannel(&target);
        target.attachChannelForTest(channel);
        channel->start();

        QTemporaryDir historyDir;
        QVERIFY(historyDir.isValid());
        AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
        goal.setTargetSession(&target, &model);

        GoalAgent::StartRequest req;
        req.targetSessionId = QStringLiteral("s1");
        req.successCriteriaList = QStringList{QStringLiteral("first"), QStringLiteral("second")};
        req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
        req.autoCompact = true;
        QVERIFY(goal.start(req));

        GoalAction action;
        action.type = GoalAction::Complete;
        action.text = reason;
        goal.applyJudgeAction(action);

        QCOMPARE(goal.status(), GoalAgent::Cancelled);
        QCOMPARE(goal.currentCriterionIndex(), 0);
        QCOMPARE(goal.criteria().at(0).status, GoalAgent::CriterionActive);
        QCOMPARE(goal.criteria().at(1).status, GoalAgent::Pending);
        QVERIFY(goal.criteria().at(0).verdict.isEmpty());
        QCOMPARE(model.messages().size(), 0);
        QVERIFY(GoalActionParser::isUnmetComplete(reason));
    }

    QVERIFY(!GoalActionParser::isUnmetComplete(QStringLiteral("Tests passed.")));
    QVERIFY(!GoalActionParser::isUnmetComplete(
        QStringLiteral("Done. This is not a need human-in-the-loop case.")));
}

void TestGoalAgent::completeAction_maxIterationsReached_advancesToNextCriterion()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{
        QStringLiteral("say hi in Chinese"),
        QStringLiteral("say hi in Japanese"),
        QStringLiteral("say hi in Korean"),
    };
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.maxIterations = 1;
    QVERIFY(goal.start(req));

    GoalAction action;
    action.type = GoalAction::Complete;
    action.text = QStringLiteral(
        "max iterations reached: the assistant replied \"Hi\" in English and did not "
        "say hi in Chinese, so the success criterion is still unmet.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Active);
    QCOMPARE(goal.currentCriterionIndex(), 1);
    QCOMPARE(goal.criteria().at(0).status, GoalAgent::Archived);
    QCOMPARE(goal.criteria().at(1).status, GoalAgent::CriterionActive);
    QCOMPARE(goal.criteria().at(1).iteration, 0);
    QCOMPARE(goal.criteria().at(2).status, GoalAgent::Pending);
    QCOMPARE(model.messages().size(), 1);
    QCOMPARE(model.messages().last().fromGoalAgent, true);
    QCOMPARE(model.messages().last().content.first().text, QStringLiteral("say hi in Japanese"));
}

void TestGoalAgent::completeAction_maxIterationsReached_onLastCriterion_cancels()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("say hi in Chinese")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.maxIterations = 1;
    QVERIFY(goal.start(req));

    GoalAction action;
    action.type = GoalAction::Complete;
    action.text = QStringLiteral("max iterations reached: still unmet.");
    goal.applyJudgeAction(action);

    QCOMPARE(goal.status(), GoalAgent::Cancelled);
    QCOMPARE(goal.currentCriterionIndex(), 0);
    QCOMPARE(model.messages().size(), 0);
}

void TestGoalAgent::builtinPrompt_matchesGoalAgentSpec()
{
    const QString &prompt = GoalAgentSettings::builtinPromptContent();
    QFile file(QStringLiteral(GOAL_AGENT_PROMPT_FILE));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(prompt.toUtf8(), file.readAll());
    QVERIFY(prompt.contains(QStringLiteral("{{developerRequests}}")));
    QVERIFY(prompt.contains(QStringLiteral("<original-message>")));
    QVERIFY(prompt.contains(QStringLiteral("{{criterionIndex}}")));
    QVERIFY(prompt.contains(QStringLiteral("{{totalCriteria}}")));
    QVERIFY(prompt.contains(QStringLiteral("{{goal}}")));
    QVERIFY(prompt.contains(QStringLiteral("{{iteration}}")));
    QVERIFY(prompt.contains(QStringLiteral("{{maxIterations}}")));
    QVERIFY(prompt.contains(QStringLiteral("{{conversation}}")));
    QVERIFY(prompt.contains(QStringLiteral("submit_goal_verdict")));
    QVERIFY(!prompt.contains(QStringLiteral("<action type=")));
    QCOMPARE(GoalAgentSettings().defaultTemplate().content, prompt);
}

void TestGoalAgent::parseToolCall_readsStatusAndText()
{
    GoalAction action;
    QJsonObject input;
    input.insert(QStringLiteral("status"), QStringLiteral("continue"));
    input.insert(QStringLiteral("text"), QStringLiteral("do the thing"));
    QVERIFY(GoalActionParser::parseToolCall(
        QStringLiteral("mcp__goal-verdict__submit_goal_verdict"), {}, input, &action));
    QCOMPARE(action.type, GoalAction::Continue);
    QCOMPARE(action.text, QStringLiteral("do the thing"));

    input.insert(QStringLiteral("status"), QStringLiteral("complete"));
    QVERIFY(GoalActionParser::parseToolCall(
        QStringLiteral("submit_goal_verdict"), QStringLiteral("Submit"), input, &action));
    QCOMPARE(action.type, GoalAction::Complete);

    QVERIFY(!GoalActionParser::parseToolCall(QStringLiteral("Read"), QStringLiteral("Read"), input, &action));
}

void TestGoalAgent::verdictToolCall_appliesContinueWithoutXml()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *targetChannel = new RecordingChannel(&target);
    target.attachChannelForTest(targetChannel);
    targetChannel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    goal.m_awaitingJudgeResponse = true;
    goal.m_judgeVerdictApplied = false;
    QJsonObject input;
    input.insert(QStringLiteral("status"), QStringLiteral("continue"));
    input.insert(QStringLiteral("text"), QStringLiteral("Please run the tests."));
    goal.considerJudgeVerdict(QStringLiteral("submit_goal_verdict"), {}, input);

    int judgeRows = 0;
    for (const auto &m : model.messages()) {
        if (m.fromGoalJudge || m.marker == QLatin1String(kAcpMarkerGoalJudging))
            ++judgeRows;
    }
    QCOMPARE(judgeRows, 0);
    QCOMPARE(model.messages().last().role, QStringLiteral("user"));
    QVERIFY(model.messages().last().fromGoalAgent);
    QCOMPARE(model.messages().last().content.first().text, QStringLiteral("Please run the tests."));
}

void TestGoalAgent::verdictToolCall_cancelsLeftoverJudgePrompt()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *targetChannel = new RecordingChannel(&target);
    target.attachChannelForTest(targetChannel);
    targetChannel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    auto *judge = new AcpConnection(&goal);
    auto *judgeChannel = new RecordingChannel(judge);
    judge->attachChannelForTest(judgeChannel);
    judgeChannel->start();
    judge->setSessionIdForTest(QStringLiteral("j1"));
    goal.configureHeadlessJudge(judge);
    goal.m_judgeConnection = judge;
    goal.m_awaitingJudgeResponse = true;
    goal.m_judgeVerdictApplied = false;

    QJsonObject input;
    input.insert(QStringLiteral("status"), QStringLiteral("continue"));
    input.insert(QStringLiteral("text"), QStringLiteral("Please run the tests."));
    goal.considerJudgeVerdict(QStringLiteral("submit_goal_verdict"), {}, input);

    QVERIFY(judgeChannel->wroteSessionCancel());
    QVERIFY(targetChannel->written.contains("\"method\":\"session/prompt\""));
    QCOMPARE(judgeChannel->sessionPromptCount(), 0);
}

void TestGoalAgent::verdictToolCall_completeCancelsBeforeAuthoringPrompt()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *targetChannel = new RecordingChannel(&target);
    target.attachChannelForTest(targetChannel);
    targetChannel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("first"), QStringLiteral("second")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    auto *judge = new AcpConnection(&goal);
    auto *judgeChannel = new RecordingChannel(judge);
    judge->attachChannelForTest(judgeChannel);
    judgeChannel->start();
    judge->setSessionIdForTest(QStringLiteral("j1"));
    goal.configureHeadlessJudge(judge);
    goal.m_judgeConnection = judge;
    goal.m_awaitingJudgeResponse = true;
    goal.m_judgeVerdictApplied = false;

    QJsonObject input;
    input.insert(QStringLiteral("status"), QStringLiteral("complete"));
    input.insert(QStringLiteral("text"), QStringLiteral("first is met"));
    goal.considerJudgeVerdict(QStringLiteral("submit_goal_verdict"), {}, input);

    QVERIFY(judgeChannel->wroteSessionCancel());
    QCOMPARE(judgeChannel->sessionPromptCount(), 1);
    const qsizetype cancelAt = judgeChannel->written.indexOf("session/cancel");
    const qsizetype promptAt = judgeChannel->written.indexOf("\"method\":\"session/prompt\"");
    QVERIFY(cancelAt >= 0);
    QVERIFY(promptAt > cancelAt);
}

void TestGoalAgent::verdictToolCallUpdate_appliesWhenNameOnlyOnCreate()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *targetChannel = new RecordingChannel(&target);
    target.attachChannelForTest(targetChannel);
    targetChannel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    goal.m_awaitingJudgeResponse = true;
    goal.m_judgeVerdictApplied = false;

    AcpProtocol::AcpToolCall created;
    created.id = QStringLiteral("call-1");
    created.name = QStringLiteral("mcp__goal-verdict__submit_goal_verdict");
    goal.handleJudgeToolCall(created);
    QVERIFY(!goal.m_judgeVerdictApplied);

    AcpProtocol::AcpToolCallUpdate update;
    update.id = QStringLiteral("call-1");
    update.rawInput = QJsonObject{
        {QStringLiteral("status"), QStringLiteral("continue")},
        {QStringLiteral("text"), QStringLiteral("Please run the tests.")},
    };
    goal.handleJudgeToolCallUpdate(update);

    QVERIFY(goal.m_judgeVerdictApplied);
    QCOMPARE(model.messages().last().role, QStringLiteral("user"));
    QVERIFY(model.messages().last().fromGoalAgent);
    QCOMPARE(model.messages().last().content.first().text, QStringLiteral("Please run the tests."));
}

void TestGoalAgent::verdictMcp_listsSubmitTool()
{
    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), 2);
    req.insert(QStringLiteral("method"), QStringLiteral("tools/list"));
    const QJsonObject reply = GoalVerdictMcp::handle(req);
    const QJsonArray tools = reply.value(QStringLiteral("result")).toObject()
                                 .value(QStringLiteral("tools")).toArray();
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("submit_goal_verdict"));
    QVERIFY(tools.at(0).toObject().value(QStringLiteral("description")).toString()
                .contains(QStringLiteral("stop"), Qt::CaseInsensitive));
}

void TestGoalAgent::verdictMcp_callResult_tellsJudgeToStop()
{
    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), 3);
    req.insert(QStringLiteral("method"), QStringLiteral("tools/call"));
    QJsonObject args;
    args.insert(QStringLiteral("status"), QStringLiteral("continue"));
    args.insert(QStringLiteral("text"), QStringLiteral("Say hi in Japanese."));
    QJsonObject params;
    params.insert(QStringLiteral("name"), QStringLiteral("submit_goal_verdict"));
    params.insert(QStringLiteral("arguments"), args);
    req.insert(QStringLiteral("params"), params);
    const QJsonObject reply = GoalVerdictMcp::handle(req);
    const QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), false);
    const QString text = result.value(QStringLiteral("content")).toArray()
                             .at(0).toObject().value(QStringLiteral("text")).toString();
    QCOMPARE(text, QString::fromLatin1(GoalVerdictMcp::kStopGuide));
    QVERIFY(text.contains(QStringLiteral("Stop immediately")));
    QVERIFY(text.contains(QStringLiteral("do not carry out the follow-up yourself")));
}

void TestGoalAgent::verdictMcp_newlineDiscoverAndInitialize()
{
    const QByteArray discover = QByteArrayLiteral(
        R"({"jsonrpc":"2.0","id":"server-discover-probe-1","method":"server/discover","params":{}})");
    const QByteArray discReply = GoalVerdictMcp::encodeReply(discover, true);
    QVERIFY(discReply.endsWith('\n'));
    QVERIFY(!discReply.startsWith("Content-Length"));
    const QJsonObject disc = QJsonDocument::fromJson(discReply).object();
    QCOMPARE(disc.value(QStringLiteral("id")).toString(),
             QStringLiteral("server-discover-probe-1"));
    QCOMPARE(disc.value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("resultType")).toString(),
             QStringLiteral("complete"));

    const QByteArray init = QByteArrayLiteral(
        R"({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-11-25"}})");
    const QByteArray initReply = GoalVerdictMcp::encodeReply(init, true);
    const QJsonObject ir = QJsonDocument::fromJson(initReply).object();
    QCOMPARE(ir.value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("protocolVersion")).toString(),
             QStringLiteral("2025-11-25"));

    const QByteArray framed = GoalVerdictMcp::encodeReply(init, false);
    QVERIFY(framed.startsWith("Content-Length:"));
}

void TestGoalAgent::verdictMcp_sessionNewIncludesEnvArray()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);
    AcpConnection conn;
    goal.attachVerdictMcp(&conn);
    const QJsonArray servers = conn.mcpServersForTest();
    QCOMPARE(servers.size(), 1);
    const QJsonObject server = servers.at(0).toObject();
    QCOMPARE(server.value(QStringLiteral("name")).toString(),
             QStringLiteral("goal-verdict"));
    QVERIFY(server.contains(QStringLiteral("env")));
    QCOMPARE(server.value(QStringLiteral("env")).toArray().size(), 0);
    QVERIFY(!server.contains(QStringLiteral("type")));
    QVERIFY(server.value(QStringLiteral("args")).toArray()
                .contains(QJsonValue(QStringLiteral("--goal-verdict-mcp"))));
}

void TestGoalAgent::piAcp_isDetectedFromCommandOrNpxArg()
{
    AcpAgentDefinition npx;
    npx.command = QStringLiteral("npx");
    npx.args = QStringList{QStringLiteral("-y"), QStringLiteral("pi-acp")};
    QVERIFY(PiAcpJudgeConfig::isPiAcpAgent(npx));

    AcpAgentDefinition cmd;
    cmd.command = QStringLiteral("pi-acp");
    QVERIFY(PiAcpJudgeConfig::isPiAcpAgent(cmd));

    AcpAgentDefinition bunx;
    bunx.command = QStringLiteral("bunx");
    bunx.args = QStringList{QStringLiteral("pi-acp")};
    QVERIFY(PiAcpJudgeConfig::isPiAcpAgent(bunx));

    AcpAgentDefinition bunX;
    bunX.command = QStringLiteral("bun");
    bunX.args = QStringList{QStringLiteral("x"), QStringLiteral("pi-acp")};
    QVERIFY(PiAcpJudgeConfig::isPiAcpAgent(bunX));

    AcpAgentDefinition bunLine;
    bunLine.command = QStringLiteral("bun x pi-acp");
    QVERIFY(PiAcpJudgeConfig::isPiAcpAgent(bunLine));

    AcpAgentDefinition claude;
    claude.command = QStringLiteral("npx");
    claude.args = QStringList{QStringLiteral("-y"),
                              QStringLiteral("@agentclientprotocol/claude-agent-acp@latest")};
    QVERIFY(!PiAcpJudgeConfig::isPiAcpAgent(claude));
}

void TestGoalAgent::piAcp_sourceAgentDir_prefersAcpEnvOverHome()
{
    AcpAgentDefinition agent;
    agent.env.insert(QStringLiteral("PI_CODING_AGENT_DIR"),
                     QStringLiteral("D:/custom/pi-agent"));
    QCOMPARE(PiAcpJudgeConfig::sourceAgentDir(agent),
             QDir::cleanPath(QStringLiteral("D:/custom/pi-agent")));
}

void TestGoalAgent::piAcp_stageJudgeDir_mergesVerdictAndSkipsSessions()
{
    QTemporaryDir src;
    QVERIFY(src.isValid());
    QVERIFY(QDir(src.path()).mkpath(QStringLiteral("sessions")));
    QFile sessionFile(src.path() + QStringLiteral("/sessions/old.json"));
    QVERIFY(sessionFile.open(QIODevice::WriteOnly));
    sessionFile.write("{}");
    sessionFile.close();
    QFile existing(src.path() + QStringLiteral("/mcp.json"));
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write(R"({"mcpServers":{"docs":{"command":"echo"}}})");
    existing.close();

    QTemporaryDir dst;
    QVERIFY(dst.isValid());
    QVERIFY(PiAcpJudgeConfig::stageJudgeAgentDir(
        src.path(), dst.path(), QStringLiteral("C:/NotepadAI.exe")));
    QVERIFY(!QFile::exists(dst.path() + QStringLiteral("/sessions/old.json")));
    QFile out(dst.path() + QStringLiteral("/mcp.json"));
    QVERIFY(out.open(QIODevice::ReadOnly));
    const QJsonObject servers = QJsonDocument::fromJson(out.readAll()).object()
                                    .value(QStringLiteral("mcpServers")).toObject();
    QVERIFY(servers.contains(QStringLiteral("docs")));
    const QJsonObject verdict = servers.value(QStringLiteral("goal-verdict")).toObject();
    QCOMPARE(verdict.value(QStringLiteral("command")).toString(),
             QStringLiteral("C:/NotepadAI.exe"));
    QCOMPARE(verdict.value(QStringLiteral("args")).toArray().at(0).toString(),
             QStringLiteral("--goal-verdict-mcp"));
    QCOMPARE(verdict.value(QStringLiteral("exposure")).toString(),
             QStringLiteral("direct"));
}

void TestGoalAgent::piAcp_stageJudgeDir_restoresLastSessionModel()
{
    QTemporaryDir src;
    QVERIFY(src.isValid());
    QVERIFY(QDir(src.path()).mkpath(QStringLiteral("sessions/proj")));
    QFile session(src.path() + QStringLiteral("/sessions/proj/run.jsonl"));
    QVERIFY(session.open(QIODevice::WriteOnly));
    session.write("{\"type\":\"session\"}\n");
    session.write("{\"type\":\"model_change\",\"provider\":\"6api-sonnet\",\"modelId\":\"6api/sonnet\"}\n");
    session.write("{\"type\":\"thinking_level_change\",\"thinkingLevel\":\"medium\"}\n");
    session.close();

    QFile settings(src.path() + QStringLiteral("/settings.json"));
    QVERIFY(settings.open(QIODevice::WriteOnly));
    settings.write("{\"theme\":\"dark\"}");
    settings.close();

    QTemporaryDir dst;
    QVERIFY(dst.isValid());
    QVERIFY(PiAcpJudgeConfig::stageJudgeAgentDir(
        src.path(), dst.path(), QStringLiteral("C:/NotepadAI.exe")));
    QVERIFY(!QFile::exists(dst.path() + QStringLiteral("/sessions/proj/run.jsonl")));

    QFile out(dst.path() + QStringLiteral("/settings.json"));
    QVERIFY(out.open(QIODevice::ReadOnly));
    const QJsonObject obj = QJsonDocument::fromJson(out.readAll()).object();
    QCOMPARE(obj.value(QStringLiteral("defaultProvider")).toString(),
             QStringLiteral("6api-sonnet"));
    QCOMPARE(obj.value(QStringLiteral("defaultModel")).toString(),
             QStringLiteral("6api/sonnet"));
    QCOMPARE(obj.value(QStringLiteral("defaultThinkingLevel")).toString(),
             QStringLiteral("medium"));
    QCOMPARE(obj.value(QStringLiteral("theme")).toString(), QStringLiteral("dark"));
}

void TestGoalAgent::stop_withAutoCompact_doesNotSendCompact()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.autoCompact = true;
    QVERIFY(goal.start(req));

    goal.stop();

    QCOMPARE(goal.status(), GoalAgent::Cancelled);
    QCOMPARE(model.messages().size(), 0);
    QVERIFY(!channel->wroteSessionCancel());
}

void TestGoalAgent::start_attachToExistingConversation_whenIdle_evaluatesImmediately()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    model.appendUserMessage(QStringLiteral("fix the crash"), {});
    goal.setTargetSession(&target, &model);

    QSignalSpy logs(&goal, &GoalAgent::debugLogEntry);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.attachToExistingConversation = true;
    QVERIFY(goal.start(req));

    bool evaluated = false;
    bool includedExisting = false;
    for (const auto &row : logs) {
        const QString entry = row.at(0).toString();
        if (entry.contains(QLatin1String("evaluateViaHttp")))
            evaluated = true;
        if (entry.contains(QLatin1String("startIdx=0")))
            includedExisting = true;
    }
    QVERIFY(evaluated);
    QVERIFY(includedExisting);
}

void TestGoalAgent::start_attachToExistingConversation_whenProcessing_waitsForPromptEnded()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    model.appendUserMessage(QStringLiteral("fix the crash"), {});
    model.onPromptStarted();
    goal.setTargetSession(&target, &model);

    QSignalSpy logs(&goal, &GoalAgent::debugLogEntry);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.attachToExistingConversation = true;
    QVERIFY(goal.start(req));
    QCOMPARE(goal.status(), GoalAgent::Active);

    auto logHas = [](const QSignalSpy &spy, const char *needle) {
        for (const auto &row : spy) {
            if (row.at(0).toString().contains(QLatin1String(needle)))
                return true;
        }
        return false;
    };
    QVERIFY(!logHas(logs, "evaluateViaHttp"));

    QVERIFY(QMetaObject::invokeMethod(&target, "promptEnded", Qt::DirectConnection));
    QVERIFY(logHas(logs, "evaluateViaHttp"));
}

void TestGoalAgent::start_attachToExistingConversation_usesLastUserMessageAsOriginal()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    model.appendUserMessage(QStringLiteral("fix the crash"), {});
    model.onPromptStarted();
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.attachToExistingConversation = true;
    QVERIFY(goal.start(req));
    QCOMPARE(goal.m_originalUserMessage, QStringLiteral("fix the crash"));
}

void TestGoalAgent::start_attach_ignoresRequestsBeforeGoalAchieved()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    model.appendUserMessage(QStringLiteral("old request"), {});
    model.appendSystemMessage(QStringLiteral("✓ Goal achieved: done"),
                             QLatin1String(kAcpMarkerGoalAchieved));
    model.appendUserMessage(QStringLiteral("new request"), {});
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.attachToExistingConversation = true;
    QVERIFY(goal.start(req));
    QCOMPARE(goal.m_originalUserMessage, QStringLiteral("new request"));
    QVERIFY(!goal.m_developerRequests.contains(QStringLiteral("old request")));
    QVERIFY(goal.m_developerRequests.contains(
        QStringLiteral("<original-message>new request</original-message>")));
    QVERIFY(!goal.m_developerRequests.contains(QStringLiteral("<request>new request</request>")));
}

void TestGoalAgent::start_snapshotsDeveloperRequestsBeforeLaterUserMessage()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *channel = new RecordingChannel(&target);
    target.attachChannelForTest(channel);
    channel->start();

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    model.appendUserMessage(QStringLiteral("old request"), {});
    model.appendSystemMessage(QStringLiteral("✓ Goal achieved: done"),
                             QLatin1String(kAcpMarkerGoalAchieved));
    model.appendUserMessage(QStringLiteral("after achieved"), {});
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    req.originalUserMessage = QStringLiteral("add a button");
    QVERIFY(goal.start(req));

    const QString snapshot = goal.m_developerRequests;
    model.appendUserMessage(QStringLiteral("typed mid loop"), {});
    QCOMPARE(goal.m_developerRequests, snapshot);
    QVERIFY(snapshot.contains(QStringLiteral("<request>after achieved</request>")));
    QVERIFY(!snapshot.contains(QStringLiteral("old request")));
    QVERIFY(!snapshot.contains(QStringLiteral("typed mid loop")));
    QVERIFY(snapshot.contains(QStringLiteral("<original-message>add a button</original-message>")));
}

void TestGoalAgent::launchAction_emptyIdleNoHistory_needsComposer()
{
    QCOMPARE(GoalAgent::launchAction(false, false, false), GoalAgent::LaunchAction::NeedComposer);
}

void TestGoalAgent::launchAction_emptyWithHistory_attaches()
{
    QCOMPARE(GoalAgent::launchAction(false, true, false), GoalAgent::LaunchAction::Attach);
}

void TestGoalAgent::launchAction_emptyWhileProcessing_attaches()
{
    QCOMPARE(GoalAgent::launchAction(false, false, true), GoalAgent::LaunchAction::Attach);
}

void TestGoalAgent::launchAction_composerIdle_sends()
{
    QCOMPARE(GoalAgent::launchAction(true, false, false), GoalAgent::LaunchAction::Send);
}

void TestGoalAgent::launchAction_composerWhileProcessing_attaches()
{
    QCOMPARE(GoalAgent::launchAction(true, true, true), GoalAgent::LaunchAction::Attach);
}

void TestGoalAgent::nativeGoalCommand_prefixesCriterion()
{
    QCOMPARE(GoalAgent::nativeGoalCommands(QStringList{QStringLiteral("viết bằng tiếng Lào")}),
             QStringList{QStringLiteral("/goal viết bằng tiếng Lào")});
}

void TestGoalAgent::nativeGoalCommands_oneSlashPerRow()
{
    const QStringList commands = GoalAgent::nativeGoalCommands(
        {QStringLiteral("hi in Vietnamese"), QStringLiteral("hi in japanase")});
    QCOMPARE(commands, QStringList({QStringLiteral("/goal hi in Vietnamese"),
                                    QStringLiteral("/goal hi in japanase")}));
}

void TestGoalAgent::nativeGoalWireText_appendsWorktreeInstruction()
{
    const QString command = QStringLiteral("/goal say hi in korean");
    QCOMPARE(GoalAgent::nativeGoalWireText(command, false), command);
    QCOMPARE(GoalAgent::nativeGoalWireText(command, true),
             QStringLiteral("/goal say hi in korean\n\n"
                            "Create a new git worktree for this task inside the repository's .claude/worktrees "
                            "directory. Do not choose any other location. When finished, merge the result "
                            "into the current branch and remove the worktree to free disk space."));
}

void TestGoalAgent::nativeGoalCommand_skipsBlankRows()
{
    QCOMPARE(GoalAgent::nativeGoalCommands({QStringLiteral("  "), QStringLiteral("xong")}),
             QStringList{QStringLiteral("/goal xong")});
    QCOMPARE(GoalAgent::nativeGoalCommands({}), QStringList());
}

void TestGoalAgent::isNativeGoalSlash_matchesGoalCommandOnly()
{
    QVERIFY(GoalAgent::isNativeGoalSlash(QStringLiteral("/goal xxx")));
    QVERIFY(GoalAgent::isNativeGoalSlash(QStringLiteral("  /goal")));
    QVERIFY(!GoalAgent::isNativeGoalSlash(QStringLiteral("/goals")));
    QVERIFY(!GoalAgent::isNativeGoalSlash(QStringLiteral("hello /goal")));
    QVERIFY(!GoalAgent::isNativeGoalSlash(QStringLiteral("/compact")));
}

void TestGoalAgent::nativeGoalUsesSidePrompt_onlyWhenAgentAdvertisesGoal()
{
    QVERIFY(GoalAgent::nativeGoalUsesSidePrompt(QStringLiteral("/goal x"), true));
    QVERIFY(!GoalAgent::nativeGoalUsesSidePrompt(QStringLiteral("/goal x"), false));
    QVERIFY(!GoalAgent::nativeGoalUsesSidePrompt(QStringLiteral("hello"), true));
    QCOMPARE(AcpPromptQueue::classifySend(
                 true, true, GoalAgent::nativeGoalUsesSidePrompt(QStringLiteral("/goal x"), false)),
             AcpPromptQueue::SendKind::Enqueue);
    QCOMPARE(AcpPromptQueue::classifySend(
                 true, true, GoalAgent::nativeGoalUsesSidePrompt(QStringLiteral("/goal x"), true)),
             AcpPromptQueue::SendKind::SidePrompt);
}


void TestGoalAgent::prefixGoalMessage_prefixesUnlessAlreadyGoal()
{
    QCOMPARE(GoalAgent::prefixGoalMessage(QStringLiteral("fix the parser")),
             QStringLiteral("/goal fix the parser"));
    QCOMPARE(GoalAgent::prefixGoalMessage(QStringLiteral("/goal fix the parser")),
             QStringLiteral("/goal fix the parser"));
    QCOMPARE(GoalAgent::prefixGoalMessage(QStringLiteral("  /goal")),
             QStringLiteral("  /goal"));
    QCOMPARE(GoalAgent::prefixGoalMessage(QStringLiteral("/goals")),
             QStringLiteral("/goal /goals"));
    QCOMPARE(GoalAgent::prefixGoalMessage(QString()), QString());
}

void TestGoalAgent::sidePrompt_whileTurnInFlight_doesNotEndTurn()
{
    AcpConnection conn;
    auto *channel = new RecordingChannel(&conn);
    conn.attachChannelForTest(channel);
    channel->start();
    conn.setSessionIdForTest(QStringLiteral("s1"));

    int started = 0;
    int ended = 0;
    connect(&conn, &AcpConnection::promptStarted, &conn, [&]() { ++started; });
    connect(&conn, &AcpConnection::promptEnded, &conn, [&]() { ++ended; });

    conn.sendPrompt(QStringLiteral("hello"), {});
    QCOMPARE(started, 1);
    QCOMPARE(ended, 0);

    conn.sendSidePrompt(QStringLiteral("/goal xong"));
    QCOMPARE(started, 1);
    QVERIFY(channel->written.contains("\"/goal xong\""));

    channel->pushStdout(QByteArray("{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}\n"));
    QCOMPARE(ended, 0);
    QCOMPARE(started, 1);

    channel->pushStdout(QByteArray("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n"));
    QTRY_COMPARE(ended, 1);
}

void TestGoalAgent::sidePrompts_twoGoalsWhileTurnInFlight_bothGoOutImmediately()
{
    AcpConnection conn;
    auto *channel = new RecordingChannel(&conn);
    conn.attachChannelForTest(channel);
    channel->start();
    conn.setSessionIdForTest(QStringLiteral("s1"));

    int ended = 0;
    connect(&conn, &AcpConnection::promptEnded, &conn, [&]() { ++ended; });

    conn.sendPrompt(QStringLiteral("hi"), {});
    conn.sendSidePrompt(QStringLiteral("/goal hi in Vietnamese"));
    conn.sendSidePrompt(QStringLiteral("/goal hi in japanase"));

    QVERIFY(channel->written.contains("\"/goal hi in Vietnamese\""));
    QVERIFY(channel->written.contains("\"/goal hi in japanase\""));
    QCOMPARE(ended, 0);

    channel->pushStdout(QByteArray("{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}\n"));
    channel->pushStdout(QByteArray("{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{}}\n"));
    QCOMPARE(ended, 0);
    channel->pushStdout(QByteArray("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}\n"));
    QTRY_COMPARE(ended, 1);
}

void TestGoalAgent::userPromptResult_whileGoalOutstanding_doesNotEndTurn()
{
    AcpConnection conn;
    auto *channel = new RecordingChannel(&conn);
    conn.attachChannelForTest(channel);
    channel->start();
    conn.setSessionIdForTest(QStringLiteral("s1"));

    int ended = 0;
    connect(&conn, &AcpConnection::promptEnded, &conn, [&]() { ++ended; });

    conn.sendPrompt(QStringLiteral("hi"), {});
    conn.sendSidePrompt(QStringLiteral("/goal say hi in korean"));

    channel->pushStdout(QByteArray("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"stopReason\":\"end_turn\"}}\n"));
    QCOMPARE(ended, 0);

    channel->pushStdout(QByteArray("{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"stopReason\":\"end_turn\"}}\n"));
    QTRY_COMPARE(ended, 1);
}

void TestGoalAgent::promptEnded_waitsIdleAfterRpcResult()
{
    AcpConnection conn;
    auto *channel = new RecordingChannel(&conn);
    conn.attachChannelForTest(channel);
    channel->start();
    conn.setSessionIdForTest(QStringLiteral("s1"));

    int ended = 0;
    connect(&conn, &AcpConnection::promptEnded, &conn, [&]() { ++ended; });

    conn.sendPrompt(QStringLiteral("hi"), {});
    channel->pushStdout(rpcResultFrame(1));
    QCOMPARE(ended, 0);
    QTRY_COMPARE(ended, 1);
}

void TestGoalAgent::promptEnded_chunkAfterRpcKeepsTurnOpen()
{
    AcpConnection conn;
    auto *channel = new RecordingChannel(&conn);
    conn.attachChannelForTest(channel);
    channel->start();
    conn.setSessionIdForTest(QStringLiteral("s1"));

    int ended = 0;
    connect(&conn, &AcpConnection::promptEnded, &conn, [&]() { ++ended; });

    conn.sendPrompt(QStringLiteral("hi"), {});
    channel->pushStdout(rpcResultFrame(1));
    QCOMPARE(ended, 0);

    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), QStringLiteral("still working"));
    QTest::qWait(50);
    QCOMPARE(ended, 0);
    channel->pushStdout(sessionUpdateFrame(
        QStringLiteral("agent_message_chunk"),
        QJsonObject{{QStringLiteral("content"), content}}));
    QTest::qWait(200);
    QCOMPARE(ended, 0);
    QTRY_COMPARE(ended, 1);
}

void TestGoalAgent::promptEnded_availableCommandsDoesNotKeepTurnOpen()
{
    AcpConnection conn;
    auto *channel = new RecordingChannel(&conn);
    conn.attachChannelForTest(channel);
    channel->start();
    conn.setSessionIdForTest(QStringLiteral("s1"));

    int ended = 0;
    connect(&conn, &AcpConnection::promptEnded, &conn, [&]() { ++ended; });

    conn.sendPrompt(QStringLiteral("hi"), {});
    channel->pushStdout(rpcResultFrame(1));
    channel->pushStdout(sessionUpdateFrame(
        QStringLiteral("available_commands_update"),
        QJsonObject{{QStringLiteral("availableCommands"), QJsonArray{}}}));
    QTRY_COMPARE(ended, 1);
}

void TestGoalAgent::streamingChunk_whileIdle_startsTurn()
{
    AcpConnection conn;
    auto *channel = new RecordingChannel(&conn);
    conn.attachChannelForTest(channel);
    channel->start();
    conn.setSessionIdForTest(QStringLiteral("s1"));

    int started = 0;
    int ended = 0;
    connect(&conn, &AcpConnection::promptStarted, &conn, [&]() { ++started; });
    connect(&conn, &AcpConnection::promptEnded, &conn, [&]() { ++ended; });

    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), QStringLiteral("autonomous"));
    channel->pushStdout(sessionUpdateFrame(
        QStringLiteral("agent_message_chunk"),
        QJsonObject{{QStringLiteral("content"), content}}));
    QCOMPARE(started, 1);
    QCOMPARE(ended, 0);
    QTRY_COMPARE(ended, 1);
}

void TestGoalAgent::sessionBusy_doesNotFail_retriesPrompt()
{
    AcpConnection conn;
    auto *channel = new RecordingChannel(&conn);
    conn.attachChannelForTest(channel);
    channel->start();
    conn.setSessionIdForTest(QStringLiteral("s1"));

    int failed = 0;
    int ended = 0;
    connect(&conn, &AcpConnection::requestFailed, &conn, [&](const QString &msg) {
        if (msg.contains(QLatin1String("already processing"), Qt::CaseInsensitive))
            ++failed;
    });
    connect(&conn, &AcpConnection::promptEnded, &conn, [&]() { ++ended; });

    conn.sendPrompt(QStringLiteral("hi"), {});
    QCOMPARE(channel->sessionPromptCount(), 1);

    QJsonObject err;
    err.insert(QStringLiteral("code"), -32003);
    err.insert(QStringLiteral("message"),
               QStringLiteral("Agent is already processing. Use steer() or followUp() "
                              "to queue messages, or wait for completion."));
    QJsonObject data;
    data.insert(QStringLiteral("reason"), QStringLiteral("session_busy"));
    err.insert(QStringLiteral("data"), data);
    channel->pushStdout(rpcErrorFrame(1, err));

    QCOMPARE(failed, 0);
    QCOMPARE(ended, 0);
    QTRY_VERIFY(channel->sessionPromptCount() >= 2);

    channel->pushStdout(rpcResultFrame(2));
    QTRY_COMPARE(ended, 1);
    QCOMPARE(failed, 0);
}

void TestGoalAgent::sessionBusy_followUpAfterRpc_doesNotFireUntilIdle()
{
    AcpConnection conn;
    auto *channel = new RecordingChannel(&conn);
    conn.attachChannelForTest(channel);
    channel->start();
    conn.setSessionIdForTest(QStringLiteral("s1"));

    int followUps = 0;
    connect(&conn, &AcpConnection::promptEnded, &conn, [&]() {
        ++followUps;
        conn.sendPrompt(QStringLiteral("goal follow-up"), {});
    });

    conn.sendPrompt(QStringLiteral("hi"), {});
    channel->pushStdout(rpcResultFrame(1));
    QCOMPARE(followUps, 0);
    QCOMPARE(channel->sessionPromptCount(), 1);

    QJsonObject content;
    content.insert(QStringLiteral("type"), QStringLiteral("text"));
    content.insert(QStringLiteral("text"), QStringLiteral("still going"));
    channel->pushStdout(sessionUpdateFrame(
        QStringLiteral("agent_message_chunk"),
        QJsonObject{{QStringLiteral("content"), content}}));
    QTest::qWait(200);
    QCOMPARE(followUps, 0);
    QCOMPARE(channel->sessionPromptCount(), 1);

    QTRY_COMPARE(followUps, 1);
    QCOMPARE(channel->sessionPromptCount(), 2);
}

void TestGoalAgent::judgeConnection_permissionRequest_isAutoApproved()
{
    // A headless judge has no permission UI. Allow-all must answer
    // session/request_permission or the judge turn never ends and Goal
    // never sends the next prompt to the coding agent.
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection judge;
    auto *channel = new RecordingChannel(&judge);
    judge.attachChannelForTest(channel);
    channel->start();

    goal.configureHeadlessJudge(&judge);

    QJsonObject allowOnce;
    allowOnce.insert(QStringLiteral("optionId"), QStringLiteral("allow-once"));
    allowOnce.insert(QStringLiteral("name"), QStringLiteral("Allow once"));
    allowOnce.insert(QStringLiteral("kind"), QStringLiteral("allow_once"));
    QJsonObject deny;
    deny.insert(QStringLiteral("optionId"), QStringLiteral("deny"));
    deny.insert(QStringLiteral("name"), QStringLiteral("Deny"));
    deny.insert(QStringLiteral("kind"), QStringLiteral("deny"));
    QJsonObject params;
    params.insert(QStringLiteral("title"), QStringLiteral("Read file"));
    params.insert(QStringLiteral("options"), QJsonArray{allowOnce, deny});
    QJsonObject req;
    req.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    req.insert(QStringLiteral("id"), 42);
    req.insert(QStringLiteral("method"), QStringLiteral("session/request_permission"));
    req.insert(QStringLiteral("params"), params);

    channel->pushStdout(QJsonDocument(req).toJson(QJsonDocument::Compact) + '\n');

    QVERIFY(channel->written.contains("\"id\":42"));
    QVERIFY(channel->written.contains("\"optionId\":\"allow-once\""));
}

void TestGoalAgent::judgePrompt_neverEnds_failsClosed()
{
    // After the coding agent turn ends, Goal waits on the ACP judge. If
    // session/prompt never returns, Goal must fail-closed instead of staying
    // Active forever (no continue is sent to the coding agent).
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *targetChannel = new RecordingChannel(&target);
    target.attachChannelForTest(targetChannel);
    targetChannel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));
    QCOMPARE(goal.status(), GoalAgent::Active);

    auto *judge = new AcpConnection(&goal);
    auto *judgeChannel = new RecordingChannel(judge);
    judge->attachChannelForTest(judgeChannel);
    judgeChannel->start();
    judge->setSessionIdForTest(QStringLiteral("j1"));
    goal.configureHeadlessJudge(judge);

    goal.m_agentId = QStringLiteral("claude");
    goal.m_judgeConnection = judge;
    goal.m_judgePromptTimeoutMs = 50;
    goal.evaluateCurrentCriterion();

    QCOMPARE(goal.status(), GoalAgent::Active);
    QVERIFY(model.messages().size() >= 1);
    QCOMPARE(model.messages().first().marker, QLatin1String(kAcpMarkerGoalJudging));
    QTRY_COMPARE(goal.status(), GoalAgent::Failed);
    int goalUser = 0;
    for (const auto &m : model.messages()) {
        if (m.fromGoalAgent)
            ++goalUser;
    }
    QCOMPARE(goalUser, 0);
}

void TestGoalAgent::judgeMessageChunk_mirrorsOntoTargetModel()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *targetChannel = new RecordingChannel(&target);
    target.attachChannelForTest(targetChannel);
    targetChannel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    auto *judge = new AcpConnection(&goal);
    auto *judgeChannel = new RecordingChannel(judge);
    judge->attachChannelForTest(judgeChannel);
    judgeChannel->start();
    judge->setSessionIdForTest(QStringLiteral("j1"));
    goal.configureHeadlessJudge(judge);
    goal.m_agentId = QStringLiteral("claude");
    goal.m_judgeConnection = judge;
    goal.evaluateCurrentCriterion();

    QCOMPARE(model.messages().first().marker, QLatin1String(kAcpMarkerGoalJudging));
    goal.onJudgeMessageChunk(QStringLiteral("hello "));
    goal.onJudgeMessageChunk(QStringLiteral("judge"));
    QCOMPARE(model.messages().size(), 2);
    QVERIFY(model.messages().last().fromGoalJudge);
    QCOMPARE(model.messages().last().role, QStringLiteral("assistant"));
    QCOMPARE(model.messages().last().content.first().text, QStringLiteral("hello judge"));
}

void TestGoalAgent::actionDisplayText_stripsActionTags()
{
    QCOMPARE(GoalActionParser::displayText(
                 QStringLiteral("<action type=\"continue\">Say hi in Japanese.</action>")),
             QStringLiteral("Say hi in Japanese."));
    QCOMPARE(GoalActionParser::displayText(QStringLiteral("<action type=\"complete\">")),
             QString());
    QCOMPARE(GoalActionParser::displayText(QStringLiteral("<action type=\"continue\">partial")),
             QStringLiteral("partial"));
    QCOMPARE(GoalActionParser::displayText(QStringLiteral("no tags")),
             QStringLiteral("no tags"));
    QCOMPARE(GoalActionParser::displayText(QStringLiteral("<act")),
             QString());
}

void TestGoalAgent::continueAction_dropsJudgeBubblesKeepsResult()
{
    ApplicationSettings settings;
    GoalAgent goal(nullptr, &settings);

    AcpConnection target;
    auto *targetChannel = new RecordingChannel(&target);
    target.attachChannelForTest(targetChannel);
    targetChannel->start();
    target.setSessionIdForTest(QStringLiteral("s1"));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("p1"), historyDir.path());
    goal.setTargetSession(&target, &model);

    GoalAgent::StartRequest req;
    req.targetSessionId = QStringLiteral("s1");
    req.successCriteriaList = QStringList{QStringLiteral("done")};
    req.agentId = QLatin1String(GoalHttpJudge::kAgentId);
    QVERIFY(goal.start(req));

    auto *judge = new AcpConnection(&goal);
    auto *judgeChannel = new RecordingChannel(judge);
    judge->attachChannelForTest(judgeChannel);
    judgeChannel->start();
    judge->setSessionIdForTest(QStringLiteral("j1"));
    goal.configureHeadlessJudge(judge);
    goal.m_agentId = QStringLiteral("claude");
    goal.m_judgeConnection = judge;
    goal.evaluateCurrentCriterion();
    goal.m_awaitingJudgeResponse = true;
    goal.onJudgeMessageChunk(QStringLiteral("<action type=\"continue\">Please run the tests.</action>"));

    QCOMPARE(model.messages().last().content.first().text, QStringLiteral("Please run the tests."));
    QVERIFY(!model.messages().last().content.first().text.contains(QLatin1String("<action")));

    GoalAction action;
    action.type = GoalAction::Continue;
    action.text = QStringLiteral("Please run the tests.");
    goal.applyJudgeAction(action);

    int judgeRows = 0;
    for (const auto &m : model.messages()) {
        if (m.fromGoalJudge || m.marker == QLatin1String(kAcpMarkerGoalJudging))
            ++judgeRows;
    }
    QCOMPARE(judgeRows, 0);
    QCOMPARE(model.messages().last().role, QStringLiteral("user"));
    QVERIFY(model.messages().last().fromGoalAgent);
    QCOMPARE(model.messages().last().content.first().text, QStringLiteral("Please run the tests."));
}

QTEST_MAIN(TestGoalAgent)
#include "test_goal_agent.moc"
