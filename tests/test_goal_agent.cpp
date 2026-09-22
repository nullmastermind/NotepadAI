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
#include <QTemporaryDir>

#include "AcpConnection.h"
#include "AcpSessionModel.h"
#include "ApplicationSettings.h"
#include "GoalAgent.h"
#include "GoalHttpJudge.h"
#include "IAcpProcessChannel.h"

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

    bool wroteSessionCancel() const
    {
        return written.contains("session/cancel");
    }

private:
    bool m_running = false;
};

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
    void continueAction_appliesTargetPromptDecorator_hidesFromTranscript();
    void restartAction_appliesTargetPromptDecorator_hidesFromTranscript();
    void completeAction_stillAchievesSingleCriterion();
    void completeAction_withAutoCompact_sendsCompactToTarget();
    void completeAction_withAutoCompact_skipsTargetPromptDecorator();
    void completeAction_withoutAutoCompact_doesNotSendCompact();
    void stop_withAutoCompact_doesNotSendCompact();
    void start_attachToExistingConversation_whenIdle_evaluatesImmediately();
    void start_attachToExistingConversation_whenProcessing_waitsForPromptEnded();
    void start_attachToExistingConversation_usesLastUserMessageAsOriginal();
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
    void sidePrompt_whileTurnInFlight_doesNotEndTurn();
    void sidePrompts_twoGoalsWhileTurnInFlight_bothGoOutImmediately();
    void userPromptResult_whileGoalOutstanding_doesNotEndTurn();
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
                            "Create a new git worktree for this task. When finished, merge the result "
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
    QCOMPARE(ended, 1);
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
    QCOMPARE(ended, 1);
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
    QCOMPARE(ended, 1);
}

QTEST_MAIN(TestGoalAgent)
#include "test_goal_agent.moc"
