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

#include "AcpAgentManager.h"

#include "AcpAgentDefinition.h"
#include "AcpAgentRegistry.h"
#include "AcpConnection.h"
#include "AcpHistoryStore.h"
#include "AcpSessionModel.h"
#include "AiAgentDock.h"
#include "ApplicationSettings.h"
#include "ProfileScope.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUuid>

Q_LOGGING_CATEGORY(lcAcpManager, "notepadnext.acp.manager")

namespace {
constexpr int kIdleReaperIntervalMs = 5 * 60 * 1000;     // 5 minutes
constexpr qint64 kDockGoneTimeoutMs = 60 * 60 * 1000;    // 1 hour
} // namespace

AcpAgentManager::AcpAgentManager(ApplicationSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_registry = new AcpAgentRegistry(settings, this);

    m_historyStore = new AcpHistoryStore(nullptr);
    m_historyThread = new QThread(this);
    m_historyThread->setObjectName(QStringLiteral("AcpHistoryThread"));
    m_historyStore->moveToThread(m_historyThread);
    // Ensure the store is destroyed on its own thread.
    connect(m_historyThread, &QThread::finished,
            m_historyStore, &QObject::deleteLater);
    m_historyThread->start();

    m_idleReaperTimer = new QTimer(this);
    m_idleReaperTimer->setInterval(kIdleReaperIntervalMs);
    connect(m_idleReaperTimer, &QTimer::timeout,
            this, &AcpAgentManager::onIdleReaperTick);
    m_idleReaperTimer->start();
}

AcpAgentManager::~AcpAgentManager()
{
    // shutdown() is idempotent — call it here in case the caller forgot.
    shutdown();
}

AiAgentDock *AcpAgentManager::openAgent(const QString &agentId, const QString &workingDirectory)
{
    AcpAgentDefinition agent = m_registry->agent(agentId);
    if (agent.id.isEmpty()) {
        const QString fallbackId = m_registry->defaultAgentId();
        if (fallbackId != agentId) {
            agent = m_registry->agent(fallbackId);
        }
    }
    if (agent.id.isEmpty()) {
        qCWarning(lcAcpManager) << "openAgent: no agent matched id" << agentId
                                << "and default id was not resolvable";
        return nullptr;
    }

    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Canonicalize the working directory for use as the projectId. If the
    // canonical path cannot be resolved (e.g. directory doesn't exist yet) we
    // fall back to the raw path rather than refusing the spawn.
    QString projectId = QFileInfo(workingDirectory).canonicalFilePath();
    if (projectId.isEmpty()) {
        projectId = workingDirectory;
    }

    auto *conn = new AcpConnection(this);
    QPointer<AcpAgentRegistry> registryGuard(m_registry);
    conn->setAutoApprovePolicyProvider([registryGuard]() -> QString {
        return registryGuard ? registryGuard->autoApprovePolicy() : QString();
    });

    auto *model = new AcpSessionModel(sessionId, projectId, /*historyDirOverride=*/QString(), this);
    model->setHistoryStore(m_historyStore);

    wireConnectionToModel(conn, model);

    auto *dock = new AiAgentDock(sessionId, agent.name, projectId, model, conn, m_registry, this, m_settings, /*parent=*/nullptr);
    connect(dock, &QObject::destroyed,
            this, &AcpAgentManager::onDockDestroyed);
    connect(dock, &AiAgentDock::restartRequested,
            this, &AcpAgentManager::restartSession);

    Session session;
    session.connection = conn;
    session.model = model;
    session.dock = dock;
    m_sessions.insert(sessionId, session);

    emit agentOpened(sessionId, dock);

    // Spawn last — so the dock + model are already wired and ready to receive
    // signals by the time initialize/session/new complete.
    conn->spawn(agent, projectId);

    qCInfo(lcAcpManager) << "openAgent: spawned session" << sessionId
                         << "agent" << agent.id
                         << "cwd" << projectId;
    return dock;
}

void AcpAgentManager::closeSession(const QString &sessionId)
{
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) {
        return;
    }

    Session session = it.value();
    m_sessions.erase(it);
    teardownSession(session);
}

void AcpAgentManager::deleteSessionHistory(const QString &sessionId)
{
    if (sessionId.isEmpty() || !m_historyStore) {
        return;
    }
    // The store lives on its own worker thread — hop over via a queued
    // invocation so we never touch its state from the main thread.
    QMetaObject::invokeMethod(m_historyStore,
                              "deleteHistory",
                              Qt::QueuedConnection,
                              Q_ARG(QString, sessionId));
}

void AcpAgentManager::restartSession(const QString &oldSessionId)
{
    auto it = m_sessions.find(oldSessionId);
    if (it == m_sessions.end()) {
        qCWarning(lcAcpManager) << "restartSession: unknown id" << oldSessionId;
        return;
    }
    Session old = it.value();
    if (!old.dock) {
        qCWarning(lcAcpManager) << "restartSession: dock already gone for" << oldSessionId;
        m_sessions.erase(it);
        return;
    }

    // Snapshot what we need before tearing down the old connection.
    AcpAgentDefinition agent;
    QString workingDir;
    if (old.connection) {
        agent = old.connection->definition();
        workingDir = old.connection->workingDirectory();
    }
    if (workingDir.isEmpty()) {
        workingDir = old.dock->workingDirectory();
    }

    // Allocate the new session id.
    const QString newSessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Build the new connection + model BEFORE tearing down the old, so the
    // dock's rebind() is atomic.
    auto *newConn = new AcpConnection(this);
    QPointer<AcpAgentRegistry> registryGuard(m_registry);
    newConn->setAutoApprovePolicyProvider([registryGuard]() -> QString {
        return registryGuard ? registryGuard->autoApprovePolicy() : QString();
    });
    auto *newModel = new AcpSessionModel(newSessionId, workingDir,
                                         /*historyDirOverride=*/QString(), this);
    newModel->setHistoryStore(m_historyStore);
    wireConnectionToModel(newConn, newModel);

    // Persist an empty record for the new session BEFORE deleting the old —
    // spec scenario "Persist new HostSession record BEFORE deleting the old".
    if (m_historyStore) {
        QJsonObject empty;
        empty.insert(QStringLiteral("sessionId"), newSessionId);
        QMetaObject::invokeMethod(m_historyStore,
                                  "scheduleWrite",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, newSessionId),
                                  Q_ARG(QJsonObject, empty));
        QMetaObject::invokeMethod(m_historyStore,
                                  "flushAll",
                                  Qt::QueuedConnection);
    }

    // Rebind the existing dock to the new pair.
    old.dock->rebind(newConn, newModel, newSessionId, agent.name);

    // Now tear down the old connection + model. The dock survives.
    if (old.connection) {
        old.connection->disconnect();
        old.connection->cancelPrompt();
        old.connection->deleteLater();
    }
    if (old.model) {
        old.model->setHistoryStore(nullptr);
        old.model->deleteLater();
    }

    // Re-key the session table.
    m_sessions.erase(it);
    Session fresh;
    fresh.connection = newConn;
    fresh.model = newModel;
    fresh.dock = old.dock;
    m_sessions.insert(newSessionId, fresh);

    // Delete the prior session's history file (queued — store lives off-thread).
    deleteSessionHistory(oldSessionId);

    // Spawn the new agent only if we actually have a usable agent definition.
    if (!agent.command.isEmpty()) {
        newConn->spawn(agent, workingDir);
    }

    qCInfo(lcAcpManager) << "restartSession: replaced" << oldSessionId
                         << "with" << newSessionId
                         << "agent" << agent.id;
}

void AcpAgentManager::shutdown()
{
    PROFILE_SCOPE("AcpAgentManager::shutdown");
    if (m_idleReaperTimer) {
        m_idleReaperTimer->stop();
    }

    const auto sessionIds = m_sessions.keys();
    for (const QString &id : sessionIds) {
        auto it = m_sessions.find(id);
        if (it == m_sessions.end()) continue;
        Session session = it.value();
        m_sessions.erase(it);
        teardownSession(session);
    }

    if (m_historyStore && m_historyThread && m_historyThread->isRunning()) {
        QMetaObject::invokeMethod(m_historyStore, "flushAll", Qt::BlockingQueuedConnection);
    }

    if (m_historyThread && m_historyThread->isRunning()) {
        m_historyThread->quit();
        if (!m_historyThread->wait(5000)) {
            qCWarning(lcAcpManager) << "history thread did not quit within 5s";
        }
    }
}

void AcpAgentManager::onIdleReaperTick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<QString> toReap;
    for (auto it = m_sessions.cbegin(); it != m_sessions.cend(); ++it) {
        const Session &s = it.value();
        if (!s.dock.isNull()) continue;
        if (s.lastDockDetachedAtMs == 0) continue;
        if ((now - s.lastDockDetachedAtMs) > kDockGoneTimeoutMs) {
            toReap.append(it.key());
        }
    }
    for (const QString &id : toReap) {
        qCInfo(lcAcpManager) << "reaping idle session" << id;
        closeSession(id);
    }
}

void AcpAgentManager::onDockDestroyed(QObject *obj)
{
    Q_UNUSED(obj);
    // QPointer-stored docks are cleared during the destroyed() signal phase,
    // so we cannot reliably disambiguate by pointer here. Walk the table and
    // stamp any session whose dock has gone null but has no timestamp yet —
    // that is the session that just lost its dock.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        if (it.value().dock.isNull() && it.value().lastDockDetachedAtMs == 0) {
            it.value().lastDockDetachedAtMs = now;
        }
    }
}

void AcpAgentManager::wireConnectionToModel(AcpConnection *conn, AcpSessionModel *model)
{
    connect(conn, &AcpConnection::initialized,
            model, &AcpSessionModel::onInitialized);
    connect(conn, &AcpConnection::messageChunk,
            model, &AcpSessionModel::onMessageChunk);
    connect(conn, &AcpConnection::thoughtChunk,
            model, &AcpSessionModel::onThoughtChunk);
    connect(conn, &AcpConnection::toolCallReceived,
            model, &AcpSessionModel::onToolCallReceived);
    connect(conn, &AcpConnection::toolCallUpdated,
            model, &AcpSessionModel::onToolCallUpdated);
    connect(conn, &AcpConnection::planReceived,
            model, &AcpSessionModel::onPlanReceived);
    connect(conn, &AcpConnection::availableCommandsUpdated,
            model, &AcpSessionModel::onAvailableCommandsUpdated);
    connect(conn, &AcpConnection::currentModeChanged,
            model, &AcpSessionModel::onCurrentModeChanged);
    connect(conn, &AcpConnection::configOptionsUpdated,
            model, &AcpSessionModel::onConfigOptionsUpdated);
    connect(conn, &AcpConnection::usageUpdated,
            model, &AcpSessionModel::onUsageUpdated);
    connect(conn, &AcpConnection::usageReplaced,
            model, &AcpSessionModel::onUsageReplaced);
    connect(conn, &AcpConnection::promptStarted,
            model, &AcpSessionModel::onPromptStarted);
    connect(conn, &AcpConnection::promptEnded,
            model, &AcpSessionModel::onPromptEnded);
    // permissionRequested + errorOccurred bypass the model — the view will
    // connect directly to the connection for those (the dock holds both
    // pointers). See AiAgentDock for the wiring (Group 5).
}

void AcpAgentManager::teardownSession(Session &session)
{
    // Disconnect everything from the connection first so signals queued at
    // tear-down don't fire into a half-destroyed model.
    if (session.connection) {
        session.connection->disconnect();
        session.connection->cancelPrompt();
        session.connection->deleteLater();
        session.connection = nullptr;
    }
    if (session.model) {
        // Detach the history store before delete so no queued writes leak.
        session.model->setHistoryStore(nullptr);
        session.model->deleteLater();
        session.model = nullptr;
    }
    if (!session.dock.isNull()) {
        // WA_DeleteOnClose handles deletion if the user closed the dock.
        // If we're tearing down for reasons other than user-close (e.g. app
        // shutdown), close it explicitly.
        session.dock->close();
    }
}
