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

#include "GoalConversationSummary.h"

#include "AcpProtocol.h"
#include "AcpSessionModel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

QString textContent(const AcpMessage &msg)
{
    QString text;
    for (const auto &block : msg.content) {
        if (block.kind == AcpProtocol::AcpContentBlock::Kind::Text)
            text += block.text;
    }
    return text;
}

constexpr int kMaxToolStringChars = 256;

QString xmlText(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (c == QLatin1Char('&'))
            out += QLatin1String("&amp;");
        else if (c == QLatin1Char('<'))
            out += QLatin1String("&lt;");
        else if (c == QLatin1Char('>'))
            out += QLatin1String("&gt;");
        else
            out += c;
    }
    return out;
}

QJsonValue truncateJsonValue(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::String: {
        QString s = value.toString();
        if (s.size() > kMaxToolStringChars) {
            s.truncate(kMaxToolStringChars);
            if (!s.isEmpty() && s.back().isHighSurrogate())
                s.chop(1);
            s.append(QChar(0x2026));
        }
        return s;
    }
    case QJsonValue::Array: {
        QJsonArray out;
        const QJsonArray in = value.toArray();
        for (const auto &v : in)
            out.append(truncateJsonValue(v));
        return out;
    }
    case QJsonValue::Object: {
        QJsonObject out;
        const QJsonObject in = value.toObject();
        for (auto it = in.constBegin(); it != in.constEnd(); ++it)
            out.insert(it.key(), truncateJsonValue(it.value()));
        return out;
    }
    default:
        return value;
    }
}

QString compactTruncated(const QJsonValue &value)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        if (obj.isEmpty())
            return {};
        return xmlText(QString::fromUtf8(
            QJsonDocument(truncateJsonValue(obj).toObject()).toJson(QJsonDocument::Compact)));
    }
    if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        if (arr.isEmpty())
            return {};
        return xmlText(QString::fromUtf8(
            QJsonDocument(truncateJsonValue(arr).toArray()).toJson(QJsonDocument::Compact)));
    }
    return {};
}

void appendMessage(QString &xml, const AcpMessage &msg)
{
    if (msg.role == QLatin1String("user")) {
        xml += QStringLiteral("  <message role=\"user\">")
               + textContent(msg).toHtmlEscaped()
               + QStringLiteral("</message>\n");
    } else if (msg.role == QLatin1String("assistant")) {
        xml += QStringLiteral("  <message role=\"assistant\">")
               + textContent(msg).toHtmlEscaped()
               + QStringLiteral("</message>\n");
    }
}

void appendToolCall(QString &xml, const AcpProtocol::AcpToolCall &tc)
{
    xml += QStringLiteral("  <tool-call id=\"")
           + tc.id.toHtmlEscaped()
           + QStringLiteral("\" title=\"")
           + tc.title.toHtmlEscaped()
           + QStringLiteral("\" kind=\"")
           + tc.kind.toHtmlEscaped()
           + QStringLiteral("\" status=\"")
           + tc.status.toHtmlEscaped()
           + QStringLiteral("\">\n");

    const QString input = compactTruncated(tc.rawInput);
    if (!input.isEmpty()) {
        xml += QStringLiteral("    <input>")
               + input
               + QStringLiteral("</input>\n");
    }
    QString output = compactTruncated(tc.rawOutput);
    if (output.isEmpty())
        output = compactTruncated(tc.content);
    if (!output.isEmpty()) {
        xml += QStringLiteral("    <output>")
               + output
               + QStringLiteral("</output>\n");
    }
    xml += QStringLiteral("  </tool-call>\n");
}

int timelineBegin(const QVector<AcpTimelineEntry> &timeline, int startIndex)
{
    if (startIndex <= 0)
        return 0;
    int begin = 0;
    for (int i = 0; i < timeline.size(); ++i) {
        const auto &entry = timeline[i];
        if (entry.kind == AcpTimelineEntry::Kind::Message && entry.messageIndex < startIndex)
            begin = i + 1;
    }
    return begin;
}

} // namespace

QString GoalConversationSummary::fromModel(const AcpSessionModel *model, int startIndex)
{
    if (!model)
        return QStringLiteral("<conversation />");

    const auto &msgs = model->messages();
    if (startIndex < 0)
        startIndex = 0;
    if (startIndex > msgs.size())
        startIndex = msgs.size();

    const auto &timeline = model->timeline();
    const int begin = timelineBegin(timeline, startIndex);

    QString xml;
    xml.reserve(32 + (timeline.size() - begin) * 96);
    xml = QStringLiteral("<conversation>\n");

    const auto &toolCalls = model->toolCalls();
    for (int i = begin; i < timeline.size(); ++i) {
        const auto &entry = timeline[i];
        if (entry.kind == AcpTimelineEntry::Kind::Message) {
            if (entry.messageIndex >= 0 && entry.messageIndex < msgs.size())
                appendMessage(xml, msgs[entry.messageIndex]);
            continue;
        }
        const auto it = toolCalls.constFind(entry.toolCallId);
        if (it != toolCalls.cend())
            appendToolCall(xml, it.value());
    }

    xml += QStringLiteral("</conversation>");
    return xml;
}
