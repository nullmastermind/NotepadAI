/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// The Control API end to end (docs/adr/0007): real HTTP requests running
// commands against a test window, the way an external agent drives NotepadAI.

#include <QtTest>
#include <QDockWidget>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QTcpSocket>

#include "control/ControlActivityLog.h"
#include "control/ControlApi.h"
#include "control/ControlEvents.h"

using control::Arg;
using control::ArgType;
using control::Command;
using control::CommandContext;
using control::ControlApi;

namespace {

// Two tabified panels; the name field lives in the one behind.
class Window : public QMainWindow
{
public:
    Window()
    {
        setObjectName(QStringLiteral("MainWindow"));
        resize(800, 600);
        setCentralWidget(new QWidget(this));
        panelA = new QDockWidget(QStringLiteral("Files"), this);
        panelA->setObjectName(QStringLiteral("panelA"));
        panelA->setWidget(new QWidget(panelA));
        panelB = new QDockWidget(QStringLiteral("Settings"), this);
        panelB->setObjectName(QStringLiteral("panelB"));
        name = new QLineEdit(panelB);
        panelB->setWidget(name);
        addDockWidget(Qt::LeftDockWidgetArea, panelA);
        addDockWidget(Qt::LeftDockWidgetArea, panelB);
        tabifyDockWidget(panelA, panelB);
        panelA->raise();
    }

    QDockWidget *panelA = nullptr;
    QDockWidget *panelB = nullptr;
    QLineEdit *name = nullptr;
    QString password;
};

// A tabified panel behind its tab is moved off-screen, not hidden.
bool inFront(const QDockWidget *dock)
{
    const QRect g = dock->geometry();
    return dock->isVisible() && g.right() >= 0 && g.bottom() >= 0;
}

Arg arg(const QString &name, ArgType type, bool required, bool secret = false)
{
    Arg a;
    a.name = name;
    a.type = type;
    a.required = required;
    a.secret = secret;
    a.description = name;
    return a;
}

void registerTestCommands(ControlApi *api, Window *w)
{
    Command set;
    set.name = QStringLiteral("test.setName");
    set.summary = QStringLiteral("Set the name");
    set.args = {arg(QStringLiteral("text"), ArgType::String, true), arg(QStringLiteral("password"), ArgType::String, false, true)};
    set.handler = [w](const CommandContext &ctx) {
        w->name->setText(ctx.str(QStringLiteral("text")));
        w->password = ctx.str(QStringLiteral("password"));
        ctx.reveal(w->name);
        ctx.ok({{QStringLiteral("name"), w->name->text()}});
    };
    api->commands().add(set);

    Command read;
    read.name = QStringLiteral("test.read");
    read.summary = QStringLiteral("Read the name");
    read.changes = false;
    read.args = {arg(QStringLiteral("upper"), ArgType::Bool, false)};
    read.handler = [w](const CommandContext &ctx) {
        const QString n = w->name->text();
        ctx.ok({{QStringLiteral("name"), ctx.flag(QStringLiteral("upper")) ? n.toUpper() : n}});
    };
    api->commands().add(read);

    Command slow;
    slow.name = QStringLiteral("test.slow");
    slow.summary = QStringLiteral("Answer later");
    slow.handler = [w](const CommandContext &ctx) {
        QTimer::singleShot(100, w, [ctx] { ctx.ok({{QStringLiteral("late"), true}}); });
    };
    api->commands().add(slow);

    Command forget;
    forget.name = QStringLiteral("test.forget");
    forget.summary = QStringLiteral("Forget to answer");
    forget.handler = [](const CommandContext &) {};
    api->commands().add(forget);
}

struct Response
{
    bool done = false;
    int status = 0;
    QByteArray body;
    QJsonObject json;
};

} // namespace

class TestControlApi : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void everyRequestNeedsTheToken();
    void aRequestWithoutTheTokenIsRefusedBeforeItsBody();
    void commandsDescribeThemselves();
    void aCommandRunsShowsWhatItChangedAndIsLogged();
    void invalidArgumentsAreRefusedBeforeTheCommandRuns();
    void readsAnswerGetAndAreSilent();
    void anUnknownCommandNamesItsDomain();
    void aCommandMayAnswerLaterButMustAnswer();
    void aDialogTheAppOpensDuringACommandIsCancelledAndNamed();
    void theLogMasksSecrets();
    void waitPollsAReadCommandUntilItMatches();
    void waitForAnEvent();
    void theSkillEndsWithTheCommandReference();
    void theOldUiEndpointsAreGone();
    void screenshotIsAPng();

private:
    Response request(const QByteArray &method, const QString &path, const QJsonObject &body = {},
                     bool withToken = true);

    Window *m_window = nullptr;
    ControlApi *m_api = nullptr;
    QNetworkAccessManager *m_net = nullptr;
};

void TestControlApi::init()
{
    m_window = new Window;
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window));
    m_api = new ControlApi(ControlApi::Host{}, m_window);
    registerTestCommands(m_api, m_window);
    m_api->setEnabled(true);
    QVERIFY2(m_api->isListening(), qPrintable(m_api->lastError()));
    QVERIFY(m_api->tokenIsSessionOnly());
    m_net = new QNetworkAccessManager;
    m_net->setProxy(QNetworkProxy::NoProxy);
}

void TestControlApi::cleanup()
{
    delete m_net;
    delete m_api;
    delete m_window;
}

Response TestControlApi::request(const QByteArray &method, const QString &path, const QJsonObject &body,
                                 bool withToken)
{
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_api->port()).arg(path)));
    if (withToken)
        req.setRawHeader("Authorization", "Bearer " + m_api->token().toLatin1());
    QNetworkReply *reply = nullptr;
    if (method == "GET") {
        reply = m_net->get(req);
    } else {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }
    Response r;
    connect(reply, &QNetworkReply::finished, this, [reply, &r] {
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        r.json = QJsonDocument::fromJson(r.body).object();
        r.done = true;
        reply->deleteLater();
    });
    QElapsedTimer t;
    t.start();
    while (!r.done && t.elapsed() < 15000)
        QTest::qWait(5);
    return r;
}

void TestControlApi::everyRequestNeedsTheToken()
{
    QCOMPARE(request("GET", QStringLiteral("/instance"), {}, false).status, 401);
    QCOMPARE(request("GET", QStringLiteral("/skill"), {}, false).status, 401);
    QCOMPARE(request("POST", QStringLiteral("/commands/test.setName"), {{QStringLiteral("text"), QStringLiteral("x")}}, false)
                 .status,
             401);
    QVERIFY(m_window->name->text().isEmpty());
    const Response r = request("GET", QStringLiteral("/instance"));
    QCOMPARE(r.status, 200);
    QCOMPARE(r.json.value(QStringLiteral("apiVersion")).toInt(), ControlApi::kApiVersion);
    QCOMPARE(r.json.value(QStringLiteral("commands")).toInt(), 4);
}

void TestControlApi::aRequestWithoutTheTokenIsRefusedBeforeItsBody()
{
    // Announces a 4 MB body and sends none of it: the head alone is refused.
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    socket.connectToHost(QHostAddress::LocalHost, m_api->port());
    QVERIFY(socket.waitForConnected(5000));
    socket.write(QByteArray("POST /commands/test.setName HTTP/1.1\r\nHost: 127.0.0.1:") + QByteArray::number(m_api->port())
                 + "\r\nContent-Type: application/json\r\nContent-Length: 4000000\r\n\r\n");
    QByteArray answer;
    QElapsedTimer t;
    t.start();
    while (!answer.contains("\r\n\r\n") && t.elapsed() < 5000) {
        QTest::qWait(5);
        answer += socket.readAll();
    }
    QVERIFY2(answer.startsWith("HTTP/1.1 401"), answer.constData());
}

void TestControlApi::commandsDescribeThemselves()
{
    const Response all = request("GET", QStringLiteral("/commands"));
    QCOMPARE(all.status, 200);
    const QJsonArray commands = all.json.value(QStringLiteral("commands")).toArray();
    QCOMPARE(commands.size(), 4);
    const QJsonObject set = commands.at(0).toObject();
    QCOMPARE(set.value(QStringLiteral("name")).toString(), QStringLiteral("test.setName"));
    QCOMPARE(set.value(QStringLiteral("changes")).toBool(), true);
    const QJsonObject text = set.value(QStringLiteral("args")).toArray().at(0).toObject();
    QCOMPARE(text.value(QStringLiteral("name")).toString(), QStringLiteral("text"));
    QCOMPARE(text.value(QStringLiteral("type")).toString(), QStringLiteral("string"));
    QCOMPARE(text.value(QStringLiteral("required")).toBool(), true);

    QCOMPARE(request("GET", QStringLiteral("/commands?domain=test")).json.value(QStringLiteral("commands")).toArray().size(), 4);
    QCOMPARE(request("GET", QStringLiteral("/commands?domain=files")).json.value(QStringLiteral("commands")).toArray().size(), 0);
}

void TestControlApi::aCommandRunsShowsWhatItChangedAndIsLogged()
{
    QVERIFY(inFront(m_window->panelA));
    QVERIFY(!inFront(m_window->panelB));
    QSignalSpy ran(m_api, &ControlApi::commandRan);

    const Response r = request("POST", QStringLiteral("/commands/test.setName"), {{QStringLiteral("text"), QStringLiteral("Ada")}});
    QCOMPARE(r.status, 200);
    QVERIFY(r.json.value(QStringLiteral("ok")).toBool());
    QCOMPARE(r.json.value(QStringLiteral("name")).toString(), QStringLiteral("Ada"));
    QCOMPARE(m_window->name->text(), QStringLiteral("Ada"));
    // The user sees it: the panel holding the field came to the front.
    QTRY_VERIFY(inFront(m_window->panelB));
    // The indicator names it, the log keeps it with its arguments.
    QCOMPARE(ran.size(), 1);
    // The command and its leading arguments, short enough for the indicator.
    QCOMPARE(ran.at(0).at(0).toString(), QStringLiteral("test.setName \"Ada\""));
    const control::ControlActivityLog *log = m_api->activityLog();
    QCOMPARE(log->size(), 1);
    QCOMPARE(log->at(0).op, QStringLiteral("test.setName"));
    QVERIFY(log->at(0).ok);
    QCOMPARE(log->at(0).args.value(QStringLiteral("text")).toString(), QStringLiteral("Ada"));
}

void TestControlApi::invalidArgumentsAreRefusedBeforeTheCommandRuns()
{
    const Response unknown = request("POST", QStringLiteral("/commands/test.setName"), {{QStringLiteral("txt"), QStringLiteral("x")}});
    QCOMPARE(unknown.status, 400);
    QVERIFY(unknown.json.value(QStringLiteral("error")).toString().contains(QStringLiteral("text*")));
    const Response wrongType = request("POST", QStringLiteral("/commands/test.setName"), {{QStringLiteral("text"), 5}});
    QCOMPARE(wrongType.status, 400);
    QVERIFY(m_window->name->text().isEmpty());
    // Refusals are logged as failed steps.
    QCOMPARE(m_api->activityLog()->size(), 2);
    QVERIFY(!m_api->activityLog()->at(0).ok);

    const Response notJson = request("POST", QStringLiteral("/commands/test.setName"), {});
    QCOMPARE(notJson.status, 400);
}

void TestControlApi::readsAnswerGetAndAreSilent()
{
    m_window->name->setText(QStringLiteral("grace"));
    QSignalSpy ran(m_api, &ControlApi::commandRan);
    const Response get = request("GET", QStringLiteral("/commands/test.read?upper=true"));
    QCOMPARE(get.status, 200);
    QCOMPARE(get.json.value(QStringLiteral("name")).toString(), QStringLiteral("GRACE"));
    const Response post = request("POST", QStringLiteral("/commands/test.read"), {});
    QCOMPARE(post.json.value(QStringLiteral("name")).toString(), QStringLiteral("grace"));
    QCOMPARE(ran.size(), 0);
    QCOMPARE(m_api->activityLog()->size(), 0);
    QVERIFY(inFront(m_window->panelA)); // nothing was brought forward

    // A changing command is never run by GET; the 405 describes it.
    const Response wrong = request("GET", QStringLiteral("/commands/test.setName?text=x"));
    QCOMPARE(wrong.status, 405);
    QCOMPARE(wrong.json.value(QStringLiteral("command")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("test.setName"));
    QCOMPARE(m_window->name->text(), QStringLiteral("grace"));
}

void TestControlApi::anUnknownCommandNamesItsDomain()
{
    const Response r = request("POST", QStringLiteral("/commands/test.nope"), {});
    QCOMPARE(r.status, 404);
    const QString error = r.json.value(QStringLiteral("error")).toString();
    QVERIFY(error.contains(QStringLiteral("test.setName")));
    QVERIFY(error.contains(QStringLiteral("test.read")));
}

void TestControlApi::aCommandMayAnswerLaterButMustAnswer()
{
    const Response later = request("POST", QStringLiteral("/commands/test.slow"), {});
    QCOMPARE(later.status, 200);
    QVERIFY(later.json.value(QStringLiteral("late")).toBool());
    const Response forgot = request("POST", QStringLiteral("/commands/test.forget"), {});
    QCOMPARE(forgot.status, 500);
}

void TestControlApi::aDialogTheAppOpensDuringACommandIsCancelledAndNamed()
{
    // As closing a CSV preview brings forward the text editor of the same
    // file, which asks to reload it: the command must still answer.
    Command prompt;
    prompt.name = QStringLiteral("test.prompt");
    prompt.summary = QStringLiteral("Ask the user something on the way");
    prompt.handler = [w = m_window](const CommandContext &ctx) {
        const auto answer = QMessageBox::question(w, QStringLiteral("Reload"),
                                                  QStringLiteral("data.csv changed on disk. Reload it?"),
                                                  QMessageBox::Yes | QMessageBox::No);
        ctx.ok({{QStringLiteral("reloaded"), answer == QMessageBox::Yes}});
    };
    m_api->commands().add(prompt);

    const Response r = request("POST", QStringLiteral("/commands/test.prompt"), {});
    QCOMPARE(r.status, 200);
    QCOMPARE(r.json.value(QStringLiteral("reloaded")).toBool(), false); // cancelled: nothing changed
    const QJsonArray dialogs = r.json.value(QStringLiteral("dismissedDialogs")).toArray();
    QCOMPARE(dialogs.size(), 1);
    QCOMPARE(dialogs.at(0).toObject().value(QStringLiteral("title")).toString(), QStringLiteral("Reload"));
    QCOMPARE(dialogs.at(0).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("data.csv changed on disk. Reload it?"));
    QVERIFY(!QApplication::activeModalWidget());

    // A command that opens nothing says nothing about dialogs.
    const Response plain = request("POST", QStringLiteral("/commands/test.setName"), {{QStringLiteral("text"), QStringLiteral("x")}});
    QVERIFY(!plain.json.contains(QStringLiteral("dismissedDialogs")));
}

void TestControlApi::theLogMasksSecrets()
{
    QCOMPARE(request("POST", QStringLiteral("/commands/test.setName"),
                     {{QStringLiteral("text"), QStringLiteral("root")}, {QStringLiteral("password"), QStringLiteral("hunter2")}})
                 .status,
             200);
    QCOMPARE(m_window->password, QStringLiteral("hunter2"));
    const Response log = request("GET", QStringLiteral("/log"));
    QVERIFY2(!log.body.contains("hunter2"), log.body.constData());
    const QJsonObject args = log.json.value(QStringLiteral("entries")).toArray().last().toObject().value(QStringLiteral("args")).toObject();
    QCOMPARE(args.value(QStringLiteral("text")).toString(), QStringLiteral("root"));
    QCOMPARE(args.value(QStringLiteral("password")).toString(), QStringLiteral("••••"));
}

void TestControlApi::waitPollsAReadCommandUntilItMatches()
{
    QTimer::singleShot(250, m_window, [this] { m_window->name->setText(QStringLiteral("build done")); });
    const Response r = request("POST", QStringLiteral("/wait"),
                               {{QStringLiteral("command"), QStringLiteral("test.read")},
                                {QStringLiteral("contains"), QJsonObject{{QStringLiteral("name"), QStringLiteral("done")}}},
                                {QStringLiteral("timeout"), 10}});
    QCOMPARE(r.status, 200);
    QCOMPARE(r.json.value(QStringLiteral("result")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("build done"));

    const Response either = request("POST", QStringLiteral("/wait"),
                                    {{QStringLiteral("command"), QStringLiteral("test.read")},
                                     {QStringLiteral("args"), QJsonObject{{QStringLiteral("upper"), true}}},
                                     {QStringLiteral("until"), QJsonObject{{QStringLiteral("name"),
                                                                            QJsonArray{QStringLiteral("X"), QStringLiteral("BUILD DONE")}}}},
                                     {QStringLiteral("timeout"), 5}});
    QCOMPARE(either.status, 200);

    const Response late = request("POST", QStringLiteral("/wait"),
                                  {{QStringLiteral("command"), QStringLiteral("test.read")},
                                   {QStringLiteral("until"), QJsonObject{{QStringLiteral("name"), QStringLiteral("never")}}},
                                   {QStringLiteral("timeout"), 0.3}});
    QCOMPARE(late.status, 408);
    // The last result tells the agent where things stand.
    QCOMPARE(late.json.value(QStringLiteral("last")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("build done"));

    // Only reads can be waited on, and the condition is required.
    QCOMPARE(request("POST", QStringLiteral("/wait"),
                     {{QStringLiteral("command"), QStringLiteral("test.setName")},
                      {QStringLiteral("until"), QJsonObject{{QStringLiteral("name"), QStringLiteral("x")}}}})
                 .status,
             400);
    QCOMPARE(request("POST", QStringLiteral("/wait"), {{QStringLiteral("command"), QStringLiteral("test.read")}}).status, 400);
    QVERIFY(!control::events::wanted()); // nothing left listening
}

void TestControlApi::waitForAnEvent()
{
    QTimer::singleShot(100, this, [] { control::events::publish(QStringLiteral("chat.turnEnded"), {{QStringLiteral("session"), QStringLiteral("b")}}); });
    QTimer::singleShot(200, this, [] { control::events::publish(QStringLiteral("chat.turnEnded"), {{QStringLiteral("session"), QStringLiteral("a")}}); });
    const Response r = request("POST", QStringLiteral("/wait"),
                               {{QStringLiteral("event"), QStringLiteral("chat.*")},
                                {QStringLiteral("match"), QJsonObject{{QStringLiteral("session"), QStringLiteral("a")}}},
                                {QStringLiteral("timeout"), 10}});
    QCOMPARE(r.status, 200);
    QCOMPARE(r.json.value(QStringLiteral("event")).toString(), QStringLiteral("chat.turnEnded"));
    QCOMPARE(r.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("session")).toString(), QStringLiteral("a"));
}

void TestControlApi::theSkillEndsWithTheCommandReference()
{
    const Response r = request("GET", QStringLiteral("/skill"));
    QCOMPARE(r.status, 200);
    QVERIFY(r.body.startsWith("---\nname: notepadai-control"));
    QVERIFY(r.body.contains("## Command reference"));
    QVERIFY(r.body.contains("- `test.read` (read): Read the name"));
    QVERIFY(!r.body.contains(m_api->token().toUtf8()));
}

void TestControlApi::theOldUiEndpointsAreGone()
{
    QCOMPARE(request("GET", QStringLiteral("/ui")).status, 404);
    QCOMPARE(request("POST", QStringLiteral("/ui/click"), {{QStringLiteral("id"), QStringLiteral("x")}}).status, 404);
    QCOMPARE(request("POST", QStringLiteral("/actions/trigger"), {{QStringLiteral("id"), QStringLiteral("x")}}).status, 404);
}

void TestControlApi::screenshotIsAPng()
{
    const Response r = request("GET", QStringLiteral("/screenshot?maxWidth=200"));
    QCOMPARE(r.status, 200);
    QVERIFY(r.body.startsWith("\x89PNG"));
}

QTEST_MAIN(TestControlApi)
#include "test_control_api.moc"
