/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

class QThread;
class QTimer;

class AcpAgentRegistry;
class AcpConnection;
class AcpHistoryStore;
class AcpSessionModel;
class AiAgentDock;
class ApplicationSettings;

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
    AiAgentDock *openAgent(const QString &agentId, const QString &workingDirectory);

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
    // deleted. No-op when oldSessionId is unknown.
    void restartSession(const QString &oldSessionId);

    // Flush the history worker and quit its thread. Called from
    // NotepadNextApplication's aboutToQuit handler.
    void shutdown();

signals:
    // Informational — emitted right before openAgent() returns. The dock is
    // still parentless at this point; the receiver (MainWindow) docks it.
    void agentOpened(const QString &sessionId, AiAgentDock *dock);

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
    };

    void wireConnectionToModel(AcpConnection *conn, AcpSessionModel *model);
    void teardownSession(Session &session);

    AcpAgentRegistry *m_registry = nullptr;
    AcpHistoryStore *m_historyStore = nullptr;
    QThread *m_historyThread = nullptr;
    QTimer *m_idleReaperTimer = nullptr;

    QHash<QString, Session> m_sessions;
};

#endif // ACP_AGENT_MANAGER_H
