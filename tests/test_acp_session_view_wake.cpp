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
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <memory>

#include "AcpAgentDefinition.h"
#include "AcpAgentRegistry.h"
#include "AcpConnection.h"
#include "AcpSessionModel.h"
#include "ApplicationSettings.h"
#include "IAcpProcessChannel.h"
#include "AcpMessageWidget.h"
#include "AcpSessionView.h"
#include "AcpToolCallCard.h"
#include "AcpTurnFoldRow.h"
#include "AiAgentDock.h"
#include "GrillMeFormWidget.h"
#include "widgets/AiDockSessionStrip.h"

// AcpSessionView fed through AcpConnection + AcpSessionModel with frames shaped
// like claude-agent-acp 0.90.0's real wire.

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

QByteArray frame(const QJsonObject &obj)
{
    return QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n';
}

QByteArray update(const QString &kind, QJsonObject extra = {})
{
    extra.insert(QStringLiteral("sessionUpdate"), kind);
    return frame({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                  {QStringLiteral("method"), QStringLiteral("session/update")},
                  {QStringLiteral("params"), QJsonObject{{QStringLiteral("sessionId"), QStringLiteral("s1")},
                                                         {QStringLiteral("update"), extra}}}});
}

QByteArray agentText(const QString &text, const QString &messageId)
{
    return update(QStringLiteral("agent_message_chunk"),
                  {{QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                           {QStringLiteral("text"), text}}},
                   {QStringLiteral("messageId"), messageId}});
}

// The usage_update claude-agent-acp sends at the SDK result of every model
// cycle; `origin` is "human" for a prompt, "task-notification" for a wake-up.
QByteArray cycleResult(const QString &origin)
{
    return update(QStringLiteral("usage_update"),
                  {{QStringLiteral("used"), 48850},
                   {QStringLiteral("size"), 1000000},
                   {QStringLiteral("cost"), QJsonObject{{QStringLiteral("amount"), 0.38},
                                                        {QStringLiteral("currency"), QStringLiteral("USD")}}},
                   {QStringLiteral("_meta"), QJsonObject{{QStringLiteral("_claude/origin"),
                                                          QJsonObject{{QStringLiteral("kind"), origin}}}}}});
}

// Tool-call frames as claude-agent-acp sends them to a client that declares
// the AIR capability (NotepadAI does): only the first frame names the tool,
// later updates carry no `_meta`, and results come as `content` text only.
QByteArray toolStart(const QString &tool, const QString &id)
{
    return update(QStringLiteral("tool_call"),
                  {{QStringLiteral("_meta"), QJsonObject{{QStringLiteral("claudeCode"),
                                                          QJsonObject{{QStringLiteral("toolName"), tool}}}}},
                   {QStringLiteral("toolCallId"), id},
                   {QStringLiteral("name"), tool},
                   {QStringLiteral("title"), tool},
                   {QStringLiteral("kind"), QStringLiteral("other")},
                   {QStringLiteral("status"), QStringLiteral("pending")},
                   {QStringLiteral("content"), QJsonArray{}}});
}

QByteArray toolInput(const QString &id, const QJsonObject &rawInput)
{
    return update(QStringLiteral("tool_call_update"),
                  {{QStringLiteral("toolCallId"), id}, {QStringLiteral("rawInput"), rawInput}});
}

QByteArray toolDone(const QString &id, const QString &output)
{
    return update(QStringLiteral("tool_call_update"),
                  {{QStringLiteral("toolCallId"), id},
                   {QStringLiteral("status"), QStringLiteral("completed")},
                   {QStringLiteral("content"), QJsonArray{QJsonObject{
                        {QStringLiteral("type"), QStringLiteral("content")},
                        {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                                {QStringLiteral("text"), output}}}}}}});
}

// Captured from claude-agent-acp 0.90.0 with NotepadAI's initialize params.
QList<QByteArray> monitorFrames(const QString &taskId)
{
    const QString id = QStringLiteral("toolu_monitor");
    return {toolStart(QStringLiteral("Monitor"), id),
            toolInput(id, {{QStringLiteral("command"), QStringLiteral("for i in 1 2 3 4 5 6; do echo \"tick $i\"; sleep 10; done")},
                           {QStringLiteral("description"), QStringLiteral("test ticks every 10s for 1 minute")},
                           {QStringLiteral("timeout_ms"), 70000}}),
            toolDone(id, QStringLiteral("Monitor started (task %1, expires in 1m 10s unless the source ends "
                                        "first; you get one notice at expiry).").arg(taskId))};
}

void wire(AcpConnection *conn, AcpSessionModel *model)
{
    QObject::connect(conn, &AcpConnection::messageChunk, model,
                     qOverload<const QString &, const QString &>(&AcpSessionModel::onMessageChunk));
    QObject::connect(conn, &AcpConnection::thoughtChunk, model, &AcpSessionModel::onThoughtChunk);
    QObject::connect(conn, &AcpConnection::toolCallReceived, model, &AcpSessionModel::onToolCallReceived);
    QObject::connect(conn, &AcpConnection::toolCallUpdated, model, &AcpSessionModel::onToolCallUpdated);
    QObject::connect(conn, &AcpConnection::promptStarted, model, &AcpSessionModel::onPromptStarted);
    QObject::connect(conn, &AcpConnection::promptEnded, model, &AcpSessionModel::onPromptEnded);
    QObject::connect(conn, &AcpConnection::asyncTasksChanged, model, &AcpSessionModel::onAsyncTasksChanged);
}

// The transcript top to bottom, one line per visible message, card or fold row;
// a fold row reads "fold (live)" while its clock ticks.
QStringList transcript(AcpSessionView &view)
{
    QStringList out;
    QVBoxLayout *layout = nullptr;
    for (auto *scroll : view.findChildren<QScrollArea *>()) {
        if (scroll->widget() && (layout = qobject_cast<QVBoxLayout *>(scroll->widget()->layout())))
            break;
    }
    if (!layout)
        return {QStringLiteral("<no transcript>")};
    for (int i = 0; i < layout->count(); ++i) {
        QWidget *w = layout->itemAt(i)->widget();
        if (!w || w->isHidden())
            continue;
        if (auto *m = qobject_cast<AcpMessageWidget *>(w))
            out << QStringLiteral("%1: %2").arg(m->role(), m->plainText());
        else if (auto *c = qobject_cast<AcpToolCallCard *>(w))
            out << QStringLiteral("card %1").arg(c->id());
        else if (auto *r = qobject_cast<AcpTurnFoldRow *>(w))
            out << (r->isLive() ? QStringLiteral("fold (live)") : QStringLiteral("fold"));
    }
    return out;
}

// The visible status line text naming background work, empty when none shows.
QString statusLine(AcpSessionView &view)
{
    for (auto *label : view.findChildren<QLabel *>()) {
        if (!label->isHidden() && (label->text().contains(QStringLiteral("Monitor ("))
                                   || label->text().contains(QStringLiteral("Waiting background"))))
            return label->text();
    }
    return {};
}

// One session: connection -> model -> view, as AcpAgentManager wires them.
struct Session
{
    QTemporaryDir historyDir;
    AcpConnection conn;
    RecordingChannel *channel = nullptr;
    std::unique_ptr<AcpSessionModel> model;
    std::unique_ptr<AcpSessionView> view;

    // With a registry the session belongs to agent "test-agent", so its
    // selector changes have an agent-defaults entry to write to.
    explicit Session(AcpAgentRegistry *registry = nullptr)
    {
        channel = new RecordingChannel(&conn);
        conn.attachChannelForTest(channel);
        channel->start();
        conn.setSessionIdForTest(QStringLiteral("s1"));
        conn.setBackgroundWakeGraceMsForTest(100);
        if (registry) {
            AcpAgentDefinition agent;
            agent.id = QStringLiteral("test-agent");
            agent.name = QStringLiteral("Test agent");
            conn.setDefinitionForTest(agent);
        }
        model = std::make_unique<AcpSessionModel>(QStringLiteral("s1"), QStringLiteral("p"), historyDir.path());
        wire(&conn, model.get());
        view = std::make_unique<AcpSessionView>(model.get(), &conn, registry);
        view->resize(520, 900);
        view->show();
    }
    ~Session()
    {
        view.reset();
        model.reset();
    }

    // Sends a prompt; returns its JSON-RPC id.
    int prompt(const QString &text)
    {
        model->appendUserMessage(text, {});
        channel->written.clear();
        conn.sendPrompt(text, {});
        return QJsonDocument::fromJson(channel->written.trimmed()).object().value(QStringLiteral("id")).toInt();
    }

    void endPrompt(int id)
    {
        channel->pushStdout(cycleResult(QStringLiteral("human")));
        channel->pushStdout(frame({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                   {QStringLiteral("id"), id},
                                   {QStringLiteral("result"), QJsonObject{{QStringLiteral("stopReason"),
                                                                           QStringLiteral("end_turn")}}}}));
    }

    // A wake-up cycle: the agent acts on an event by itself, then the cycle ends.
    void wakeUp(const QList<QByteArray> &frames)
    {
        for (const QByteArray &f : frames)
            channel->pushStdout(f);
        channel->pushStdout(cycleResult(QStringLiteral("task-notification")));
    }
};

} // namespace

class TestAcpSessionViewWake : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void monitor_keepsTurnOpenUntilStopped();
    void backgroundTask_keepsTurnOpenUntilIdle();
    void wakeUpsWithoutPendingWork_foldAtConfirmedEnd();
    void dock_monitorKeepsSessionWorking_untilStopped();
    void dock_backgroundTaskKeepsSessionWorking_untilItEnds();
    void waiting_permissionRequest_untilAnswered();
    void waiting_questionForm_untilDismissed();
    void waiting_formCancelledForUnattendedSession_doesNotCount();
    void dock_waitingSession_showsDotUntilTheUserCanSeeIt();
    void dock_waiting_beatsWorking_onTheProjectTab();
    void dock_tabbedAway_showsDotOnTheOpenSession();
    void draft_selectorChange_becomesAgentDefault();
    void afterUserSend_selectorChange_staysInThisSession();
    void goalAndHostPrompts_doNotEndTheDraft();
    void agentReportedModel_savedWhileDraft_notAfterUserSend();
    void savedEffort_ofSavedModel_restoredAfterModelSwitch();
    void savedModel_sameProviderAsCurrent_restored();
    void savedModelWithEffortSuffix_restoresItsBaseModel();
    void restart_startsANewDraft();
    void afterUserSend_selectorTooltipsSaySessionOnly();

private:
    QTemporaryDir m_settingsDir;
};

void TestAcpSessionViewWake::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("NotepadNextTest"));
    QCoreApplication::setApplicationName(QStringLiteral("NotepadNextTest_SessionViewWake"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void TestAcpSessionViewWake::init()
{
    ApplicationSettings settings;
    settings.clear();
    settings.sync();
}

// A Monitor wakes the agent on every line its command prints, so the turn that
// armed it is not over: it stays open (row ticking, cards and replies shown,
// "👁 Monitor" on the status line) through every wake-up, and folds as soon as
// the watch is stopped.
void TestAcpSessionViewWake::monitor_keepsTurnOpenUntilStopped()
{
    Session s;
    const int id = s.prompt(QStringLiteral("thử dùng monitor tool 1 phút xem"));
    for (const QByteArray &f : monitorFrames(QStringLiteral("bp6eipu4t")))
        s.channel->pushStdout(f);
    s.channel->pushStdout(agentText(QStringLiteral("armed"), QStringLiteral("msg_0")));
    s.endPrompt(id);
    QTest::qWait(500); // past the 250 ms quiet period that ends the prompt turn
    const QStringList armed{QStringLiteral("user: thử dùng monitor tool 1 phút xem"),
                            QStringLiteral("fold (live)"), QStringLiteral("card toolu_monitor"),
                            QStringLiteral("assistant: armed")};
    QCOMPARE(transcript(*s.view), armed);
    QVERIFY(!s.model->isProcessing()); // the composer is free meanwhile
    QVERIFY2(statusLine(*s.view).contains(QStringLiteral("Monitor (1)")), qPrintable(statusLine(*s.view)));

    // Two events 1 s apart, each answered in a wake-up cycle of its own.
    for (int tick = 1; tick <= 2; ++tick) {
        s.wakeUp({agentText(QStringLiteral("got tick %1").arg(tick), QStringLiteral("msg_%1").arg(tick))});
        QTest::qWait(1000);
    }
    QCOMPARE(transcript(*s.view), (armed + QStringList{QStringLiteral("assistant: got tick 1"),
                                                       QStringLiteral("assistant: got tick 2")}));

    // The agent stops the watch: nothing is left to wake it, the turn folds.
    s.wakeUp({toolStart(QStringLiteral("TaskStop"), QStringLiteral("toolu_stop")),
              toolInput(QStringLiteral("toolu_stop"), {{QStringLiteral("task_id"), QStringLiteral("bp6eipu4t")}}),
              toolDone(QStringLiteral("toolu_stop"), QStringLiteral("{\"task_id\":\"bp6eipu4t\"}")),
              agentText(QStringLiteral("stopped"), QStringLiteral("msg_3"))});
    QTest::qWait(100);
    QCOMPARE(transcript(*s.view), (QStringList{QStringLiteral("user: thử dùng monitor tool 1 phút xem"),
                                               QStringLiteral("fold"), QStringLiteral("assistant: stopped")}));
    QVERIFY(statusLine(*s.view).isEmpty());
}

// A background task outlives the prompt the same way, until it ends with no
// wake-up after it.
void TestAcpSessionViewWake::backgroundTask_keepsTurnOpenUntilIdle()
{
    Session s;
    const int id = s.prompt(QStringLiteral("start the server"));
    s.channel->pushStdout(toolStart(QStringLiteral("Bash"), QStringLiteral("toolu_t1")));
    s.channel->pushStdout(update(QStringLiteral("async_task_spawned"),
                                 {{QStringLiteral("asyncTaskId"), QStringLiteral("t1")},
                                  {QStringLiteral("name"), QStringLiteral("dev server")},
                                  {QStringLiteral("canStop"), true},
                                  {QStringLiteral("toolCallId"), QStringLiteral("toolu_t1")}}));
    s.channel->pushStdout(toolDone(QStringLiteral("toolu_t1"), QStringLiteral("started")));
    s.channel->pushStdout(agentText(QStringLiteral("server is up"), QStringLiteral("msg_0")));
    s.endPrompt(id);
    QTest::qWait(500);
    QCOMPARE(transcript(*s.view), (QStringList{QStringLiteral("user: start the server"),
                                               QStringLiteral("fold (live)"), QStringLiteral("card toolu_t1"),
                                               QStringLiteral("assistant: server is up")}));
    QVERIFY(statusLine(*s.view).contains(QStringLiteral("Waiting background (1)")));

    s.channel->pushStdout(update(QStringLiteral("async_task_state_update"),
                                 {{QStringLiteral("asyncTaskId"), QStringLiteral("t1")},
                                  {QStringLiteral("state"), QStringLiteral("completed")}}));
    QTRY_COMPARE(transcript(*s.view), (QStringList{QStringLiteral("user: start the server"),
                                                   QStringLiteral("fold"),
                                                   QStringLiteral("assistant: server is up")}));
}

// With nothing left to wake it, every wake-up (a scheduled one, a Monitor's
// expiry notice) folds back the moment its cycle ends, even when they come
// faster than the quiet-period guess settles: they once piled up as one bubble
// each under a row that never stopped.
void TestAcpSessionViewWake::wakeUpsWithoutPendingWork_foldAtConfirmedEnd()
{
    Session s;
    const int id = s.prompt(QStringLiteral("check back later"));
    s.channel->pushStdout(agentText(QStringLiteral("ok"), QStringLiteral("msg_0")));
    s.endPrompt(id);
    QTRY_COMPARE(transcript(*s.view), (QStringList{QStringLiteral("user: check back later"),
                                                   QStringLiteral("fold"), QStringLiteral("assistant: ok")}));
    for (int n = 1; n <= 2; ++n) {
        s.wakeUp({agentText(QStringLiteral("woke %1").arg(n), QStringLiteral("msg_%1").arg(n))});
        QTest::qWait(1000);
    }
    QCOMPARE(transcript(*s.view), (QStringList{QStringLiteral("user: check back later"),
                                               QStringLiteral("fold"), QStringLiteral("assistant: woke 2")}));
}

// The session number spins for a Monitor watch too, though no turn is running.
// isSessionBusy keeps its meaning (a turn or goal): scheduled tasks skip on it.
void TestAcpSessionViewWake::dock_monitorKeepsSessionWorking_untilStopped()
{
    Session s;
    AiAgentDock dock(QStringLiteral("s1"), QStringLiteral("claude"), QStringLiteral("/tmp"),
                     s.model.get(), &s.conn, nullptr, nullptr, nullptr);
    const int id = s.prompt(QStringLiteral("watch it"));
    for (const QByteArray &f : monitorFrames(QStringLiteral("bp6eipu4t")))
        s.channel->pushStdout(f);
    s.endPrompt(id);
    QTest::qWait(500);

    QVERIFY(!s.model->isProcessing());
    QVERIFY(!dock.isSessionBusy(QStringLiteral("s1")));
    QVERIFY(dock.isSessionWorking(QStringLiteral("s1")));

    s.wakeUp({toolStart(QStringLiteral("TaskStop"), QStringLiteral("toolu_stop")),
              toolInput(QStringLiteral("toolu_stop"), {{QStringLiteral("task_id"), QStringLiteral("bp6eipu4t")}}),
              toolDone(QStringLiteral("toolu_stop"), QStringLiteral("{\"task_id\":\"bp6eipu4t\"}"))});
    QTRY_VERIFY(!dock.isSessionWorking(QStringLiteral("s1")));
}

void TestAcpSessionViewWake::dock_backgroundTaskKeepsSessionWorking_untilItEnds()
{
    Session s;
    AiAgentDock dock(QStringLiteral("s1"), QStringLiteral("claude"), QStringLiteral("/tmp"),
                     s.model.get(), &s.conn, nullptr, nullptr, nullptr);
    const int id = s.prompt(QStringLiteral("start the server"));
    s.channel->pushStdout(toolStart(QStringLiteral("Bash"), QStringLiteral("toolu_t1")));
    s.channel->pushStdout(update(QStringLiteral("async_task_spawned"),
                                 {{QStringLiteral("asyncTaskId"), QStringLiteral("t1")},
                                  {QStringLiteral("name"), QStringLiteral("dev server")},
                                  {QStringLiteral("canStop"), true},
                                  {QStringLiteral("toolCallId"), QStringLiteral("toolu_t1")}}));
    s.channel->pushStdout(toolDone(QStringLiteral("toolu_t1"), QStringLiteral("started")));
    s.endPrompt(id);
    QTest::qWait(500);
    QVERIFY(!dock.isSessionBusy(QStringLiteral("s1")));
    QVERIFY(dock.isSessionWorking(QStringLiteral("s1")));

    s.channel->pushStdout(update(QStringLiteral("async_task_state_update"),
                                 {{QStringLiteral("asyncTaskId"), QStringLiteral("t1")},
                                  {QStringLiteral("state"), QStringLiteral("completed")}}));
    QTRY_VERIFY(!dock.isSessionWorking(QStringLiteral("s1")));
}

namespace {

// What claude-agent-acp sends when a tool needs the user's go-ahead.
QByteArray permissionRequest(int rpcId)
{
    return frame({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                  {QStringLiteral("id"), rpcId},
                  {QStringLiteral("method"), QStringLiteral("session/request_permission")},
                  {QStringLiteral("params"),
                   QJsonObject{{QStringLiteral("sessionId"), QStringLiteral("s1")},
                               {QStringLiteral("toolCall"),
                                QJsonObject{{QStringLiteral("toolCallId"), QStringLiteral("toolu_perm")},
                                            {QStringLiteral("title"), QStringLiteral("Run ls")}}},
                               {QStringLiteral("options"),
                                QJsonArray{QJsonObject{{QStringLiteral("optionId"), QStringLiteral("allow")},
                                                       {QStringLiteral("name"), QStringLiteral("Allow")},
                                                       {QStringLiteral("kind"), QStringLiteral("allow_once")}},
                                           QJsonObject{{QStringLiteral("optionId"), QStringLiteral("deny")},
                                                       {QStringLiteral("name"), QStringLiteral("Deny")},
                                                       {QStringLiteral("kind"), QStringLiteral("reject_once")}}}}}}});
}

// A completed grill_me ask call: the view turns it into a question form.
QList<QByteArray> grillMeFrames()
{
    const QString id = QStringLiteral("toolu_grill");
    const QString tool = QStringLiteral("mcp__grill_me__ask");
    return {toolStart(tool, id),
            toolInput(id, {{QStringLiteral("questions"),
                            QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("Q1")},
                                                   {QStringLiteral("title"), QStringLiteral("Go ahead?")},
                                                   {QStringLiteral("type"), QStringLiteral("confirm")}}}}}),
            toolDone(id, QStringLiteral("Form shown."))};
}

AiDockSessionStrip *stripOf(AiAgentDock &dock)
{
    return dock.findChild<AiDockSessionStrip *>();
}

QColor iconCenter(const QDockWidget &dock)
{
    return dock.windowIcon().pixmap(8, 8).toImage().pixelColor(4, 4);
}

} // namespace

void TestAcpSessionViewWake::waiting_permissionRequest_untilAnswered()
{
    Session s;
    QSignalSpy changes(s.view.get(), &AcpSessionView::waitingChanged);
    QVERIFY(!s.view->isWaitingForUser());
    s.prompt(QStringLiteral("list files"));

    s.channel->pushStdout(permissionRequest(41));
    QVERIFY(s.view->isWaitingForUser());
    QCOMPARE(changes.count(), 1);
    QCOMPARE(changes.last().at(0).toBool(), true);

    s.conn.respondToPermission(QStringLiteral("41"), QStringLiteral("selected"), QStringLiteral("allow"));
    QVERIFY(!s.view->isWaitingForUser());
    QCOMPARE(changes.count(), 2);
    QCOMPARE(changes.last().at(0).toBool(), false);
}

void TestAcpSessionViewWake::waiting_questionForm_untilDismissed()
{
    Session s;
    QSignalSpy changes(s.view.get(), &AcpSessionView::waitingChanged);
    s.prompt(QStringLiteral("grill me"));
    for (const QByteArray &f : grillMeFrames())
        s.channel->pushStdout(f);

    QVERIFY(s.view->isWaitingForUser());
    QCOMPARE(changes.count(), 1);

    auto *form = s.view->findChild<GrillMeFormWidget *>();
    QVERIFY(form);
    QVERIFY(QMetaObject::invokeMethod(form, "dismissed"));
    QVERIFY(!s.view->isWaitingForUser());
    QCOMPARE(changes.count(), 2);
}

// Nobody is there to answer (goal running / scheduled task): the form is
// cancelled on the spot, so the session is not waiting on anyone.
void TestAcpSessionViewWake::waiting_formCancelledForUnattendedSession_doesNotCount()
{
    Session s;
    s.view->setScheduledUnattended(true);
    QSignalSpy changes(s.view.get(), &AcpSessionView::waitingChanged);
    s.prompt(QStringLiteral("grill me"));
    for (const QByteArray &f : grillMeFrames())
        s.channel->pushStdout(f);

    QVERIFY(s.view->findChild<GrillMeFormWidget *>()); // shown, but cancelled
    QVERIFY(!s.view->isWaitingForUser());
    QCOMPARE(changes.count(), 0);
}

// The red dot is for a user who is looking elsewhere: it hides while the open
// session's dock is on screen, but the session is still waiting.
void TestAcpSessionViewWake::dock_waitingSession_showsDotUntilTheUserCanSeeIt()
{
    Session s;
    AiAgentDock dock(QStringLiteral("s1"), QStringLiteral("claude"), QStringLiteral("/tmp"),
                     s.model.get(), &s.conn, nullptr, nullptr, nullptr);
    s.prompt(QStringLiteral("list files"));
    QVERIFY(!dock.isSessionWaiting(QStringLiteral("s1")));

    s.channel->pushStdout(permissionRequest(41));
    QVERIFY(dock.isSessionWaiting(QStringLiteral("s1")));
    AiDockSessionStrip *strip = nullptr;
    QTRY_VERIFY((strip = stripOf(dock)) != nullptr);
    QTRY_VERIFY(strip->waitingAt(0)); // dock not on screen: the dot shows

    dock.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dock));
    QTRY_VERIFY(!strip->waitingAt(0)); // open session, dock on screen: user sees the prompt
    QVERIFY(dock.isSessionWaiting(QStringLiteral("s1")));

    dock.hide();
    QTRY_VERIFY(strip->waitingAt(0));

    s.conn.respondToPermission(QStringLiteral("41"), QStringLiteral("selected"), QStringLiteral("allow"));
    QVERIFY(!dock.isSessionWaiting(QStringLiteral("s1")));
    QTRY_VERIFY(!strip->waitingAt(0));
}

void TestAcpSessionViewWake::dock_waiting_beatsWorking_onTheProjectTab()
{
    Session s;
    AiAgentDock dock(QStringLiteral("s1"), QStringLiteral("claude"), QStringLiteral("/tmp"),
                     s.model.get(), &s.conn, nullptr, nullptr, nullptr);
    s.prompt(QStringLiteral("list files")); // a turn is running
    QCOMPARE(dock.projectMark(), AiAgentDock::ProjectMark::Working);

    s.channel->pushStdout(permissionRequest(41));
    QCOMPARE(dock.projectMark(), AiAgentDock::ProjectMark::Waiting);
    QCOMPARE(iconCenter(dock), AiDockSessionStrip::attentionColor()); // a red dot

    s.conn.respondToPermission(QStringLiteral("41"), QStringLiteral("selected"), QStringLiteral("allow"));
    QCOMPARE(dock.projectMark(), AiAgentDock::ProjectMark::Working);
}

// Switching to another project's tab does not hide a tabbed QDockWidget: Qt
// parks it off-screen and it stays isVisible(). The open session's dot and the
// project tab's mark must still come back.
void TestAcpSessionViewWake::dock_tabbedAway_showsDotOnTheOpenSession()
{
    Session s;
    QMainWindow win;
    win.setCentralWidget(new QWidget(&win));
    auto *dock = new AiAgentDock(QStringLiteral("s1"), QStringLiteral("claude"), QStringLiteral("/tmp"),
                                 s.model.get(), &s.conn, nullptr, nullptr, nullptr, &win);
    auto *other = new QDockWidget(QStringLiteral("other"), &win);
    other->setObjectName(QStringLiteral("other"));
    other->setWidget(new QLabel(QStringLiteral("other project"), other));
    win.addDockWidget(Qt::RightDockWidgetArea, dock);
    win.addDockWidget(Qt::RightDockWidgetArea, other);
    win.tabifyDockWidget(dock, other);
    win.resize(900, 600);
    win.show();
    QVERIFY(QTest::qWaitForWindowExposed(&win));
    dock->raise();

    s.prompt(QStringLiteral("list files"));
    s.channel->pushStdout(permissionRequest(41));
    QVERIFY(dock->isSessionWaiting(QStringLiteral("s1")));
    AiDockSessionStrip *strip = nullptr;
    QTRY_VERIFY((strip = win.findChild<AiDockSessionStrip *>()) != nullptr);
    QTRY_VERIFY(!strip->waitingAt(0)); // on screen: the user sees the prompt

    other->raise();
    QTRY_VERIFY(strip->waitingAt(0));
    QCOMPARE(dock->projectMark(), AiAgentDock::ProjectMark::Waiting);

    dock->raise();
    QTRY_VERIFY(!strip->waitingAt(0));
    QCOMPARE(dock->projectMark(), AiAgentDock::ProjectMark::None);
}

namespace {

// The model catalogue session/new reports (Opus current unless told otherwise).
void announceModels(AcpSessionModel &model, const QString &current = QStringLiteral("opus"))
{
    model.onInitialized({}, {}, {}, {},
                        {AcpProtocol::AcpModelInfo{QStringLiteral("opus"), QStringLiteral("Opus 5.5"), {}},
                         AcpProtocol::AcpModelInfo{QStringLiteral("sonnet"), QStringLiteral("Sonnet 5.5"), {}}},
                        current, {});
}

QComboBox *modelCombo(AcpSessionView &view)
{
    for (auto *combo : view.findChildren<QComboBox *>()) {
        if (combo->findData(QStringLiteral("sonnet")) >= 0)
            return combo;
    }
    return nullptr;
}

void pick(QComboBox *combo, const QString &id)
{
    combo->setCurrentIndex(combo->findData(id));
}

// Types into the composer and presses Enter, as the user does.
void userSends(AcpSessionView &view, const QString &text)
{
    auto *input = view.findChild<QPlainTextEdit *>();
    QVERIFY(input);
    input->setPlainText(text);
    QTest::keyClick(input, Qt::Key_Return);
}

QString defaultModel(AcpAgentRegistry &registry)
{
    return registry.agentPreference(QStringLiteral("test-agent"), QStringLiteral("model"));
}

} // namespace

// A draft session's choices are the agent defaults: the next new session
// starts with them.
void TestAcpSessionViewWake::draft_selectorChange_becomesAgentDefault()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    Session s(&registry);
    announceModels(*s.model);
    QComboBox *combo = modelCombo(*s.view);
    QVERIFY(combo);

    pick(combo, QStringLiteral("sonnet"));

    QCOMPARE(defaultModel(registry), QStringLiteral("sonnet"));
}

// Opus to plan, Sonnet to implement: the switch belongs to this session only.
void TestAcpSessionViewWake::afterUserSend_selectorChange_staysInThisSession()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    Session s(&registry);
    announceModels(*s.model);
    QComboBox *combo = modelCombo(*s.view);
    QVERIFY(combo);
    pick(combo, QStringLiteral("sonnet")); // chosen before sending: the default
    QCOMPARE(defaultModel(registry), QStringLiteral("sonnet"));

    userSends(*s.view, QStringLiteral("plan it"));
    pick(combo, QStringLiteral("opus"));

    QCOMPARE(defaultModel(registry), QStringLiteral("sonnet"));
    QCOMPARE(combo->currentData().toString(), QStringLiteral("opus")); // this session did switch
}

// A goal's prompts and the host's own are not the user sending anything.
void TestAcpSessionViewWake::goalAndHostPrompts_doNotEndTheDraft()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    Session s(&registry);
    announceModels(*s.model);
    QComboBox *combo = modelCombo(*s.view);
    QVERIFY(combo);

    s.view->enqueueFollowUp(QStringLiteral("/goal ship it"), /*fromGoalAgent=*/true);
    QTRY_VERIFY(s.model->messages().size() >= 1);
    pick(combo, QStringLiteral("sonnet"));

    QCOMPARE(defaultModel(registry), QStringLiteral("sonnet"));
}

// What the agent reports follows the same rule as what the user picks.
void TestAcpSessionViewWake::agentReportedModel_savedWhileDraft_notAfterUserSend()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    Session s(&registry);
    announceModels(*s.model, QStringLiteral("opus"));
    QCOMPARE(defaultModel(registry), QStringLiteral("opus"));

    announceModels(*s.model, QStringLiteral("sonnet")); // the agent switched by itself
    QCOMPARE(defaultModel(registry), QStringLiteral("sonnet"));

    userSends(*s.view, QStringLiteral("go"));
    announceModels(*s.model, QStringLiteral("opus")); // ... and again, mid-conversation
    QCOMPARE(defaultModel(registry), QStringLiteral("sonnet"));
}

namespace {

AcpProtocol::AcpConfigOption configOption(const QString &id, const QString &category,
                                          const QString &current, const QStringList &values)
{
    AcpProtocol::AcpConfigOption opt;
    opt.id = id;
    opt.name = id;
    opt.category = category;
    opt.currentValue = current;
    for (const QString &v : values)
        opt.options.append({v, v});
    return opt;
}

// omp 18.15's catalog: the thinking levels depend on the model, and a model
// switch reports thinking "off" until a level is set.
QList<AcpProtocol::AcpConfigOption> ompOptions(const QString &model, const QString &thinking)
{
    const bool reasoning = model == QLatin1String("local-ccr/composer");
    return {configOption(QStringLiteral("model"), QStringLiteral("model"), model,
                         {QStringLiteral("meta/muse"), QStringLiteral("local-ccr/composer"),
                          QStringLiteral("local-ccr/abliterated-model-large-v2-1")}),
            configOption(QStringLiteral("thinking"), QStringLiteral("thought_level"), thinking,
                         reasoning ? QStringList{QStringLiteral("off"), QStringLiteral("auto"),
                                                 QStringLiteral("high"), QStringLiteral("xhigh")}
                                   : QStringList{QStringLiteral("off"), QStringLiteral("auto")})};
}

// The `session/set_config_option` values the view sent, as "configId=value".
QStringList configSets(const QByteArray &written)
{
    QStringList out;
    for (const QByteArray &line : written.split('\n')) {
        const QJsonObject msg = QJsonDocument::fromJson(line).object();
        if (msg.value(QStringLiteral("method")).toString() != QLatin1String("session/set_config_option"))
            continue;
        const QJsonObject p = msg.value(QStringLiteral("params")).toObject();
        out << p.value(QStringLiteral("configId")).toString() + QLatin1Char('=')
                   + p.value(QStringLiteral("value")).toString();
    }
    return out;
}

} // namespace

// The saved effort is only offered by the saved model: restoring the model
// must not save the "off" the agent reports for it, and the effort follows.
void TestAcpSessionViewWake::savedEffort_ofSavedModel_restoredAfterModelSwitch()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    const QString agent = QStringLiteral("test-agent");
    registry.setAgentPreference(agent, QStringLiteral("model"), QStringLiteral("local-ccr/composer"));
    registry.setAgentPreference(agent, QStringLiteral("thinking"), QStringLiteral("high"));
    Session s(&registry);
    s.channel->written.clear();

    s.model->onInitialized({}, {}, {}, {}, {}, {}, ompOptions(QStringLiteral("meta/muse"), QStringLiteral("off")));
    QCOMPARE(configSets(s.channel->written), QStringList{QStringLiteral("model=local-ccr/composer")});

    s.model->onConfigOptionsUpdated(ompOptions(QStringLiteral("local-ccr/composer"), QStringLiteral("off")));

    QCOMPARE(registry.agentPreference(agent, QStringLiteral("thinking")), QStringLiteral("high"));
    QCOMPARE(configSets(s.channel->written),
             (QStringList{QStringLiteral("model=local-ccr/composer"), QStringLiteral("thinking=high")}));
}

// pi-acp and omp model ids are "provider/model"; pi-acp also reports the
// current one in its models block. A saved model from the same provider as the
// agent's default is still another model.
void TestAcpSessionViewWake::savedModel_sameProviderAsCurrent_restored()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    registry.setAgentPreference(QStringLiteral("test-agent"), QStringLiteral("model"),
                                QStringLiteral("local-ccr/composer"));
    Session s(&registry);
    s.channel->written.clear();

    const QString current = QStringLiteral("local-ccr/abliterated-model-large-v2-1");
    QList<AcpProtocol::AcpModelInfo> models;
    for (const QString &id : {QStringLiteral("meta/muse"), QStringLiteral("local-ccr/composer"), current})
        models.append({id, id, {}});
    s.model->onInitialized({}, {}, {}, {}, models, current, ompOptions(current, QStringLiteral("off")));

    QCOMPARE(configSets(s.channel->written), QStringList{QStringLiteral("model=local-ccr/composer")});
}

// Codex lists base models as choices; an id saved as "model/effort" names the
// base model, which is restored only when it is not already current.
void TestAcpSessionViewWake::savedModelWithEffortSuffix_restoresItsBaseModel()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    registry.setAgentPreference(QStringLiteral("test-agent"), QStringLiteral("model"),
                                QStringLiteral("gpt-5.5/high"));
    const QStringList models{QStringLiteral("gpt-5.5"), QStringLiteral("gpt-6")};
    {
        Session s(&registry);
        s.channel->written.clear();
        s.model->onInitialized({}, {}, {}, {}, {}, {},
                               {configOption(QStringLiteral("model"), QStringLiteral("model"),
                                             QStringLiteral("gpt-5.5"), models)});
        QCOMPARE(configSets(s.channel->written), QStringList{});
    }
    registry.setAgentPreference(QStringLiteral("test-agent"), QStringLiteral("model"),
                                QStringLiteral("gpt-5.5/high"));
    Session s(&registry);
    s.channel->written.clear();
    s.model->onInitialized({}, {}, {}, {}, {}, {},
                           {configOption(QStringLiteral("model"), QStringLiteral("model"),
                                         QStringLiteral("gpt-6"), models)});
    QCOMPARE(configSets(s.channel->written), QStringList{QStringLiteral("model=gpt-5.5")});
}

// Restarting a session discards its conversation: it is a draft again.
void TestAcpSessionViewWake::restart_startsANewDraft()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    Session s(&registry);
    announceModels(*s.model);
    userSends(*s.view, QStringLiteral("go"));
    QComboBox *combo = modelCombo(*s.view);
    QVERIFY(combo);
    pick(combo, QStringLiteral("sonnet"));
    QVERIFY(defaultModel(registry) != QStringLiteral("sonnet"));

    Session fresh(&registry);
    s.view->rebind(fresh.model.get(), &fresh.conn);
    announceModels(*fresh.model);
    combo = modelCombo(*s.view);
    QVERIFY(combo);
    pick(combo, QStringLiteral("sonnet"));

    QCOMPARE(defaultModel(registry), QStringLiteral("sonnet"));
}

void TestAcpSessionViewWake::afterUserSend_selectorTooltipsSaySessionOnly()
{
    ApplicationSettings settings;
    AcpAgentRegistry registry(&settings);
    Session s(&registry);
    announceModels(*s.model);
    QComboBox *combo = modelCombo(*s.view);
    QVERIFY(combo);
    const QString draftTip = combo->toolTip();
    QVERIFY(!draftTip.contains(QStringLiteral("this session only")));

    userSends(*s.view, QStringLiteral("go"));

    QVERIFY(combo->toolTip().contains(QStringLiteral("Applies to this session only")));
    QVERIFY(combo->toolTip().contains(draftTip)); // still says what the selector is
}

QTEST_MAIN(TestAcpSessionViewWake)
#include "test_acp_session_view_wake.moc"
