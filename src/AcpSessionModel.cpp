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

#include "AcpSessionModel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QStandardPaths>
#include <utility>

#include "AcpHistoryStore.h"

using namespace AcpProtocol;

namespace {

QString defaultHistoryDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/acp-history");
}

QJsonArray contentBlocksToJson(const QVector<AcpContentBlock> &blocks)
{
    QJsonArray arr;
    for (const auto &b : blocks) {
        arr.append(contentBlockToJson(b));
    }
    return arr;
}

QVector<AcpContentBlock> contentBlocksFromJson(const QJsonArray &arr)
{
    QVector<AcpContentBlock> out;
    out.reserve(arr.size());
    for (const auto v : arr) {
        if (v.isObject()) {
            out.append(contentBlockFromJson(v.toObject()));
        }
    }
    return out;
}

QJsonObject usageToJson(const AcpUsage &u)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("inputTokens"),
               u.inputTokens.has_value() ? QJsonValue(*u.inputTokens) : QJsonValue());
    obj.insert(QStringLiteral("outputTokens"),
               u.outputTokens.has_value() ? QJsonValue(*u.outputTokens) : QJsonValue());
    obj.insert(QStringLiteral("totalTokens"),
               u.totalTokens.has_value() ? QJsonValue(*u.totalTokens) : QJsonValue());
    obj.insert(QStringLiteral("maxTokens"),
               u.maxTokens.has_value() ? QJsonValue(*u.maxTokens) : QJsonValue());
    obj.insert(QStringLiteral("costAmount"),
               u.costAmount.has_value() ? QJsonValue(*u.costAmount) : QJsonValue());
    obj.insert(QStringLiteral("costCurrency"),
               u.costCurrency.has_value() ? QJsonValue(*u.costCurrency) : QJsonValue());
    return obj;
}

AcpUsage usageFromJson(const QJsonObject &obj)
{
    AcpUsage u;
    const auto in = obj.value(QStringLiteral("inputTokens"));
    if (in.isDouble()) {
        u.inputTokens = in.toInt();
    }
    const auto out = obj.value(QStringLiteral("outputTokens"));
    if (out.isDouble()) {
        u.outputTokens = out.toInt();
    }
    const auto tot = obj.value(QStringLiteral("totalTokens"));
    if (tot.isDouble()) {
        u.totalTokens = tot.toInt();
    }
    const auto mx = obj.value(QStringLiteral("maxTokens"));
    if (mx.isDouble()) {
        u.maxTokens = mx.toInt();
    }
    const auto amt = obj.value(QStringLiteral("costAmount"));
    if (amt.isDouble()) {
        u.costAmount = amt.toDouble();
    }
    const auto cur = obj.value(QStringLiteral("costCurrency"));
    if (cur.isString()) {
        u.costCurrency = cur.toString();
    }
    return u;
}

} // namespace

AcpSessionModel::AcpSessionModel(QString sessionId, QString projectId, QObject *parent)
    : AcpSessionModel(std::move(sessionId), std::move(projectId), QString(), parent)
{
}

AcpSessionModel::AcpSessionModel(QString sessionId,
                                 QString projectId,
                                 QString historyDirOverride,
                                 QObject *parent)
    : QObject(parent)
    , m_sessionId(std::move(sessionId))
    , m_projectId(std::move(projectId))
    , m_historyDirOverride(std::move(historyDirOverride))
{
    loadFromDisk();
}

AcpSessionModel::~AcpSessionModel() = default;

void AcpSessionModel::setHistoryStore(AcpHistoryStore *store)
{
    m_historyStore = store;
}

void AcpSessionModel::setAcpSessionId(const QString &id)
{
    m_acpSessionId = id;
    schedulePersistIfNeeded();
}

QString AcpSessionModel::resolveHistoryDir() const
{
    return m_historyDirOverride.isEmpty() ? defaultHistoryDir() : m_historyDirOverride;
}

QString AcpSessionModel::resolveHistoryFilePath() const
{
    return resolveHistoryDir() + QStringLiteral("/") + m_sessionId
           + QStringLiteral(".json");
}

void AcpSessionModel::loadFromDisk()
{
    if (m_sessionId.isEmpty()) {
        return;
    }
    const QString path = resolveHistoryFilePath();
    QFile f(path);
    if (!f.exists()) {
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "[acp-history] load open failed:" << path << f.errorString();
        return;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[acp-history] load parse failed:" << path << err.errorString();
        return;
    }
    const QJsonObject obj = doc.object();

    // projectId — keep ctor value if null/missing.
    const QJsonValue pid = obj.value(QStringLiteral("projectId"));
    if (pid.isString()) {
        m_projectId = pid.toString();
    }
    const QJsonValue acpId = obj.value(QStringLiteral("acpSessionId"));
    if (acpId.isString()) {
        m_acpSessionId = acpId.toString();
    }

    // messages
    const QJsonArray msgs = obj.value(QStringLiteral("messages")).toArray();
    for (const auto mv : msgs) {
        if (!mv.isObject()) {
            continue;
        }
        const QJsonObject mo = mv.toObject();
        AcpMessage msg;
        msg.role = mo.value(QStringLiteral("role")).toString();
        msg.content = contentBlocksFromJson(mo.value(QStringLiteral("content")).toArray());
        msg.timestamp = static_cast<qint64>(mo.value(QStringLiteral("timestamp")).toDouble());
        const QJsonValue cmd = mo.value(QStringLiteral("command"));
        if (cmd.isString()) {
            msg.command = cmd.toString();
        }
        const QJsonValue ec = mo.value(QStringLiteral("exitCode"));
        if (ec.isDouble()) {
            msg.exitCode = ec.toInt();
        }
        msg.fromGoalAgent = mo.value(QStringLiteral("fromGoalAgent")).toBool(false);
        m_messages.append(msg);
    }

    // toolCalls
    const QJsonArray tcs = obj.value(QStringLiteral("toolCalls")).toArray();
    for (const auto tv : tcs) {
        if (!tv.isObject()) {
            continue;
        }
        const QJsonObject to = tv.toObject();
        AcpToolCall tc;
        tc.id = to.value(QStringLiteral("toolCallId")).toString();
        tc.title = to.value(QStringLiteral("title")).toString();
        tc.status = to.value(QStringLiteral("status")).toString();
        tc.content = to.value(QStringLiteral("content")).toArray();
        tc.rawInput = to.value(QStringLiteral("rawInput")).toObject();
        tc.groupId = to.value(QStringLiteral("groupId")).toInt();
        if (!tc.id.isEmpty()) {
            m_toolCalls.insert(tc.id, tc);
            if (tc.groupId > m_currentGroupId) {
                m_currentGroupId = tc.groupId;
            }
        }
    }

    // timeline
    const QJsonArray tl = obj.value(QStringLiteral("timeline")).toArray();
    for (const auto ev : tl) {
        if (!ev.isObject()) {
            continue;
        }
        const QJsonObject eo = ev.toObject();
        AcpTimelineEntry entry;
        const QString type = eo.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("tool_call")) {
            entry.kind = AcpTimelineEntry::Kind::ToolCall;
            entry.toolCallId = eo.value(QStringLiteral("toolCallId")).toString();
            const auto it = m_toolCalls.constFind(entry.toolCallId);
            if (it != m_toolCalls.constEnd()) {
                entry.groupId = it.value().groupId;
            }
        } else {
            entry.kind = AcpTimelineEntry::Kind::Message;
            entry.messageIndex = eo.value(QStringLiteral("messageIndex")).toInt(-1);
        }
        m_timeline.append(entry);
    }

    // usage
    const QJsonValue uv = obj.value(QStringLiteral("usage"));
    if (uv.isObject()) {
        m_usage = usageFromJson(uv.toObject());
    }
}

QJsonObject AcpSessionModel::toHistoryJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("projectId"),
               m_projectId.isEmpty() ? QJsonValue() : QJsonValue(m_projectId));
    if (!m_acpSessionId.isEmpty()) {
        obj.insert(QStringLiteral("acpSessionId"), m_acpSessionId);
    }

    QJsonArray msgs;
    for (const AcpMessage &m : m_messages) {
        QJsonObject mo;
        mo.insert(QStringLiteral("role"), m.role);
        mo.insert(QStringLiteral("content"), contentBlocksToJson(m.content));
        mo.insert(QStringLiteral("timestamp"), QJsonValue(static_cast<double>(m.timestamp)));
        if (m.command.has_value()) {
            mo.insert(QStringLiteral("command"), *m.command);
        }
        if (m.exitCode.has_value()) {
            mo.insert(QStringLiteral("exitCode"), *m.exitCode);
        }
        if (m.fromGoalAgent) {
            mo.insert(QStringLiteral("fromGoalAgent"), true);
        }
        msgs.append(mo);
    }
    obj.insert(QStringLiteral("messages"), msgs);

    QJsonArray tcs;
    for (auto it = m_toolCalls.cbegin(); it != m_toolCalls.cend(); ++it) {
        const AcpToolCall &tc = it.value();
        QJsonObject to;
        to.insert(QStringLiteral("toolCallId"), tc.id);
        to.insert(QStringLiteral("title"), tc.title);
        to.insert(QStringLiteral("status"), tc.status);
        to.insert(QStringLiteral("content"), tc.content);
        to.insert(QStringLiteral("groupId"), tc.groupId);
        if (!tc.rawInput.isEmpty()) {
            QJsonObject slim;
            auto copyField = [&](const QLatin1String &key) {
                if (tc.rawInput.contains(key))
                    slim.insert(key, tc.rawInput.value(key));
            };
            copyField(QLatin1String("file_path"));
            copyField(QLatin1String("pattern"));
            copyField(QLatin1String("query"));
            copyField(QLatin1String("url"));
            copyField(QLatin1String("information_request"));
            copyField(QLatin1String("skill"));
            copyField(QLatin1String("subagent_type"));
            if (tc.rawInput.contains(QLatin1String("command"))) {
                QString cmd = tc.rawInput.value(QLatin1String("command")).toString();
                const int nl = cmd.indexOf(QLatin1Char('\n'));
                if (nl >= 0) cmd.truncate(nl);
                if (cmd.size() > 200) cmd.truncate(200);
                slim.insert(QStringLiteral("command"), cmd);
            }
            if (tc.rawInput.contains(QLatin1String("description"))) {
                QString desc = tc.rawInput.value(QLatin1String("description")).toString();
                if (desc.size() > 200) desc.truncate(200);
                slim.insert(QStringLiteral("description"), desc);
            }
            if (!slim.isEmpty()) {
                to.insert(QStringLiteral("rawInput"), slim);
            }
        }
        tcs.append(to);
    }
    obj.insert(QStringLiteral("toolCalls"), tcs);

    QJsonArray timeline;
    for (const AcpTimelineEntry &e : m_timeline) {
        QJsonObject eo;
        if (e.kind == AcpTimelineEntry::Kind::ToolCall) {
            eo.insert(QStringLiteral("type"), QStringLiteral("tool_call"));
            eo.insert(QStringLiteral("toolCallId"), e.toolCallId);
        } else {
            eo.insert(QStringLiteral("type"), QStringLiteral("message"));
            eo.insert(QStringLiteral("messageIndex"), e.messageIndex);
        }
        timeline.append(eo);
    }
    obj.insert(QStringLiteral("timeline"), timeline);

    obj.insert(QStringLiteral("usage"),
               m_usage.has_value() ? QJsonValue(usageToJson(*m_usage)) : QJsonValue());
    obj.insert(QStringLiteral("updatedAt"),
               QJsonValue(static_cast<double>(QDateTime::currentMSecsSinceEpoch())));
    return obj;
}

void AcpSessionModel::schedulePersistIfNeeded()
{
    if (m_historyStore == nullptr) {
        return;
    }
    if (isEmpty()) {
        return;
    }
    const QJsonObject payload = toHistoryJson();
    QMetaObject::invokeMethod(m_historyStore,
                              "scheduleWrite",
                              Qt::QueuedConnection,
                              Q_ARG(QString, m_sessionId),
                              Q_ARG(QJsonObject, payload));
}

void AcpSessionModel::onInitialized(const AcpAgentInfo &info,
                                    const QStringList &availableCommands,
                                    const QList<AcpModeInfo> &modes,
                                    const QString &currentMode,
                                    const QList<AcpModelInfo> &models,
                                    const QString &currentModel,
                                    const QList<AcpConfigOption> &configOptions)
{
    m_agentInfo = info;
    m_availableCommands = availableCommands;
    m_availableModes = modes;
    m_currentModeId = currentMode;
    m_availableModels = models;
    m_currentModelId = currentModel;
    m_configOptions = configOptions;
    emit metadataChanged();
    // No persistence for metadata-only events.
}

void AcpSessionModel::onMessageChunk(const QString &text)
{
    // Auto-close any active streaming thought.
    m_streamingThoughtMessageIndex = -1;

    if (m_streamingAssistantMessageIndex < 0) {
        AcpMessage msg;
        msg.role = QStringLiteral("assistant");
        msg.timestamp = QDateTime::currentMSecsSinceEpoch();
        AcpContentBlock block;
        block.kind = AcpContentBlock::Kind::Text;
        block.text = text;
        msg.content.append(block);
        m_messages.append(msg);
        const int idx = m_messages.size() - 1;
        m_streamingAssistantMessageIndex = idx;

        AcpTimelineEntry entry;
        entry.kind = AcpTimelineEntry::Kind::Message;
        entry.messageIndex = idx;
        entry.groupId = m_currentGroupId;
        m_timeline.append(entry);

        emit messageAppended(idx);
    } else {
        const int idx = m_streamingAssistantMessageIndex;
        AcpMessage &msg = m_messages[idx];
        if (msg.content.isEmpty()
            || msg.content.first().kind != AcpContentBlock::Kind::Text) {
            AcpContentBlock block;
            block.kind = AcpContentBlock::Kind::Text;
            block.text = text;
            msg.content.prepend(block);
            emit messageChunkAppended(idx, text);
        } else {
            // Compaction transition: the agent emits "Compacting..." as a
            // placeholder bubble, performs context compaction, then sends a
            // follow-up chunk starting with "\n\nCompacting completed." into
            // the same assistant message. Treat that as an in-place rewrite
            // so the bubble shows only the completion text instead of both
            // lines stacked.
            const QString &existing = msg.content.first().text;
            const QLatin1String kPlaceholder("Compacting...");
            const QLatin1String kCompletionPrefix("\n\nCompacting completed.");
            if (existing == kPlaceholder && text.startsWith(kCompletionPrefix)) {
                msg.content.first().text = text.mid(2); // strip leading "\n\n"
                emit messageReplaced(idx, msg.content.first().text);
            } else {
                msg.content.first().text.append(text);
                emit messageChunkAppended(idx, text);
            }
        }
    }
    schedulePersistIfNeeded();
}

void AcpSessionModel::onThoughtChunk(const QString &text)
{
    if (m_streamingThoughtMessageIndex < 0) {
        // Symmetric to onMessageChunk closing any active thought: when a new
        // thought stream begins, close any active assistant text stream so the
        // next text chunk opens a fresh bubble below this thought.
        m_streamingAssistantMessageIndex = -1;

        AcpMessage msg;
        msg.role = QStringLiteral("thought");
        msg.timestamp = QDateTime::currentMSecsSinceEpoch();
        AcpContentBlock block;
        block.kind = AcpContentBlock::Kind::Text;
        block.text = text;
        msg.content.append(block);
        m_messages.append(msg);
        const int idx = m_messages.size() - 1;
        m_streamingThoughtMessageIndex = idx;

        AcpTimelineEntry entry;
        entry.kind = AcpTimelineEntry::Kind::Message;
        entry.messageIndex = idx;
        entry.groupId = m_currentGroupId;
        m_timeline.append(entry);

        emit thoughtAppended(idx);
    } else {
        const int idx = m_streamingThoughtMessageIndex;
        AcpMessage &msg = m_messages[idx];
        if (msg.content.isEmpty()
            || msg.content.first().kind != AcpContentBlock::Kind::Text) {
            AcpContentBlock block;
            block.kind = AcpContentBlock::Kind::Text;
            block.text = text;
            msg.content.prepend(block);
        } else {
            msg.content.first().text.append(text);
        }
        emit thoughtChunkAppended(idx, text);
    }
    schedulePersistIfNeeded();
}

void AcpSessionModel::onToolCallReceived(const AcpToolCall &tc)
{
    // A tool-call card is a new timeline peer — close any active streaming
    // assistant text or thought so a subsequent text/thought chunk opens a
    // fresh bubble below the card instead of merging into the previous one.
    m_streamingAssistantMessageIndex = -1;
    m_streamingThoughtMessageIndex = -1;

    AcpToolCall copy = tc;
    copy.groupId = m_currentGroupId;
    m_toolCalls.insert(copy.id, copy);

    AcpTimelineEntry entry;
    entry.kind = AcpTimelineEntry::Kind::ToolCall;
    entry.toolCallId = copy.id;
    entry.groupId = m_currentGroupId;
    m_timeline.append(entry);

    emit toolCallAddedOrUpdated(copy.id);
    schedulePersistIfNeeded();
}

void AcpSessionModel::onToolCallUpdated(const AcpToolCallUpdate &update)
{
    auto it = m_toolCalls.find(update.id);
    if (it == m_toolCalls.end()) {
        // Treat as a new entry — defensive in case updates outrun the
        // initial tool_call. We still want it in the timeline.
        // Also close any active streaming text/thought, since this branch
        // adds a fresh timeline peer (same rule as onToolCallReceived).
        m_streamingAssistantMessageIndex = -1;
        m_streamingThoughtMessageIndex = -1;

        AcpToolCall tc;
        tc.id = update.id;
        if (update.status.has_value()) {
            tc.status = *update.status;
        }
        if (update.content.has_value()) {
            tc.content = *update.content;
        }
        if (update.rawInput.has_value()) {
            tc.rawInput = *update.rawInput;
        }
        tc.groupId = m_currentGroupId;
        m_toolCalls.insert(update.id, tc);

        AcpTimelineEntry entry;
        entry.kind = AcpTimelineEntry::Kind::ToolCall;
        entry.toolCallId = update.id;
        entry.groupId = m_currentGroupId;
        m_timeline.append(entry);
    } else {
        if (update.status.has_value()) {
            it.value().status = *update.status;
        }
        if (update.content.has_value()) {
            it.value().content = *update.content;
        }
        if (update.rawInput.has_value()) {
            it.value().rawInput = *update.rawInput;
        }
    }
    emit toolCallAddedOrUpdated(update.id);
    schedulePersistIfNeeded();
}

void AcpSessionModel::onPlanReceived(const QList<AcpPlanEntry> &plan)
{
    m_plan = plan;
    emit planUpdated();
    schedulePersistIfNeeded();
}

void AcpSessionModel::onAvailableCommandsUpdated(const QStringList &commands)
{
    m_availableCommands = commands;
    emit availableCommandsChanged();
    // Metadata-only — skip persistence.
}

void AcpSessionModel::onCurrentModeChanged(const QString &modeId)
{
    m_currentModeId = modeId;
    emit currentModeChanged(modeId);
    schedulePersistIfNeeded();
}

void AcpSessionModel::onConfigOptionsUpdated(const QList<AcpConfigOption> &options)
{
    m_configOptions = options;

    // Agents (e.g. Claude Code) signal a model or mode change via a
    // config_option_update notification rather than the dedicated
    // session/set_model or current_mode_update channels. Without syncing the
    // single-field current* state, the next metadata-driven re-hydration
    // would snap the combo back to whatever was set at session creation.
    for (const auto &opt : options) {
        if (opt.id == QLatin1String("model")) {
            const QString v = opt.currentValue.toString();
            if (!v.isEmpty()) m_currentModelId = v;
        } else if (opt.id == QLatin1String("mode")) {
            const QString v = opt.currentValue.toString();
            if (!v.isEmpty() && v != m_currentModeId) {
                m_currentModeId = v;
                emit currentModeChanged(v);
            }
        }
    }

    emit metadataChanged();
}

void AcpSessionModel::onUsageUpdated(const AcpUsage &usage)
{
    // Merge into existing snapshot: different wire events (usage_update vs.
    // session_info_update vs. session/prompt response) carry overlapping but
    // not identical fields. Replacing wholesale would drop e.g. maxTokens
    // when a later event only reports totalTokens.
    AcpUsage merged = m_usage.value_or(AcpUsage{});
    if (usage.inputTokens.has_value())  merged.inputTokens  = usage.inputTokens;
    if (usage.outputTokens.has_value()) merged.outputTokens = usage.outputTokens;
    if (usage.totalTokens.has_value())  merged.totalTokens  = usage.totalTokens;
    if (usage.maxTokens.has_value())    merged.maxTokens    = usage.maxTokens;
    if (usage.costAmount.has_value())   merged.costAmount   = usage.costAmount;
    if (usage.costCurrency.has_value()) merged.costCurrency = usage.costCurrency;
    m_usage = merged;
    emit usageChanged();
    schedulePersistIfNeeded();
}

void AcpSessionModel::onUsageReplaced(const AcpUsage &usage)
{
    // `usage_update` is the agent's authoritative running total — overwrite
    // the snapshot so stale fields (e.g. a previous turn's cost) don't linger.
    m_usage = usage;
    emit usageChanged();
    schedulePersistIfNeeded();
}

void AcpSessionModel::onPromptStarted()
{
    m_isProcessing = true;
    ++m_currentGroupId;
    emit isProcessingChanged(true);
    schedulePersistIfNeeded();
}

void AcpSessionModel::onPromptEnded()
{
    m_isProcessing = false;
    const int turnGroup = m_currentGroupId;
    m_streamingAssistantMessageIndex = -1;
    m_streamingThoughtMessageIndex = -1;
    emit isProcessingChanged(false);
    emit turnEnded(turnGroup);
    schedulePersistIfNeeded();
}

void AcpSessionModel::appendUserMessage(const QString &text,
                                       const QVector<QPair<QByteArray, QString>> &images,
                                       bool fromGoalAgent)
{
    AcpMessage msg;
    msg.role = QStringLiteral("user");
    msg.timestamp = QDateTime::currentMSecsSinceEpoch();
    msg.fromGoalAgent = fromGoalAgent;
    if (!text.isEmpty()) {
        AcpContentBlock tb;
        tb.kind = AcpContentBlock::Kind::Text;
        tb.text = text;
        msg.content.append(tb);
    }
    for (const auto &img : images) {
        AcpContentBlock ib;
        ib.kind = AcpContentBlock::Kind::Image;
        ib.imageData = img.first;
        ib.mimeType = img.second;
        msg.content.append(ib);
    }
    m_messages.append(msg);
    const int idx = m_messages.size() - 1;

    AcpTimelineEntry entry;
    entry.kind = AcpTimelineEntry::Kind::Message;
    entry.messageIndex = idx;
    entry.groupId = m_currentGroupId;
    m_timeline.append(entry);

    // A user-initiated message also closes any streaming assistant/thought.
    m_streamingAssistantMessageIndex = -1;
    m_streamingThoughtMessageIndex = -1;

    emit messageAppended(idx);
    schedulePersistIfNeeded();
}

void AcpSessionModel::appendSystemMessage(const QString &text)
{
    AcpMessage msg;
    msg.role = QStringLiteral("system");
    msg.timestamp = QDateTime::currentMSecsSinceEpoch();
    AcpContentBlock tb;
    tb.kind = AcpContentBlock::Kind::Text;
    tb.text = text;
    msg.content.append(tb);
    m_messages.append(msg);
    const int idx = m_messages.size() - 1;

    AcpTimelineEntry entry;
    entry.kind = AcpTimelineEntry::Kind::Message;
    entry.messageIndex = idx;
    entry.groupId = m_currentGroupId;
    m_timeline.append(entry);

    emit messageAppended(idx);
    schedulePersistIfNeeded();
}
