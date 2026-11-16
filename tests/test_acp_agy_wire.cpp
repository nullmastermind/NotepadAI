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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <memory>

#include "AcpCatchUp.h"
#include "AcpConnection.h"
#include "AcpProtocol.h"
#include "AcpSessionModel.h"
#include "AcpToolCallCard.h"
#include "IAcpProcessChannel.h"

// Antigravity CLI (`agy acp`, 1.5.9) wire, replayed through AcpConnection into
// AcpSessionModel. Frames are the captured ones with the workspace path
// shortened to D:\w; agy sends `locations` / diff paths as "/D:/w/..." once a
// call completes, PascalCase rawInput keys, and both `name` and `title`.

namespace {

class RecordingChannel : public IAcpProcessChannel
{
public:
    QByteArray written;
    explicit RecordingChannel(QObject *parent = nullptr) : IAcpProcessChannel(parent) {}
    void start() override { m_running = true; }
    void write(const QByteArray &bytes) override { written.append(bytes); }
    void kill() override { m_running = false; }
    bool isRunning() const override { return m_running; }
    void pushStdout(const QByteArray &chunk) { emit readyReadStdout(chunk); }

private:
    bool m_running = false;
};

QByteArray update(const char *json)
{
    return QByteArrayLiteral(R"({"jsonrpc":"2.0","method":"session/update","params":{"sessionId":"s1","update":)")
        + QByteArray(json) + QByteArrayLiteral("}}\n");
}

// read_file: in_progress with a Windows path, completed with "/D:/..." paths.
const char *kReadStart = R"({"sessionUpdate":"tool_call","toolCallId":"call_read","name":"read_file","args":{"Path":"D:\\w\\a.txt"},"status":"in_progress","kind":"read","title":"File a.txt","locations":[{"path":"D:\\w\\a.txt"}],"rawInput":{"Path":"D:\\w\\a.txt"}})";
const char *kReadDone = R"({"sessionUpdate":"tool_call_update","toolCallId":"call_read","name":"read_file","args":{"Path":"D:\\w\\a.txt"},"status":"completed","kind":"read","title":"File /D:/w/a.txt","locations":[{"path":"/D:/w/a.txt"}],"rawInput":{"Path":"D:\\w\\a.txt"}})";

const char *kEditStart = R"({"sessionUpdate":"tool_call","toolCallId":"call_edit","name":"edit_file","args":{"Path":"D:\\w\\a.txt","Replacement":"line 2","Target":"line two"},"status":"in_progress","kind":"edit","title":"File a.txt","locations":[{"path":"D:\\w\\a.txt"}],"rawInput":{"Path":"D:\\w\\a.txt","Replacement":"line 2","Target":"line two"}})";
const char *kEditDone = R"({"sessionUpdate":"tool_call_update","toolCallId":"call_edit","name":"edit_file","args":{"Path":"D:\\w\\a.txt","Replacement":"line 2","Target":"line two"},"status":"completed","kind":"edit","title":"File /D:/w/a.txt","locations":[{"path":"/D:/w/a.txt"}],"rawInput":{"Path":"D:\\w\\a.txt","Replacement":"line 2","Target":"line two"},"rawOutput":{},"content":[{"type":"diff","path":"/D:/w/a.txt","newText":"line one\nline 2\n","oldText":"line one\nline two\n"}]})";

const char *kWriteDone = R"({"sessionUpdate":"tool_call_update","toolCallId":"call_write","name":"write_file","args":{"Content":"# Tasks\n","Overwrite":true,"Path":"C:\\b\\task.md"},"status":"completed","kind":"edit","title":"File /C:/b/task.md","locations":[{"path":"/C:/b/task.md"}],"rawInput":{"Content":"# Tasks\n","Overwrite":true,"Path":"C:\\b\\task.md"},"rawOutput":{},"content":[{"type":"diff","path":"/C:/b/task.md","newText":"# Tasks\n"}]})";

const char *kEchoDone = R"({"sessionUpdate":"tool_call_update","toolCallId":"call_echo","name":"run_command","args":{"CommandLine":"echo hello","WaitMsBeforeAsync":5000,"toolSummary":"Echo hello command"},"status":"completed","kind":"execute","title":"echo hello","rawInput":{"CommandLine":"echo hello","WaitMsBeforeAsync":5000,"toolSummary":"Echo hello command"},"rawOutput":{"output":"Command exited with code 0.\n\nCommand output:\nhello\r\n","exit_code":0},"content":[{"type":"content","content":{"type":"text","text":"Command exited with code 0.\n\nCommand output:\nhello\r\n"}}]})";

const char *kSubagent = R"({"sessionUpdate":"tool_call","toolCallId":"call_sub","name":"invoke_subagent","args":{"Prompt":"Count the lines in a.txt and report back.","Role":"Line Counter","TypeName":"research"},"status":"in_progress","kind":"other","title":"Run invoke_subagent","rawInput":{"Prompt":"Count the lines in a.txt and report back.","Role":"Line Counter","TypeName":"research"}})";

// run_command past WaitMsBeforeAsync: agy ends the turn with the call still
// in_progress and sends its completion only when the next prompt starts.
const char *kPingStart = R"({"sessionUpdate":"tool_call","toolCallId":"call_ping","name":"run_command","args":{"CommandLine":"ping -n 8 127.0.0.1","WaitMsBeforeAsync":500,"toolSummary":"Background ping localhost"},"status":"in_progress","kind":"execute","title":"ping -n 8 127.0.0.1","rawInput":{"CommandLine":"ping -n 8 127.0.0.1","WaitMsBeforeAsync":500,"toolSummary":"Background ping localhost"}})";
const char *kPingDone = R"({"sessionUpdate":"tool_call_update","toolCallId":"call_ping","name":"run_command","args":{"CommandLine":"ping -n 8 127.0.0.1","WaitMsBeforeAsync":500,"toolSummary":"Background ping localhost"},"status":"completed","kind":"execute","title":"ping -n 8 127.0.0.1","rawInput":{"CommandLine":"ping -n 8 127.0.0.1","WaitMsBeforeAsync":500,"toolSummary":"Background ping localhost"},"rawOutput":{"output":"Command exited with code 0.\n","exit_code":0},"content":[{"type":"content","content":{"type":"text","text":"Command exited with code 0.\n"}}]})";

const char *kStartedText = R"({"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"Started ping in the background."}})";
const char *kPacketsText = R"j({"sessionUpdate":"agent_message_chunk","content":{"type":"text","text":"Packets: Sent = 8, Received = 8, Lost = 0 (0% loss)"}})j";
const char *kUserPrompt = R"({"sessionUpdate":"user_message_chunk","content":{"type":"text","text":"start ping in the background"}})";
// session/load replay of a finished background run, as agy 1.5.9 orders it.
const char *kPingReplayed = R"({"sessionUpdate":"tool_call","toolCallId":"call_ping","name":"run_command","status":"completed","kind":"execute","title":"ping -n 8 127.0.0.1","rawInput":{"CommandLine":"ping -n 8 127.0.0.1"},"rawOutput":{"output":"Command exited with code 0.\n","exit_code":0}})";
const char *kPingReplayedRunning = R"({"sessionUpdate":"tool_call","toolCallId":"call_ping","name":"run_command","status":"in_progress","kind":"execute","title":"ping -n 8 127.0.0.1","rawInput":{"CommandLine":"ping -n 8 127.0.0.1"}})";
const char *kOldCall = R"({"sessionUpdate":"tool_call","toolCallId":"call_old","name":"read_file","status":"completed","kind":"read","title":"File a.txt"})";

QJsonObject obj(const char *json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

QString kindOf(const QJsonObject &u)
{
    return u.value(QStringLiteral("sessionUpdate")).toString();
}

QString textOf(const QJsonObject &u)
{
    return u.value(QStringLiteral("content")).toObject().value(QStringLiteral("text")).toString();
}

struct Session
{
    QTemporaryDir historyDir;
    AcpConnection conn;
    RecordingChannel *channel = nullptr;
    std::unique_ptr<AcpSessionModel> model;

    Session()
    {
        channel = new RecordingChannel(&conn);
        conn.attachChannelForTest(channel);
        channel->start();
        conn.setSessionIdForTest(QStringLiteral("s1"));
        model = std::make_unique<AcpSessionModel>(QStringLiteral("s1"), QStringLiteral("p"), historyDir.path());
        QObject::connect(&conn, &AcpConnection::messageChunk, model.get(),
                         qOverload<const QString &, const QString &>(&AcpSessionModel::onMessageChunk));
        QObject::connect(&conn, &AcpConnection::toolCallReceived, model.get(), &AcpSessionModel::onToolCallReceived);
        QObject::connect(&conn, &AcpConnection::toolCallUpdated, model.get(), &AcpSessionModel::onToolCallUpdated);
        QObject::connect(&conn, &AcpConnection::promptStarted, model.get(), &AcpSessionModel::onPromptStarted);
        QObject::connect(&conn, &AcpConnection::promptEnded, model.get(), &AcpSessionModel::onPromptEnded);
    }

    int prompt(const QString &text)
    {
        channel->written.clear();
        conn.sendPrompt(text, {});
        return QJsonDocument::fromJson(channel->written.trimmed()).object().value(QStringLiteral("id")).toInt();
    }

    void endPrompt(int id, const char *stopReason = "end_turn")
    {
        channel->pushStdout(QStringLiteral(R"({"jsonrpc":"2.0","result":{"stopReason":"%1","usage":{"inputTokens":18216,"outputTokens":208,"totalTokens":32194}},"id":%2})")
                                .arg(QLatin1String(stopReason)).arg(id).toUtf8() + '\n');
    }

    void push(const char *updateJson) { channel->pushStdout(update(updateJson)); }

    // Agent that can be caught up with (agy advertises both).
    void enableCatchUp()
    {
        AcpProtocol::AcpCapabilities caps;
        caps.sessionLoad = true;
        caps.sessionList = true;
        conn.setCapabilitiesForTest(caps);
        conn.setCatchUpTimingForTest(10, 150);
    }

    // The next request with `method` written after the last one taken.
    QJsonObject takeRequest(const char *method)
    {
        const QList<QByteArray> lines = channel->written.split('\n');
        for (qsizetype i = m_readLines; i < lines.size(); ++i) {
            const QJsonObject o = QJsonDocument::fromJson(lines.at(i)).object();
            if (o.value(QStringLiteral("method")).toString() == QLatin1String(method)) {
                m_readLines = i + 1;
                return o;
            }
        }
        return {};
    }

    // takeRequest, waiting up to 2 s for it to be written.
    QJsonObject waitRequest(const char *method)
    {
        for (int i = 0; i < 400; ++i) {
            const QJsonObject r = takeRequest(method);
            if (!r.isEmpty())
                return r;
            QTest::qWait(5);
        }
        return {};
    }

    void answer(const QJsonObject &request, const QJsonObject &result)
    {
        channel->pushStdout(QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                                      {QStringLiteral("id"), request.value(QStringLiteral("id"))},
                                                      {QStringLiteral("result"), result}})
                                .toJson(QJsonDocument::Compact)
                            + '\n');
    }

    QJsonObject listResult(const QString &updatedAt) const
    {
        return {{QStringLiteral("sessions"),
                 QJsonArray{QJsonObject{{QStringLiteral("sessionId"), QStringLiteral("other")},
                                        {QStringLiteral("updatedAt"), QStringLiteral("2026-11-15T20:06:32Z")}},
                            QJsonObject{{QStringLiteral("sessionId"), QStringLiteral("s1")},
                                        {QStringLiteral("updatedAt"), updatedAt}}}}};
    }

    QStringList assistantTexts() const
    {
        QStringList out;
        for (const auto &m : model->messages()) {
            if (m.role == QLatin1String("assistant") && !m.content.isEmpty())
                out << m.content.first().text;
        }
        return out;
    }

    qsizetype m_readLines = 0;

    AcpProtocol::AcpToolCall call(const char *id) const
    {
        return model->toolCalls().value(QString::fromLatin1(id));
    }
};

QString diffPath(const AcpProtocol::AcpToolCall &tc)
{
    for (const auto &v : tc.content) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() == QLatin1String("diff"))
            return o.value(QStringLiteral("path")).toString();
    }
    return {};
}

} // namespace

class TestAcpAgyWire : public QObject
{
    Q_OBJECT

private slots:
    void toolCards_titledByWhatTheyDid();
    void windowsDrivePaths_loseTheLeadingSlash();
    void invokeSubagent_isAnAgentCard();
    void commandLeftRunningAtTurnEnd_showsAsBackground_untilItsCompletionArrives();
    void cancelledTurn_doesNotMarkOpenCallsAsBackground();
    void catchUp_returnsTheUnseenReplyAndTheCompletion();
    void catchUp_runningCallReplayedBeforeItsPrompt_addsNothing();
    void catchUp_textCutInsideAChunk_andMismatchedText();
    void catchUp_olderTurnsCallsIgnored_newCallsAdded();
    void catchUp_wakeUpWithNoStream_isShownAfterUpdatedAtMoves();
    void catchUp_notArmedWithoutSessionList();
    void catchUp_listFails_commandNoLongerPendingWork();
};

void TestAcpAgyWire::catchUp_returnsTheUnseenReplyAndTheCompletion()
{
    AcpCatchUp::Seen seen;
    seen.turnText = QStringLiteral("Started ping in the background.");
    seen.toolStatus.insert(QStringLiteral("call_ping"), QStringLiteral("in_progress"));
    const auto out = AcpCatchUp::missingUpdates(
        {obj(kUserPrompt), obj(kPingReplayed), obj(kStartedText), obj(kPacketsText)}, seen);
    QCOMPARE(out.size(), 2);
    QCOMPARE(kindOf(out.at(0)), QStringLiteral("tool_call_update"));
    QCOMPARE(out.at(0).value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
    QCOMPARE(textOf(out.at(1)), QStringLiteral("Packets: Sent = 8, Received = 8, Lost = 0 (0% loss)"));
}

void TestAcpAgyWire::catchUp_runningCallReplayedBeforeItsPrompt_addsNothing()
{
    // A load while the command still runs: agy replays the call first.
    AcpCatchUp::Seen seen;
    seen.turnText = QStringLiteral("Started ping in the background.");
    seen.toolStatus.insert(QStringLiteral("call_ping"), QStringLiteral("in_progress"));
    QVERIFY(AcpCatchUp::missingUpdates({obj(kPingReplayedRunning), obj(kUserPrompt), obj(kStartedText)}, seen)
                .isEmpty());
}

void TestAcpAgyWire::catchUp_textCutInsideAChunk_andMismatchedText()
{
    AcpCatchUp::Seen seen;
    seen.turnText = QStringLiteral("Started ping");
    auto out = AcpCatchUp::missingUpdates({obj(kUserPrompt), obj(kStartedText)}, seen);
    QCOMPARE(out.size(), 1);
    QCOMPARE(textOf(out.first()), QStringLiteral(" in the background."));

    seen.turnText = QStringLiteral("Something else entirely");
    QVERIFY(AcpCatchUp::missingUpdates({obj(kUserPrompt), obj(kStartedText), obj(kPacketsText)}, seen).isEmpty());
}

void TestAcpAgyWire::catchUp_olderTurnsCallsIgnored_newCallsAdded()
{
    AcpCatchUp::Seen seen;
    const auto out = AcpCatchUp::missingUpdates({obj(kOldCall), obj(kUserPrompt), obj(kPingReplayed)}, seen);
    QCOMPARE(out.size(), 1);
    QCOMPARE(kindOf(out.first()), QStringLiteral("tool_call"));
    QCOMPARE(out.first().value(QStringLiteral("toolCallId")).toString(), QStringLiteral("call_ping"));
}

void TestAcpAgyWire::catchUp_wakeUpWithNoStream_isShownAfterUpdatedAtMoves()
{
    Session s;
    s.enableCatchUp();
    const int id = s.prompt(QStringLiteral("start ping in the background"));
    s.push(kPingStart);
    s.push(kStartedText);
    s.endPrompt(id);
    QTRY_VERIFY(!s.model->isProcessing());
    QVERIFY(s.conn.catchUpArmed());

    QJsonObject req;
    req = s.waitRequest("session/list");
    QVERIFY(!req.isEmpty());
    s.answer(req, s.listResult(QStringLiteral("2026-11-16T04:29:50Z"))); // baseline
    req = s.waitRequest("session/list");
    QVERIFY(!req.isEmpty());
    s.answer(req, s.listResult(QStringLiteral("2026-11-16T04:29:50Z"))); // nothing yet
    QVERIFY(s.takeRequest("session/load").isEmpty());

    // agy finished the command and replied with no stream open.
    req = s.waitRequest("session/list");
    QVERIFY(!req.isEmpty());
    s.answer(req, s.listResult(QStringLiteral("2026-11-16T04:30:40Z")));
    QJsonObject load;
    load = s.waitRequest("session/load");
    QVERIFY(!load.isEmpty());
    QCOMPARE(load.value(QStringLiteral("params")).toObject().value(QStringLiteral("sessionId")).toString(),
             QStringLiteral("s1"));
    for (const char *u : {kUserPrompt, kPingReplayed, kStartedText, kPacketsText})
        s.push(u);
    s.answer(load, QJsonObject{{QStringLiteral("sessionId"), QStringLiteral("s1")}});

    QCOMPARE(s.call("call_ping").status, QStringLiteral("completed"));
    QVERIFY(!s.call("call_ping").backgroundRunning);
    const QStringList texts = s.assistantTexts();
    QCOMPARE(texts.count(QStringLiteral("Started ping in the background.")), 1);
    QVERIFY2(texts.join(QLatin1Char('\n')).contains(QStringLiteral("0% loss")), qPrintable(texts.join(" | ")));
    QTRY_VERIFY(!s.model->isProcessing());

    // Nothing moves any more: the poll settles.
    for (int i = 0; i < 400 && s.conn.catchUpArmed(); ++i) {
        const QJsonObject list = s.takeRequest("session/list");
        if (!list.isEmpty())
            s.answer(list, s.listResult(QStringLiteral("2026-11-16T04:30:40Z")));
        QTest::qWait(5);
    }
    QVERIFY(!s.conn.catchUpArmed());
    QVERIFY(s.takeRequest("session/load").isEmpty());
}

void TestAcpAgyWire::catchUp_notArmedWithoutSessionList()
{
    Session s; // capabilities as Claude Code: no session/list
    const int id = s.prompt(QStringLiteral("go"));
    s.push(kPingStart);
    s.endPrompt(id);
    QTRY_VERIFY(!s.model->isProcessing());
    QVERIFY(!s.conn.catchUpArmed());
    QVERIFY(s.takeRequest("session/list").isEmpty());
    // Nothing would ever report its end: shown, never waited for.
    QVERIFY(!s.conn.agentWorkPending());
}

void TestAcpAgyWire::catchUp_listFails_commandNoLongerPendingWork()
{
    Session s;
    s.enableCatchUp();
    QSignalSpy idle(&s.conn, &AcpConnection::backgroundIdle);
    const int id = s.prompt(QStringLiteral("start ping in the background"));
    s.push(kPingStart);
    s.endPrompt(id);
    QTRY_VERIFY(!s.model->isProcessing());
    QVERIFY(s.conn.agentWorkPending());

    const QJsonObject req = s.waitRequest("session/list");
    QVERIFY(!req.isEmpty());
    s.channel->pushStdout(QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                                    {QStringLiteral("id"), req.value(QStringLiteral("id"))},
                                                    {QStringLiteral("error"),
                                                     QJsonObject{{QStringLiteral("code"), -32601},
                                                                 {QStringLiteral("message"), QStringLiteral("nope")}}}})
                              .toJson(QJsonDocument::Compact)
                          + '\n');
    QTRY_VERIFY(!s.conn.catchUpArmed());
    QTRY_COMPARE(idle.count(), 1);
    QVERIFY(!s.conn.agentWorkPending());
    QVERIFY(s.call("call_ping").backgroundRunning); // still shown as running
}

void TestAcpAgyWire::toolCards_titledByWhatTheyDid()
{
    Session s;
    const int id = s.prompt(QStringLiteral("go"));
    s.push(kReadStart);
    s.push(kReadDone);
    s.push(kEditStart);
    s.push(kEditDone);
    s.push(kWriteDone);
    s.push(kEchoDone);
    s.endPrompt(id);

    QCOMPARE(AcpToolCallCard::enrichedTitle(s.call("call_read")), QStringLiteral("Read: D:/w/a.txt"));
    QCOMPARE(AcpToolCallCard::enrichedTitle(s.call("call_edit")), QStringLiteral("Edit: D:/w/a.txt"));
    QCOMPARE(AcpToolCallCard::enrichedTitle(s.call("call_write")), QStringLiteral("Write: C:/b/task.md"));
    QCOMPARE(AcpToolCallCard::enrichedTitle(s.call("call_echo")), QStringLiteral("Command: echo hello"));
}

void TestAcpAgyWire::windowsDrivePaths_loseTheLeadingSlash()
{
    Session s;
    const int id = s.prompt(QStringLiteral("go"));
    s.push(kEditStart);
    s.push(kEditDone);
    s.endPrompt(id);

    const auto tc = s.call("call_edit");
    QCOMPARE(tc.rawInput.value(QStringLiteral("path")).toString(), QStringLiteral("D:/w/a.txt"));
    QCOMPARE(diffPath(tc), QStringLiteral("D:/w/a.txt"));

    QCOMPARE(AcpProtocol::stripUriDriveSlash(QStringLiteral("/c:\\x")), QStringLiteral("c:\\x"));
    QCOMPARE(AcpProtocol::stripUriDriveSlash(QStringLiteral("/D:")), QStringLiteral("D:"));
    for (const char *kept : {"/usr/a.txt", "/D:x", "/1:/x", "D:/w", "/", ""})
        QCOMPARE(AcpProtocol::stripUriDriveSlash(QString::fromLatin1(kept)), QString::fromLatin1(kept));
}

void TestAcpAgyWire::invokeSubagent_isAnAgentCard()
{
    Session s;
    const int id = s.prompt(QStringLiteral("go"));
    s.push(kSubagent);
    s.endPrompt(id);

    const auto tc = s.call("call_sub");
    QVERIFY(AcpProtocol::isSubagentToolInput(tc.rawInput));
    QCOMPARE(AcpProtocol::subagentInstructions(tc.rawInput), QStringLiteral("Count the lines in a.txt and report back."));
    QCOMPARE(AcpToolCallCard::enrichedTitle(tc), QStringLiteral("Agent research: Line Counter"));
}

void TestAcpAgyWire::commandLeftRunningAtTurnEnd_showsAsBackground_untilItsCompletionArrives()
{
    Session s;
    const int first = s.prompt(QStringLiteral("start ping in the background"));
    s.push(kPingStart);
    s.endPrompt(first);
    QTRY_VERIFY(!s.model->isProcessing());

    auto tc = s.call("call_ping");
    QCOMPARE(tc.status, QStringLiteral("in_progress"));
    QVERIFY2(tc.backgroundRunning, "a call agy left running at end_turn must read as background work");

    s.prompt(QStringLiteral("is it done?"));
    s.push(kPingDone); // agy flushes it before echoing the new prompt
    tc = s.call("call_ping");
    QCOMPARE(tc.status, QStringLiteral("completed"));
    QVERIFY(!tc.backgroundRunning);
}

void TestAcpAgyWire::cancelledTurn_doesNotMarkOpenCallsAsBackground()
{
    Session s;
    const int id = s.prompt(QStringLiteral("go"));
    s.push(kPingStart);
    s.endPrompt(id, "cancelled");
    QTRY_VERIFY(!s.model->isProcessing());
    QVERIFY(!s.call("call_ping").backgroundRunning);
}

QTEST_MAIN(TestAcpAgyWire)
#include "test_acp_agy_wire.moc"
