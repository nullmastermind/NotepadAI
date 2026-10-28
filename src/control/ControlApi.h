/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_API_H
#define CONTROL_API_H

#include "ControlEffects.h"
#include "ControlHttp.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class ApplicationSettings;
class QMainWindow;

namespace control {

class ControlActivityLog;
class ControlEvents;
class ControlFeedback;
class ControlServer;
namespace ui { struct Target; }

// Answers one request exactly once; copies share that "once".
class Reply
{
public:
    Reply() = default;
    Reply(ControlServer *server, quint64 id);

    void json(int status, const QJsonObject &body) const;
    void bytes(int status, const QByteArray &contentType, const QByteArray &body) const;
    // {"ok":false,"error":message, ...extra}
    void error(int status, const QString &message, const QJsonObject &extra = {}) const;
    bool sent() const { return m_sent && *m_sent; }
    // The server that received the request still runs (not stopped since).
    bool live() const { return !m_server.isNull(); }

private:
    QPointer<ControlServer> m_server;
    quint64 m_id = 0;
    std::shared_ptr<bool> m_sent;
};

// The Control API on the GUI thread (docs/adr/0006): settings, token,
// routing, the action endpoints, feedback and the activity log. Other
// modules (UI tree, events) add routes. Off: no server, no thread.
class ControlApi : public QObject
{
    Q_OBJECT

public:
    static constexpr int kApiVersion = 1;
    static constexpr int kSkillVersion = 3;
    static constexpr quint16 kDefaultPort = 47821;
    // The keychain entry holding the token.
    static constexpr char kTokenSecretKey[] = "control-api-token";

    struct Host
    {
        ApplicationSettings *settings = nullptr;
        // Token persistence (the OS keychain in the app). loadToken: the stored
        // token, "" when none, nullopt when the store cannot be read. Unset or
        // failing: a session-only token.
        std::function<std::optional<QString>()> loadToken;
        std::function<bool(const QString &token)> saveToken;
        // Window title, workspaces, active workspace for GET /instance.
        std::function<QJsonObject()> instanceInfo;
        // Whether a window or dock counts as open (effects).
        ControlSnapshot::Presence presence;
    };

    using Handler = std::function<void(const http::Request &request, const QJsonObject &body, const Reply &reply)>;

    ControlApi(const Host &host, QMainWindow *window, QObject *parent = nullptr);
    ~ControlApi() override;

    QMainWindow *window() const;
    ControlActivityLog *activityLog() const { return m_log; }
    ControlFeedback *feedback() const { return m_feedback; }
    ControlEvents *events() const { return m_events; }
    const Host &host() const { return m_host; }

    // --- Settings ---------------------------------------------------------
    bool isEnabled() const;
    void setEnabled(bool on);
    // Turns the server on if the setting says so (startup).
    void startFromSettings();
    quint16 configuredPort() const;
    void setConfiguredPort(quint16 port);
    bool allowLan() const;
    void setAllowLan(bool on);
    QString lanAddress() const;
    void setLanAddress(const QString &address);

    // --- State ------------------------------------------------------------
    bool isListening() const;
    quint16 port() const;
    QString lastError() const { return m_lastError; }
    QString baseUrl() const;
    QString token();
    bool tokenIsSessionOnly() const { return m_tokenSessionOnly; }
    void regenerateToken();
    QString connectionPrompt();

    // --- Extension points ---------------------------------------------------
    void route(const QByteArray &method, const QByteArray &path, Handler handler);
    // Documents that an action or button opens a picker, so GET /actions and
    // the tree say what `value` it takes (any picker accepts one; see
    // control::picker).
    void notePicker(const QString &id, const QString &valueHint);
    QString pickerHint(const QString &id) const { return m_pickers.value(id); }
    // Logs the step, flashes nothing (callers flash what they touched).
    void logAction(const QString &op, const QString &target, const QString &label, bool ok = true,
                   const QString &detail = QString(), const QJsonObject &args = {});

    // Runs `act` as a command: answers with {ok, completed, effects} once it
    // finished or a modal dialog / popup took over. With `pickerValues`, the
    // first picker `act` opens returns them (a direct command).
    void runCommand(const Reply &reply, std::function<QString()> act, const QJsonObject &extra = {},
                    const QStringList *pickerValues = nullptr);
    // Strings from a JSON `value`: an array, or a string split at newlines.
    static QStringList pickerValues(const QJsonValue &value);

    static QByteArray skillText();
    static QJsonObject versionJson();
    // Addresses of this machine's interfaces a LAN client can use.
    static QStringList lanAddresses();

signals:
    void stateChanged();
    void commandRan();

private:
    void restart();
    void stopServer();
    void onRequest(quint64 id, const http::Request &request);
    void handleInstance(const Reply &reply);
    void handleActions(const http::Request &request, const Reply &reply);
    void handleTrigger(const QJsonObject &body, const Reply &reply);
    void registerUiRoutes();
    void registerEventRoutes(); // /wait, /screenshot
    // Resolve, check a user could act on it, flash, log, run.
    void uiCommand(const QJsonObject &body, const Reply &reply, const QString &op, bool needsTarget,
                   const std::function<QString(const ui::Target &)> &fn);

    Host m_host;
    QPointer<QMainWindow> m_window;
    ControlServer *m_server = nullptr;
    ControlActivityLog *m_log = nullptr;
    ControlFeedback *m_feedback = nullptr;
    ControlEvents *m_events = nullptr;
    QHash<QByteArray, Handler> m_routes; // "GET /path"
    QHash<QString, QString> m_pickers; // id -> value hint
    QString m_token;
    bool m_tokenSessionOnly = false;
    QString m_lastError;
};

} // namespace control

#endif // CONTROL_API_H
