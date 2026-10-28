/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// The Control API end to end: real HTTP requests against a real window,
// the way an external agent drives NotepadAI.

#include <QtTest>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPushButton>
#include <QSplitter>
#include <QTcpSocket>
#include <QVBoxLayout>

#include "control/ControlApi.h"
#include "control/ControlEvents.h"
#include "control/ControlIds.h"
#include "control/ControlPicker.h"

using control::ControlApi;

namespace {

class Window : public QMainWindow
{
public:
    Window()
    {
        setObjectName(QStringLiteral("MainWindow"));
        QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
        file->setObjectName(QStringLiteral("menuFile"));

        hello = file->addAction(QStringLiteral("Hello"));
        hello->setObjectName(QStringLiteral("actionHello"));
        connect(hello, &QAction::triggered, this, [this] { ++hellos; });

        QAction *disabled = file->addAction(QStringLiteral("Disabled"));
        disabled->setObjectName(QStringLiteral("actionDisabled"));
        disabled->setEnabled(false);

        QAction *pick = file->addAction(QStringLiteral("Open Folder…"));
        pick->setObjectName(QStringLiteral("actionPickFolder"));
        connect(pick, &QAction::triggered, this,
                [this] { picked = control::picker::getExistingDirectory(this, QStringLiteral("Pick")); });

        // Hidden in this state; its visible twin does the same.
        QAction *openOld = file->addAction(QStringLiteral("Open Project…"));
        openOld->setObjectName(QStringLiteral("actionOpenProject"));
        openOld->setVisible(false);
        QAction *openNew = file->addAction(QStringLiteral("New Project Folder…"));
        openNew->setObjectName(QStringLiteral("actionOpenProjectNew"));
        control::ids::setAlternative(openOld, QStringLiteral("actionOpenProjectNew"));

        // A context menu run with exec(), as the app's tree and tab menus are.
        QAction *popup = file->addAction(QStringLiteral("Menu…"));
        popup->setObjectName(QStringLiteral("actionPopup"));
        connect(popup, &QAction::triggered, this, [this] {
            QMenu menu(this);
            menu.setObjectName(QStringLiteral("contextMenu"));
            menu.addAction(QStringLiteral("Rename"))->setObjectName(QStringLiteral("actionRename"));
            menuOpen = true;
            menu.exec(mapToGlobal(QPoint(10, 10)));
            menuOpen = false;
        });

        QAction *settings = file->addAction(QStringLiteral("Settings…"));
        settings->setObjectName(QStringLiteral("actionSettings"));
        connect(settings, &QAction::triggered, this, [this] {
            QDialog dialog(this);
            dialog.setObjectName(QStringLiteral("SettingsDialog"));
            auto *layout = new QVBoxLayout(&dialog);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            buttons->setObjectName(QStringLiteral("buttonBox"));
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            layout->addWidget(buttons);
            dialogOpen = true;
            dialogResult = dialog.exec();
            dialogOpen = false;
            if (dialogResult == QDialog::Accepted) {
                auto *applied = new QWidget(this, Qt::Window);
                applied->setObjectName(QStringLiteral("SettingsApplied"));
                applied->setAttribute(Qt::WA_DeleteOnClose);
                applied->show();
            }
        });

        auto *central = new QWidget(this);
        auto *layout = new QVBoxLayout(central);
        name = new QLineEdit(central);
        name->setObjectName(QStringLiteral("editName"));
        password = new QLineEdit(central);
        password->setObjectName(QStringLiteral("editPassword"));
        password->setEchoMode(QLineEdit::Password);
        status = new QLabel(QStringLiteral("idle"), central);
        status->setObjectName(QStringLiteral("labelStatus"));
        // Stands in for the Control API's own settings and Stop chip.
        auto *chrome = new QWidget(central);
        chrome->setObjectName(QStringLiteral("controlChrome"));
        control::ids::setIgnored(chrome);
        auto *stop = new QPushButton(QStringLiteral("Stop"), chrome);
        stop->setObjectName(QStringLiteral("buttonStop"));
        connect(stop, &QPushButton::clicked, this, [this] { ++stops; });
        (new QVBoxLayout(chrome))->addWidget(stop);
        layout->addWidget(name);
        layout->addWidget(password);
        layout->addWidget(status);
        layout->addWidget(chrome);
        // Two panes with a handle between them.
        auto *split = new QSplitter(Qt::Horizontal, central);
        split->setObjectName(QStringLiteral("split"));
        splitLeft = new QWidget(split);
        splitLeft->setObjectName(QStringLiteral("splitLeft"));
        auto *splitRight = new QWidget(split);
        splitRight->setObjectName(QStringLiteral("splitRight"));
        layout->addWidget(split, 1);
        setCentralWidget(central);

        // Two tabbed panels, like workspace panels.
        for (QDockWidget **panel : {&panelA, &panelB}) {
            *panel = new QDockWidget(panel == &panelA ? QStringLiteral("alpha") : QStringLiteral("beta"), this);
            (*panel)->setObjectName(panel == &panelA ? QStringLiteral("panelA") : QStringLiteral("panelB"));
            (*panel)->setWidget(new QWidget(*panel));
            addDockWidget(Qt::LeftDockWidgetArea, *panel);
        }
        tabifyDockWidget(panelA, panelB);
    }

    QAction *hello = nullptr;
    QLineEdit *name = nullptr;
    QLineEdit *password = nullptr;
    QLabel *status = nullptr;
    QDockWidget *panelA = nullptr;
    QWidget *splitLeft = nullptr;
    QDockWidget *panelB = nullptr;
    int hellos = 0;
    int stops = 0;
    QString picked;
    bool dialogOpen = false;
    bool menuOpen = false;
    int dialogResult = -1;
};

struct Response
{
    bool done = false;
    int status = 0;
    QByteArray body;
    QJsonObject json;
    std::function<void()> then; // runs when answered, possibly inside a nested event loop
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
    void triggerClicksTheActionAndReportsEffects();
    void aUserCouldNotClickItSoNeitherCanTheAgent();
    void theControlApisOwnControlsAreTheUsersAlone();
    void aHiddenActionNamesItsVisibleTwin();
    void aPanelIsSelectedAndClosedLikeItsTabAndTitleBar();
    void aPaneIsResizedLikeDraggingItsEdge();
    void aModalDialogAnswersWhileOpenAndIsAnsweredThroughTheApi();
    void nothingOutsideAnOpenMenuRunsUnderIt();
    void aDirectCommandFillsThePicker();
    void typeThenReadAField();
    void theLogKeepsWhatEachStepWasGiven();
    void waitForTextAndTimeOut();
    void waitForAnEvent();
    void screenshotIsAPng();

private:
    // Sends a request; `out` is filled (and `out->then` runs) when it is answered.
    void send(const QByteArray &method, const QString &path, const QJsonObject &body, Response *out,
              bool withToken = true);
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
    m_api = new ControlApi(ControlApi::Host{}, m_window);
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

void TestControlApi::send(const QByteArray &method, const QString &path, const QJsonObject &body, Response *out,
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
    connect(reply, &QNetworkReply::finished, this, [reply, out] {
        out->status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out->body = reply->readAll();
        out->json = QJsonDocument::fromJson(out->body).object();
        out->done = true;
        reply->deleteLater();
        if (out->then)
            out->then();
    });
}

Response TestControlApi::request(const QByteArray &method, const QString &path,
                                   const QJsonObject &body, bool withToken)
{
    Response r;
    send(method, path, body, &r, withToken);
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
    const Response r = request("GET", QStringLiteral("/instance"));
    QCOMPARE(r.status, 200);
    QVERIFY(r.json.value(QStringLiteral("ok")).toBool());
    QCOMPARE(r.json.value(QStringLiteral("apiVersion")).toInt(), ControlApi::kApiVersion);
}

void TestControlApi::aRequestWithoutTheTokenIsRefusedBeforeItsBody()
{
    // Announces a 4 MB body and sends none of it: the head alone is refused.
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    socket.connectToHost(QHostAddress::LocalHost, m_api->port());
    QVERIFY(socket.waitForConnected(5000));
    socket.write(QByteArray("POST /trigger HTTP/1.1\r\nHost: 127.0.0.1:") + QByteArray::number(m_api->port())
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

void TestControlApi::triggerClicksTheActionAndReportsEffects()
{
    const Response r = request("POST", QStringLiteral("/actions/trigger"), {{QStringLiteral("id"), QStringLiteral("actionHello")}});
    QCOMPARE(r.status, 200);
    QVERIFY(r.json.value(QStringLiteral("completed")).toBool());
    QCOMPARE(m_window->hellos, 1);
    QVERIFY(r.json.value(QStringLiteral("effects")).isObject());

    // The step is in the activity log the user reads.
    const Response log = request("GET", QStringLiteral("/log"));
    const QJsonArray entries = log.json.value(QStringLiteral("entries")).toArray();
    QVERIFY(!entries.isEmpty());
    QVERIFY(QJsonDocument(entries).toJson().contains("actionHello"));
}

void TestControlApi::aUserCouldNotClickItSoNeitherCanTheAgent()
{
    const Response r = request("POST", QStringLiteral("/actions/trigger"), {{QStringLiteral("id"), QStringLiteral("actionDisabled")}});
    QCOMPARE(r.status, 409);
    QVERIFY(!r.json.value(QStringLiteral("error")).toString().isEmpty());
    QCOMPARE(request("POST", QStringLiteral("/ui/click"), {{QStringLiteral("id"), QStringLiteral("MainWindow/nope")}}).status,
             404);
}

void TestControlApi::theControlApisOwnControlsAreTheUsersAlone()
{
    const Response r = request("POST", QStringLiteral("/ui/click"),
                               {{QStringLiteral("id"), QStringLiteral("MainWindow/controlChrome/buttonStop")}});
    QCOMPARE(r.status, 409);
    QCOMPARE(m_window->stops, 0);
    const Response tree = request("GET", QStringLiteral("/ui?root=MainWindow"));
    QCOMPARE(tree.status, 200);
    QVERIFY(!tree.body.contains("buttonStop"));
}

void TestControlApi::aHiddenActionNamesItsVisibleTwin()
{
    const Response r = request("POST", QStringLiteral("/actions/trigger"),
                               {{QStringLiteral("id"), QStringLiteral("actionOpenProject")}});
    QCOMPARE(r.status, 409);
    QVERIFY2(r.json.value(QStringLiteral("error")).toString().contains(QLatin1String("actionOpenProjectNew")),
             r.body.constData());
}

void TestControlApi::aPanelIsSelectedAndClosedLikeItsTabAndTitleBar()
{
    m_window->panelA->raise(); // tabs switch only once the window is shown
    using control::ids::isBehindTab;
    QTRY_VERIFY(!isBehindTab(m_window->panelA) && isBehindTab(m_window->panelB));
    // The panel behind is listed, out of view; the tab bar over the panels is not a control of its own.
    const Response tree = request("GET", QStringLiteral("/ui?root=MainWindow&depth=1"));
    QVERIFY2(!tree.body.contains("QTabBar"), tree.body.constData());
    bool listedOutOfView = false;
    for (const QJsonValue &v : tree.json.value(QStringLiteral("nodes")).toArray().at(0).toObject().value(QStringLiteral("children")).toArray()) {
        const QJsonObject n = v.toObject();
        if (n.value(QStringLiteral("id")).toString() == QLatin1String("MainWindow/panelB"))
            listedOutOfView = !n.value(QStringLiteral("visible")).toBool(true) && !n.value(QStringLiteral("current")).toBool(true);
    }
    QVERIFY2(listedOutOfView, tree.body.constData());

    const Response select = request("POST", QStringLiteral("/ui/select"), {{QStringLiteral("id"), QStringLiteral("MainWindow/panelB")}});
    QCOMPARE(select.status, 200);
    QTRY_VERIFY(isBehindTab(m_window->panelA) && !isBehindTab(m_window->panelB));

    const Response close = request("POST", QStringLiteral("/ui/close"), {{QStringLiteral("id"), QStringLiteral("MainWindow/panelB")}});
    QCOMPARE(close.status, 200);
    QVERIFY(!m_window->panelB->toggleViewAction()->isChecked());
    QCOMPARE(request("POST", QStringLiteral("/ui/close"), {{QStringLiteral("id"), QStringLiteral("MainWindow/panelB")}}).status,
             409);
}

void TestControlApi::aPaneIsResizedLikeDraggingItsEdge()
{
    m_window->resize(1000, 700);
    QTRY_VERIFY(m_window->splitLeft->width() > 0);
    auto resize = [this](const QString &id, const QJsonObject &args) {
        return request("POST", QStringLiteral("/ui/do"),
                       {{QStringLiteral("id"), id}, {QStringLiteral("op"), QStringLiteral("resize")},
                        {QStringLiteral("args"), args}});
    };

    // A splitter child: its handle moves, the neighbour takes the rest.
    const QString left = control::ids::nodeId(m_window->splitLeft).id;
    const Response pane = resize(left, {{QStringLiteral("width"), 150}});
    QCOMPARE(pane.status, 200);
    QTRY_COMPARE(m_window->splitLeft->width(), 150);

    // A main window's panel, even one behind its tab: the group's separator moves.
    QTRY_VERIFY(control::ids::isBehindTab(m_window->panelA));
    const Response panel = resize(QStringLiteral("MainWindow/panelA"), {{QStringLiteral("width"), 300}});
    QCOMPARE(panel.status, 200);
    QTRY_VERIFY2(qAbs(m_window->panelB->width() - 300) <= 4, qPrintable(QString::number(m_window->panelB->width())));

    // The central area has no edge of its own: the panel beside it gives way.
    const Response central = resize(QStringLiteral("MainWindow/editName"), {{QStringLiteral("width"), 600}});
    QCOMPARE(central.status, 200);
    QTRY_VERIFY2(qAbs(m_window->centralWidget()->width() - 600) <= 4,
                 qPrintable(QString::number(m_window->centralWidget()->width())));

    // No edge sets the splitter child's height, and nothing up to the window does either.
    QCOMPARE(resize(left, {{QStringLiteral("height"), 100}}).status, 422);
    QCOMPARE(resize(left, {}).status, 422);
}

void TestControlApi::aModalDialogAnswersWhileOpenAndIsAnsweredThroughTheApi()
{
    // The trigger is answered while exec() runs, so every next step is sent
    // from the previous answer: this test's own code resumes only once the
    // dialog has closed.
    Response trigger;
    Response blocked;
    Response click;
    bool openWhenAnswered = false;
    trigger.then = [&] {
        openWhenAnswered = m_window->dialogOpen;
        // Behind the dialog, the window cannot be clicked.
        send("POST", QStringLiteral("/ui/click"), {{QStringLiteral("id"), QStringLiteral("MainWindow/editName")}},
             &blocked);
    };
    blocked.then = [&] {
        // The agent clicks OK by its role id.
        send("POST", QStringLiteral("/ui/click"),
             {{QStringLiteral("id"), QStringLiteral("SettingsDialog/buttonBox/button:ok")}}, &click);
    };

    // A broken chain would leave exec() running forever: close it so the test fails instead.
    QTimer::singleShot(12000, this, [] {
        if (QWidget *modal = QApplication::activeModalWidget())
            modal->close();
    });
    send("POST", QStringLiteral("/actions/trigger"), {{QStringLiteral("id"), QStringLiteral("actionSettings")}}, &trigger);
    QTRY_VERIFY_WITH_TIMEOUT(click.done && !m_window->dialogOpen, 15000);

    QVERIFY(openWhenAnswered);
    QCOMPARE(trigger.status, 200);
    QVERIFY(!trigger.json.value(QStringLiteral("completed")).toBool());
    const QJsonObject effects = trigger.json.value(QStringLiteral("effects")).toObject();
    QCOMPARE(effects.value(QStringLiteral("modal")).toString(), QStringLiteral("SettingsDialog"));
    QCOMPARE(blocked.status, 409);
    QCOMPARE(click.status, 200);
    QCOMPARE(m_window->dialogResult, int(QDialog::Accepted));
    // OK answers once what follows the dialog's exec() has run, so the
    // window it opens is among its effects.
    const QJsonArray opened = click.json.value(QStringLiteral("effects")).toObject().value(QStringLiteral("opened")).toArray();
    QVERIFY2(QJsonDocument(opened).toJson().contains("SettingsApplied"), QJsonDocument(click.json).toJson().constData());
}

void TestControlApi::nothingOutsideAnOpenMenuRunsUnderIt()
{
    // While the menu's exec() runs, a command elsewhere could close its owner
    // under it; only the menu itself and Escape are answered.
    Response open;
    Response type;
    Response trigger;
    Response escape;
    open.then = [&] {
        send("POST", QStringLiteral("/ui/type"),
             {{QStringLiteral("id"), QStringLiteral("MainWindow/editName")}, {QStringLiteral("text"), QStringLiteral("x")}},
             &type);
    };
    type.then = [&] {
        send("POST", QStringLiteral("/actions/trigger"), {{QStringLiteral("id"), QStringLiteral("actionHello")}}, &trigger);
    };
    trigger.then = [&] { send("POST", QStringLiteral("/ui/key"), {{QStringLiteral("keys"), QStringLiteral("Escape")}}, &escape); };
    QTimer::singleShot(12000, this, [] {
        if (QWidget *popup = QApplication::activePopupWidget())
            popup->close();
    });
    send("POST", QStringLiteral("/actions/trigger"), {{QStringLiteral("id"), QStringLiteral("actionPopup")}}, &open);
    QTRY_VERIFY_WITH_TIMEOUT(escape.done && !m_window->menuOpen, 15000);

    QCOMPARE(open.json.value(QStringLiteral("effects")).toObject().value(QStringLiteral("popup")).toString(),
             QStringLiteral("contextMenu"));
    QCOMPARE(type.status, 409);
    QCOMPARE(trigger.status, 409);
    QCOMPARE(m_window->hellos, 0);
    QVERIFY(m_window->name->text().isEmpty());
    QCOMPARE(escape.status, 200);
}

void TestControlApi::aDirectCommandFillsThePicker()
{
    const Response r = request("POST", QStringLiteral("/actions/trigger"),
                               {{QStringLiteral("id"), QStringLiteral("actionPickFolder")},
                                {QStringLiteral("value"), QStringLiteral("C:/work/app")}});
    QCOMPARE(r.status, 200);
    QVERIFY(r.json.value(QStringLiteral("valueUsed")).toBool());
    QCOMPARE(m_window->picked, QStringLiteral("C:/work/app"));
    QVERIFY(!control::picker::isArmed()); // nothing left for the next picker
}

void TestControlApi::typeThenReadAField()
{
    QCOMPARE(request("POST", QStringLiteral("/ui/type"),
                     {{QStringLiteral("id"), QStringLiteral("MainWindow/editName")}, {QStringLiteral("text"), QStringLiteral("hello")}})
                 .status,
             200);
    QCOMPARE(m_window->name->text(), QStringLiteral("hello"));
    const Response read = request("GET", QStringLiteral("/ui/read?id=MainWindow/editName"));
    QCOMPARE(read.json.value(QStringLiteral("text")).toString(), QStringLiteral("hello"));
}

void TestControlApi::theLogKeepsWhatEachStepWasGiven()
{
    QCOMPARE(request("POST", QStringLiteral("/ui/type"),
                     {{QStringLiteral("id"), QStringLiteral("MainWindow/editName")}, {QStringLiteral("text"), QStringLiteral("hello")}})
                 .status,
             200);
    QCOMPARE(request("POST", QStringLiteral("/ui/type"),
                     {{QStringLiteral("id"), QStringLiteral("MainWindow/editPassword")}, {QStringLiteral("text"), QStringLiteral("hunter2")}})
                 .status,
             200);
    QCOMPARE(m_window->password->text(), QStringLiteral("hunter2"));

    const Response log = request("GET", QStringLiteral("/log"));
    QVERIFY2(!log.body.contains("hunter2"), log.body.constData());
    QJsonObject typedName;
    QJsonObject typedPassword;
    for (const QJsonValue &v : log.json.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject e = v.toObject();
        if (e.value(QStringLiteral("target")).toString() == QLatin1String("MainWindow/editName"))
            typedName = e.value(QStringLiteral("args")).toObject();
        else if (e.value(QStringLiteral("target")).toString() == QLatin1String("MainWindow/editPassword"))
            typedPassword = e.value(QStringLiteral("args")).toObject();
    }
    QCOMPARE(typedName.value(QStringLiteral("text")).toString(), QStringLiteral("hello"));
    QCOMPARE(typedPassword.value(QStringLiteral("text")).toString(), QStringLiteral("••••"));
}

void TestControlApi::waitForTextAndTimeOut()
{
    QTimer::singleShot(150, m_window, [this] { m_window->status->setText(QStringLiteral("build done")); });
    const Response r = request("POST", QStringLiteral("/wait"),
                               {{QStringLiteral("id"), QStringLiteral("MainWindow/labelStatus")},
                                {QStringLiteral("text"), QStringLiteral("done")},
                                {QStringLiteral("timeout"), 10}});
    QCOMPARE(r.status, 200);
    QVERIFY(r.json.value(QStringLiteral("ok")).toBool());

    const Response late = request("POST", QStringLiteral("/wait"),
                                  {{QStringLiteral("id"), QStringLiteral("MainWindow/labelStatus")},
                                   {QStringLiteral("text"), QStringLiteral("never")},
                                   {QStringLiteral("timeout"), 0.3}});
    QCOMPARE(late.status, 408);

    // Any of several states: the answer names the one that held.
    const Response either = request("POST", QStringLiteral("/wait"),
                                    {{QStringLiteral("id"), QStringLiteral("MainWindow/labelStatus")},
                                     {QStringLiteral("state"), QJsonArray{QStringLiteral("gone"), QStringLiteral("visible")}},
                                     {QStringLiteral("timeout"), 5}});
    QCOMPARE(either.status, 200);
    QCOMPARE(either.json.value(QStringLiteral("state")).toString(), QStringLiteral("visible"));
    QCOMPARE(request("POST", QStringLiteral("/wait"),
                     {{QStringLiteral("id"), QStringLiteral("MainWindow/labelStatus")},
                      {QStringLiteral("state"), QJsonArray{QStringLiteral("idle"), QStringLiteral("asleep")}}})
                 .status,
             400);
    QVERIFY(!control::events::wanted()); // nothing left listening
}

void TestControlApi::waitForAnEvent()
{
    QTimer::singleShot(100, this, [] { control::events::publish(QStringLiteral("chat.turnEnded"), {{QStringLiteral("id"), QStringLiteral("chat:b")}}); });
    QTimer::singleShot(200, this, [] { control::events::publish(QStringLiteral("chat.turnEnded"), {{QStringLiteral("id"), QStringLiteral("chat:a")}}); });
    const Response r = request("POST", QStringLiteral("/wait"),
                               {{QStringLiteral("event"), QStringLiteral("chat.*")},
                                {QStringLiteral("match"), QJsonObject{{QStringLiteral("id"), QStringLiteral("chat:a")}}},
                                {QStringLiteral("timeout"), 10}});
    QCOMPARE(r.status, 200);
    QCOMPARE(r.json.value(QStringLiteral("event")).toString(), QStringLiteral("chat.turnEnded"));
    QCOMPARE(r.json.value(QStringLiteral("data")).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("chat:a"));
}

void TestControlApi::screenshotIsAPng()
{
    const Response r = request("GET", QStringLiteral("/screenshot?maxWidth=200"));
    QCOMPARE(r.status, 200);
    QVERIFY(r.body.startsWith("\x89PNG"));
}

QTEST_MAIN(TestControlApi)
#include "test_control_api.moc"
