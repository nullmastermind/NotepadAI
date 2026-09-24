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
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include "AcpHistoryStore.h"
#include "AcpProtocol.h"
#include "AcpSessionModel.h"

class TestAcpSessionModel : public QObject
{
    Q_OBJECT

private slots:
    void emptySessionDoesNotPersist();
    void streamingConcatenation();
    void messageIdChange_startsNewBubble();
    void promptEndedClosesStreaming();
    void thoughtStreamAutoClosesOnAssistantChunk();
    void toolCallMerge();
    void terminalPlaceholderStrippedOnReceive();
    void terminalOutputDeltasAccumulate();
    void groupIdIncrementsPerTurn();
    void loadRoundTrip();
    void imageBlocksSurviveRoundTrip();
    void detachingHistoryStoreFlushesPendingSnapshot();
    void usageReplace_zeroUsedSameSizeDuringTurnDoesNotClobberOccupancy();
    void usageReplace_smallerNonZeroDuringTurnReplacesOccupancy();
    void usageReplace_zeroUsedWhileIdleReplacesOccupancy();
    void usageReplace_zeroUsedDifferentSizeDuringTurnReplacesOccupancy();
    void usageUpdate_promptTurnTotalDoesNotClobberContextOccupancy();
    void usageUpdate_promptTurnTotalAppliesWhenNoContextWindow();
};

void TestAcpSessionModel::emptySessionDoesNotPersist()
{
    QTemporaryDir tmp;
    AcpHistoryStore store;
    store.setHistoryDir(tmp.path());

    AcpSessionModel model(QStringLiteral("e1"), QStringLiteral("proj"), tmp.path());
    model.setHistoryStore(&store);
    QVERIFY(model.isEmpty());

    QSignalSpy spy(&store, &AcpHistoryStore::flushed);
    // No mutations — model should not have scheduled any writes.
    QVERIFY(!spy.wait(800));
    QCOMPARE(spy.count(), 0);
    QVERIFY(!QFile::exists(tmp.path() + QStringLiteral("/e1.json")));
}

void TestAcpSessionModel::streamingConcatenation()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    QSignalSpy appended(&model, &AcpSessionModel::messageAppended);
    QSignalSpy chunkSpy(&model, &AcpSessionModel::messageChunkAppended);

    model.onMessageChunk(QStringLiteral("Hello, "));
    model.onMessageChunk(QStringLiteral("world"));

    QCOMPARE(model.messages().size(), 1);
    QCOMPARE(model.messages().first().role, QStringLiteral("assistant"));
    QCOMPARE(model.messages().first().content.size(), 1);
    QCOMPARE(model.messages().first().content.first().text, QStringLiteral("Hello, world"));
    QCOMPARE(appended.count(), 1);
    QCOMPARE(chunkSpy.count(), 1);
}

void TestAcpSessionModel::messageIdChange_startsNewBubble()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    model.onMessageChunk(QStringLiteral("안녕하세요!"), QStringLiteral("54a1d2a8"));
    model.onMessageChunk(QStringLiteral("Goal set: say hi in japanese"), QStringLiteral("82bf3ee5"));

    QCOMPARE(model.messages().size(), 2);
    QCOMPARE(model.messages().at(0).content.first().text, QStringLiteral("안녕하세요!"));
    QCOMPARE(model.messages().at(1).content.first().text, QStringLiteral("Goal set: say hi in japanese"));
}

void TestAcpSessionModel::promptEndedClosesStreaming()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("first"));
    model.onPromptEnded();

    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("second"));

    QCOMPARE(model.messages().size(), 2);
    QCOMPARE(model.messages().at(0).content.first().text, QStringLiteral("first"));
    QCOMPARE(model.messages().at(1).content.first().text, QStringLiteral("second"));
}

void TestAcpSessionModel::thoughtStreamAutoClosesOnAssistantChunk()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    model.onThoughtChunk(QStringLiteral("thinking..."));
    model.onMessageChunk(QStringLiteral("answer"));
    model.onThoughtChunk(QStringLiteral("more thinking"));

    QCOMPARE(model.messages().size(), 3);
    QCOMPARE(model.messages().at(0).role, QStringLiteral("thought"));
    QCOMPARE(model.messages().at(0).content.first().text, QStringLiteral("thinking..."));
    QCOMPARE(model.messages().at(1).role, QStringLiteral("assistant"));
    QCOMPARE(model.messages().at(1).content.first().text, QStringLiteral("answer"));
    QCOMPARE(model.messages().at(2).role, QStringLiteral("thought"));
    QCOMPARE(model.messages().at(2).content.first().text, QStringLiteral("more thinking"));
}

void TestAcpSessionModel::toolCallMerge()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());
    QSignalSpy spy(&model, &AcpSessionModel::toolCallAddedOrUpdated);

    AcpProtocol::AcpToolCall tc;
    tc.id = QStringLiteral("call-1");
    tc.title = QStringLiteral("read");
    tc.status = QStringLiteral("pending");
    model.onToolCallReceived(tc);

    AcpProtocol::AcpToolCallUpdate up;
    up.id = QStringLiteral("call-1");
    up.status = QStringLiteral("completed");
    model.onToolCallUpdated(up);

    QCOMPARE(model.toolCalls().value(QStringLiteral("call-1")).status,
             QStringLiteral("completed"));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("call-1"));
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("call-1"));
}

void TestAcpSessionModel::terminalPlaceholderStrippedOnReceive()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpToolCall tc;
    tc.id = QStringLiteral("bash-1");
    tc.title = QStringLiteral("ls");
    tc.kind = QStringLiteral("execute");
    QJsonObject term;
    term.insert(QStringLiteral("type"), QStringLiteral("terminal"));
    term.insert(QStringLiteral("terminalId"), QStringLiteral("bash-1"));
    tc.content.append(term);
    model.onToolCallReceived(tc);

    QCOMPARE(model.toolCalls().value(QStringLiteral("bash-1")).content.size(), 0);
}

void TestAcpSessionModel::terminalOutputDeltasAccumulate()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpToolCall tc;
    tc.id = QStringLiteral("bash-1");
    tc.title = QStringLiteral("ls");
    model.onToolCallReceived(tc);

    AcpProtocol::AcpToolCallUpdate u1;
    u1.id = QStringLiteral("bash-1");
    u1.status = QStringLiteral("in_progress");
    u1.terminalOutputDelta = QStringLiteral("foo");
    model.onToolCallUpdated(u1);

    AcpProtocol::AcpToolCallUpdate u2;
    u2.id = QStringLiteral("bash-1");
    u2.terminalOutputDelta = QStringLiteral("bar");
    model.onToolCallUpdated(u2);

    const QJsonArray content = model.toolCalls().value(QStringLiteral("bash-1")).content;
    QCOMPARE(content.size(), 1);
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("foobar"));
}

void TestAcpSessionModel::groupIdIncrementsPerTurn()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    model.onPromptStarted(); // group becomes 1
    AcpProtocol::AcpToolCall a;
    a.id = QStringLiteral("a");
    model.onToolCallReceived(a);
    model.onPromptEnded();

    model.onPromptStarted(); // group becomes 2
    AcpProtocol::AcpToolCall b;
    b.id = QStringLiteral("b");
    model.onToolCallReceived(b);
    model.onPromptEnded();

    QCOMPARE(model.toolCalls().value(QStringLiteral("a")).groupId, 1);
    QCOMPARE(model.toolCalls().value(QStringLiteral("b")).groupId, 2);
}

void TestAcpSessionModel::loadRoundTrip()
{
    QTemporaryDir tmp;
    AcpHistoryStore store;
    store.setHistoryDir(tmp.path());

    {
        AcpSessionModel a(QStringLiteral("rt"), QStringLiteral("projA"), tmp.path());
        a.setHistoryStore(&store);

        a.onPromptStarted();
        a.onMessageChunk(QStringLiteral("Hi "));
        a.onMessageChunk(QStringLiteral("there"));
        AcpProtocol::AcpToolCall tc;
        tc.id = QStringLiteral("t1");
        tc.title = QStringLiteral("fs/read");
        tc.status = QStringLiteral("completed");
        a.onToolCallReceived(tc);
        a.onPromptEnded();

        QSignalSpy spy(&store, &AcpHistoryStore::flushed);
        QVERIFY(spy.wait(2000));
    }

    AcpSessionModel b(QStringLiteral("rt"), QStringLiteral("ignored"), tmp.path());
    QCOMPARE(b.messages().size(), 1);
    QCOMPARE(b.messages().first().role, QStringLiteral("assistant"));
    QCOMPARE(b.messages().first().content.first().text, QStringLiteral("Hi there"));
    QCOMPARE(b.toolCalls().size(), 1);
    QVERIFY(b.toolCalls().contains(QStringLiteral("t1")));
    QCOMPARE(b.toolCalls().value(QStringLiteral("t1")).status, QStringLiteral("completed"));
    QCOMPARE(b.timeline().size(), 2);
    QCOMPARE(b.projectId(), QStringLiteral("projA"));
}

void TestAcpSessionModel::imageBlocksSurviveRoundTrip()
{
    QTemporaryDir tmp;
    AcpHistoryStore store;
    store.setHistoryDir(tmp.path());

    const QByteArray imgBytes("\x89PNG\r\n\x1a\nFAKEDATA", 14);

    {
        AcpSessionModel a(QStringLiteral("img"), QStringLiteral("p"), tmp.path());
        a.setHistoryStore(&store);
        QVector<QPair<QByteArray, QString>> images;
        images.append({imgBytes, QStringLiteral("image/png")});
        a.appendUserMessage(QStringLiteral("see this"), images);

        QSignalSpy spy(&store, &AcpHistoryStore::flushed);
        QVERIFY(spy.wait(2000));
    }

    AcpSessionModel b(QStringLiteral("img"), QStringLiteral("p"), tmp.path());
    QCOMPARE(b.messages().size(), 1);
    const auto &content = b.messages().first().content;
    QCOMPARE(content.size(), 2);
    QCOMPARE(content.at(0).kind, AcpProtocol::AcpContentBlock::Kind::Text);
    QCOMPARE(content.at(0).text, QStringLiteral("see this"));
    QCOMPARE(content.at(1).kind, AcpProtocol::AcpContentBlock::Kind::Image);
    QCOMPARE(content.at(1).imageData, imgBytes);
    QCOMPARE(content.at(1).mimeType, QStringLiteral("image/png"));
}

void TestAcpSessionModel::usageReplace_zeroUsedSameSizeDuringTurnDoesNotClobberOccupancy()
{
    // claude-agent-acp emits usage_update {used:0, size:same} at each assistant
    // message_start, before the stream snapshot is populated. Applying it makes
    // the context bar flash to 0% and back for the rest of the turn.
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("usage"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpUsage context;
    context.totalTokens = 149700;
    context.maxTokens = 1000000;
    model.onUsageReplaced(context);
    model.onPromptStarted();

    AcpProtocol::AcpUsage flash;
    flash.totalTokens = 0;
    flash.maxTokens = 1000000;
    model.onUsageReplaced(flash);

    QVERIFY(model.usage().has_value());
    QCOMPARE(model.usage()->totalTokens, std::optional<int>(149700));
    QCOMPARE(model.usage()->maxTokens, std::optional<int>(1000000));
}

void TestAcpSessionModel::usageReplace_smallerNonZeroDuringTurnReplacesOccupancy()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("usage"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpUsage context;
    context.totalTokens = 149700;
    context.maxTokens = 1000000;
    model.onUsageReplaced(context);
    model.onPromptStarted();

    AcpProtocol::AcpUsage compacted;
    compacted.totalTokens = 42000;
    compacted.maxTokens = 1000000;
    model.onUsageReplaced(compacted);

    QCOMPARE(model.usage()->totalTokens, std::optional<int>(42000));
    QCOMPARE(model.usage()->maxTokens, std::optional<int>(1000000));
}

void TestAcpSessionModel::usageReplace_zeroUsedWhileIdleReplacesOccupancy()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("usage"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpUsage context;
    context.totalTokens = 149700;
    context.maxTokens = 1000000;
    model.onUsageReplaced(context);

    AcpProtocol::AcpUsage cleared;
    cleared.totalTokens = 0;
    cleared.maxTokens = 1000000;
    model.onUsageReplaced(cleared);

    QCOMPARE(model.usage()->totalTokens, std::optional<int>(0));
    QCOMPARE(model.usage()->maxTokens, std::optional<int>(1000000));
}

void TestAcpSessionModel::usageReplace_zeroUsedDifferentSizeDuringTurnReplacesOccupancy()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("usage"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpUsage context;
    context.totalTokens = 149700;
    context.maxTokens = 1000000;
    model.onUsageReplaced(context);
    model.onPromptStarted();

    AcpProtocol::AcpUsage switched;
    switched.totalTokens = 0;
    switched.maxTokens = 200000;
    model.onUsageReplaced(switched);

    QCOMPARE(model.usage()->totalTokens, std::optional<int>(0));
    QCOMPARE(model.usage()->maxTokens, std::optional<int>(200000));
}

void TestAcpSessionModel::usageUpdate_promptTurnTotalDoesNotClobberContextOccupancy()
{
    // session/prompt usage.totalTokens is turn accounting (sum of each round),
    // not tokens currently in context. Merging it over a usage_update occupancy
    // jumps the bar at the end of the turn.
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("usage"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpUsage context;
    context.totalTokens = 149700;
    context.maxTokens = 1000000;
    model.onUsageReplaced(context);

    AcpProtocol::AcpUsage turn;
    turn.inputTokens = 500000;
    turn.outputTokens = 2000;
    turn.totalTokens = 502000;
    model.onUsageUpdated(turn);

    QVERIFY(model.usage().has_value());
    QCOMPARE(model.usage()->totalTokens, std::optional<int>(149700));
    QCOMPARE(model.usage()->maxTokens, std::optional<int>(1000000));
}

void TestAcpSessionModel::usageUpdate_promptTurnTotalAppliesWhenNoContextWindow()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("usage"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpUsage turn;
    turn.inputTokens = 1000;
    turn.outputTokens = 801;
    turn.totalTokens = 1801;
    model.onUsageUpdated(turn);

    QVERIFY(model.usage().has_value());
    QCOMPARE(model.usage()->totalTokens, std::optional<int>(1801));
    QVERIFY(!model.usage()->maxTokens.has_value());
}

void TestAcpSessionModel::detachingHistoryStoreFlushesPendingSnapshot()
{
    QTemporaryDir tmp;
    AcpHistoryStore store;
    store.setHistoryDir(tmp.path());
    QSignalSpy spy(&store, &AcpHistoryStore::flushed);

    {
        AcpSessionModel model(QStringLiteral("detach"), QStringLiteral("proj"), tmp.path());
        model.setHistoryStore(&store);
        model.onMessageChunk(QStringLiteral("pending snapshot"));
        model.setHistoryStore(nullptr);
    }

    QVERIFY(spy.wait(2000));

    AcpSessionModel reloaded(QStringLiteral("detach"), QStringLiteral("ignored"), tmp.path());
    QCOMPARE(reloaded.messages().size(), 1);
    QCOMPARE(reloaded.messages().first().content.first().text,
             QStringLiteral("pending snapshot"));
    QCOMPARE(reloaded.projectId(), QStringLiteral("proj"));
}

QTEST_GUILESS_MAIN(TestAcpSessionModel)
#include "test_acp_session_model.moc"
