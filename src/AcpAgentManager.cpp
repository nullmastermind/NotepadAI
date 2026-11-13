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
#include "AcpErrorClassifier.h"
#include "AcpHistoryStore.h"
#include "AcpOpenSessions.h"
#include "AcpSessionModel.h"
#include "AcpSwitchAgent.h"
#include "AiAgentDock.h"
#include "AiDockGroup.h"
#include "ApplicationSettings.h"
#include "ClaudeSessionFiles.h"
#include "GrillMeMcp.h"
#include "ProfileScope.h"
#include "remote/ExecutionContext.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QThreadPool>
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

namespace {

// Question forms: the grill_me MCP server. Only for sessions with a chat view
// (never headless/judge).
void configureInteractiveQuestions(AcpConnection *conn, bool enabled, bool remote)
{
    if (!enabled)
        return;
    conn->setMcpServers(QJsonArray{GrillMeMcp::serverEntry(remote)});
}

} // namespace

AiAgentDock *AcpAgentManager::dockForGroup(const QString &groupKey) const
{
    return m_docksByGroup.value(groupKey).data();
}

AiAgentDock *AcpAgentManager::openAgent(const QString &agentId, const QString &workingDirectory,
                                        bool recordAsLastUsed, remote::ExecutionContext *context,
                                        bool raiseNewSession)
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

    // Single chokepoint for recording the "last used" AI agent. Uses the
    // RESOLVED agent.id (post-fallback), never the requested agentId.
    if (recordAsLastUsed && m_settings) {
        m_settings->setLastUsedAiAgentId(agent.id);
    }

    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Is this session remote? Only when the context is remote AND the app has
    // injected the SSH transport factory — otherwise we spawn locally (no
    // fallback to a local agent with a remote cwd is ever taken: the connection
    // probes the remote binary first and errors out if absent).
    const bool isRemote = context && context->isRemote() && m_remoteChannelBuilder;

    // projectId identifies the session's working dir. For a remote workspace the
    // path is on another host, so a local canonicalFilePath would resolve to
    // empty / a wrong local path — keep the remote path lexical. Local keeps the
    // exact canonicalization it always used.
    QString projectId;
    if (isRemote) {
        projectId = workingDirectory;
    } else {
        projectId = QFileInfo(workingDirectory).canonicalFilePath();
        if (projectId.isEmpty()) {
            projectId = workingDirectory;
        }
    }

    // Remote (capture-at-spawn): the connection probes the binary on
    // `context`'s host, then builds the SSH transport via the injected factory
    // with projectId as the remote cwd.
    AcpConnection *conn = newConnection(agent, isRemote ? context : nullptr);

    auto *model = new AcpSessionModel(sessionId, projectId, /*historyDirOverride=*/QString(), this);
    model->setHistoryStore(m_historyStore);

    wireConnectionToModel(conn, model);

    const QString sshProfileId = context ? context->sshProfileId() : QString();
    const QString groupKey = aiDockGroupKey(projectId, sshProfileId);

    if (AiAgentDock *existing = m_docksByGroup.value(groupKey).data()) {
        existing->addSlot(sessionId, agent.name, model, conn, raiseNewSession);
        Session session;
        session.connection = conn;
        session.model = model;
        session.dock = existing;
        m_sessions.insert(sessionId, session);
        emit agentOpened(sessionId, existing);
        conn->spawn(agent, projectId);
        schedulePersistOpenSessions();
        qCInfo(lcAcpManager) << "openAgent: appended session" << sessionId
                             << "agent" << agent.id
                             << "cwd" << projectId
                             << "group" << groupKey
                             << "raise" << raiseNewSession;
        return existing;
    }

    AiAgentDock *dock = newDock(sessionId, agent.name, projectId, sshProfileId, model, conn);

    Session session;
    session.connection = conn;
    session.model = model;
    session.dock = dock;
    m_sessions.insert(sessionId, session);

    emit agentOpened(sessionId, dock);

    // Spawn last — so the dock + model are already wired and ready to receive
    // signals by the time initialize/session/new complete.
    conn->spawn(agent, projectId);
    schedulePersistOpenSessions();

    qCInfo(lcAcpManager) << "openAgent: spawned session" << sessionId
                         << "agent" << agent.id
                         << "cwd" << projectId
                         << "remote" << isRemote;
    return dock;
}

QString AcpAgentManager::runHeadlessPrompt(const QString &agentId, const QString &workingDirectory,
                                           const QString &prompt, remote::ExecutionContext *context)
{
    AcpAgentDefinition agent = m_registry->agent(agentId);
    if (agent.id.isEmpty()) {
        const QString fallbackId = m_registry->defaultAgentId();
        if (fallbackId != agentId) {
            agent = m_registry->agent(fallbackId);
        }
    }
    if (agent.id.isEmpty()) {
        qCWarning(lcAcpManager) << "runHeadlessPrompt: no agent matched id" << agentId
                                << "and default id was not resolvable";
        return {};
    }

    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const bool isRemote = context && context->isRemote() && m_remoteChannelBuilder;

    QString projectId;
    if (isRemote) {
        projectId = workingDirectory;
    } else {
        projectId = QFileInfo(workingDirectory).canonicalFilePath();
        if (projectId.isEmpty()) {
            projectId = workingDirectory;
        }
    }

    auto *conn = new AcpConnection(this);
    // No permission UI on a headless session — auto-approve every request.
    conn->setAutoApprovePolicyProvider([]() {
        return QStringLiteral("allowAll");
    });
    if (isRemote) {
        conn->setRemoteSpawn(context, m_remoteChannelBuilder);
    }

    auto *model = new AcpSessionModel(sessionId, projectId, /*historyDirOverride=*/QString(), this);
    // Fire-and-forget: skip history persistence so teardown leaves no disk residue.
    model->setHistoryStore(nullptr);
    wireConnectionToModel(conn, model);

    Session session;
    session.connection = conn;
    session.model = model;
    session.headless = true;
    // Stamp as detached so the idle reaper is a backstop if the turn never ends.
    session.lastDockDetachedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_sessions.insert(sessionId, session);

    connect(conn, &AcpConnection::initialized, this, [this, sessionId, prompt]() {
        AcpConnection *c = connectionFor(sessionId);
        if (!c) {
            return;
        }
        c->sendPrompt(prompt, {});
    });
    connect(conn, &AcpConnection::promptEnded, this, [this, sessionId]() {
        finishHeadlessSession(sessionId, true);
    });
    connect(conn, &AcpConnection::errorOccurred, this,
            [this, sessionId](AcpErrorClassifier::AcpErrorKind, const QString &) {
                finishHeadlessSession(sessionId, false);
            });
    connect(conn, &AcpConnection::requestFailed, this, [this, sessionId](const QString &) {
        finishHeadlessSession(sessionId, false);
    });
    connect(conn, &AcpConnection::agentExited, this,
            [this, sessionId](int, QProcess::ExitStatus) {
                finishHeadlessSession(sessionId, false);
            });

    conn->spawn(agent, projectId);

    qCInfo(lcAcpManager) << "runHeadlessPrompt: spawned session" << sessionId
                         << "agent" << agent.id
                         << "cwd" << projectId
                         << "remote" << isRemote;
    return sessionId;
}

void AcpAgentManager::finishHeadlessSession(const QString &sessionId, bool ok)
{
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || !it->headless || it->headlessFinished) {
        return;
    }
    it->headlessFinished = true;
    emit headlessSessionFinished(sessionId, ok);
    closeSession(sessionId);
}

void AcpAgentManager::closeSession(const QString &sessionId)
{
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end()) {
        return;
    }

    abortSwitch(sessionId);
    Session session = it.value();
    m_sessions.erase(it);
    if (session.dock)
        session.dock->detachSlot(sessionId);
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

QString AcpAgentManager::restartSession(const QString &oldSessionId)
{
    auto it = m_sessions.find(oldSessionId);
    if (it == m_sessions.end()) {
        qCWarning(lcAcpManager) << "restartSession: unknown id" << oldSessionId;
        return {};
    }
    abortSwitch(oldSessionId);
    // Copy required: m_sessions is re-keyed (erase + insert) further down,
    // which invalidates `it`. We read `old` after that, so a reference into
    // it.value() would dangle.
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    Session old = it.value();
    if (!old.dock) {
        qCWarning(lcAcpManager) << "restartSession: dock already gone for" << oldSessionId;
        m_sessions.erase(it);
        return {};
    }

    // Snapshot what we need before tearing down the old connection.
    AcpAgentDefinition agent;
    QString workingDir;
    remote::ExecutionContext *execCtx = nullptr;
    if (old.connection) {
        agent = old.connection->definition();
        workingDir = old.connection->workingDirectory();
        execCtx = old.connection->executionContext();
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

    // Rebind the existing dock to the new pair. objectName stays the group key.
    old.dock->rebind(oldSessionId, newConn, newModel, newSessionId, agent.name);

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
        const bool isRemote = execCtx && execCtx->isRemote() && m_remoteChannelBuilder;
        if (isRemote) {
            newConn->setRemoteSpawn(execCtx, m_remoteChannelBuilder);
        }
        // Pick up a settings change made since the first spawn.
        const AcpAgentDefinition latest = m_registry->agent(agent.id);
        configureInteractiveQuestions(newConn,
                                      latest.id.isEmpty() ? agent.interactiveQuestions
                                                          : latest.interactiveQuestions,
                                      isRemote);
        newConn->spawn(agent, workingDir);
    }

    qCInfo(lcAcpManager) << "restartSession: replaced" << oldSessionId
                         << "with" << newSessionId
                         << "agent" << agent.id;
    return newSessionId;
}

AcpConnection *AcpAgentManager::connectionFor(const QString &sessionId) const
{
    auto it = m_sessions.constFind(sessionId);
    return (it == m_sessions.cend()) ? nullptr : it.value().connection;
}

AcpSessionModel *AcpAgentManager::modelFor(const QString &sessionId) const
{
    auto it = m_sessions.constFind(sessionId);
    return (it == m_sessions.cend()) ? nullptr : it.value().model;
}

bool AcpAgentManager::sessionIsBusy(const QString &sessionId) const
{
    auto it = m_sessions.constFind(sessionId);
    if (it == m_sessions.cend())
        return false;
    if (it.value().dock)
        return it.value().dock->isSessionBusy(sessionId);
    return it.value().model && it.value().model->isProcessing();
}

void AcpAgentManager::shutdown()
{
    PROFILE_SCOPE("AcpAgentManager::shutdown");
    if (m_idleReaperTimer) {
        m_idleReaperTimer->stop();
    }
    freezeOpenSessions();
    const auto switching = m_switching.keys();
    for (const QString &id : switching)
        abortSwitch(id);
    // A session-file copy still running would post back into a dying manager.
    m_switchPool.waitForDone();

    const auto sessionIds = m_sessions.keys();
    for (const QString &id : sessionIds) {
        auto it = m_sessions.find(id);
        if (it == m_sessions.end()) continue;
        Session session = it.value();
        m_sessions.erase(it);
        if (session.dock)
            session.dock->detachSlot(id);
        teardownSession(session);
    }
    m_docksByGroup.clear();

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
    for (auto it = m_docksByGroup.begin(); it != m_docksByGroup.end(); ) {
        if (it.value().isNull())
            it = m_docksByGroup.erase(it);
        else
            ++it;
    }
}

void AcpAgentManager::wireConnectionToModel(AcpConnection *conn, AcpSessionModel *model)
{
    if (m_scheduledNextFire)
        conn->setScheduledNextFireProvider(m_scheduledNextFire);
    connect(conn, &AcpConnection::initialized,
            model, &AcpSessionModel::onInitialized);
    // Restored sessions and Switch agent resume the agent's own session id.
    connect(conn, &AcpConnection::initialized, model, [model, conn]() {
        if (conn->isSessionReady())
            model->setAcpSessionId(conn->sessionId());
    });
    // The session name rides on the next session/new or session/resume (a
    // live session is renamed by AiAgentDock::renameSession with `/rename`).
    conn->setSessionName(model->sessionName());
    connect(model, &AcpSessionModel::sessionNameChanged, conn, &AcpConnection::setSessionName);
    connect(conn, &AcpConnection::messageChunk,
            model, qOverload<const QString &, const QString &>(&AcpSessionModel::onMessageChunk));
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
    connect(conn, &AcpConnection::promptEnded,
            this, &AcpAgentManager::agentTurnEnded);
    connect(conn, &AcpConnection::asyncTasksChanged,
            model, &AcpSessionModel::onAsyncTasksChanged);
    // permissionRequested + errorOccurred bypass the model — the view will
    // connect directly to the connection for those (the dock holds both
    // pointers). See AiAgentDock for the wiring (Group 5).

    // String-based invokeMethod because NotepadNextApplication.h pulls in
    // SingleApplication which isn't available in test targets. Compile-time
    // safety: GitController.cpp uses a typed connect to the same signal, so
    // a rename breaks the build there.
    connect(conn, &AcpConnection::fileWrittenOnDisk, this, [](const QString &path) {
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  "gitWorkingTreeDirtied",
                                  Q_ARG(QString, path));
    });

    // Trigger git refresh when an agent completes a file-modifying tool call
    // (Edit/Write). Most agents write files directly and only send tool_call
    // notifications — they don't use the ACP fs/write_text_file call above.
    // Context `conn`: Switch agent replaces a session's connection and this
    // must die with the old one.
    connect(model, &AcpSessionModel::toolCallAddedOrUpdated, conn,
            [model, conn](const QString &toolCallId) {
        const auto it = model->toolCalls().constFind(toolCallId);
        if (it == model->toolCalls().constEnd())
            return;
        const auto &tc = it.value();
        if (tc.status != QLatin1String("completed"))
            return;
        if (!AcpProtocol::toolCallLooksFileMutating(tc))
            return;
        QString path = AcpProtocol::toolCallMutatedPath(tc.rawInput, tc.content, tc.rawOutput);
        if (path.isEmpty())
            path = conn->workingDirectory();
        if (path.isEmpty())
            return;
        QMetaObject::invokeMethod(QCoreApplication::instance(),
                                  "gitWorkingTreeDirtied",
                                  Q_ARG(QString, path));
    });
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
    // The dock is shared across slots. closeSession already called detachSlot
    // (last slot destroys the widget). Never dock->close() here — that would
    // close a sibling slot.
}

// ---------- Shared construction ------------------------------------------

AcpConnection *AcpAgentManager::newConnection(const AcpAgentDefinition &agent,
                                              remote::ExecutionContext *remoteContext)
{
    auto *conn = new AcpConnection(this);
    QPointer<AcpAgentRegistry> registryGuard(m_registry);
    conn->setAutoApprovePolicyProvider([registryGuard]() -> QString {
        return registryGuard ? registryGuard->autoApprovePolicy() : QString();
    });
    const bool isRemote = remoteContext && remoteContext->isRemote() && m_remoteChannelBuilder;
    // Set before spawn(): the connection captures its host at spawn time.
    if (isRemote)
        conn->setRemoteSpawn(remoteContext, m_remoteChannelBuilder);
    configureInteractiveQuestions(conn, agent.interactiveQuestions, isRemote);
    return conn;
}

AiAgentDock *AcpAgentManager::newDock(const QString &sessionId, const QString &agentName,
                                      const QString &projectId, const QString &sshProfileId,
                                      AcpSessionModel *model, AcpConnection *conn)
{
    const QString groupKey = aiDockGroupKey(projectId, sshProfileId);
    auto *dock = new AiAgentDock(sessionId, agentName, projectId, model, conn, m_registry, this, m_settings,
                                 /*parent=*/nullptr);
    dock->setObjectName(aiDockObjectName(groupKey));
    dock->setSshProfileId(sshProfileId);
    m_docksByGroup.insert(groupKey, dock);
    m_dockOrder.append(dock);
    connect(dock, &QObject::destroyed, this, &AcpAgentManager::onDockDestroyed);
    connect(dock, &AiAgentDock::restartRequested, this, &AcpAgentManager::restartSession);
    connect(dock, &AiAgentDock::slotsChanged, this, &AcpAgentManager::schedulePersistOpenSessions);
    return dock;
}

// ---------- Switch agent ---------------------------------------------------

namespace {

constexpr int kSwitchTimeoutMs = 120 * 1000;
constexpr char kMarkerAgentSwitched[] = "agent-switched";
constexpr char kMarkerAgentSwitchFailed[] = "agent-switch-failed";

struct SwitchPrep
{
    bool ok = false;
    QString error;
};

// Pool thread: find the session file, check it holds the whole conversation,
// copy it into the target's config dir when that differs.
SwitchPrep prepareSwitch(const QString &sourceConfig, const QString &targetConfig, const QString &cwd,
                         const QString &acpSessionId, qint64 conversationNewestMs)
{
    SwitchPrep prep;
    const QString transcript = ClaudeSessionFiles::locateTranscript(sourceConfig, cwd, acpSessionId);
    if (transcript.isEmpty()) {
        prep.error = AcpAgentManager::tr("Claude Code has no saved conversation for this session in %1")
                         .arg(QDir::toNativeSeparators(sourceConfig));
        return prep;
    }
    const qint64 fileNewest = ClaudeSessionFiles::newestRecordMs(transcript);
    if (!AcpSwitchAgent::transcriptIsCurrent(fileNewest, conversationNewestMs)) {
        prep.error = AcpAgentManager::tr("Claude Code's saved conversation is older than this session "
                                         "(its context was probably cleared)");
        return prep;
    }
    if (!ClaudeSessionFiles::sameDir(sourceConfig, targetConfig)) {
        const ClaudeSessionFiles::CopyOutcome copy = ClaudeSessionFiles::copySession(transcript, targetConfig);
        if (!copy.ok) {
            prep.error = AcpAgentManager::tr("could not copy the conversation: %1").arg(copy.error);
            return prep;
        }
    }
    prep.ok = true;
    return prep;
}

} // namespace

void AcpAgentManager::switchAgent(const QString &sessionId, const QString &targetAgentId)
{
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || !it->dock || !it->connection || !it->model || it->headless)
        return;
    if (m_switching.contains(sessionId))
        return;
    AcpConnection *source = it->connection;
    AcpSessionModel *model = it->model;
    AiAgentDock *dock = it->dock;
    const AcpAgentDefinition sourceAgent = source->definition();
    const AcpAgentDefinition target = m_registry->agent(targetAgentId);
    if (target.id.isEmpty() || target.id == sourceAgent.id || !AcpSwitchAgent::isClaudeCodeAgent(target))
        return;
    if (AcpSwitchAgent::blockReason(dock->switchState(sessionId)) != AcpSwitchAgent::Block::None)
        return;

    const QString cwd = source->workingDirectory().isEmpty() ? dock->workingDirectory()
                                                            : source->workingDirectory();
    const QString acpId = !model->acpSessionId().isEmpty() ? model->acpSessionId() : source->sessionId();
    // Nothing the agent saw yet (no prompt, or no agent session): just start
    // the target fresh. Otherwise its saved conversation must move with it.
    const bool resume = AcpSwitchAgent::hasConversation(model->messages()) && !acpId.isEmpty();

    SwitchOp op;
    op.source = source;
    op.targetAgent = target;
    op.sourceName = sourceAgent.name;
    m_switching.insert(sessionId, op);
    dock->setSessionSwitching(sessionId, target.name);
    qCInfo(lcAcpManager) << "switchAgent:" << sessionId << sourceAgent.id << "->" << target.id
                         << "resume" << resume;

    if (!resume) {
        startSwitchTarget(sessionId, false, QString(), cwd);
        return;
    }

    const QProcessEnvironment processEnv = QProcessEnvironment::systemEnvironment();
    const QString home = QDir::homePath();
    const QString sourceConfig = ClaudeSessionFiles::configDir(sourceAgent.env, processEnv, home);
    const QString targetConfig = ClaudeSessionFiles::configDir(target.env, processEnv, home);
    const qint64 newest = AcpSwitchAgent::newestConversationMs(model->messages());
    m_switchPool.start([this, sessionId, sourceConfig, targetConfig, cwd, acpId, newest]() {
        const SwitchPrep prep = prepareSwitch(sourceConfig, targetConfig, cwd, acpId, newest);
        QMetaObject::invokeMethod(this, [this, sessionId, prep, acpId, cwd]() {
            if (!m_switching.contains(sessionId))
                return; // closed, restarted or shut down meanwhile
            if (!prep.ok) {
                failSwitch(sessionId, prep.error);
                return;
            }
            startSwitchTarget(sessionId, true, acpId, cwd);
        }, Qt::QueuedConnection);
    });
}

void AcpAgentManager::startSwitchTarget(const QString &sessionId, bool resume, const QString &acpSessionId,
                                        const QString &cwd)
{
    auto opIt = m_switching.find(sessionId);
    if (opIt == m_switching.end())
        return;
    const AcpAgentDefinition target = opIt->targetAgent;
    AcpConnection *conn = newConnection(target, nullptr); // local only (blockReason)
    if (resume)
        conn->setResumeSessionId(acpSessionId);
    // Wired to the model only on commit, but the name must ride on the resume.
    if (const auto sit = m_sessions.constFind(sessionId); sit != m_sessions.constEnd() && sit->model)
        conn->setSessionName(sit->model->sessionName());
    opIt->target = conn;

    // Not wired to the model or view until it has resumed: a failure must
    // leave the session exactly as it was.
    connect(conn, &AcpConnection::initialized, this, [this, sessionId, conn]() {
        commitSwitch(sessionId, conn);
    });
    connect(conn, &AcpConnection::resumeFailed, this, [this, sessionId](const QString &reason) {
        failSwitch(sessionId, reason);
    });
    connect(conn, &AcpConnection::errorOccurred, this,
            [this, sessionId](AcpErrorClassifier::AcpErrorKind, const QString &friendly) {
                failSwitch(sessionId, friendly);
            });
    connect(conn, &AcpConnection::agentExited, this, [this, sessionId](int exitCode, QProcess::ExitStatus) {
        failSwitch(sessionId, tr("the agent exited (code %1)").arg(exitCode));
    });
    QTimer::singleShot(kSwitchTimeoutMs, conn, [this, sessionId, conn]() {
        failSwitch(sessionId, tr("the agent did not answer within %1 s").arg(kSwitchTimeoutMs / 1000), conn);
    });
    conn->spawn(target, cwd);
}

void AcpAgentManager::commitSwitch(const QString &sessionId, AcpConnection *target)
{
    auto opIt = m_switching.find(sessionId);
    if (opIt == m_switching.end() || opIt->target != target)
        return;
    const SwitchOp op = std::move(opIt.value());
    m_switching.erase(opIt);
    // Drop the temporary outcome handlers (all use `this` as context).
    disconnect(target, nullptr, this, nullptr);

    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end() || !it->dock || !it->model) {
        target->deleteLater();
        return;
    }
    AcpConnection *old = it->connection;
    wireConnectionToModel(target, it->model);
    it->connection = target;
    it->dock->rebindConnection(sessionId, target, op.targetAgent.name);
    // The model learns the new agent's catalogs (and session id); the view
    // re-applies the session's model / mode / effort on them.
    target->reemitInitialized();
    it->model->appendSystemMessage(tr("Switched from %1 to %2").arg(op.sourceName, op.targetAgent.name),
                                   QLatin1String(kMarkerAgentSwitched));
    if (old) {
        old->disconnect();
        old->cancelPrompt();
        old->deleteLater();
    }
    schedulePersistOpenSessions();
    qCInfo(lcAcpManager) << "switchAgent: committed" << sessionId << "->" << op.targetAgent.id;
}

void AcpAgentManager::failSwitch(const QString &sessionId, const QString &reason, const AcpConnection *target)
{
    auto opIt = m_switching.find(sessionId);
    // `target` set: only that attempt may fail (a committed one's timer must
    // not fail a later switch of the same session).
    if (opIt == m_switching.end() || (target && opIt->target != target))
        return;
    const SwitchOp op = std::move(opIt.value());
    m_switching.erase(opIt);
    if (op.target) {
        op.target->disconnect(this);
        op.target->deleteLater(); // kills the target process; the source keeps running
    }
    auto it = m_sessions.find(sessionId);
    if (it == m_sessions.end())
        return;
    if (it->dock)
        it->dock->setSessionSwitching(sessionId, QString());
    if (it->model) {
        it->model->appendSystemMessage(tr("Could not switch to %1: %2").arg(op.targetAgent.name, reason),
                                       QLatin1String(kMarkerAgentSwitchFailed));
    }
    qCInfo(lcAcpManager) << "switchAgent: failed" << sessionId << reason;
}

void AcpAgentManager::abortSwitch(const QString &sessionId)
{
    auto opIt = m_switching.find(sessionId);
    if (opIt == m_switching.end())
        return;
    if (opIt->target) {
        opIt->target->disconnect(this);
        opIt->target->deleteLater();
    }
    m_switching.erase(opIt);
}

// ---------- Restored sessions ----------------------------------------------

void AcpAgentManager::schedulePersistOpenSessions()
{
    if (m_openSessionsFrozen || m_restoring || m_persistQueued || !m_settings)
        return;
    m_persistQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_persistQueued = false;
        if (!m_openSessionsFrozen && !m_restoring)
            persistOpenSessions();
    });
}

void AcpAgentManager::persistOpenSessions()
{
    if (!m_settings)
        return;
    QVector<AcpOpenSessions::Group> groups;
    for (auto it = m_dockOrder.begin(); it != m_dockOrder.end();) {
        if (it->isNull()) {
            it = m_dockOrder.erase(it);
            continue;
        }
        AiAgentDock *dock = it->data();
        ++it;
        AcpOpenSessions::Group group;
        group.cwd = dock->workingDirectory();
        group.sshProfileId = dock->sshProfileId();
        group.current = dock->currentIndex();
        group.sessions = dock->openSessions();
        if (!group.sessions.isEmpty())
            groups.append(std::move(group));
    }
    m_settings->setValue(QLatin1String(AcpOpenSessions::kSettingsKey),
                         QString::fromUtf8(AcpOpenSessions::toJson(groups)));
}

void AcpAgentManager::freezeOpenSessions()
{
    if (m_openSessionsFrozen)
        return;
    for (const QPointer<AiAgentDock> &dock : std::as_const(m_dockOrder)) {
        if (dock)
            dock->noteAppClosing();
    }
    if (!m_restoring)
        persistOpenSessions();
    m_openSessionsFrozen = true;
}

QList<AiAgentDock *> AcpAgentManager::restoreOpenSessions(const ContextResolver &resolve)
{
    QList<AiAgentDock *> docks;
    if (!m_settings || m_openSessionsFrozen)
        return docks;
    const QVector<AcpOpenSessions::Group> groups = AcpOpenSessions::fromJson(
        m_settings->value(QLatin1String(AcpOpenSessions::kSettingsKey)).toString().toUtf8());
    m_restoring = true;
    for (const AcpOpenSessions::Group &group : groups) {
        remote::ExecutionContext *context = nullptr;
        if (!group.sshProfileId.isEmpty()) {
            context = resolve ? resolve(group.sshProfileId) : nullptr;
            if (!context)
                continue; // SSH profile removed
        } else if (!QFileInfo(group.cwd).isDir()) {
            continue; // folder removed
        }
        const bool isRemote = context && context->isRemote() && m_remoteChannelBuilder;
        const QString groupKey = aiDockGroupKey(group.cwd, group.sshProfileId);
        AiAgentDock *dock = m_docksByGroup.value(groupKey).data();
        const bool newGroupDock = !dock;
        for (const AcpOpenSessions::Session &saved : group.sessions) {
            if (m_sessions.contains(saved.id))
                continue;
            AcpAgentDefinition agent = m_registry->agent(saved.agentId);
            const bool agentMissing = agent.id.isEmpty();
            if (agentMissing) {
                agent.id = saved.agentId;
                agent.name = saved.agentName.isEmpty() ? tr("Removed agent") : saved.agentName;
            }
            auto *model = new AcpSessionModel(saved.id, group.cwd, /*historyDirOverride=*/QString(), this);
            model->setHistoryStore(m_historyStore);
            AcpConnection *conn = newConnection(agent, isRemote ? context : nullptr);
            wireConnectionToModel(conn, model);
            // Its agent starts on the first prompt. A conversation resumes the
            // agent's own session; one that cannot must say so rather than
            // start a new session under the old transcript.
            // A draft (host notices at most) has nothing to resume.
            if (agentMissing)
                conn->setCannotResume(tr("the agent \"%1\" no longer exists").arg(agent.name));
            else if (AcpSwitchAgent::hasConversation(model->messages()) && model->acpSessionId().isEmpty())
                conn->setCannotResume(tr("this conversation was saved without its agent session"));
            else if (AcpSwitchAgent::hasConversation(model->messages()))
                conn->setResumeSessionId(model->acpSessionId());
            conn->prepareDormant(agent, group.cwd);

            Session session;
            session.connection = conn;
            session.model = model;
            if (!dock) {
                dock = newDock(saved.id, agent.name, group.cwd, group.sshProfileId, model, conn);
            } else {
                dock->addSlot(saved.id, agent.name, model, conn, /*makeCurrent=*/true);
            }
            session.dock = dock;
            m_sessions.insert(saved.id, session);
        }
        if (dock) {
            // By id: skipped sessions or slots already in the dock shift indexes.
            const QString currentId = group.sessions.at(group.current).id;
            for (int i = 0; i < dock->slotCount(); ++i) {
                if (dock->sessionIdAt(i) == currentId) {
                    dock->switchTo(i);
                    break;
                }
            }
            if (newGroupDock)
                docks.append(dock);
        }
    }
    m_restoring = false;
    schedulePersistOpenSessions();
    qCInfo(lcAcpManager) << "restoreOpenSessions: restored" << docks.size() << "dock(s)";
    return docks;
}
