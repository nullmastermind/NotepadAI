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
    void completeAction_stillAchievesSingleCriterion();
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
}

QTEST_MAIN(TestGoalAgent)
#include "test_goal_agent.moc"
