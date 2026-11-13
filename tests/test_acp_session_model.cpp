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
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include <memory>

#include "AcpHistoryStore.h"
#include "AcpProtocol.h"
#include "AcpSessionModel.h"
#include "DataPaths.h"

class TestAcpSessionModel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void emptySessionDoesNotPersist();
    void grillMeRawInputSurvivesHistory();
    void streamingConcatenation();
    void messageIdChange_startsNewBubble();
    void promptEndedClosesStreaming();
    void sameMessageIdAfterIdleTurnEndKeepsBubble();
    void blankOpeningChunkDoesNotCreateBubble();
    void thoughtStreamAutoClosesOnAssistantChunk();
    void toolCallMerge();
    void subagentCallsNestUnderTheirAgentCall();
    void lateSubagentAttributionKeepsTimeline();
    void subagentChildrenSurviveRoundTrip();
    void restoreFieldsSurviveRoundTrip();
    void prewalkModelSurvivesRoundTrip();
    void historyKeepsSessionSelectionWhilePrewalkSwitched();
    void terminalPlaceholderStrippedOnReceive();
    void terminalOutputDeltasAccumulate();
    void ompWriteSynthesizesDiffAndPreservesOnComplete();
    void ompHashlineInputInjectsPath();

    void initializedAlwaysExposesHostGoalCommand();
    void availableCommandsUpdateKeepsHostGoalCommand();
    void hostInjectedGoalIsNotAgentAdvertised();
    void agentAdvertisedGoalSurvivesHostInject();


    void groupIdIncrementsPerTurn();
    void loadRoundTrip();
    void imageBlocksSurviveRoundTrip();
    void detachingHistoryStoreFlushesPendingSnapshot();
    void usageReplace_zeroUsedSameSizeDuringTurnDoesNotClobberOccupancy();
    void backgroundedMarker_setsToolCallFlag();
    void asyncTasksChanged_flagsLiveToolCallsAndCount();
    void asyncSubagent_runsInBackgroundUntilTurnEnds();
    void usageReplace_smallerNonZeroDuringTurnReplacesOccupancy();
    void usageReplace_zeroUsedWhileIdleReplacesOccupancy();
    void usageReplace_zeroUsedDifferentSizeDuringTurnReplacesOccupancy();
    void usageUpdate_promptTurnTotalDoesNotClobberContextOccupancy();
    void usageUpdate_promptTurnTotalAppliesWhenNoContextWindow();

    void goalJudgeChunk_taggedAndConcatenated();
    void goalJudgeThought_independentOfCodingAgentStream();
    void goalJudgeToolCall_namespacesId();
    void goalJudge_survivesHistoryRoundTrip();
    void codingAgentPromptEnded_doesNotCloseGoalJudgeStream();
    void goalJudgeBeginTurn_appendsJudgingMarker();
    void replaceGoalJudgeText_rewritesBusyRow();
    void blankGoalJudgeChunk_doesNotOpenRow();
    void removeGoalJudgeTurn_dropsSuffixKeepsPrior();
};

void TestAcpSessionModel::initTestCase()
{
    DataPaths::init(QDir::tempPath(), DataPaths::Source::Default);
}

void TestAcpSessionModel::grillMeRawInputSurvivesHistory()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    AcpSessionModel model(QStringLiteral("au1"), QStringLiteral("proj"), tmp.path());
    model.appendUserMessage(QStringLiteral("grill me"), {});
    AcpProtocol::AcpToolCall ask;
    ask.id = QStringLiteral("t-ask");
    ask.title = QStringLiteral("mcp__grill_me__ask");
    ask.status = QStringLiteral("completed");
    ask.rawInput = QJsonObject{{QStringLiteral("questions"), QJsonArray{QJsonObject{
        {QStringLiteral("id"), QStringLiteral("Q1")}, {QStringLiteral("title"), QStringLiteral("What?")},
        {QStringLiteral("type"), QStringLiteral("text")}}}}};
    model.onToolCallReceived(ask);
    AcpProtocol::AcpToolCall other;
    other.id = QStringLiteral("t-write");
    other.title = QStringLiteral("Write");
    other.status = QStringLiteral("completed");
    other.rawInput = QJsonObject{{QStringLiteral("path"), QStringLiteral("a.txt")},
                                 {QStringLiteral("content"), QStringLiteral("big body")}};
    model.onToolCallReceived(other);

    const QJsonArray calls = model.toHistoryJson().value(QStringLiteral("toolCalls")).toArray();
    QJsonObject askJson;
    QJsonObject writeJson;
    for (const QJsonValue &v : calls) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("toolCallId")).toString() == QLatin1String("t-ask"))
            askJson = o;
        else
            writeJson = o;
    }
    // grill_me ask keeps its arguments (the form is rebuilt from them)...
    QCOMPARE(askJson.value(QStringLiteral("rawInput")).toObject(), ask.rawInput);
    // ...other tools stay slim.
    QVERIFY(!writeJson.value(QStringLiteral("rawInput")).toObject().contains(QStringLiteral("content")));
}

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

void TestAcpSessionModel::sameMessageIdAfterIdleTurnEndKeepsBubble()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    // Wake-up turn: the host's idle timer ended the turn between two chunks of
    // one agent message.
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("Task nền `"), QStringLiteral("msg_a"));
    model.onPromptEnded();
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("bll7` đã chạy"), QStringLiteral("msg_a"));
    model.onPromptEnded();

    QCOMPARE(model.messages().size(), 1);
    QCOMPARE(model.messages().at(0).content.first().text, QStringLiteral("Task nền `bll7` đã chạy"));

    // A different messageId is a new bubble.
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("next"), QStringLiteral("msg_b"));
    QCOMPARE(model.messages().size(), 2);

    // Something landing after the bubble (a user message) blocks the resume.
    model.onPromptEnded();
    model.appendUserMessage(QStringLiteral("hi"), {}, false);
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("late"), QStringLiteral("msg_b"));
    QCOMPARE(model.messages().size(), 4);
}

void TestAcpSessionModel::blankOpeningChunkDoesNotCreateBubble()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    model.onMessageChunk(QStringLiteral(" \n"));
    model.onThoughtChunk(QString());
    QCOMPARE(model.messages().size(), 0);
    QCOMPARE(model.timeline().size(), 0);

    model.onMessageChunk(QStringLiteral("\n"));
    model.onMessageChunk(QStringLiteral("kept"));
    QCOMPARE(model.messages().size(), 1);
    QCOMPARE(model.messages().first().content.first().text, QStringLiteral("kept"));
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

namespace {

AcpProtocol::AcpToolCall makeToolCall(const QString &id, const QString &parent = {})
{
    AcpProtocol::AcpToolCall tc;
    tc.id = id;
    tc.title = id;
    tc.status = QStringLiteral("pending");
    tc.parentToolCallId = parent;
    return tc;
}

} // namespace

void TestAcpSessionModel::subagentCallsNestUnderTheirAgentCall()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("Spawning two "));
    model.onToolCallReceived(makeToolCall(QStringLiteral("agentA")));
    model.onToolCallReceived(makeToolCall(QStringLiteral("agentB")));
    model.onMessageChunk(QStringLiteral("Waiting on "));
    // Interleaved subagent calls: no timeline entries, arrival order per parent.
    model.onToolCallReceived(makeToolCall(QStringLiteral("a1"), QStringLiteral("agentA")));
    model.onToolCallReceived(makeToolCall(QStringLiteral("b1"), QStringLiteral("agentB")));
    model.onToolCallReceived(makeToolCall(QStringLiteral("a2"), QStringLiteral("agentA")));
    AcpProtocol::AcpToolCallUpdate early; // an update that outran its tool_call
    early.id = QStringLiteral("b2");
    early.status = QStringLiteral("in_progress");
    early.parentToolCallId = QStringLiteral("agentB");
    model.onToolCallUpdated(early);
    // ...and do not split the main agent's streaming bubble.
    model.onMessageChunk(QStringLiteral("them."));

    QCOMPARE(model.timeline().size(), 4); // text, agentA, agentB, text
    QCOMPARE(model.subagentChildren(QStringLiteral("agentA")),
             (QStringList{QStringLiteral("a1"), QStringLiteral("a2")}));
    QCOMPARE(model.subagentChildren(QStringLiteral("agentB")),
             (QStringList{QStringLiteral("b1"), QStringLiteral("b2")}));
    QCOMPARE(model.toolCalls().value(QStringLiteral("b2")).parentToolCallId, QStringLiteral("agentB"));
    QCOMPARE(model.messages().size(), 2);
    QCOMPARE(model.messages().at(1).content.first().text, QStringLiteral("Waiting on them."));
}

void TestAcpSessionModel::lateSubagentAttributionKeepsTimeline()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());
    model.onToolCallReceived(makeToolCall(QStringLiteral("agent")));
    // An eager permission tool_call before the adapter knew its subagent.
    model.onToolCallReceived(makeToolCall(QStringLiteral("eager")));
    QCOMPARE(model.timeline().size(), 2);

    AcpProtocol::AcpToolCallUpdate up;
    up.id = QStringLiteral("eager");
    up.parentToolCallId = QStringLiteral("agent");
    model.onToolCallUpdated(up);

    // The entry stays (view indices hold); readers skip it by its parent.
    QCOMPARE(model.timeline().size(), 2);
    QCOMPARE(model.toolCalls().value(QStringLiteral("eager")).parentToolCallId, QStringLiteral("agent"));
    QCOMPARE(model.subagentChildren(QStringLiteral("agent")), QStringList{QStringLiteral("eager")});
    // A repeated update does not list it twice.
    model.onToolCallUpdated(up);
    QCOMPARE(model.subagentChildren(QStringLiteral("agent")).size(), 1);
}

void TestAcpSessionModel::subagentChildrenSurviveRoundTrip()
{
    QTemporaryDir tmp;
    AcpSessionModel a(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());
    a.onToolCallReceived(makeToolCall(QStringLiteral("agent")));
    // Ids out of hash order: the parent's list keeps arrival order.
    for (const char *id : {"z", "m", "a", "q"})
        a.onToolCallReceived(makeToolCall(QLatin1String(id), QStringLiteral("agent")));

    QFile f(tmp.path() + QStringLiteral("/s2.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(a.toHistoryJson()).toJson());
    f.close();

    AcpSessionModel b(QStringLiteral("s2"), QStringLiteral("proj"), tmp.path());
    QCOMPARE(b.timeline().size(), 1);
    QCOMPARE(b.subagentChildren(QStringLiteral("agent")),
             (QStringList{QStringLiteral("z"), QStringLiteral("m"), QStringLiteral("a"), QStringLiteral("q")}));
    QCOMPARE(b.toolCalls().value(QStringLiteral("q")).parentToolCallId, QStringLiteral("agent"));
}

// A restored session resumes the agent's session id, stays out of draft, and
// shows its pickers and slash commands before its agent starts.
void TestAcpSessionModel::restoreFieldsSurviveRoundTrip()
{
    QTemporaryDir tmp;
    AcpSessionModel a(QStringLiteral("r1"), QStringLiteral("proj"), tmp.path());
    AcpProtocol::AcpConfigOption effort;
    effort.id = QStringLiteral("effort");
    effort.name = QStringLiteral("Effort");
    effort.category = QStringLiteral("thought_level");
    effort.currentValue = QStringLiteral("high");
    effort.options = {{QStringLiteral("low"), QStringLiteral("Low")}, {QStringLiteral("high"), QStringLiteral("High")}};
    a.onInitialized({}, {{QStringLiteral("compact"), QStringLiteral("Compact"), QString()}},
                    {{QStringLiteral("default"), QStringLiteral("Default"), QString()},
                     {QStringLiteral("plan"), QStringLiteral("Plan"), QString()}},
                    QStringLiteral("plan"),
                    {{QStringLiteral("opus"), QStringLiteral("Opus"), QString()}}, QStringLiteral("opus"),
                    {effort});
    a.setAcpSessionId(QStringLiteral("ef4daa04"));
    a.appendUserMessage(QStringLiteral("hi"), {});
    a.setUserSent();
    QSignalSpy nameSpy(&a, &AcpSessionModel::sessionNameChanged);
    a.setSessionName(QStringLiteral("  api   worker "));
    QCOMPARE(a.sessionName(), QStringLiteral("api worker"));
    a.setSessionName(QStringLiteral("api worker")); // unchanged: no signal
    QCOMPARE(nameSpy.count(), 1);

    QFile f(tmp.path() + QStringLiteral("/r2.json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(a.toHistoryJson()).toJson());
    f.close();

    AcpSessionModel b(QStringLiteral("r2"), QStringLiteral("proj"), tmp.path());
    QCOMPARE(b.acpSessionId(), QStringLiteral("ef4daa04"));
    QVERIFY(b.userSent());
    QCOMPARE(b.sessionName(), QStringLiteral("api worker"));
    QCOMPARE(b.currentModeId(), QStringLiteral("plan"));
    QCOMPARE(b.availableModes().size(), 2);
    QCOMPARE(b.currentModelId(), QStringLiteral("opus"));
    QCOMPARE(b.availableModels().size(), 1);
    QCOMPARE(b.configOptions().size(), 1);
    QCOMPARE(b.configOptions().at(0).currentValue.toString(), QStringLiteral("high"));
    QCOMPARE(b.configOptions().at(0).options.size(), 2);
    QCOMPARE(b.configOptions().at(0).category, QStringLiteral("thought_level"));
    bool hasCompact = false;
    for (const auto &c : b.availableCommands())
        hasCompact = hasCompact || c.name == QLatin1String("compact");
    QVERIFY(hasCompact);
    QVERIFY(!b.agentAdvertisesGoalCommand());

    // A session that never had a user send stays a draft.
    AcpSessionModel c(QStringLiteral("r3"), QStringLiteral("proj"), tmp.path());
    QVERIFY(!c.userSent());
}

namespace {

// Writes `a`'s history as `<id>.json` and loads it back as a new model.
std::unique_ptr<AcpSessionModel> reload(const AcpSessionModel &a, const QString &dir, const QString &id)
{
    QFile f(dir + QLatin1Char('/') + id + QStringLiteral(".json"));
    if (!f.open(QIODevice::WriteOnly))
        return nullptr;
    f.write(QJsonDocument(a.toHistoryJson()).toJson());
    f.close();
    return std::make_unique<AcpSessionModel>(id, QStringLiteral("proj"), dir);
}

AcpProtocol::AcpConfigOption selectOption(const char *id, const char *current, QStringList values)
{
    AcpProtocol::AcpConfigOption opt;
    opt.id = QString::fromLatin1(id);
    opt.name = opt.id;
    opt.currentValue = QString::fromLatin1(current);
    for (const QString &v : values)
        opt.options.append({v, v});
    return opt;
}

} // namespace

// A new session has no Prewalk choice yet (it takes the agent default); a
// chosen one, Off included, comes back with the restored session.
void TestAcpSessionModel::prewalkModelSurvivesRoundTrip()
{
    QTemporaryDir tmp;
    AcpSessionModel fresh(QStringLiteral("p1"), QStringLiteral("proj"), tmp.path());
    QVERIFY(!fresh.prewalkModel().has_value());
    fresh.appendUserMessage(QStringLiteral("hi"), {});
    auto unset = reload(fresh, tmp.path(), QStringLiteral("p2"));
    QVERIFY(unset);
    QVERIFY(!unset->prewalkModel().has_value());

    AcpSessionModel on(QStringLiteral("p3"), QStringLiteral("proj"), tmp.path());
    on.appendUserMessage(QStringLiteral("hi"), {});
    on.setPrewalkModel(QStringLiteral("sonnet"));
    auto onBack = reload(on, tmp.path(), QStringLiteral("p4"));
    QCOMPARE(onBack->prewalkModel(), std::optional<QString>(QStringLiteral("sonnet")));

    AcpSessionModel off(QStringLiteral("p5"), QStringLiteral("proj"), tmp.path());
    off.appendUserMessage(QStringLiteral("hi"), {});
    off.setPrewalkModel(QString());
    auto offBack = reload(off, tmp.path(), QStringLiteral("p6"));
    QCOMPARE(offBack->prewalkModel(), std::optional<QString>(QString()));
}

// Closing while Prewalk runs the prewalk model must not make it the session's
// model: history records the selection the session goes back to.
void TestAcpSessionModel::historyKeepsSessionSelectionWhilePrewalkSwitched()
{
    QTemporaryDir tmp;
    AcpSessionModel a(QStringLiteral("h1"), QStringLiteral("proj"), tmp.path());
    const QStringList models{QStringLiteral("opus"), QStringLiteral("sonnet")};
    const QStringList efforts{QStringLiteral("low"), QStringLiteral("high")};
    a.onInitialized({}, {}, {}, QString(), {}, QString(),
                    {selectOption("model", "opus", models), selectOption("effort", "high", efforts)});
    a.appendUserMessage(QStringLiteral("hi"), {});
    // Switched: the agent now reports Sonnet, and reset effort.
    a.onConfigOptionsUpdated({selectOption("model", "sonnet", models), selectOption("effort", "low", efforts)});
    a.setSelectionOverride(AcpSessionModel::SelectionOverride{
        QStringLiteral("opus"),
        {{QStringLiteral("model"), QStringLiteral("opus")}, {QStringLiteral("effort"), QStringLiteral("high")}}});

    // The live session is untouched.
    QCOMPARE(a.currentModelId(), QStringLiteral("sonnet"));
    QCOMPARE(a.configOptions().at(0).currentValue.toString(), QStringLiteral("sonnet"));

    auto b = reload(a, tmp.path(), QStringLiteral("h2"));
    QCOMPARE(b->currentModelId(), QStringLiteral("opus"));
    QCOMPARE(b->configOptions().at(0).currentValue.toString(), QStringLiteral("opus"));
    QCOMPARE(b->configOptions().at(1).currentValue.toString(), QStringLiteral("high"));

    // Restored: history follows the live selection again.
    a.setSelectionOverride(std::nullopt);
    auto c = reload(a, tmp.path(), QStringLiteral("h3"));
    QCOMPARE(c->currentModelId(), QStringLiteral("sonnet"));
    QCOMPARE(c->configOptions().at(1).currentValue.toString(), QStringLiteral("low"));
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

void TestAcpSessionModel::ompWriteSynthesizesDiffAndPreservesOnComplete()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpToolCall tc;
    tc.id = QStringLiteral("w1");
    tc.title = QStringLiteral("edit: probe.txt");
    tc.kind = QStringLiteral("edit");
    tc.status = QStringLiteral("pending");
    tc.rawInput.insert(QStringLiteral("path"), QStringLiteral("D:/tmp/test-git/probe.txt"));
    tc.rawInput.insert(QStringLiteral("content"), QStringLiteral("hello\n"));
    model.onToolCallReceived(tc);

    const AcpProtocol::AcpToolCall stored = model.toolCalls().value(QStringLiteral("w1"));
    QCOMPARE(stored.rawInput.value(QStringLiteral("path")).toString(),
             QStringLiteral("D:/tmp/test-git/probe.txt"));
    QCOMPARE(stored.content.size(), 1);
    const QJsonObject diff = stored.content.at(0).toObject();
    QCOMPARE(diff.value(QStringLiteral("type")).toString(), QStringLiteral("diff"));
    QCOMPARE(diff.value(QStringLiteral("path")).toString(),
             QStringLiteral("D:/tmp/test-git/probe.txt"));
    QVERIFY(diff.value(QStringLiteral("oldText")).isNull());
    QCOMPARE(diff.value(QStringLiteral("newText")).toString(), QStringLiteral("hello\n"));

    AcpProtocol::AcpToolCallUpdate up;
    up.id = QStringLiteral("w1");
    up.status = QStringLiteral("completed");
    QJsonObject text;
    text.insert(QStringLiteral("type"), QStringLiteral("text"));
    text.insert(QStringLiteral("text"), QStringLiteral("Wrote probe.txt"));
    QJsonArray incoming;
    incoming.append(text);
    up.content = incoming;
    model.onToolCallUpdated(up);

    const AcpProtocol::AcpToolCall done = model.toolCalls().value(QStringLiteral("w1"));
    QCOMPARE(done.status, QStringLiteral("completed"));
    bool hasDiff = false;
    for (const auto &v : done.content) {
        if (v.toObject().value(QStringLiteral("type")).toString() == QLatin1String("diff")) {
            hasDiff = true;
            QCOMPARE(v.toObject().value(QStringLiteral("newText")).toString(),
                     QStringLiteral("hello\n"));
        }
    }
    QVERIFY(hasDiff);
}

void TestAcpSessionModel::ompHashlineInputInjectsPath()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpToolCall tc;
    tc.id = QStringLiteral("e1");
    tc.title = QStringLiteral("edit");
    tc.kind = QStringLiteral("edit");
    tc.rawInput.insert(QStringLiteral("input"),
                       QStringLiteral("[conflict.txt#A1B2]\nPUT 1:=hello"));
    model.onToolCallReceived(tc);

    QCOMPARE(model.toolCalls().value(QStringLiteral("e1")).rawInput
                 .value(QStringLiteral("path")).toString(),
             QStringLiteral("conflict.txt"));
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

void TestAcpSessionModel::initializedAlwaysExposesHostGoalCommand()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("goal-init"), QStringLiteral("proj"), tmp.path());
    model.onInitialized({}, {}, {}, QString(), {}, QString(), {});
    QCOMPARE(model.availableCommands().size(), 1);
    QCOMPARE(model.availableCommands().first().name, QStringLiteral("goal"));
}

void TestAcpSessionModel::availableCommandsUpdateKeepsHostGoalCommand()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("goal-upd"), QStringLiteral("proj"), tmp.path());
    AcpProtocol::AcpCommandInfo compact;
    compact.name = QStringLiteral("compact");
    model.onAvailableCommandsUpdated({compact});
    QCOMPARE(model.availableCommands().size(), 2);
    QCOMPARE(model.availableCommands().first().name, QStringLiteral("goal"));
    QCOMPARE(model.availableCommands().at(1).name, QStringLiteral("compact"));

    AcpProtocol::AcpCommandInfo agentGoal;
    agentGoal.name = QStringLiteral("/goal");
    agentGoal.description = QStringLiteral("agent copy");
    model.onAvailableCommandsUpdated({agentGoal});
    QCOMPARE(model.availableCommands().size(), 1);
    QCOMPARE(model.availableCommands().first().description, QStringLiteral("agent copy"));
}

void TestAcpSessionModel::hostInjectedGoalIsNotAgentAdvertised()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("goal-host"), QStringLiteral("proj"), tmp.path());
    model.onInitialized({}, {}, {}, QString(), {}, QString(), {});
    QCOMPARE(model.availableCommands().first().name, QStringLiteral("goal"));
    QVERIFY(!model.agentAdvertisesGoalCommand());
}

void TestAcpSessionModel::agentAdvertisedGoalSurvivesHostInject()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("goal-agent"), QStringLiteral("proj"), tmp.path());
    AcpProtocol::AcpCommandInfo agentGoal;
    agentGoal.name = QStringLiteral("goal");
    agentGoal.description = QStringLiteral("agent copy");
    model.onInitialized({}, {agentGoal}, {}, QString(), {}, QString(), {});
    QVERIFY(model.agentAdvertisesGoalCommand());
    QCOMPARE(model.availableCommands().size(), 1);

    AcpProtocol::AcpCommandInfo compact;
    compact.name = QStringLiteral("compact");
    model.onAvailableCommandsUpdated({compact});
    QVERIFY(!model.agentAdvertisesGoalCommand());
    model.onAvailableCommandsUpdated({agentGoal});
    QVERIFY(model.agentAdvertisesGoalCommand());
}


void TestAcpSessionModel::goalJudgeChunk_taggedAndConcatenated()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("gj1"), QStringLiteral("proj"), tmp.path());

    model.appendGoalJudgeChunk(QStringLiteral("Hello "));
    model.appendGoalJudgeChunk(QStringLiteral("judge"));

    QCOMPARE(model.messages().size(), 1);
    QCOMPARE(model.messages().first().role, QStringLiteral("assistant"));
    QVERIFY(model.messages().first().fromGoalJudge);
    QVERIFY(!model.messages().first().fromGoalAgent);
    QCOMPARE(model.messages().first().content.first().text, QStringLiteral("Hello judge"));
}

void TestAcpSessionModel::goalJudgeThought_independentOfCodingAgentStream()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("gj2"), QStringLiteral("proj"), tmp.path());

    model.onMessageChunk(QStringLiteral("coding"));
    model.appendGoalJudgeThoughtChunk(QStringLiteral("weighing"));
    model.onMessageChunk(QStringLiteral(" still"));

    QCOMPARE(model.messages().size(), 2);
    QCOMPARE(model.messages().at(0).role, QStringLiteral("assistant"));
    QVERIFY(!model.messages().at(0).fromGoalJudge);
    QCOMPARE(model.messages().at(0).content.first().text, QStringLiteral("coding still"));
    QCOMPARE(model.messages().at(1).role, QStringLiteral("thought"));
    QVERIFY(model.messages().at(1).fromGoalJudge);
    QCOMPARE(model.messages().at(1).content.first().text, QStringLiteral("weighing"));
}

void TestAcpSessionModel::goalJudgeToolCall_namespacesId()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("gj3"), QStringLiteral("proj"), tmp.path());

    model.onMessageChunk(QStringLiteral("coding"));
    AcpProtocol::AcpToolCall tc;
    tc.id = QStringLiteral("read-1");
    tc.title = QStringLiteral("Read");
    tc.status = QStringLiteral("pending");
    model.upsertGoalJudgeToolCall(tc);
    model.onMessageChunk(QStringLiteral(" still"));

    QVERIFY(model.toolCalls().contains(QStringLiteral("goal-judge:read-1")));
    QVERIFY(!model.toolCalls().contains(QStringLiteral("read-1")));
    QCOMPARE(model.messages().first().content.first().text, QStringLiteral("coding still"));

    AcpProtocol::AcpToolCallUpdate upd;
    upd.id = QStringLiteral("read-1");
    upd.status = QStringLiteral("completed");
    model.applyGoalJudgeToolCallUpdate(upd);
    QCOMPARE(model.toolCalls().value(QStringLiteral("goal-judge:read-1")).status,
             QStringLiteral("completed"));
}

void TestAcpSessionModel::goalJudge_survivesHistoryRoundTrip()
{
    QTemporaryDir tmp;
    AcpHistoryStore store;
    store.setHistoryDir(tmp.path());

    {
        AcpSessionModel a(QStringLiteral("gj-rt"), QStringLiteral("projA"), tmp.path());
        a.setHistoryStore(&store);
        a.beginGoalJudgeTurn(QStringLiteral("Goal · judging 1/1"));
        a.appendGoalJudgeChunk(QStringLiteral("verdict"));
        QSignalSpy spy(&store, &AcpHistoryStore::flushed);
        QVERIFY(spy.wait(2000));
    }

    AcpSessionModel b(QStringLiteral("gj-rt"), QStringLiteral("ignored"), tmp.path());
    QCOMPARE(b.messages().size(), 2);
    QCOMPARE(b.messages().at(0).role, QStringLiteral("system"));
    QCOMPARE(b.messages().at(0).marker, QLatin1String(kAcpMarkerGoalJudging));
    QVERIFY(b.messages().at(1).fromGoalJudge);
    QCOMPARE(b.messages().at(1).content.first().text, QStringLiteral("verdict"));
}

void TestAcpSessionModel::codingAgentPromptEnded_doesNotCloseGoalJudgeStream()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("gj4"), QStringLiteral("proj"), tmp.path());

    model.appendGoalJudgeChunk(QStringLiteral("busy"));
    model.onPromptStarted();
    model.onPromptEnded();
    model.appendGoalJudgeChunk(QStringLiteral(" more"));

    QCOMPARE(model.messages().size(), 1);
    QCOMPARE(model.messages().first().content.first().text, QStringLiteral("busy more"));
}

void TestAcpSessionModel::goalJudgeBeginTurn_appendsJudgingMarker()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("gj5"), QStringLiteral("proj"), tmp.path());

    model.beginGoalJudgeTurn(QStringLiteral("Goal · judging 2/6"));

    QCOMPARE(model.messages().size(), 1);
    QCOMPARE(model.messages().first().role, QStringLiteral("system"));
    QCOMPARE(model.messages().first().marker, QLatin1String(kAcpMarkerGoalJudging));
    QCOMPARE(model.messages().first().content.first().text, QStringLiteral("Goal · judging 2/6"));
}

void TestAcpSessionModel::replaceGoalJudgeText_rewritesBusyRow()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("gj6"), QStringLiteral("proj"), tmp.path());

    model.appendGoalJudgeChunk(QStringLiteral("Goal is judging…"));
    model.replaceGoalJudgeText(QStringLiteral("continue: try again"));

    QCOMPARE(model.messages().size(), 1);
    QVERIFY(model.messages().first().fromGoalJudge);
    QCOMPARE(model.messages().first().content.first().text, QStringLiteral("continue: try again"));
}

void TestAcpSessionModel::blankGoalJudgeChunk_doesNotOpenRow()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("gj7"), QStringLiteral("proj"), tmp.path());

    model.appendGoalJudgeChunk(QStringLiteral(" \n"));
    model.appendGoalJudgeThoughtChunk(QString());
    QCOMPARE(model.messages().size(), 0);
}

void TestAcpSessionModel::removeGoalJudgeTurn_dropsSuffixKeepsPrior()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("gj8"), QStringLiteral("proj"), tmp.path());

    model.appendUserMessage(QStringLiteral("hi"), {});
    model.onMessageChunk(QStringLiteral("hello"));
    model.beginGoalJudgeTurn(QStringLiteral("Goal · judging 1/1"));
    model.appendGoalJudgeChunk(QStringLiteral("verdict body"));
    AcpProtocol::AcpToolCall tc;
    tc.id = QStringLiteral("read-1");
    tc.title = QStringLiteral("Read");
    model.upsertGoalJudgeToolCall(tc);
    model.beginGoalJudgeTurn(QStringLiteral("Goal · judging 1/1"));

    QCOMPARE(model.messages().size(), 4); // user, coding, judging, verdict — no second marker
    QVERIFY(model.toolCalls().contains(QStringLiteral("goal-judge:read-1")));

    QSignalSpy spy(&model, &AcpSessionModel::goalJudgeTurnRemoved);
    model.removeGoalJudgeTurn();

    QCOMPARE(model.messages().size(), 2);
    QCOMPARE(model.messages().at(0).role, QStringLiteral("user"));
    QCOMPARE(model.messages().at(1).role, QStringLiteral("assistant"));
    QVERIFY(!model.messages().at(1).fromGoalJudge);
    QVERIFY(model.toolCalls().isEmpty());
    QCOMPARE(spy.count(), 1);
}

void TestAcpSessionModel::backgroundedMarker_setsToolCallFlag()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());

    AcpProtocol::AcpToolCall tc;
    tc.id = QStringLiteral("call-1");
    tc.title = QStringLiteral("sleep 20");
    tc.status = QStringLiteral("pending");
    model.onToolCallReceived(tc);
    QVERIFY(!model.toolCalls().value(QStringLiteral("call-1")).backgrounded);

    AcpProtocol::AcpToolCallUpdate up;
    up.id = QStringLiteral("call-1");
    up.status = QStringLiteral("completed");
    up.backgrounded = true;
    model.onToolCallUpdated(up);

    const auto stored = model.toolCalls().value(QStringLiteral("call-1"));
    QVERIFY(stored.backgrounded);
    QCOMPARE(stored.status, QStringLiteral("completed"));
}

void TestAcpSessionModel::asyncTasksChanged_flagsLiveToolCallsAndCount()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());
    QSignalSpy countSpy(&model, &AcpSessionModel::backgroundTaskCountChanged);
    QSignalSpy cardSpy(&model, &AcpSessionModel::toolCallAddedOrUpdated);

    AcpProtocol::AcpToolCall tc;
    tc.id = QStringLiteral("call-1");
    tc.status = QStringLiteral("completed");
    model.onToolCallReceived(tc);
    cardSpy.clear();

    // A live task whose tool call is known flags that card as running in
    // the background and counts once.
    model.onAsyncTasksChanged(1, QStringList{QStringLiteral("call-1")});
    QCOMPARE(model.backgroundTaskCount(), 1);
    QVERIFY(model.toolCalls().value(QStringLiteral("call-1")).backgroundRunning);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(cardSpy.count(), 1);

    // Same snapshot again: no churn.
    model.onAsyncTasksChanged(1, QStringList{QStringLiteral("call-1")});
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(cardSpy.count(), 1);

    // A second live task with no known tool call still counts.
    model.onAsyncTasksChanged(2, QStringList{QStringLiteral("call-1")});
    QCOMPARE(model.backgroundTaskCount(), 2);
    QCOMPARE(countSpy.count(), 2);

    // Tasks end: flag clears, count drops to zero.
    model.onAsyncTasksChanged(0, QStringList{});
    QCOMPARE(model.backgroundTaskCount(), 0);
    QVERIFY(!model.toolCalls().value(QStringLiteral("call-1")).backgroundRunning);
    QCOMPARE(countSpy.count(), 3);
}

// claude-agent-acp reports an async Agent call `completed` the moment it is
// launched and publishes no async task for it; the subagent keeps working and
// the adapter holds the turn open until it settles.
void TestAcpSessionModel::asyncSubagent_runsInBackgroundUntilTurnEnds()
{
    QTemporaryDir tmp;
    AcpSessionModel model(QStringLiteral("s1"), QStringLiteral("proj"), tmp.path());
    const QString agentId = QStringLiteral("agent-1");
    auto running = [&]() { return model.toolCalls().value(agentId).backgroundRunning; };

    model.onPromptStarted();
    AcpProtocol::AcpToolCall agent;
    agent.id = agentId;
    agent.title = QStringLiteral("Agent: Research");
    agent.status = QStringLiteral("in_progress");
    agent.rawInput = QJsonObject{{QStringLiteral("description"), QStringLiteral("Research")},
                                 {QStringLiteral("prompt"), QStringLiteral("Look around.")},
                                 {QStringLiteral("subagent_type"), QStringLiteral("general-purpose")},
                                 {QStringLiteral("run_in_background"), true}};
    model.onToolCallReceived(agent);

    AcpProtocol::AcpToolCallUpdate launched;
    launched.id = agentId;
    launched.status = QStringLiteral("completed");
    launched.backgrounded = true;
    model.onToolCallUpdated(launched);
    QVERIFY(running());

    AcpProtocol::AcpToolCall child;
    child.id = QStringLiteral("child-1");
    child.title = QStringLiteral("WebSearch");
    child.status = QStringLiteral("in_progress");
    child.parentToolCallId = agentId;
    model.onToolCallReceived(child);
    QVERIFY(running());

    // The held turn ends once the subagent settled.
    QSignalSpy cardSpy(&model, &AcpSessionModel::toolCallAddedOrUpdated);
    model.onPromptEnded();
    QVERIFY(!running());
    QVERIFY(cardSpy.contains(QVariantList{agentId}));

    // A wake-up turn: the subagent's own calls prove it is alive again.
    model.onPromptStarted();
    AcpProtocol::AcpToolCallUpdate more;
    more.id = QStringLiteral("child-2");
    more.status = QStringLiteral("in_progress");
    more.parentToolCallId = agentId;
    model.onToolCallUpdated(more);
    QVERIFY(running());
    model.onPromptEnded();
    QVERIFY(!running());

    // A backgrounded Bash call is the async-task feed's business, not this.
    model.onPromptStarted();
    AcpProtocol::AcpToolCall bash;
    bash.id = QStringLiteral("bash-1");
    bash.title = QStringLiteral("sleep 20");
    bash.status = QStringLiteral("in_progress");
    model.onToolCallReceived(bash);
    AcpProtocol::AcpToolCallUpdate bashBg;
    bashBg.id = bash.id;
    bashBg.status = QStringLiteral("completed");
    bashBg.backgrounded = true;
    model.onToolCallUpdated(bashBg);
    QVERIFY(!model.toolCalls().value(bash.id).backgroundRunning);
}

QTEST_GUILESS_MAIN(TestAcpSessionModel)
#include "test_acp_session_model.moc"
