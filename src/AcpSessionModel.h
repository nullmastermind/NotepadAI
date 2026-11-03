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

#ifndef ACP_SESSION_MODEL_H
#define ACP_SESSION_MODEL_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

#include "AcpProtocol.h"

class AcpHistoryStore;
class QTimer;

// Host-side per-message record. Distinct from AcpContentBlock (which is a
// wire payload type) — kept in the host-side header so it doesn't pollute
// the wire-protocol namespace.
struct AcpMessage
{
    QString role; // "user", "assistant", "thought", "system"
    QVector<AcpProtocol::AcpContentBlock> content;
    qint64 timestamp = 0; // ms since epoch
    std::optional<QString> command;
    std::optional<int> exitCode;
    bool fromGoalAgent = false;
    // Judge output mirrored into the target transcript. Distinct from
    // fromGoalAgent (user prompts Goal sent *to* the coding agent).
    bool fromGoalJudge = false;
    // Locale-stable tag for host-generated system rows. Display text is tr()'d;
    // the summarizer keys off this, not the translated body.
    QString marker;
};

inline constexpr char kAcpMarkerGoalAchieved[] = "goal-achieved";
inline constexpr char kAcpMarkerGoalJudging[] = "goal-judging";
inline constexpr char kGoalJudgeToolIdPrefix[] = "goal-judge:";

inline bool isGoalJudgeToolId(const QString &id)
{
    return id.startsWith(QLatin1String(kGoalJudgeToolIdPrefix));
}

// One entry in the rendered transcript ordering. Either a pointer (by index)
// into m_messages, or a tool-call id pointing into m_toolCalls. groupId
// tracks the "turn" the entry belongs to so the view can auto-collapse
// large tool-call groups per turn.
struct AcpTimelineEntry
{
    enum class Kind : std::uint8_t { Message, ToolCall };
    Kind kind = Kind::Message;
    int messageIndex = -1;
    QString toolCallId;
    int groupId = 0;
};

// In-memory ACP session state. Owns the messages, tool-calls, plan, usage,
// available modes/models/commands/config options. Subscribes to AcpConnection
// signals via public slots — Group 4 wires the QObject connections.
//
// Persistence: when `setHistoryStore` is called, state-mutating events coalesce
// a full JSON snapshot before posting it to the worker store via
// QMetaObject::invokeMethod (queued, thread-safe). The model never blocks on
// writes and never rebuilds snapshots per streamed tool update.
//
// Load-on-construct: the constructor synchronously reads
// `<historyDirOverride or AppDataLocation/acp-history>/<sessionId>.json`
// if it exists. Missing file = empty model.
class AcpSessionModel : public QObject
{
    Q_OBJECT

public:
    AcpSessionModel(QString sessionId, QString projectId, QObject *parent = nullptr);
    AcpSessionModel(QString sessionId,
                    QString projectId,
                    QString historyDirOverride,
                    QObject *parent = nullptr);
    ~AcpSessionModel() override;

    // Attach (or detach with nullptr) a worker history store. Non-owning.
    // After attach, every state-mutating event posts a snapshot to the store
    // via Qt::QueuedConnection.
    void setHistoryStore(AcpHistoryStore *store);

    QString sessionId() const { return m_sessionId; }
    QString projectId() const { return m_projectId; }
    // The agent's own session id (session/new result), persisted so a restored
    // session or Switch agent can resume it.
    QString acpSessionId() const { return m_acpSessionId; }
    void setAcpSessionId(const QString &id);
    // The user has sent something themselves: the session is no longer a draft
    // (GLOSSARY). Persisted so a restored session keeps it.
    bool userSent() const { return m_userSent; }
    void setUserSent();
    // Prewalk model of this session (GLOSSARY): empty = Off, nullopt = never
    // chosen (a new session takes the agent default). Persisted.
    const std::optional<QString> &prewalkModel() const { return m_prewalkModel; }
    void setPrewalkModel(const QString &model);
    // While Prewalk runs the prewalk model, history records the selection the
    // session goes back to instead of the live one: `currentModel` and these
    // config option values (option id -> value). The live state is untouched.
    struct SelectionOverride
    {
        QString currentModel;
        QHash<QString, QString> configValues;
    };
    void setSelectionOverride(std::optional<SelectionOverride> selection);

    bool isProcessing() const { return m_isProcessing; }
    // Live background tasks (Claude Code async tasks). Independent of
    // isProcessing: the turn that spawned them has usually already ended.
    int backgroundTaskCount() const { return m_backgroundTaskCount; }
    bool isEmpty() const { return m_messages.isEmpty() && m_toolCalls.isEmpty(); }

    const QVector<AcpMessage> &messages() const { return m_messages; }
    const QHash<QString, AcpProtocol::AcpToolCall> &toolCalls() const { return m_toolCalls; }
    const QVector<AcpTimelineEntry> &timeline() const { return m_timeline; }
    // Thought message still receiving chunks (main agent or goal judge).
    bool isStreamingThought(int messageIndex) const
    {
        return messageIndex >= 0
            && (messageIndex == m_streamingThoughtMessageIndex
                || messageIndex == m_streamingGoalJudgeThoughtIndex);
    }
    // Tool calls a subagent made, in arrival order, keyed by the Agent/Task
    // tool call that spawned it. They live in toolCalls() but get no timeline
    // entry (a call attributed late keeps the one it already had; readers skip
    // tool calls with a parentToolCallId).
    QStringList subagentChildren(const QString &parentToolCallId) const
    {
        return m_subagentChildren.value(parentToolCallId);
    }
    const std::optional<AcpProtocol::AcpUsage> &usage() const { return m_usage; }
    const QList<AcpProtocol::AcpCommandInfo> &availableCommands() const { return m_availableCommands; }
    // True only when the agent advertised `goal`/`/goal`. Host inject does not set this.
    bool agentAdvertisesGoalCommand() const { return m_agentAdvertisesGoalCommand; }

    const QList<AcpProtocol::AcpModeInfo> &availableModes() const { return m_availableModes; }
    QString currentModeId() const { return m_currentModeId; }
    const QList<AcpProtocol::AcpModelInfo> &availableModels() const { return m_availableModels; }
    QString currentModelId() const { return m_currentModelId; }
    const QList<AcpProtocol::AcpConfigOption> &configOptions() const { return m_configOptions; }
    const AcpProtocol::AcpAgentInfo &agentInfo() const { return m_agentInfo; }

    // Synchronously read the on-disk snapshot (if any) for this session's id
    // from the resolved history directory. Called from the constructor; safe
    // to call again to re-hydrate after an external write.
    void loadFromDisk();

    // Build the persistable JSON snapshot. Exposed for tests.
    QJsonObject toHistoryJson() const;

public slots:
    // -- Hooks for AcpConnection signals (Group 4 wires these up). ---------
    void onInitialized(const AcpProtocol::AcpAgentInfo &info,
                       const QList<AcpProtocol::AcpCommandInfo> &availableCommands,
                       const QList<AcpProtocol::AcpModeInfo> &modes,
                       const QString &currentMode,
                       const QList<AcpProtocol::AcpModelInfo> &models,
                       const QString &currentModel,
                       const QList<AcpProtocol::AcpConfigOption> &configOptions);
    void onMessageChunk(const QString &text);
    void onMessageChunk(const QString &text, const QString &messageId);
    void onThoughtChunk(const QString &text);
    void onToolCallReceived(const AcpProtocol::AcpToolCall &tc);
    void onToolCallUpdated(const AcpProtocol::AcpToolCallUpdate &update);
    void onPlanReceived(const QList<AcpProtocol::AcpPlanEntry> &plan);
    void onAvailableCommandsUpdated(const QList<AcpProtocol::AcpCommandInfo> &commands);
    void onCurrentModeChanged(const QString &modeId);
    void onConfigOptionsUpdated(const QList<AcpProtocol::AcpConfigOption> &options);
    void onUsageUpdated(const AcpProtocol::AcpUsage &usage);
    // Authoritative replacement (from `usage_update` notifications). Overwrites
    // m_usage wholesale instead of merging — except a mid-turn used:0 with an
    // unchanged size, which claude-agent-acp emits before the real snapshot
    // and which must not flash the context bar to 0%.
    void onUsageReplaced(const AcpProtocol::AcpUsage &usage);
    void onPromptStarted();
    void onPromptEnded();
    // Snapshot from AcpConnection::asyncTasksChanged. Live-only state, never
    // persisted: a restored session has no running tasks.
    void onAsyncTasksChanged(int liveCount, const QStringList &liveToolCallIds);

    // -- User-initiated input ----------------------------------------------
    // Appends a user-role message. `images` is a list of (data, mimeType)
    // pairs; data is stored raw (NOT base64) — base64 encoding only happens
    // in toHistoryJson() / when sent over the wire.
    void appendUserMessage(const QString &text,
                           const QVector<QPair<QByteArray, QString>> &images,
                           bool fromGoalAgent = false,
                           const QString &marker = {});
    void appendSystemMessage(const QString &text, const QString &marker = {});

    // Goal-judge mirror. Does not touch coding-agent streaming indices or
    // isProcessing. Tool-call ids are stored with prefix kGoalJudgeToolIdPrefix.
    void beginGoalJudgeTurn(const QString &text);
    void appendGoalJudgeChunk(const QString &text);
    void appendGoalJudgeThoughtChunk(const QString &text);
    void upsertGoalJudgeToolCall(const AcpProtocol::AcpToolCall &tc);
    void applyGoalJudgeToolCallUpdate(const AcpProtocol::AcpToolCallUpdate &update);
    void replaceGoalJudgeText(const QString &fullText);
    void closeGoalJudgeStreaming();
    void removeGoalJudgeTurn();

signals:
    void metadataChanged();
    void messageAppended(int idx);
    void messageChunkAppended(int idx, const QString &chunk);
    // Emitted when the model rewrites a streaming message body in place (e.g.
    // compaction status replacement). View should set the bubble text wholesale.
    void messageReplaced(int idx, const QString &fullText);
    void thoughtAppended(int idx);
    void thoughtChunkAppended(int idx, const QString &chunk);
    void toolCallAddedOrUpdated(const QString &toolCallId);
    void planUpdated();
    void availableCommandsChanged();
    void currentModeChanged(const QString &modeId);
    void usageChanged();
    void isProcessingChanged(bool processing);
    void backgroundTaskCountChanged(int count);
    void turnEnded(int groupId);
    void goalJudgeTurnRemoved(const QVector<int> &messageIndices,
                              const QStringList &toolCallIds);

private:
    void schedulePersistIfNeeded();
    void flushPendingPersist();
    void attachSubagentChild(const QString &parentToolCallId, const QString &childId);
    bool isBackgroundRunning(const QString &toolCallId) const;
    bool isAsyncSubagentLaunch(const AcpProtocol::AcpToolCall &tc) const;
    void noteSubagentActivity(const QString &parentToolCallId);
    void settleAsyncSubagents();
    QString resolveHistoryDir() const;
    QString resolveHistoryFilePath() const;

    QString m_sessionId;
    QString m_projectId;
    QString m_acpSessionId;
    bool m_userSent = false;
    std::optional<QString> m_prewalkModel;
    std::optional<SelectionOverride> m_selectionOverride;
    QString m_historyDirOverride; // empty = use default AppDataLocation

    AcpHistoryStore *m_historyStore = nullptr; // non-owning
    QTimer *m_persistTimer = nullptr;
    bool m_persistDirty = false;

    QVector<AcpMessage> m_messages;
    QHash<QString, AcpProtocol::AcpToolCall> m_toolCalls;
    QVector<AcpTimelineEntry> m_timeline;
    QHash<QString, QStringList> m_subagentChildren; // parent tool call -> children, arrival order

    int m_currentGroupId = 0;
    std::optional<AcpProtocol::AcpUsage> m_usage;
    QList<AcpProtocol::AcpCommandInfo> m_availableCommands;
    bool m_agentAdvertisesGoalCommand = false;

    QList<AcpProtocol::AcpModeInfo> m_availableModes;
    QString m_currentModeId;
    QList<AcpProtocol::AcpModelInfo> m_availableModels;
    QString m_currentModelId;
    QList<AcpProtocol::AcpConfigOption> m_configOptions;
    QList<AcpProtocol::AcpPlanEntry> m_plan;
    AcpProtocol::AcpAgentInfo m_agentInfo;

    bool m_isProcessing = false;
    int m_backgroundTaskCount = 0;
    QSet<QString> m_backgroundToolCallIds;
    // Agent calls launched async whose subagent still runs. Live-only: the
    // adapter holds the turn open until they settle, so a turn end clears it.
    QSet<QString> m_liveAsyncSubagents;
    int m_streamingAssistantMessageIndex = -1;
    QString m_streamingAssistantMessageId;
    // Bubble that was streaming when the turn was declared over. The host ends
    // wake-up turns on an idle timer, so a slow stream can outlast it; a chunk
    // with the same messageId must keep filling that bubble, not open a new one.
    QString m_resumableAssistantMessageId;
    int m_resumableAssistantMessageIndex = -1;
    int m_streamingThoughtMessageIndex = -1;
    int m_streamingGoalJudgeAssistantIndex = -1;
    int m_streamingGoalJudgeThoughtIndex = -1;
    int m_goalJudgeTurnMessageBegin = -1;
    int m_goalJudgeTurnTimelineBegin = -1;
};

#endif // ACP_SESSION_MODEL_H
