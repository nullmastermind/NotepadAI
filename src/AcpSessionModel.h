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

#ifndef ACP_SESSION_MODEL_H
#define ACP_SESSION_MODEL_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

#include "AcpProtocol.h"

class AcpHistoryStore;

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
};

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
// Persistence: when `setHistoryStore` is called, every state-mutating event
// posts a full JSON snapshot to the worker store via QMetaObject::invokeMethod
// (queued, thread-safe). The model never blocks on writes.
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
    QString acpSessionId() const { return m_acpSessionId; }
    void setAcpSessionId(const QString &id);

    bool isProcessing() const { return m_isProcessing; }
    bool isEmpty() const { return m_messages.isEmpty() && m_toolCalls.isEmpty(); }

    const QVector<AcpMessage> &messages() const { return m_messages; }
    const QHash<QString, AcpProtocol::AcpToolCall> &toolCalls() const { return m_toolCalls; }
    const QVector<AcpTimelineEntry> &timeline() const { return m_timeline; }
    const std::optional<AcpProtocol::AcpUsage> &usage() const { return m_usage; }
    const QStringList &availableCommands() const { return m_availableCommands; }
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
                       const QStringList &availableCommands,
                       const QList<AcpProtocol::AcpModeInfo> &modes,
                       const QString &currentMode,
                       const QList<AcpProtocol::AcpModelInfo> &models,
                       const QString &currentModel,
                       const QList<AcpProtocol::AcpConfigOption> &configOptions);
    void onMessageChunk(const QString &text);
    void onThoughtChunk(const QString &text);
    void onToolCallReceived(const AcpProtocol::AcpToolCall &tc);
    void onToolCallUpdated(const AcpProtocol::AcpToolCallUpdate &update);
    void onPlanReceived(const QList<AcpProtocol::AcpPlanEntry> &plan);
    void onAvailableCommandsUpdated(const QStringList &commands);
    void onCurrentModeChanged(const QString &modeId);
    void onUsageUpdated(const AcpProtocol::AcpUsage &usage);
    void onPromptStarted();
    void onPromptEnded();

    // -- User-initiated input ----------------------------------------------
    // Appends a user-role message. `images` is a list of (data, mimeType)
    // pairs; data is stored raw (NOT base64) — base64 encoding only happens
    // in toHistoryJson() / when sent over the wire.
    void appendUserMessage(const QString &text,
                           const QVector<QPair<QByteArray, QString>> &images);

signals:
    void metadataChanged();
    void messageAppended(int idx);
    void messageChunkAppended(int idx, const QString &chunk);
    void thoughtAppended(int idx);
    void thoughtChunkAppended(int idx, const QString &chunk);
    void toolCallAddedOrUpdated(const QString &toolCallId);
    void planUpdated();
    void availableCommandsChanged();
    void currentModeChanged(const QString &modeId);
    void usageChanged();
    void isProcessingChanged(bool processing);
    void turnEnded(int groupId);

private:
    void schedulePersistIfNeeded();
    QString resolveHistoryDir() const;
    QString resolveHistoryFilePath() const;

    QString m_sessionId;
    QString m_projectId;
    QString m_acpSessionId;
    QString m_historyDirOverride; // empty = use default AppDataLocation

    AcpHistoryStore *m_historyStore = nullptr; // non-owning

    QVector<AcpMessage> m_messages;
    QHash<QString, AcpProtocol::AcpToolCall> m_toolCalls;
    QVector<AcpTimelineEntry> m_timeline;

    int m_currentGroupId = 0;
    std::optional<AcpProtocol::AcpUsage> m_usage;
    QStringList m_availableCommands;
    QList<AcpProtocol::AcpModeInfo> m_availableModes;
    QString m_currentModeId;
    QList<AcpProtocol::AcpModelInfo> m_availableModels;
    QString m_currentModelId;
    QList<AcpProtocol::AcpConfigOption> m_configOptions;
    QList<AcpProtocol::AcpPlanEntry> m_plan;
    AcpProtocol::AcpAgentInfo m_agentInfo;

    bool m_isProcessing = false;
    int m_streamingAssistantMessageIndex = -1;
    int m_streamingThoughtMessageIndex = -1;
};

#endif // ACP_SESSION_MODEL_H
