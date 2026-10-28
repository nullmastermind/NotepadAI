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

#include "ControlCommands.h"
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
class QWidget;

namespace control {

class ControlActivityLog;
class ControlEvents;
class ControlFeedback;
class ControlServer;

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

// The Control API on the GUI thread (docs/adr/0007): settings, token, the
// command registry and its endpoints, feedback, the activity log, events.
// Domains register their commands in commands(). Off: no server, no thread.
class ControlApi : public QObject
{
    Q_OBJECT

public:
    static constexpr int kApiVersion = 2;
    static constexpr int kSkillVersion = 4;
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
        // Window title, workspaces, active workspace, tabs for GET /instance.
        std::function<QJsonObject()> instanceInfo;
    };

    using Handler = std::function<void(const http::Request &request, const QJsonObject &body, const Reply &reply)>;
    using Respond = std::function<void(int status, const QJsonObject &body)>;

    ControlApi(const Host &host, QMainWindow *window, QObject *parent = nullptr);
    ~ControlApi() override;

    QMainWindow *window() const;
    ControlActivityLog *activityLog() const { return m_log; }
    ControlFeedback *feedback() const { return m_feedback; }
    ControlEvents *events() const { return m_events; }
    const Host &host() const { return m_host; }
    CommandRegistry &commands() { return m_commands; }
    const CommandRegistry &commands() const { return m_commands; }

    // --- Settings ---------------------------------------------------------
    bool isEnabled() const;
    void setEnabled(bool on);
    // Turns the server on if the setting says so (startup, after the
    // commands are registered: the skill lists them).
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
    // Writes one step to the activity log (and the `action` event).
    void logAction(const QString &op, const QString &target, const QString &label, bool ok = true,
                   const QString &detail = QString(), const QJsonObject &args = {});
    // Runs a command with already validated arguments: logs and shows it when
    // it changes something. `respond` gets the HTTP status and body.
    void execute(const Command &command, QJsonObject args, Respond respond);
    // Validates, then runs (400 on invalid arguments). In-process callers
    // (the self-test) use it like an HTTP request.
    void run(const QString &name, QJsonObject args, const Respond &respond);

    // The skill: the hand-written guide followed by the command reference.
    QByteArray skill() const;
    static QByteArray skillGuide();
    static QJsonObject versionJson();
    // Addresses of this machine's interfaces a LAN client can use.
    static QStringList lanAddresses();

signals:
    void stateChanged();
    // A changing command started: what the indicator names.
    void commandRan(const QString &label);

private:
    void restart();
    void stopServer();
    void onRequest(quint64 id, const http::Request &request);
    void handleCommand(const QString &name, const http::Request &request, QJsonObject body, const Reply &reply);
    void handleInstance(const Reply &reply);
    void registerRoutes();
    void reveal(QWidget *widget);

    Host m_host;
    QPointer<QMainWindow> m_window;
    ControlServer *m_server = nullptr;
    ControlActivityLog *m_log = nullptr;
    ControlFeedback *m_feedback = nullptr;
    ControlEvents *m_events = nullptr;
    CommandRegistry m_commands;
    QHash<QByteArray, Handler> m_routes; // "GET /path"
    QString m_token;
    bool m_tokenSessionOnly = false;
    QString m_lastError;
};

// Makes `widget` visible to the user: its ADS tab current, its dock panel
// shown and in front, its page of a QTabWidget current, its window restored.
void revealWidget(QWidget *widget);

} // namespace control

#endif // CONTROL_API_H
