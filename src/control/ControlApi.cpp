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
#include "ControlActionIndex.h"
#include "ControlActivityLog.h"
#include "ControlAuth.h"
#include "ControlEvents.h"
#include "ControlFeedback.h"
#include "ControlIds.h"
#include "ControlPicker.h"
#include "ControlScreenshot.h"
#include "ControlServer.h"
#include "ControlUiTree.h"

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFutureWatcher>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QNetworkInterface>
#include <QPixmap>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

namespace control {

namespace {

const ApplicationSetting<bool> kEnabled{"ControlApi/Enabled", false};
const ApplicationSetting<int> kPort{"ControlApi/Port", int(ControlApi::kDefaultPort)};
const ApplicationSetting<bool> kAllowLan{"ControlApi/AllowLan", false};
const ApplicationSetting<QString> kLanAddress{"ControlApi/LanAddress", QString()};

// Object name of a pending /wait (a child of the API, deleted on Stop).
constexpr char kPendingWait[] = "controlPendingWait";

QByteArray routeKey(const QByteArray &method, const QByteArray &path)
{
    return method + ' ' + path;
}

QString menuLabel(const QStringList &path, const QString &label)
{
    QStringList parts = path;
    parts.append(label);
    return parts.join(QStringLiteral(" ▸ "));
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
    m_events->setPresence(m_host.presence);
    route("GET", "/instance", [this](const http::Request &, const QJsonObject &, const Reply &reply) {
        handleInstance(reply);
    });
    route("GET", "/actions", [this](const http::Request &request, const QJsonObject &, const Reply &reply) {
        handleActions(request, reply);
    });
    route("POST", "/actions/trigger", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        handleTrigger(body, reply);
    });
    route("GET", "/log", [this](const http::Request &request, const QJsonObject &, const Reply &reply) {
        const int limit = request.hasQuery("limit") ? request.queryValue("limit").toInt() : 100;
        reply.json(200, {{QStringLiteral("ok"), true},
                         {QStringLiteral("entries"), m_log->toJson(qBound(1, limit, int(ControlActivityLog::kCapacity)))}});
    });
    registerUiRoutes();
    registerEventRoutes();
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
    const QString saved = m_host.settings ? m_host.settings->get(kLanAddress) : QString();
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
               "NotepadAI (the code editor on this machine) exposes a Control API so you can operate it the "
               "way its user does: every change you make goes through its visible menus, buttons and fields, "
               "and the user sees it.\n\n"
               "NOTEPADAI_URL=%1\n"
               "NOTEPADAI_TOKEN=%2\n\n"
               "1. Fetch the skill:\n"
               "   curl -s -H \"Authorization: Bearer $NOTEPADAI_TOKEN\" \"$NOTEPADAI_URL/skill\"\n"
               "2. Save it unchanged as a skill named notepadai-control (Claude Code: "
               "~/.claude/skills/notepadai-control/SKILL.md). Never write the token into the skill or any "
               "file in a repository; keep NOTEPADAI_URL and NOTEPADAI_TOKEN as environment values.\n"
               "3. Use the skill to operate NotepadAI. If a request returns 401, ask the user for a new "
               "prompt (the token was regenerated). Check GET $NOTEPADAI_URL/skill/version to know when "
               "to fetch the skill again.\n")
        .arg(baseUrl(), token());
}

QByteArray ControlApi::skillText()
{
    QFile f(QStringLiteral(":/control/SKILL.md"));
    if (!f.open(QIODevice::ReadOnly))
        return QByteArrayLiteral("# notepadai-control\n\nSkill resource missing from this build.\n");
    return f.readAll();
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
    config.skill = skillText();
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
    m_routes.insert(routeKey(method, path), std::move(handler));
}

void ControlApi::notePicker(const QString &id, const QString &valueHint)
{
    m_pickers.insert(id, valueHint);
}

QStringList ControlApi::pickerValues(const QJsonValue &value)
{
    QStringList out;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &v : array)
            out.append(v.toVariant().toString());
    } else {
        out = value.toVariant().toString().split(QLatin1Char('\n'));
    }
    return out;
}

void ControlApi::logAction(const QString &op, const QString &target, const QString &label, bool ok,
                           const QString &detail)
{
    ControlActivityLog::Entry e;
    e.time = QDateTime::currentDateTime();
    e.op = op;
    e.target = target;
    e.label = label;
    e.ok = ok;
    e.detail = detail;
    if (events::wanted()) {
        QJsonObject data{{QStringLiteral("op"), op}, {QStringLiteral("label"), label}, {QStringLiteral("ok"), ok}};
        if (!target.isEmpty())
            data.insert(QStringLiteral("id"), target);
        if (!detail.isEmpty())
            data.insert(QStringLiteral("detail"), detail);
        events::publish(QStringLiteral("action"), data);
    }
    m_log->append(std::move(e));
    emit commandRan();
}

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

void ControlApi::handleInstance(const Reply &reply)
{
    QJsonObject o = versionJson();
    o.insert(QStringLiteral("ok"), true);
    o.insert(QStringLiteral("pid"), qint64(QCoreApplication::applicationPid()));
    o.insert(QStringLiteral("url"), baseUrl());
    if (m_host.instanceInfo) {
        const QJsonObject info = m_host.instanceInfo();
        for (auto it = info.constBegin(); it != info.constEnd(); ++it)
            o.insert(it.key(), it.value());
    }
    reply.json(200, o);
}

void ControlApi::handleActions(const http::Request &request, const Reply &reply)
{
    if (!m_window) {
        reply.error(503, QStringLiteral("no window"));
        return;
    }
    const ControlActionIndex index(m_window);
    const QString menu = request.queryValue("menu");
    const auto entries = index.list(menu);
    if (!menu.isEmpty() && entries.isEmpty() && !index.resolve(menu).action) {
        reply.error(404, QStringLiteral("no menu '%1'").arg(menu));
        return;
    }
    QJsonArray actions;
    for (const auto &e : entries) {
        QJsonObject o = ControlActionIndex::toJson(e);
        const auto picker = m_pickers.constFind(e.id);
        if (picker != m_pickers.constEnd())
            o.insert(QStringLiteral("value"), picker.value());
        actions.append(o);
    }
    reply.json(200, {{QStringLiteral("ok"), true}, {QStringLiteral("actions"), actions}});
}

void ControlApi::runCommand(const Reply &reply, std::function<QString()> act, const QJsonObject &extra,
                            const QStringList *pickerValues)
{
    const bool withValue = pickerValues != nullptr;
    // One value at a time: an earlier command may still be inside a dialog
    // its action opened before reaching its picker.
    if (withValue && picker::isArmed()) {
        reply.error(409, QStringLiteral("an earlier direct command is still waiting for its picker; answer the "
                                        "dialog it opened first"),
                    extra);
        return;
    }
    const ControlSnapshot before = ControlSnapshot::take(m_host.presence);
    auto error = std::make_shared<QString>();
    auto valueUsed = std::make_shared<bool>(false);
    const QStringList values = withValue ? *pickerValues : QStringList();
    runUntilSettled(this, [act = std::move(act), error, valueUsed, withValue, values, reply] {
            // Stopped (or restarted) since the request came in: it has no effect.
            if (!reply.live())
                return;
            // The value serves only pickers this action opens while it runs.
            if (withValue)
                picker::arm(values);
            *error = act();
            if (withValue)
                *valueUsed = picker::disarm();
        },
        [this, reply, before, error, valueUsed, extra, withValue](bool finished) {
            if (finished && !error->isEmpty()) {
                reply.error(422, *error, extra);
                return;
            }
            QJsonObject body = extra;
            if (withValue && !finished) {
                body.insert(QStringLiteral("valuePending"), picker::isArmed());
            } else if (withValue) {
                body.insert(QStringLiteral("valueUsed"), *valueUsed);
                if (!*valueUsed)
                    body.insert(QStringLiteral("warning"),
                                QStringLiteral("no picker opened, so the value was not used"));
            }
            body.insert(QStringLiteral("ok"), true);
            body.insert(QStringLiteral("completed"), finished);
            body.insert(QStringLiteral("effects"), before.diff(ControlSnapshot::take(m_host.presence)));
            reply.json(200, body);
        });
}

void ControlApi::handleTrigger(const QJsonObject &body, const Reply &reply)
{
    if (!m_window) {
        reply.error(503, QStringLiteral("no window"));
        return;
    }
    const QString id = body.value(QStringLiteral("id")).toString();
    if (id.isEmpty()) {
        reply.error(400, QStringLiteral("missing \"id\""));
        return;
    }
    const ControlActionIndex index(m_window);
    const ControlActionIndex::Resolved r = index.resolve(id);
    if (!r.action) {
        reply.error(404, r.error);
        return;
    }
    const QString label = menuLabel(r.menuPath, ControlActionIndex::plainText(r.action->text()));
    if (!r.visible) {
        logAction(QStringLiteral("trigger"), id, label, false, QStringLiteral("not visible"));
        reply.error(409, QStringLiteral("'%1' is hidden; a user could not click it").arg(label));
        return;
    }
    if (!r.enabled) {
        logAction(QStringLiteral("trigger"), id, label, false, QStringLiteral("disabled"));
        reply.error(409, QStringLiteral("'%1' is disabled; a user could not click it").arg(label));
        return;
    }
    // The window's menus and toolbars cannot be clicked behind a modal dialog
    // (an open popup menu's own items can).
    if (QWidget *modal = QApplication::activeModalWidget(); modal && !r.inPopup && modal != m_window) {
        logAction(QStringLiteral("trigger"), id, label, false, QStringLiteral("behind a modal dialog"));
        reply.error(409, QStringLiteral("the modal dialog '%1' is open; answer it first").arg(ids::nodeId(modal).id));
        return;
    }

    const bool hasValue = body.contains(QStringLiteral("value"));
    const QStringList values = hasValue ? pickerValues(body.value(QStringLiteral("value"))) : QStringList();
    std::function<QString()> act;
    if (r.inPopup && r.menu) {
        // Select it and press Return: exec() returns this action, as on a click.
        QPointer<QMenu> menu = r.menu;
        QPointer<QAction> action = r.action;
        act = [menu, action] {
            if (!menu || !action)
                return QStringLiteral("the menu closed");
            menu->setActiveAction(action);
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(menu, &press);
            return QString();
        };
    } else {
        QPointer<QAction> action = r.action;
        act = [action] {
            if (!action)
                return QStringLiteral("the action went away");
            action->trigger();
            return QString();
        };
    }

    if (!r.inPopup)
        m_feedback->flashAction(r.action);
    logAction(QStringLiteral("trigger"), id,
              hasValue ? label + QStringLiteral(" ← ") + values.join(QStringLiteral(", ")) : label);
    runCommand(reply, std::move(act), {{QStringLiteral("action"), id}, {QStringLiteral("label"), label}},
               hasValue ? &values : nullptr);
}

// --- /ui ---------------------------------------------------------------------

void ControlApi::uiCommand(const QJsonObject &body, const Reply &reply, const QString &op, bool needsTarget,
                           const std::function<QString(const ui::Target &)> &fn)
{
    const QString id = body.value(QStringLiteral("id")).toString();
    ui::Target target;
    if (!id.isEmpty()) {
        target = ui::resolve(id);
        if (!target.widget) {
            reply.error(404, target.error + QStringLiteral("; list nodes with GET /ui"));
            return;
        }
        const QString blocked = ui::blockedReason(target, op);
        if (!blocked.isEmpty()) {
            logAction(op, id, ui::labelOf(target.widget), false, blocked);
            reply.error(409, blocked);
            return;
        }
    } else if (needsTarget) {
        reply.error(400, QStringLiteral("missing \"id\""));
        return;
    }

    QString label = target.widget ? ui::labelOf(target.widget) : QString();
    if (!target.part.isEmpty())
        label += QStringLiteral(" @ ") + target.part;
    QString detail;
    if (body.contains(QStringLiteral("text")) || body.contains(QStringLiteral("value"))) {
        const QJsonValue v = body.contains(QStringLiteral("text")) ? body.value(QStringLiteral("text"))
                                                                    : body.value(QStringLiteral("value"));
        auto *edit = target.widget ? qobject_cast<QLineEdit *>(target.widget) : nullptr;
        // Never echo what goes into a password field.
        detail = edit && edit->echoMode() != QLineEdit::Normal ? QStringLiteral("••••")
                                                               : v.toVariant().toString().left(120);
    } else if (body.contains(QStringLiteral("keys"))) {
        detail = body.value(QStringLiteral("keys")).toVariant().toString();
    }
    if (target.widget)
        m_feedback->flash(target.widget, ui::targetRect(target));
    logAction(op, id, detail.isEmpty() ? label : label + QStringLiteral(" ← ") + detail);

    QPointer<QWidget> widget = target.widget;
    const QString part = target.part;
    const bool hadTarget = target.widget != nullptr;
    const bool hasValue = op == QLatin1String("click") && body.contains(QStringLiteral("value"));
    const QStringList values = hasValue ? pickerValues(body.value(QStringLiteral("value"))) : QStringList();
    runCommand(reply, [widget, part, hadTarget, fn] {
            if (hadTarget && !widget)
                return QStringLiteral("it went away");
            ui::Target t;
            t.widget = widget;
            t.part = part;
            return fn(t);
        }, {{QStringLiteral("op"), op}, {QStringLiteral("id"), id}}, hasValue ? &values : nullptr);
}

void ControlApi::registerUiRoutes()
{
    auto readOptions = [](const http::Request &r) {
        ReadOptions o;
        if (r.hasQuery("from"))
            o.from = r.queryValue("from").toLongLong();
        if (r.hasQuery("count"))
            o.count = r.queryValue("count").toLongLong();
        return o;
    };

    route("GET", "/ui", [](const http::Request &r, const QJsonObject &, const Reply &reply) {
        ui::TreeOptions options;
        if (r.hasQuery("depth"))
            options.depth = qBound(0, r.queryValue("depth").toInt(), 64);
        if (r.hasQuery("parts"))
            options.maxParts = qBound(0, r.queryValue("parts").toInt(), 2000);
        options.includeHidden = r.queryValue("hidden") == QLatin1String("1");
        QWidget *root = nullptr;
        const QString rootId = r.queryValue("root");
        if (!rootId.isEmpty()) {
            QString error;
            root = ids::resolveNode(rootId, &error);
            if (!root) {
                reply.error(404, error);
                return;
            }
        }
        reply.json(200, ui::tree(root, options));
    });
    route("GET", "/ui/node", [](const http::Request &r, const QJsonObject &, const Reply &reply) {
        const ui::Target t = ui::resolve(r.queryValue("id"));
        if (!t.widget) {
            reply.error(404, t.error);
            return;
        }
        reply.json(200, ui::node(t.widget));
    });
    route("GET", "/ui/read", [readOptions](const http::Request &r, const QJsonObject &, const Reply &reply) {
        const ui::Target t = ui::resolve(r.queryValue("id"));
        if (!t.widget) {
            reply.error(404, t.error);
            return;
        }
        const QJsonObject out = ui::read(t.widget, t.part, readOptions(r));
        reply.json(out.value(QStringLiteral("ok")).toBool() ? 200 : 404, out);
    });
    route("GET", "/ui/find", [](const http::Request &r, const QJsonObject &, const Reply &reply) {
        const int limit = r.hasQuery("limit") ? qBound(1, r.queryValue("limit").toInt(), 500) : 50;
        const QString text = r.queryValue("text");
        const QString role = r.queryValue("role");
        if (text.isEmpty() && role.isEmpty()) {
            reply.error(400, QStringLiteral("give text= and/or role="));
            return;
        }
        reply.json(200, {{QStringLiteral("ok"), true}, {QStringLiteral("nodes"), ui::find(text, role, limit)}});
    });

    route("POST", "/ui/click", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        const QString b = body.value(QStringLiteral("button")).toString();
        const Qt::MouseButton button = b == QLatin1String("right")    ? Qt::RightButton
                                       : b == QLatin1String("middle") ? Qt::MiddleButton
                                                                      : Qt::LeftButton;
        const bool dbl = body.value(QStringLiteral("double")).toBool();
        uiCommand(body, reply, QStringLiteral("click"), true,
                  [button, dbl](const ui::Target &t) { return ui::click(t, button, dbl); });
    });
    route("POST", "/ui/type", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        const QString text = body.value(QStringLiteral("text")).toString();
        const bool replace = body.value(QStringLiteral("replace")).toBool();
        uiCommand(body, reply, QStringLiteral("type"), true,
                  [text, replace](const ui::Target &t) { return ui::type(t, text, replace); });
    });
    route("POST", "/ui/key", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        QStringList sequences;
        const QJsonValue keys = body.value(QStringLiteral("keys"));
        if (keys.isArray()) {
            const QJsonArray array = keys.toArray();
            for (const QJsonValue &k : array)
                sequences.append(k.toString());
        } else {
            sequences.append(keys.toString());
        }
        uiCommand(body, reply, QStringLiteral("key"), false, [sequences](const ui::Target &t) {
            for (const QString &s : sequences) {
                const QString error = ui::keys(t, s);
                if (!error.isEmpty())
                    return error;
            }
            return QString();
        });
    });
    route("POST", "/ui/select", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        uiCommand(body, reply, QStringLiteral("select"), true,
                  [body](const ui::Target &t) { return ui::select(t, body); });
    });
    route("POST", "/ui/set", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        const QJsonValue value = body.value(QStringLiteral("value"));
        uiCommand(body, reply, QStringLiteral("set"), true,
                  [value](const ui::Target &t) { return ui::set(t, value); });
    });
    route("POST", "/ui/scroll", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        uiCommand(body, reply, QStringLiteral("scroll"), true,
                  [body](const ui::Target &t) { return ui::scroll(t, body); });
    });
    route("POST", "/ui/close", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        uiCommand(body, reply, QStringLiteral("close"), true, [](const ui::Target &t) { return ui::close(t); });
    });
    route("POST", "/ui/focus", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        uiCommand(body, reply, QStringLiteral("focus"), true, [](const ui::Target &t) { return ui::focus(t); });
    });
    route("POST", "/ui/do", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        const QString op = body.value(QStringLiteral("op")).toString();
        const QJsonObject args = body.value(QStringLiteral("args")).toObject();
        if (op.isEmpty()) {
            reply.error(400, QStringLiteral("missing \"op\""));
            return;
        }
        uiCommand(body, reply, op, true, [op, args](const ui::Target &t) { return ui::perform(t, op, args); });
    });
}

// --- /wait and /screenshot -----------------------------------------------------

namespace {

// Whether any string the read shows (text, values, rows, labels; never ids,
// keys or field names) contains `needle`.
bool shownTextContains(const QJsonValue &v, const QString &needle, const QString &key = QString())
{
    if (v.isString())
        return key != QLatin1String("id") && key != QLatin1String("key") && v.toString().contains(needle);
    if (v.isArray()) {
        const QJsonArray a = v.toArray();
        for (const QJsonValue &e : a) {
            if (shownTextContains(e, needle, key))
                return true;
        }
    } else if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            if (shownTextContains(it.value(), needle, it.key()))
                return true;
        }
    }
    return false;
}

// Whether a node is in `state` now. Missing fields count as satisfied.
bool nodeInState(const QString &id, const QString &state, const QString &text)
{
    const ui::Target t = ui::resolve(id);
    if (state == QLatin1String("gone"))
        return !t.widget;
    if (!t.widget)
        return false;
    if (!text.isEmpty())
        return shownTextContains(ui::read(t.widget, t.part, {}), text);
    if (state.isEmpty() || state == QLatin1String("exists"))
        return true;
    if (state == QLatin1String("visible"))
        return t.widget->isVisible();
    if (state == QLatin1String("hidden"))
        return !t.widget->isVisible();
    if (state == QLatin1String("enabled"))
        return t.widget->isVisible() && t.widget->isEnabled();
    if (state == QLatin1String("disabled"))
        return !t.widget->isEnabled();
    const QJsonObject node = ui::node(t.widget);
    if (state == QLatin1String("idle"))
        return !node.value(QStringLiteral("working")).toBool() && node.value(QStringLiteral("queued")).toInt() == 0
               && !node.value(QStringLiteral("waitingForUser")).toBool(); // a pending form is not idle
    if (state == QLatin1String("waitingForUser"))
        return node.value(QStringLiteral("waitingForUser")).toBool();
    return false;
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

const QStringList kStates{QStringLiteral("exists"),  QStringLiteral("gone"),     QStringLiteral("visible"),
                          QStringLiteral("hidden"),  QStringLiteral("enabled"),  QStringLiteral("disabled"),
                          QStringLiteral("idle"),    QStringLiteral("waitingForUser")};

} // namespace

void ControlApi::registerEventRoutes()
{
    // {"event": "chat.turnEnded", "match": {...}} or {"id": node, "state": a state or a list (any of
    // them), "text": ...}; "timeout" seconds.
    route("POST", "/wait", [this](const http::Request &, const QJsonObject &body, const Reply &reply) {
        const QString event = body.value(QStringLiteral("event")).toString();
        const QString id = body.value(QStringLiteral("id")).toString();
        QStringList states;
        if (const QJsonValue sv = body.value(QStringLiteral("state")); sv.isArray()) {
            for (const QJsonValue &v : sv.toArray())
                states.append(v.toString());
        } else if (!sv.toString().isEmpty()) {
            states.append(sv.toString());
        }
        const QString text = body.value(QStringLiteral("text")).toString();
        if (event.isEmpty() && id.isEmpty()) {
            reply.error(400, QStringLiteral("give \"event\" (a name, or a prefix ending in *), or \"id\" with "
                                            "\"state\" (%1) or \"text\"")
                                 .arg(kStates.join(QStringLiteral(", "))));
            return;
        }
        for (const QString &state : std::as_const(states)) {
            if (!kStates.contains(state)) {
                reply.error(400, QStringLiteral("unknown state '%1'; use one of: %2")
                                     .arg(state, kStates.join(QStringLiteral(", "))));
                return;
            }
        }
        const int timeoutMs = int(qBound(0.1, body.value(QStringLiteral("timeout")).toDouble(30), 600.0) * 1000);

        // The pending wait owns its timers; finishing deletes it (later: a
        // timer of it may be the one calling).
        auto *wait = new QObject(this);
        wait->setObjectName(QLatin1String(kPendingWait));
        auto *deadline = new QTimer(wait);
        auto clock = std::make_shared<QElapsedTimer>();
        auto waiter = std::make_shared<quint64>(0);
        auto done = std::make_shared<bool>(false);
        clock->start();
        QPointer<QObject> guard(wait);
        // fromWaiter: the event hub is calling the waiter and drops it itself.
        auto finish = [this, guard, reply, clock, waiter, done](bool ok, QJsonObject extra, bool fromWaiter) {
            if (*done)
                return;
            *done = true;
            if (*waiter && !fromWaiter)
                m_events->removeWaiter(*waiter);
            extra.insert(QStringLiteral("ok"), ok);
            extra.insert(QStringLiteral("waitedMs"), clock->elapsed());
            if (!ok)
                extra.insert(QStringLiteral("error"), QStringLiteral("timed out"));
            reply.json(ok ? 200 : 408, extra);
            if (guard)
                guard->deleteLater();
        };

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
        // A node condition: checked now, then every 100 ms until it holds.
        auto check = [finish, id, states, text] {
            if (states.isEmpty()) {
                if (nodeInState(id, QString(), text))
                    finish(true, {{QStringLiteral("id"), id}}, false);
                return;
            }
            for (const QString &state : states) {
                if (nodeInState(id, state, text)) {
                    finish(true, {{QStringLiteral("id"), id}, {QStringLiteral("state"), state}}, false);
                    return;
                }
            }
        };
        auto *poll = new QTimer(wait);
        connect(poll, &QTimer::timeout, wait, check);
        poll->start(100);
        check();
    });

    route("GET", "/screenshot", [this](const http::Request &request, const QJsonObject &, const Reply &reply) {
        QWidget *w = nullptr;
        const QString id = request.queryValue("id");
        if (!id.isEmpty()) {
            const ui::Target t = ui::resolve(id);
            if (!t.widget) {
                reply.error(404, t.error);
                return;
            }
            w = t.widget;
        } else {
            w = QApplication::activeModalWidget();
            if (!w)
                w = m_window;
        }
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

} // namespace control
