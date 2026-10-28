/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlApi.h"

#include "ApplicationSettings.h"
#include "ControlActivityLog.h"
#include "ControlAuth.h"
#include "ControlEvents.h"
#include "ControlFeedback.h"
#include "ControlScreenshot.h"
#include "ControlServer.h"

#include "DockManager.h"
#include "DockWidget.h"

#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace control {

namespace {

// NOLINTBEGIN(bugprone-throwing-static-initialization): ApplicationSetting stores std::function (file-scope idiom).
const ApplicationSetting<bool> kEnabled{"ControlApi/Enabled", false};
const ApplicationSetting<int> kPort{"ControlApi/Port", int(ControlApi::kDefaultPort)};
const ApplicationSetting<bool> kAllowLan{"ControlApi/AllowLan", false};
const ApplicationSetting<QString> kLanAddress{"ControlApi/LanAddress", QString()};
// NOLINTEND(bugprone-throwing-static-initialization)

// Object name of a pending /wait (a child of the API, deleted on Stop).
constexpr char kPendingWait[] = "controlPendingWait";

QByteArray routeKey(const QByteArray &method, const QByteArray &path)
{
    return method + ' ' + path;
}

} // namespace

// --- Reply -------------------------------------------------------------------

Reply::Reply(ControlServer *server, quint64 id)
    : m_server(server)
    , m_id(id)
    , m_sent(std::make_shared<bool>(false))
{
}

void Reply::bytes(int status, const QByteArray &contentType, const QByteArray &body) const
{
    if (!m_sent || *m_sent)
        return;
    *m_sent = true;
    if (m_server)
        m_server->reply(m_id, status, contentType, body);
}

void Reply::json(int status, const QJsonObject &body) const
{
    if (!m_sent || *m_sent)
        return;
    bytes(status, "application/json", QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void Reply::error(int status, const QString &message, const QJsonObject &extra) const
{
    QJsonObject body = extra;
    body.insert(QStringLiteral("ok"), false);
    body.insert(QStringLiteral("error"), message);
    json(status, body);
}

// --- ControlApi --------------------------------------------------------------

ControlApi::ControlApi(const Host &host, QMainWindow *window, QObject *parent)
    : QObject(parent)
    , m_host(host)
    , m_window(window)
    , m_log(new ControlActivityLog(this))
    , m_feedback(new ControlFeedback(this))
    , m_events(new ControlEvents(this))
{
    registerRoutes();
}

ControlApi::~ControlApi()
{
    stopServer();
}

QMainWindow *ControlApi::window() const
{
    return m_window;
}

bool ControlApi::isEnabled() const
{
    return m_host.settings && m_host.settings->get(kEnabled);
}

void ControlApi::setEnabled(bool on)
{
    if (m_host.settings)
        m_host.settings->set(kEnabled, on);
    if (on)
        restart();
    else
        stopServer();
    emit stateChanged();
}

void ControlApi::startFromSettings()
{
    if (isEnabled()) {
        restart();
        emit stateChanged();
    }
}

quint16 ControlApi::configuredPort() const
{
    const int p = m_host.settings ? m_host.settings->get(kPort) : int(kDefaultPort);
    return (p > 0 && p <= 65535) ? quint16(p) : kDefaultPort;
}

void ControlApi::setConfiguredPort(quint16 port)
{
    if (port == configuredPort())
        return;
    if (m_host.settings)
        m_host.settings->set(kPort, int(port));
    if (isListening())
        restart();
    emit stateChanged();
}

bool ControlApi::allowLan() const
{
    return m_host.settings && m_host.settings->get(kAllowLan);
}

void ControlApi::setAllowLan(bool on)
{
    if (on == allowLan())
        return;
    if (m_host.settings)
        m_host.settings->set(kAllowLan, on);
    if (isListening())
        restart();
    emit stateChanged();
}

QString ControlApi::lanAddress() const
{
    QString saved = m_host.settings ? m_host.settings->get(kLanAddress) : QString();
    const QStringList available = lanAddresses();
    if (available.contains(saved))
        return saved;
    return available.isEmpty() ? QString() : available.first();
}

void ControlApi::setLanAddress(const QString &address)
{
    if (m_host.settings)
        m_host.settings->set(kLanAddress, address);
    emit stateChanged();
}

QStringList ControlApi::lanAddresses()
{
    QStringList out;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || (flags & QNetworkInterface::IsLoopBack))
            continue;
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &e : entries) {
            const QHostAddress a = e.ip();
            if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLinkLocal())
                out.append(a.toString());
        }
    }
    return out;
}

bool ControlApi::isListening() const
{
    return m_server && m_server->isListening();
}

quint16 ControlApi::port() const
{
    return m_server ? m_server->port() : 0;
}

QString ControlApi::baseUrl() const
{
    const quint16 p = isListening() ? port() : configuredPort();
    const QString host = allowLan() && !lanAddress().isEmpty() ? lanAddress() : QStringLiteral("127.0.0.1");
    return QStringLiteral("http://%1:%2").arg(host).arg(p);
}

QString ControlApi::token()
{
    if (!m_token.isEmpty())
        return m_token;
    const std::optional<QString> stored = m_host.loadToken ? m_host.loadToken() : std::nullopt;
    if (stored && !stored->isEmpty()) {
        m_token = *stored;
        m_tokenSessionOnly = false;
        return m_token;
    }
    m_token = auth::generateToken();
    // A store that could not be read is not overwritten: the token lives for this session.
    m_tokenSessionOnly = !stored || !m_host.saveToken || !m_host.saveToken(m_token);
    return m_token;
}

void ControlApi::regenerateToken()
{
    m_token = auth::generateToken();
    m_tokenSessionOnly = !m_host.saveToken || !m_host.saveToken(m_token);
    if (m_server)
        m_server->setToken(m_token);
    logAction(QStringLiteral("token"), QString(), tr("Token regenerated by the user"));
    emit stateChanged();
}

QString ControlApi::connectionPrompt()
{
    return QStringLiteral(
               "NotepadAI (the code editor on this machine) exposes a Control API: you operate it by calling "
               "commands (open a project or file, edit text, run terminals, drive AI agent sessions, git, "
               "settings), and its window shows the user everything those commands do.\n\n"
               "NOTEPADAI_URL=%1\n"
               "NOTEPADAI_TOKEN=%2\n\n"
               "1. Fetch the skill:\n"
               "   curl -s -H \"Authorization: Bearer $NOTEPADAI_TOKEN\" \"$NOTEPADAI_URL/skill\"\n"
               "2. Save it unchanged as a skill named notepadai-control, wherever you keep your skills. "
               "Never write the token into the skill or any file in a repository; keep NOTEPADAI_URL and "
               "NOTEPADAI_TOKEN as environment values.\n"
               "3. Use the skill to operate NotepadAI. If a request returns 401, ask the user for a new "
               "prompt (the token was regenerated). Check GET $NOTEPADAI_URL/skill/version to know when "
               "to fetch the skill again.\n")
        .arg(baseUrl(), token());
}

QByteArray ControlApi::skillGuide()
{
    QFile f(QStringLiteral(":/control/SKILL.md"));
    if (!f.open(QIODevice::ReadOnly))
        return QByteArrayLiteral("# notepadai-control\n\nSkill resource missing from this build.\n");
    // LF whatever the checkout's line endings (autocrlf builds embed CRLF):
    // agents parse the front matter.
    return f.readAll().replace("\r\n", "\n");
}

QByteArray ControlApi::skill() const
{
    return skillGuide() + '\n' + m_commands.reference();
}

QJsonObject ControlApi::versionJson()
{
    return {{QStringLiteral("apiVersion"), kApiVersion},
            {QStringLiteral("skillVersion"), kSkillVersion},
            {QStringLiteral("app"), QCoreApplication::applicationName()},
            {QStringLiteral("appVersion"), QCoreApplication::applicationVersion()}};
}

void ControlApi::restart()
{
    stopServer();
    m_server = new ControlServer(this);
    connect(m_server, &ControlServer::requestReceived, this, &ControlApi::onRequest);
    connect(m_server, &ControlServer::subscriberCountChanged, m_events, &ControlEvents::setSubscribers);
    connect(m_events, &ControlEvents::frameReady, m_server, &ControlServer::publishEvent);

    ControlServer::Config config;
    config.port = configuredPort();
    config.token = token();
    config.skill = skill();
    config.skillVersion = QJsonDocument(versionJson()).toJson(QJsonDocument::Compact);
    if (allowLan()) {
        config.bindAddress = QHostAddress::AnyIPv4;
        config.hostAddresses = lanAddresses();
    }
    QString error;
    if (!m_server->start(config, &error)) {
        m_lastError = error;
        delete m_server;
        m_server = nullptr;
        return;
    }
    m_lastError.clear();
}

void ControlApi::stopServer()
{
    if (!m_server)
        return;
    ControlServer *server = m_server;
    m_server = nullptr;
    // Stop: pending waits end now, unanswered (their connections are gone).
    m_events->setSubscribers(0);
    m_events->clearWaiters();
    const QList<QObject *> waits = findChildren<QObject *>(QLatin1String(kPendingWait), Qt::FindDirectChildrenOnly);
    qDeleteAll(waits);
    server->stop();
    delete server;
}

void ControlApi::route(const QByteArray &method, const QByteArray &path, Handler handler)
{
    m_routes.emplace(routeKey(method, path), std::move(handler));
}

void ControlApi::logAction(const QString &op, const QString &target, const QString &label, bool ok,
                           const QString &detail, const QJsonObject &args)
{
    ControlActivityLog::Entry e;
    e.time = QDateTime::currentDateTime();
    e.op = op;
    e.target = target;
    e.label = label;
    e.ok = ok;
    e.detail = detail;
    e.args = args;
    if (events::wanted()) {
        QJsonObject data{{QStringLiteral("op"), op}, {QStringLiteral("label"), label}, {QStringLiteral("ok"), ok}};
        if (!target.isEmpty())
            data.insert(QStringLiteral("id"), target);
        if (!detail.isEmpty())
            data.insert(QStringLiteral("detail"), detail);
        if (!args.isEmpty())
            data.insert(QStringLiteral("args"), args);
        events::publish(QStringLiteral("action"), data);
    }
    m_log->append(std::move(e));
}

namespace {

// A log line's copy of a value: bounded, so a pasted file cannot fill the log.
QJsonValue loggedValue(const QJsonValue &v)
{
    constexpr qsizetype kMaxChars = 120;
    if (v.isString()) {
        const QString s = v.toString();
        return s.size() > kMaxChars ? QJsonValue(s.left(kMaxChars) + QStringLiteral("…")) : v;
    }
    if (v.isArray()) {
        QJsonArray out;
        const QJsonArray array = v.toArray();
        for (const auto &item : array)
            out.append(loggedValue(item));
        return out;
    }
    if (v.isObject()) {
        QJsonObject out;
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it)
            out.insert(it.key(), loggedValue(it.value()));
        return out;
    }
    return v;
}

// What the command was given, as the log keeps it: secrets masked.
QJsonObject loggedArgs(const Command &command, const QJsonObject &args)
{
    QJsonObject out;
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        bool secret = false;
        for (const Arg &a : command.args)
            secret = secret || (a.name == it.key() && a.secret);
        out.insert(it.key(), secret ? QJsonValue(QStringLiteral("••••")) : loggedValue(it.value()));
    }
    return out;
}

// What the status bar and the log show for a step: the command and its first
// two non-secret arguments in declared order ("files.open "src/app.py""),
// short enough for the indicator; the log keeps every argument besides.
QString stepLabel(const Command &command, const QJsonObject &args)
{
    QStringList parts{command.name};
    for (const Arg &a : command.args) {
        if (parts.size() > 2)
            break;
        const QJsonValue v = args.value(a.name);
        if (a.secret || v.isUndefined() || v.isNull())
            continue;
        if (v.isString()) {
            QString text = v.toString().simplified();
            if (text.isEmpty())
                continue;
            if (text.size() > 48)
                text = text.left(47) + QChar(0x2026);
            parts.append(QLatin1Char('"') + text + QLatin1Char('"'));
        } else if (v.isArray()) {
            parts.append(QStringLiteral("%1[%2]").arg(a.name).arg(v.toArray().size()));
        } else if (!v.isObject()) {
            parts.append(a.name + QLatin1Char('=') + argText(v));
        }
    }
    return parts.join(QLatin1Char(' '));
}

// Whether a string anywhere in `v` contains `needle`.
bool textContains(const QJsonValue &v, const QString &needle)
{
    if (v.isString())
        return v.toString().contains(needle);
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        for (const auto &e : a) {
            if (textContains(e, needle))
                return true;
        }
    } else if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (textContains(it.value(), needle))
                return true;
        }
    }
    return false;
}

// `until`: each field equals the value, or one of an array's values.
// `contains`: each field's text contains the string.
bool resultMatches(const QJsonObject &result, const QJsonObject &until, const QJsonObject &contains)
{
    for (auto it = until.constBegin(); it != until.constEnd(); ++it) {
        const QJsonValue have = result.value(it.key());
        if (it.value().isArray()) {
            if (!it.value().toArray().contains(have))
                return false;
        } else if (have != it.value()) {
            return false;
        }
    }
    for (auto it = contains.constBegin(); it != contains.constEnd(); ++it) {
        if (!textContains(result.value(it.key()), it.value().toString()))
            return false;
    }
    return true;
}

bool eventMatches(const QString &pattern, const QJsonObject &match, const QString &name, const QJsonObject &data)
{
    if (pattern.endsWith(QLatin1Char('*')) ? !name.startsWith(pattern.chopped(1)) : name != pattern)
        return false;
    for (auto it = match.constBegin(); it != match.constEnd(); ++it) {
        if (data.value(it.key()).toVariant().toString() != it.value().toVariant().toString())
            return false;
    }
    return true;
}

} // namespace

void ControlApi::onRequest(quint64 id, const http::Request &request)
{
    const Reply reply(m_server, id);
    QJsonObject body;
    if (!request.body.isEmpty()) {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(request.body, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            reply.error(400, QStringLiteral("body must be a JSON object: %1").arg(err.errorString()));
            return;
        }
        body = doc.object();
    }
    constexpr QByteArrayView kCommandPrefix("/commands/");
    if (request.path.startsWith(kCommandPrefix)) {
        const QString name =
            QString::fromUtf8(QByteArray::fromPercentEncoding(request.path.mid(kCommandPrefix.size())));
        handleCommand(name, request, std::move(body), reply);
        return;
    }
    const auto it = m_routes.constFind(routeKey(request.method, request.path));
    if (it == m_routes.constEnd()) {
        bool pathKnown = false;
        for (auto r = m_routes.constBegin(); r != m_routes.constEnd(); ++r)
            pathKnown = pathKnown || r.key().endsWith(' ' + request.path);
        if (pathKnown)
            reply.error(405, QStringLiteral("method not allowed"));
        else
            reply.error(404, QStringLiteral("no endpoint %1 %2; see GET /skill")
                                 .arg(QString::fromLatin1(request.method), QString::fromLatin1(request.path)));
        return;
    }
    (*it)(request, body, reply);
}

void ControlApi::handleCommand(const QString &name, const http::Request &request, QJsonObject body,
                               const Reply &reply)
{
    const Command *command = m_commands.find(name);
    if (!command) {
        // Same domain's commands help a near miss ("file.open" for "files.open").
        const QString domain = name.left(name.lastIndexOf(QLatin1Char('.')));
        QStringList near;
        for (const Command &c : m_commands.all()) {
            if (!domain.isEmpty() && c.name.startsWith(domain))
                near.append(c.name);
        }
        reply.error(404, near.isEmpty()
                             ? QStringLiteral("no command '%1'; GET /commands lists them").arg(name)
                             : QStringLiteral("no command '%1'; %2 has: %3")
                                   .arg(name, domain, near.join(QStringLiteral(", "))));
        return;
    }
    if (request.method == "GET") {
        if (command->changes) {
            reply.error(405, QStringLiteral("%1 changes things: POST it with a JSON body").arg(name),
                        {{QStringLiteral("command"), CommandRegistry::describe(*command)}});
            return;
        }
        body = CommandRegistry::queryArgs(*command, request);
    } else if (request.method != "POST") {
        reply.error(405, QStringLiteral("use POST (or GET for a command that changes nothing)"));
        return;
    }
    run(name, std::move(body), [reply](int status, const QJsonObject &result) { reply.json(status, result); });
}

void ControlApi::run(const QString &name, QJsonObject args, const Respond &respond)
{
    const Command *command = m_commands.find(name);
    if (!command) {
        respond(404, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no command '%1'").arg(name)}});
        return;
    }
    if (const QString error = CommandRegistry::validate(*command, args); !error.isEmpty()) {
        if (command->changes)
            logAction(command->name, QString(), stepLabel(*command, args), false, error, loggedArgs(*command, args));
        respond(400, {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}});
        return;
    }
    execute(*command, std::move(args), respond);
}

void ControlApi::execute(const Command &command, QJsonObject args, Respond respond)
{
    // A dialog or menu the app opens on its own while a handler runs (a
    // "modified by another program, reload?" prompt when the tab a command
    // brings forward changed on disk) would hold the answer until the user
    // closed it. It is cancelled, the answer that changes nothing, and the
    // answer names it. Only while the handler is on the stack: a dialog that
    // opens later is the user's to answer.
    auto dismissed = std::make_shared<QJsonArray>();
    CommandContext::Hooks hooks;
    hooks.respond = [respond = std::move(respond), dismissed](int status, const QJsonObject &body) {
        if (dismissed->isEmpty()) {
            respond(status, body);
            return;
        }
        QJsonObject withDialogs = body;
        withDialogs.insert(QStringLiteral("dismissedDialogs"), *dismissed);
        respond(status, withDialogs);
    };
    if (command.changes) {
        const QJsonObject logged = loggedArgs(command, args);
        const QString label = stepLabel(command, args);
        hooks.finished = [this, name = command.name, label, logged](bool ok, const QString &error) {
            logAction(name, QString(), label, ok, error, logged);
        };
        hooks.reveal = [this](QWidget *w) { reveal(w); };
        emit commandRan(label);
    }
    const CommandContext ctx(&command, std::move(args), std::move(hooks));
    QWidget *const modalBefore = QApplication::activeModalWidget();
    QWidget *const popupBefore = QApplication::activePopupWidget();
    QTimer trap; // fires only inside a nested event loop
    trap.setInterval(20);
    connect(&trap, &QTimer::timeout, this, [this, modalBefore, popupBefore, dismissed, name = command.name] {
        QWidget *opened = QApplication::activePopupWidget();
        if (!opened || opened == popupBefore)
            opened = QApplication::activeModalWidget();
        if (!opened || opened == modalBefore || opened == popupBefore)
            return;
        QJsonObject dialog{{QStringLiteral("title"), opened->windowTitle()}};
        if (auto *box = qobject_cast<QMessageBox *>(opened)) {
            const QString text = box->text();
            dialog.insert(QStringLiteral("text"), Qt::mightBeRichText(text)
                                                      ? QTextDocumentFragment::fromHtml(text).toPlainText()
                                                      : text);
        }
        dismissed->append(dialog);
        logAction(name, QString(), tr("Cancelled a dialog it opened: %1").arg(opened->windowTitle()), true);
        if (auto *d = qobject_cast<QDialog *>(opened))
            d->reject();
        else
            opened->close();
    });
    trap.start();
    command.handler(ctx);
    trap.stop();
    if (ctx.abandoned())
        ctx.fail(500, QStringLiteral("%1 did not answer (a bug in NotepadAI)").arg(command.name));
}

void ControlApi::reveal(QWidget *widget)
{
    revealWidget(widget);
    // Flash once the tab switch has laid it out (a new tab is shown a pass later).
    QTimer::singleShot(0, this, [this, w = QPointer<QWidget>(widget)] {
        if (w)
            m_feedback->flash(w);
    });
}

void ControlApi::handleInstance(const Reply &reply)
{
    QJsonObject o = versionJson();
    o.insert(QStringLiteral("ok"), true);
    o.insert(QStringLiteral("pid"), qint64(QCoreApplication::applicationPid()));
    o.insert(QStringLiteral("url"), baseUrl());
    o.insert(QStringLiteral("commands"), int(m_commands.all().size()));
    if (m_host.instanceInfo) {
        const QJsonObject info = m_host.instanceInfo();
        for (auto it = info.constBegin(); it != info.constEnd(); ++it)
            o.insert(it.key(), it.value());
    }
    reply.json(200, o);
}

void ControlApi::registerRoutes()
{
    route("GET", "/instance", [this](const http::Request &, const QJsonObject &, const Reply &reply) {
        handleInstance(reply);
    });
    route("GET", "/log", [this](const http::Request &request, const QJsonObject &, const Reply &reply) {
        const int limit = request.hasQuery("limit") ? request.queryValue("limit").toInt() : 100;
        reply.json(200, {{QStringLiteral("ok"), true},
                         {QStringLiteral("entries"), m_log->toJson(qBound(1, limit, int(ControlActivityLog::kCapacity)))}});
    });
    route("GET", "/commands", [this](const http::Request &request, const QJsonObject &, const Reply &reply) {
        // ?domain=files lists files.*; no domain: all of them.
        const QString domain = request.queryValue("domain");
        QJsonArray list;
        for (const Command &c : m_commands.all()) {
            if (domain.isEmpty() || c.name.startsWith(domain + QLatin1Char('.')))
                list.append(CommandRegistry::describe(c));
        }
        reply.json(200, {{QStringLiteral("ok"), true}, {QStringLiteral("commands"), list}});
    });

    // {"event": name or prefix*, "match": {...}} or {"command": a read command, "args": {...},
    // "until": {field: value | [values]}, "contains": {field: text}}; "timeout" seconds.
    route("POST", "/wait", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        const QString event = body.value(QStringLiteral("event")).toString();
        const QString commandName = body.value(QStringLiteral("command")).toString();
        if (event.isEmpty() == commandName.isEmpty()) {
            reply.error(400, QStringLiteral("give \"event\" (a name, or a prefix ending in *) with an optional "
                                            "\"match\", or \"command\" (one that changes nothing) with \"args\" "
                                            "and \"until\" / \"contains\""));
            return;
        }
        const Command *command = nullptr;
        QJsonObject args = body.value(QStringLiteral("args")).toObject();
        const QJsonObject until = body.value(QStringLiteral("until")).toObject();
        const QJsonObject contains = body.value(QStringLiteral("contains")).toObject();
        if (!commandName.isEmpty()) {
            command = m_commands.find(commandName);
            if (!command) {
                reply.error(404, QStringLiteral("no command '%1'").arg(commandName));
                return;
            }
            if (command->changes) {
                reply.error(400, QStringLiteral("%1 changes things; wait on a command that only reads").arg(commandName));
                return;
            }
            if (until.isEmpty() && contains.isEmpty()) {
                reply.error(400, QStringLiteral("give \"until\" ({field: value or [values]}) or \"contains\" "
                                                "({field: text}) for the result of %1")
                                     .arg(commandName));
                return;
            }
            if (const QString error = CommandRegistry::validate(*command, args); !error.isEmpty()) {
                reply.error(400, error);
                return;
            }
        }
        const int timeoutMs = int(qBound(0.1, body.value(QStringLiteral("timeout")).toDouble(30), 600.0) * 1000);

        // The pending wait owns its timers; finishing deletes it (later: a
        // timer of it may be the one calling).
        auto *wait = new QObject(this);
        wait->setObjectName(QLatin1String(kPendingWait));
        auto clock = std::make_shared<QElapsedTimer>();
        auto waiter = std::make_shared<quint64>(0);
        auto done = std::make_shared<bool>(false);
        auto last = std::make_shared<QJsonObject>();
        clock->start();
        QPointer<QObject> guard(wait);
        // fromWaiter: the event hub is calling the waiter and drops it itself.
        auto finish = [this, guard, reply, clock, waiter, done, last](bool ok, QJsonObject extra, bool fromWaiter) {
            if (*done)
                return;
            *done = true;
            if (*waiter && !fromWaiter)
                m_events->removeWaiter(*waiter);
            if (!ok) {
                extra.insert(QStringLiteral("error"), QStringLiteral("timed out"));
                if (!last->isEmpty())
                    extra.insert(QStringLiteral("last"), *last);
            }
            extra.insert(QStringLiteral("ok"), ok);
            extra.insert(QStringLiteral("waitedMs"), clock->elapsed());
            reply.json(ok ? 200 : 408, extra);
            if (guard)
                guard->deleteLater();
        };

        auto *deadline = new QTimer(wait);
        deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, wait, [finish] { finish(false, {}, false); });
        deadline->start(timeoutMs);

        if (!event.isEmpty()) {
            const QJsonObject match = body.value(QStringLiteral("match")).toObject();
            *waiter = m_events->addWaiter([finish, event, match](const QString &name, const QJsonObject &data) {
                if (!eventMatches(event, match, name, data))
                    return false;
                finish(true, {{QStringLiteral("event"), name}, {QStringLiteral("data"), data}}, true);
                return true;
            });
            return;
        }
        // A read command, run now and every 100 ms (one run in flight) until its result matches.
        auto inFlight = std::make_shared<bool>(false);
        auto check = [this, command, args, until, contains, finish, inFlight, done, last] {
            if (*inFlight || *done)
                return;
            *inFlight = true;
            execute(*command, args, [finish, until, contains, inFlight, last](int, const QJsonObject &result) {
                *inFlight = false;
                *last = result;
                if (resultMatches(result, until, contains))
                    finish(true, {{QStringLiteral("result"), result}}, false);
            });
        };
        auto *poll = new QTimer(wait);
        connect(poll, &QTimer::timeout, wait, check);
        poll->start(100);
        check();
    });

    route("GET", "/screenshot", [this](const http::Request &request, const QJsonObject &, const Reply &reply) {
        QWidget *w = QApplication::activeModalWidget();
        if (!w)
            w = m_window;
        if (!w) {
            reply.error(404, QStringLiteral("no window"));
            return;
        }
        const int maxWidth = request.hasQuery("maxWidth") ? qBound(64, request.queryValue("maxWidth").toInt(), 8192)
                                                          : 1600;
        // Grabbing paints on this thread; scaling and PNG encoding run on the pool.
        const QImage image = grabWidget(w);
        auto *watcher = new QFutureWatcher<QByteArray>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [watcher, reply] {
            reply.bytes(200, "image/png", watcher->result());
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([image, maxWidth] {
            const QImage scaled =
                image.width() > maxWidth ? image.scaledToWidth(maxWidth, Qt::SmoothTransformation) : image;
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            scaled.save(&buffer, "PNG");
            return png;
        }));
    });
}

void revealWidget(QWidget *widget)
{
    if (!widget)
        return;
    // Outermost last: a tab inside a panel inside a dock.
    ads::CDockWidget *tab = nullptr;
    QWidget *child = widget;
    for (QWidget *w = widget; w; child = w, w = w->parentWidget()) {
        if (auto *dock = qobject_cast<ads::CDockWidget *>(w)) {
            if (dock->isClosed())
                dock->toggleView(true);
            dock->setAsCurrentTab();
            if (!tab)
                tab = dock;
        } else if (auto *panel = qobject_cast<QDockWidget *>(w)) {
            if (panel->isHidden())
                panel->show();
            panel->raise(); // a tabified panel comes to the front
        } else if (auto *stack = qobject_cast<QStackedWidget *>(w); stack && child != w) {
            if (auto *tabs = qobject_cast<QTabWidget *>(stack->parentWidget()))
                tabs->setCurrentWidget(child);
        }
    }
    if (QWidget *window = widget->window(); window && window->isMinimized())
        window->showNormal();
    // As a click on the tab: it becomes the focused tab, which is what the
    // editor's "current editor" follows. Keyboard focus alone never moves
    // while NotepadAI is not the foreground window (the agent's terminal is).
    if (tab && tab->dockManager())
        tab->dockManager()->setDockWidgetFocused(tab);
}

} // namespace control
