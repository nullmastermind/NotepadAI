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
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>

#include "AcpAgentDefinition.h"
#include "AcpErrorClassifier.h"
#include "AcpProtocol.h"

Q_DECLARE_LOGGING_CATEGORY(lcAcp)

class QTimer;

class AcpConnection : public QObject
{
    Q_OBJECT

public:
    explicit AcpConnection(QObject *parent = nullptr);
    ~AcpConnection() override;

    // Spawn the agent child process. Asynchronous — the `initialized()` signal
    // fires after `initialize` + `session/new` complete successfully.
    void spawn(const AcpAgentDefinition &agent, const QString &workingDirectory);

    // Auto-approve policy provider — Group 4 will hand a lambda that queries
    // the registry at request time so the latest policy is always observed.
    void setAutoApprovePolicyProvider(std::function<QString()> provider);

    QString sessionId() const { return m_sessionId; }
    AcpProtocol::AcpAgentInfo agentInfo() const { return m_agentInfo; }
    AcpProtocol::AcpCapabilities capabilities() const { return m_capabilities; }
    AcpAgentDefinition definition() const { return m_agent; }
    QString workingDirectory() const { return m_workingDir; }

    // In-memory ring-buffered debug log of every spawn/stdin/stdout/stderr/error
    // event seen by this connection. Each entry is a single timestamped line.
    // Capped at kDebugLogMaxLines — oldest entries are dropped. Exposed for the
    // dock's "Debug" popup so users can self-diagnose connection failures.
    QStringList debugLog() const { return m_debugLog; }
    void clearDebugLog();

    // Test seam — invokes handleProcessFinished as if the underlying QProcess
    // had exited with the given code/status. Used by exit/restart tests so the
    // suite doesn't need to spawn a real subprocess.
    void simulateProcessFinished(int exitCode, QProcess::ExitStatus status)
    {
        handleProcessFinished(exitCode, status);
    }

public slots:
    // Outbound request slots — wrappers that build params and dispatch.
    void sendPrompt(const QString &text, const QList<QPair<QByteArray, QString>> &images);
    void cancelPrompt();
    void setMode(const QString &id);
    void setModel(const QString &id);
    void setConfigOption(const QString &id, const QJsonValue &value);

    // User-facing reply to an inbound `request_permission` request.
    // `outcome` is "selected" or "cancelled".
    void respondToPermission(const QString &requestId, const QString &outcome, const QString &optionId);

signals:
    void initialized(const AcpProtocol::AcpAgentInfo &info,
                     const QStringList &availableCommands,
                     const QList<AcpProtocol::AcpModeInfo> &modes,
                     const QString &currentMode,
                     const QList<AcpProtocol::AcpModelInfo> &models,
                     const QString &currentModel,
                     const QList<AcpProtocol::AcpConfigOption> &configOptions);

    // Inbound notification fan-out (typed Qt signals).
    void messageChunk(const QString &chunk);
    void thoughtChunk(const QString &chunk);
    void toolCallReceived(const AcpProtocol::AcpToolCall &call);
    void toolCallUpdated(const AcpProtocol::AcpToolCallUpdate &update);
    void planReceived(const QList<AcpProtocol::AcpPlanEntry> &plan);
    void availableCommandsUpdated(const QStringList &commands);
    void currentModeChanged(const QString &id);
    void configOptionsUpdated(const QList<AcpProtocol::AcpConfigOption> &options);
    void usageUpdated(const AcpProtocol::AcpUsage &usage);
    // Emitted for live `usage_update` notifications, which the agent treats
    // as the authoritative running total — receivers should overwrite their
    // usage snapshot rather than merging.
    void usageReplaced(const AcpProtocol::AcpUsage &usage);
    void promptStarted();
    void promptEnded();

    void permissionRequested(const AcpProtocol::AcpPermissionRequest &req);

    void requestFailed(const QString &message);
    void errorOccurred(AcpErrorClassifier::AcpErrorKind kind, const QString &friendly);
    void debugLogAppended(const QString &line);

    // Emitted when the agent child process exits mid-session. Listeners
    // (AcpSessionView) surface an "Agent exited" banner with a Restart
    // affordance. No auto-reconnect is attempted; the user must click Restart.
    void agentExited(int exitCode, QProcess::ExitStatus exitStatus);

private slots:
    void handleStdoutReady();
    void handleStderrReady();
    void handleProcessError(QProcess::ProcessError err);
    void handleProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    using Callback = std::function<void(const QJsonValue &result, const QJsonValue &error)>;

    void initializeHandshake();
    void sendInitialize();
    void sendNewSession();

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
    void handleRequestPermission(const QJsonValue &id, const QJsonObject &params);
    void handleExtMethod(const QJsonValue &id, const QJsonObject &params);

    // Path sandbox: canonicalize + check against the working dir.
    bool sandboxRead(const QString &rawPath, QString *canonicalOut, QString *errorOut) const;
    bool sandboxWrite(const QString &rawPath, QString *canonicalOut, QString *errorOut) const;

    void cancelAllPendingPermissions();
    void emitClassifiedError(const QString &raw);

    // Idempotent prompt-lifecycle wrappers. The agent may also signal these via
    // session/update (prompt_start / prompt_end) — both code paths route
    // through these helpers so listeners (model + view) see exactly one
    // promptStarted/promptEnded per turn regardless of which agent emits what.
    void beginPrompt();
    void endPrompt();

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

    QProcess *m_process = nullptr;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;

    int m_nextRequestId = 1;
    QHash<int, Callback> m_pending;
    QHash<int, QString> m_pendingMethod;

    AcpAgentDefinition m_agent;
    QString m_workingDir;
    QString m_workingDirCanonical;

    QString m_sessionId;
    AcpProtocol::AcpAgentInfo m_agentInfo;
    AcpProtocol::AcpCapabilities m_capabilities;
    QStringList m_availableCommands;
    QList<AcpProtocol::AcpModeInfo> m_modes;
    QString m_currentMode;
    QList<AcpProtocol::AcpModelInfo> m_models;
    QString m_currentModel;
    QList<AcpProtocol::AcpConfigOption> m_configOptions;

    QHash<QString, std::function<void(const QString &outcome, const QString &optionId)>> m_pendingPermissions;
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
    bool m_promptProducedOutput = false;

    static constexpr int kDebugLogMaxLines = 2000;
    static constexpr int kDebugLogLineMaxChars = 4096;
    QStringList m_debugLog;
};

#endif // ACP_CONNECTION_H
