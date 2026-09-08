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

QTEST_MAIN(TestGoalAgent)
#include "test_goal_agent.moc"
