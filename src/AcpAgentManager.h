/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef ACP_AGENT_MANAGER_H
#define ACP_AGENT_MANAGER_H

#include <QHash>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QString>

#include "AcpConnection.h" // AcpConnection::RemoteChannelBuilder

class QThread;
class QTimer;

class AcpAgentRegistry;
class AcpHistoryStore;
class AcpSessionModel;
class AiAgentDock;
class ApplicationSettings;

namespace remote { class ExecutionContext; }

Q_DECLARE_LOGGING_CATEGORY(lcAcpManager)

// Top-level owner of the ACP subsystem. Owned by NotepadNextApplication.
//
// Responsibilities:
//   - Owns the single AcpAgentRegistry instance (so settings dialog + spawn
//     paths share the same registry).
//   - Owns the AcpHistoryStore worker thread.
//   - Tracks live sessions (connection + model + dock) keyed by sessionId.
//   - Provides the entry point used by MainWindow to spawn a new agent
//     instance against a given working directory.
//   - Runs an idle reaper that destroys connection+model for sessions whose
//     dock has been closed for more than 1 hour.
//   - Provides a synchronous shutdown() that flushes the history worker and
//     joins its thread.
class AcpAgentManager : public QObject
{
    Q_OBJECT

public:
    explicit AcpAgentManager(ApplicationSettings *settings, QObject *parent = nullptr);
    ~AcpAgentManager() override;

    AcpAgentRegistry *registry() const { return m_registry; }
    AcpHistoryStore *historyStore() const { return m_historyStore; }

    // For tests — returns the QThread that owns the history store. Lets the
    // suite verify the store has been correctly moved off the main thread.
    QThread *historyStoreThread() const { return m_historyThread; }

    // Spawn a new agent session against workingDirectory. Returns the freshly
    // created dock (still parentless — caller is responsible for
    // addDockWidget()). Returns nullptr if the agent cannot be resolved at all.
    // When recordAsLastUsed is true, the resolved agent id is persisted as the
    // "last used" AI agent (the single chokepoint for that record).
    //
    // `context` is the workspace's ExecutionContext (D8/D9): when remote, the
    // agent is spawned on that host over an SSH exec channel with workingDirectory
    // as the remote cwd (captured at spawn, never re-resolved); when null or
    // local, the agent spawns locally exactly as before.
    AiAgentDock *openAgent(const QString &agentId, const QString &workingDirectory,
                           bool recordAsLastUsed = false,
                           remote::ExecutionContext *context = nullptr);

    // Spawn an agent with no dock (no chat UI). After initialize, sends `prompt`
    // and tears the session down when the turn ends, the process exits, or an
    // error occurs — so the child process does not linger and hold RAM.
    // Permissions are auto-approved (there is no UI to confirm them). Does not
    // record last-used and does not persist history. Returns the session id, or
    // empty if the agent cannot be resolved.
    QString runHeadlessPrompt(const QString &agentId, const QString &workingDirectory,
                              const QString &prompt,
                              remote::ExecutionContext *context = nullptr);

    // Inject the SSH exec-channel transport factory (D8). Set once by the app
    // layer (which links the SSH stack); the manager forwards it to every remote
    // AcpConnection so the connection never references SSH symbols directly. When
    // unset, a remote context falls back to local spawning.
    void setRemoteChannelBuilder(AcpConnection::RemoteChannelBuilder builder)
    {
        m_remoteChannelBuilder = std::move(builder);
    }
    const AcpConnection::RemoteChannelBuilder &remoteChannelBuilder() const
    {
        return m_remoteChannelBuilder;
    }

    // Cancel + tear down the session keyed by sessionId. Safe to call with an
    // unknown id (no-op).
    void closeSession(const QString &sessionId);

    // Delete the on-disk history file for `sessionId`. Forwarded to
    // AcpHistoryStore::deleteHistory via a queued connection (the store lives
    // on its own worker thread). Safe to call with an unknown id. This is
    // intentionally NOT called from closeSession — closing the dock keeps
    // history. Used by restartSession (W4) and exposed for callers that need
    // explicit history teardown.
    void deleteSessionHistory(const QString &sessionId);

    // Allocate a fresh session id and connection for the existing dock that
    // owns `oldSessionId`. The dock widget itself survives — only the inner
    // model + connection swap. The previous session's history file is
    // deleted. Returns the new session id, or an empty string when
    // oldSessionId is unknown / the dock is already gone.
    QString restartSession(const QString &oldSessionId);

    AcpConnection *connectionFor(const QString &sessionId) const;
    AcpSessionModel *modelFor(const QString &sessionId) const;

    // Flush the history worker and quit its thread. Called from
    // NotepadNextApplication's aboutToQuit handler.
    void shutdown();

signals:
    // Informational — emitted right before openAgent() returns. The dock is
    // still parentless at this point; the receiver (MainWindow) docks it.
    void agentOpened(const QString &sessionId, AiAgentDock *dock);

    // Fired once when a runHeadlessPrompt session ends, immediately before
    // closeSession. `ok` is true only for a completed prompt turn.
    void headlessSessionFinished(const QString &sessionId, bool ok);

private slots:
    void onIdleReaperTick();
    void onDockDestroyed(QObject *obj);

private:
    struct Session
    {
        AcpConnection *connection = nullptr;
        AcpSessionModel *model = nullptr;
        QPointer<AiAgentDock> dock;
        qint64 lastDockDetachedAtMs = 0;
        bool headless = false;
        bool headlessFinished = false;
    };

    void wireConnectionToModel(AcpConnection *conn, AcpSessionModel *model);
    void teardownSession(Session &session);
    void finishHeadlessSession(const QString &sessionId, bool ok);

    AcpAgentRegistry *m_registry = nullptr;
    ApplicationSettings *m_settings = nullptr;
    AcpHistoryStore *m_historyStore = nullptr;
    QThread *m_historyThread = nullptr;
    QTimer *m_idleReaperTimer = nullptr;
    AcpConnection::RemoteChannelBuilder m_remoteChannelBuilder; // app-injected (D8)

    QHash<QString, Session> m_sessions;
};

#endif // ACP_AGENT_MANAGER_H
