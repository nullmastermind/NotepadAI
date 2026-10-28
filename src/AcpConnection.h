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

#ifndef ACP_CONNECTION_H
#define ACP_CONNECTION_H

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>

#include "AcpAgentDefinition.h"
#include "AcpErrorClassifier.h"
#include "AcpProtocol.h"
#include "AcpScheduledJobs.h"

Q_DECLARE_LOGGING_CATEGORY(lcAcp)

class QTimer;
class IAcpProcessChannel;

namespace remote { class ExecutionContext; }

class AcpConnection : public QObject
{
    Q_OBJECT

public:
    explicit AcpConnection(QObject *parent = nullptr);
    ~AcpConnection() override;

    // Spawn the agent child process. Asynchronous — the `initialized()` signal
    // fires after `initialize` + `session/new` (or `session/resume`) complete.
    void spawn(const AcpAgentDefinition &agent, const QString &workingDirectory);

    // Resume this agent session id (`session/resume`, no replay) instead of
    // opening a new one on the next spawn()/wake(). Failure — the agent lacks
    // `sessionCapabilities.resume`, the request errors, or the process dies
    // first — emits resumeFailed() once (errorOccurred/agentExited still fire).
    void setResumeSessionId(const QString &sessionId) { m_resumeSessionId = sessionId; }
    QString resumeSessionId() const { return m_resumeSessionId; }

    // Record what to run without starting it: a restored session's agent
    // starts only when the user sends something (wake()).
    void prepareDormant(const AcpAgentDefinition &agent, const QString &workingDirectory);
    bool isDormant() const { return m_dormant; }
    void wake();
    // A restored conversation that cannot be resumed (agent removed, no saved
    // agent session id): wake() then fails with `reason` instead of spawning,
    // never silently starting a new session under the old transcript.
    void setCannotResume(const QString &reason) { m_cannotResumeReason = reason; }
    // Dormant and its first prompt must wait for a resume (or its failure).
    bool needsResume() const
    {
        return m_dormant && (!m_resumeSessionId.isEmpty() || !m_cannotResumeReason.isEmpty());
    }
    // session/new or session/resume has answered.
    bool isSessionReady() const { return !m_sessionId.isEmpty(); }
    // initialized() again with the last session setup, for a model wired after
    // the fact (Switch agent wires it only once the resume succeeded).
    void reemitInitialized();

    // Factory for the SSH exec-channel transport (D8). It builds an
    // IAcpProcessChannel that runs the agent on the remote host (cwd = the
    // remote workspace root). Injected by the app layer so AcpConnection never
    // links the SSH stack directly — the parameter is the abstract
    // ExecutionContext; the builder downcasts and pulls the connection out.
    using RemoteChannelBuilder = std::function<IAcpProcessChannel *(
        remote::ExecutionContext *ctx, const AcpAgentDefinition &agent,
        const QString &remoteCwd, QObject *parent)>;

    // Mark this connection's NEXT spawn() as remote: probe the binary on `ctx`'s
    // host and, if present, build the agent's transport via `builder` (cwd =
    // workingDirectory passed to spawn, captured at spawn — not re-resolved). A
    // null ctx / builder leaves the connection on the local QProcess path.
    void setRemoteSpawn(remote::ExecutionContext *ctx, RemoteChannelBuilder builder);

    // Auto-approve policy provider — Group 4 will hand a lambda that queries
    // the registry at request time so the latest policy is always observed.
    void setAutoApprovePolicyProvider(std::function<QString()> provider);
    void setMcpServers(const QJsonArray &servers);
    // An MCP server named `name` is among those set for new sessions.
    bool hasMcpServer(QLatin1String name) const;

    QString sessionId() const { return m_sessionId; }
    AcpProtocol::AcpAgentInfo agentInfo() const { return m_agentInfo; }
    AcpProtocol::AcpCapabilities capabilities() const { return m_capabilities; }
    AcpAgentDefinition definition() const { return m_agent; }
    QString workingDirectory() const { return m_workingDir; }
    remote::ExecutionContext *executionContext() const { return m_execContext; }

    // In-memory ring-buffered debug log of every spawn/stdin/stdout/stderr/error
    // event seen by this connection. Each entry is a single timestamped line.
    // Capped at kDebugLogMaxLines — oldest entries are dropped. Exposed for the
    // dock's "Debug" popup so users can self-diagnose connection failures.
    QStringList debugLog() const { return m_debugLog; }
    void clearDebugLog();

    // Test seam — invokes handleProcessFinished as if the underlying transport
    // (QProcess locally, the SSH exec channel remotely) had exited with the given
    // code/status. Used by exit/restart tests so the suite doesn't need to spawn
    // a real subprocess.
    void simulateProcessFinished(int exitCode, QProcess::ExitStatus status)
    {
        handleProcessFinished(exitCode, status);
    }

    // Test seam — adopt a process channel without spawn() so tests can observe
    // outbound JSON-RPC (e.g. session/cancel) without a real subprocess.
    // Caller must start() the channel afterwards if writes should be delivered.
    void attachChannelForTest(IAcpProcessChannel *channel)
    {
        adoptChannel(channel);
    }

    void setSessionIdForTest(const QString &id) { m_sessionId = id; }
    void setDefinitionForTest(const AcpAgentDefinition &agent) { m_agent = agent; }

    // The last promptEnded was the agent's own end of a model cycle (a cycle
    // result), not the 250 ms quiet-period guess a wake-up turn otherwise
    // ends on. Valid until the next promptStarted.
    bool turnEndConfirmed() const { return m_turnEndConfirmed; }

    // Live background (async) tasks the agent reported via AIR `async_task_*`.
    int backgroundTaskCount() const { return m_liveAsyncTasks.size(); }
    // Live background tasks in spawn order (for a "stop which one?" picker).
    QList<AcpProtocol::AcpBackgroundTask> backgroundTasks() const
    {
        return QList<AcpProtocol::AcpBackgroundTask>(m_liveAsyncTasks.cbegin(), m_liveAsyncTasks.cend());
    }
    bool hasStoppableBackgroundTask() const;
    // Wake-ups / cron jobs the agent scheduled for itself, in creation order.
    const QVector<AcpProtocol::AcpScheduledJob> &scheduledJobs() const { return m_scheduledJobs.jobs(); }
    // Lets one-shot cron jobs expire at their next fire time (default: 7 days).
    void setScheduledNextFireProvider(AcpProtocol::ScheduledJobTracker::NextFireFn fn)
    {
        m_scheduledJobs.setNextFireProvider(std::move(fn));
    }
    // True while background tasks are alive, and for a short grace after the
    // last one finished: Claude Code wakes the model to read the result, and
    // that wake-up turn starts a moment after the task's final state update.
    // Supervisors (goal judge, auto-compact) must not act before it settles.
    bool hasPendingBackgroundWork() const;
    // Prompts sent before session/new returned; they go out once it does.
    int pendingPromptCount() const { return int(m_pendingPrompts.size()); }
    // Background work that will wake the agent again: tasks (and the grace
    // after the last one) or a live Monitor watch. Scheduled wake-ups and cron
    // jobs are not counted.
    bool agentWorkPending() const;
    // A permission request the user has not answered yet.
    bool hasPendingPermissions() const { return !m_pendingPermissions.isEmpty(); }
    void setBackgroundWakeGraceMsForTest(int ms) { m_bgWakeGraceMs = ms; }
    QJsonArray mcpServersForTest() const { return m_mcpServers; }

public slots:
    // Outbound request slots — wrappers that build params and dispatch.
    void sendPrompt(const QString &text, const QList<QPair<QByteArray, QString>> &images);
    void sendSidePrompt(const QString &text);
    void cancelPrompt();
    // Ask the agent to stop one background task (claude-agent-acp
    // `_session/async_task/stop`). No-op for an unknown task or one the agent
    // did not mark stoppable. The task leaves the live list when the agent
    // reports `stopped`, not here.
    void stopBackgroundTask(const QString &taskId);
    void setMode(const QString &id);
    void setModel(const QString &id);
    void setConfigOption(const QString &id, const QJsonValue &value);

    // User-facing reply to an inbound permission request.
    // `outcome` is "selected" or "cancelled".
    void respondToPermission(const QString &requestId, const QString &outcome, const QString &optionId);

signals:
    void initialized(const AcpProtocol::AcpAgentInfo &info,
                     const QList<AcpProtocol::AcpCommandInfo> &availableCommands,
                     const QList<AcpProtocol::AcpModeInfo> &modes,
                     const QString &currentMode,
                     const QList<AcpProtocol::AcpModelInfo> &models,
                     const QString &currentModel,
                     const QList<AcpProtocol::AcpConfigOption> &configOptions);

    // Inbound notification fan-out (typed Qt signals).
    void messageChunk(const QString &chunk, const QString &messageId);
    void thoughtChunk(const QString &chunk);
    void toolCallReceived(const AcpProtocol::AcpToolCall &call);
    void toolCallUpdated(const AcpProtocol::AcpToolCallUpdate &update);
    void planReceived(const QList<AcpProtocol::AcpPlanEntry> &plan);
    void availableCommandsUpdated(const QList<AcpProtocol::AcpCommandInfo> &commands);
    void currentModeChanged(const QString &id);
    void configOptionsUpdated(const QList<AcpProtocol::AcpConfigOption> &options);
    void usageUpdated(const AcpProtocol::AcpUsage &usage);
    // Emitted for live `usage_update` notifications, which the agent treats
    // as the authoritative running total — receivers should overwrite their
    // usage snapshot rather than merging.
    void usageReplaced(const AcpProtocol::AcpUsage &usage);
    void promptStarted();
    void promptEnded();

    // Snapshot of live background tasks: how many, and the tool-call ids that
    // started them (tasks whose tool call is not known yet only count). Emitted
    // only when the snapshot changes.
    void asyncTasksChanged(int liveCount, const QStringList &liveToolCallIds);
    // All background tasks finished and no wake-up turn followed within the
    // grace window. If a wake-up turn did start, its promptEnded is the settle
    // signal instead and this stays silent.
    void backgroundIdle();
    // Live ScheduleWakeup / CronCreate jobs changed (display only: the agent
    // offers no host-side way to cancel them). Emitted only on a real change.
    void scheduledJobsChanged();
    // A permission request arrived, was answered, or was dropped (agent exit):
    // hasPendingPermissions() may have flipped.
    void pendingPermissionsChanged();

    void permissionRequested(const AcpProtocol::AcpPermissionRequest &req);

    void requestFailed(const QString &message);
    void errorOccurred(AcpErrorClassifier::AcpErrorKind kind, const QString &friendly);
    // The session set with setResumeSessionId() could not be resumed.
    void resumeFailed(const QString &reason);
    void debugLogAppended(const QString &line);

    // Emitted when the agent child process exits mid-session. Listeners
    // (AcpSessionView) surface an "Agent exited" banner with a Restart
    // affordance. No auto-reconnect is attempted; the user must click Restart.
    void agentExited(int exitCode, QProcess::ExitStatus exitStatus);

    void fileWrittenOnDisk(const QString &canonicalPath);

private slots:
    void handleStdoutReady(const QByteArray &chunk);
    void handleStderrReady(const QByteArray &chunk);
    void handleProcessError(QProcess::ProcessError err, const QString &message);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    using Callback = std::function<void(const QJsonValue &result, const QJsonValue &error)>;

    // Build the local QProcess transport for `agent`/`workingDirectory` and start
    // it (the prior spawn() body, now behind the IAcpProcessChannel seam). Does
    // all the spawn debug logging so a local session logs exactly as before.
    void spawnLocal(const AcpAgentDefinition &agent, const QString &workingDirectory);
    // Probe the agent binary on the remote host (`command -v <binary>`); present →
    // build the SSH transport via m_remoteChannelBuilder and start it; absent →
    // emit a clear error and STOP (no local fallback). cwd captured at spawn.
    void spawnRemote(const AcpAgentDefinition &agent, const QString &workingDirectory);
    // Wire an IAcpProcessChannel's signals into this connection and adopt it as
    // m_channel. Shared by the local and remote paths.
    void adoptChannel(IAcpProcessChannel *channel);

    void initializeHandshake();
    void sendInitialize();
    void sendNewSession();
    void sendResumeSession();
    // Shared tail of session/new and session/resume: catalogs, initialized(),
    // flush of prompts queued meanwhile.
    void finishSessionSetup(const QJsonObject &result, const char *method);
    void failResume(const QString &reason);

    int sendRequest(const char *method, const QJsonValue &params, const Callback &cb);
    void sendNotification(const char *method, const QJsonValue &params);
    void sendResponse(const QJsonValue &id, const QJsonValue &result);
    void sendErrorResponse(const QJsonValue &id, int code, const QString &message);

    void dispatchFrame(const QString &frame);
    void handleInboundRequest(const QJsonValue &id, const QString &method, const QJsonObject &params);
    void handleInboundNotification(const QString &method, const QJsonObject &params);

    // Inbound request handlers.
    void handleFsReadTextFile(const QJsonValue &id, const QJsonObject &params);
    void handleFsWriteTextFile(const QJsonValue &id, const QJsonObject &params);
    void handleTerminalCreate(const QJsonValue &id, const QJsonObject &params);
    void handleTerminalOutput(const QJsonValue &id, const QJsonObject &params);
    void handleTerminalWaitForExit(const QJsonValue &id, const QJsonObject &params);
    void handleTerminalKill(const QJsonValue &id, const QJsonObject &params);
    void handleTerminalRelease(const QJsonValue &id, const QJsonObject &params);
    void handleRequestPermission(const QJsonValue &id, const QJsonObject &params, bool nestedOutcome);
    void handleExtMethod(const QJsonValue &id, const QJsonObject &params);

    // Path sandbox: canonicalize + check against the working dir.
    bool sandboxRead(const QString &rawPath, QString *canonicalOut, QString *errorOut) const;
    bool sandboxWrite(const QString &rawPath, QString *canonicalOut, QString *errorOut) const;

    void cancelAllPendingPermissions(bool notify = true);
    void emitClassifiedError(const QString &raw);

    // Idempotent prompt-lifecycle wrappers. The agent may also signal these via
    // session/update (prompt_start / prompt_end) — both code paths route
    // through these helpers so listeners (model + view) see exactly one
    // promptStarted/promptEnded per turn regardless of which agent emits what.
    void beginPrompt();
    void endPrompt();
    void notePromptOpened();
    void notePromptClosed();
    void noteStreamingActivity();
    void schedulePromptIdleEnd();
    // A model cycle reached its SDK result (usage_update with `cost`): ends a
    // wake-up turn now instead of on the quiet-period guess.
    void noteModelCycleResult();
    // True while the agent still has unfinished work the idle timer cannot
    // see as traffic: an open tool call or a permission waiting on the user.
    bool hasOpenWork() const;
    void trackToolCallStatus(const QString &toolCallId, const QString &status);
    void applyAsyncTaskEvent(const AcpProtocol::AcpAsyncTaskEvent &ev);
    void emitAsyncTasksIfChanged();
    // Feed a tool_call / tool_call_update payload to the scheduled-job tracker.
    void noteScheduledJobUpdate(const QJsonObject &update);
    // Re-arm the single-shot expiry timer for the earliest job, or stop it.
    void rearmScheduledJobExpiry();
    void dispatchSessionPrompt(const QString &text, const QList<QPair<QByteArray, QString>> &images);
    void startSessionBusyRetry(const QString &text, const QList<QPair<QByteArray, QString>> &images);
    void stopSessionBusyRetry();

    // Append a single line to the debug ring buffer and also forward to the
    // lcAcp logging category. Safe to call from anywhere on the owning thread.
    void appendDebugLog(QString line);
    void appendDebugFrameLog(const char *direction, const QByteArray &bytes);

    struct TerminalState {
        QProcess *proc = nullptr;
        QByteArray buffer;
        bool truncated = false;
        int outputByteLimit = 256 * 1024;
        std::optional<QPair<int, QString>> exitStatus;
        QList<std::function<void()>> waiters;
    };

    void appendTerminalOutput(TerminalState &state, const QByteArray &chunk);

    IAcpProcessChannel *m_channel = nullptr; // owned (child); local or SSH transport
    remote::ExecutionContext *m_execContext = nullptr; // not owned; set for remote spawn
    RemoteChannelBuilder m_remoteChannelBuilder;       // app-injected; SSH transport factory
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;

    int m_nextRequestId = 1;
    QHash<int, Callback> m_pending;
    QHash<int, QString> m_pendingMethod;

    AcpAgentDefinition m_agent;
    QString m_workingDir;
    QString m_workingDirCanonical;

    QString m_sessionId;
    QString m_resumeSessionId;
    bool m_resumePending = false; // spawned with a resume id, not answered yet
    bool m_dormant = false;
    QString m_cannotResumeReason;
    AcpProtocol::AcpAgentInfo m_agentInfo;
    AcpProtocol::AcpCapabilities m_capabilities;
    QList<AcpProtocol::AcpCommandInfo> m_availableCommands;
    QList<AcpProtocol::AcpModeInfo> m_modes;
    QString m_currentMode;
    QList<AcpProtocol::AcpModelInfo> m_models;
    QString m_currentModel;
    QList<AcpProtocol::AcpConfigOption> m_configOptions;
    QJsonArray m_mcpServers;

    QHash<QString, std::function<void(const QString &outcome, const QString &optionId)>> m_pendingPermissions;
    // Tool calls reported but not yet in a final state. Wake-up turns have no
    // session/prompt to bound them, so this is what keeps the turn open while
    // a long tool runs silently.
    QSet<QString> m_openToolCalls;
    // Advisor is a server tool whose result the adapter drops; its trailing
    // "failed" update is normalized to completed (see tool_call_update).
    QSet<QString> m_advisorToolCallIds;
    QVector<AcpProtocol::AcpBackgroundTask> m_liveAsyncTasks; // spawn order; a handful at most
    int liveTaskIndex(const QString &taskId) const;
    QStringList m_lastEmittedAsyncToolIds;
    int m_lastEmittedAsyncCount = 0;
    QTimer *m_bgIdleTimer = nullptr;
    AcpProtocol::ScheduledJobTracker m_scheduledJobs;
    QTimer *m_scheduledExpiryTimer = nullptr; // fires at the earliest job expiry, no polling
    // Observed gap task-completed -> wake-up turn: ~1.4 s (claude-agent-acp
    // 0.89.1). Generous margin for a slow model start.
    static constexpr int kBackgroundWakeGraceMs = 8000;
    int m_bgWakeGraceMs = kBackgroundWakeGraceMs;
    std::function<QString()> m_autoApproveProvider;

    QHash<QString, TerminalState> m_terminals;
    int m_nextTerminalSeq = 1;

    // Prompts submitted by the user before `session/new` returned. We enqueue
    // here when m_sessionId is still empty and flush the queue in the
    // session/new success callback — otherwise the prompt goes out with an
    // empty sessionId and the agent rejects it with "Session not found".
    struct PendingPrompt {
        QString text;
        QList<QPair<QByteArray, QString>> images;
    };
    QList<PendingPrompt> m_pendingPrompts;
    bool m_promptInFlight = false;
    int m_openPrompts = 0;
    bool m_promptProducedOutput = false;
    bool m_turnEndConfirmed = false;
    QTimer *m_promptIdleEndTimer = nullptr;
    QTimer *m_sessionBusyRetryTimer = nullptr;
    PendingPrompt m_busyRetryPrompt;
    bool m_busyRetryPending = false;
    int m_busyRetryElapsedMs = 0;
    int m_busyRetryDelayMs = 0;

    static constexpr int kDebugLogMaxLines = 2000;
    static constexpr int kDebugLogLineMaxChars = 4096;
    QStringList m_debugLog;
};

#endif // ACP_CONNECTION_H
