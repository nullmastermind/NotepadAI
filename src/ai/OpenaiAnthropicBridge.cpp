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


#include "OpenaiAnthropicBridge.h"

#include "LlmHttpClient.h"
#include "ILlmHttpClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace ai {

namespace {

QString contentToString(const QJsonValue &content)
{
    if (content.isString())
        return content.toString();
    if (!content.isArray())
        return {};
    QString out;
    const QJsonArray arr = content.toArray();
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("type")).toString() == QLatin1String("text"))
            out += o.value(QLatin1String("text")).toString();
    }
    return out;
}

QJsonArray convertTools(const QJsonArray &openaiTools)
{
    QJsonArray out;
    for (const auto &v : openaiTools) {
        const QJsonObject fn = v.toObject().value(QLatin1String("function")).toObject();
        QJsonObject tool;
        tool.insert(QLatin1String("name"), fn.value(QLatin1String("name")));
        tool.insert(QLatin1String("description"), fn.value(QLatin1String("description")));
        tool.insert(QLatin1String("input_schema"), fn.value(QLatin1String("parameters")));
        out.append(tool);
    }
    return out;
}

QJsonObject convertToolChoice(const QJsonValue &choice)
{
    QJsonObject out;
    if (choice.isString() && choice.toString() == QLatin1String("required")) {
        out.insert(QLatin1String("type"), QLatin1String("any"));
        return out;
    }
    if (choice.isObject()) {
        const QJsonObject o = choice.toObject();
        if (o.value(QLatin1String("type")).toString() == QLatin1String("function")) {
            out.insert(QLatin1String("type"), QLatin1String("tool"));
            out.insert(QLatin1String("name"),
                       o.value(QLatin1String("function")).toObject().value(QLatin1String("name")));
            return out;
        }
    }
    out.insert(QLatin1String("type"), QLatin1String("auto"));
    return out;
}

QJsonObject assistantToAnthropic(const QJsonObject &msg)
{
    QJsonArray content;
    const QString text = contentToString(msg.value(QLatin1String("content")));
    if (!text.isEmpty()) {
        QJsonObject t;
        t.insert(QLatin1String("type"), QLatin1String("text"));
        t.insert(QLatin1String("text"), text);
        content.append(t);
    }
    const QJsonArray toolCalls = msg.value(QLatin1String("tool_calls")).toArray();
    for (const auto &v : toolCalls) {
        const QJsonObject tc = v.toObject();
        const QJsonObject fn = tc.value(QLatin1String("function")).toObject();
        QJsonObject use;
        use.insert(QLatin1String("type"), QLatin1String("tool_use"));
        use.insert(QLatin1String("id"), tc.value(QLatin1String("id")));
        use.insert(QLatin1String("name"), fn.value(QLatin1String("name")));
        QJsonParseError err{};
        const QJsonDocument args = QJsonDocument::fromJson(
            fn.value(QLatin1String("arguments")).toString().toUtf8(), &err);
        use.insert(QLatin1String("input"),
                   err.error == QJsonParseError::NoError ? args.object() : QJsonObject());
        content.append(use);
    }
    QJsonObject out;
    out.insert(QLatin1String("role"), QLatin1String("assistant"));
    out.insert(QLatin1String("content"), content);
    return out;
}

} // namespace

BridgedRequest OpenaiAnthropicBridge::translateRequest(const QUrl &openaiUrl,
                                                       const QString &apiKey,
                                                       const QByteArray &openaiBody)
{
    BridgedRequest out;
    if (apiKey.trimmed().isEmpty()) {
        out.error = QStringLiteral("API key is empty");
        return out;
    }

    ILlmHttpClient::Request norm;
    QString s = openaiUrl.toString().trimmed();
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    if (s.endsWith(QLatin1String("/chat/completions")))
        s.chop(QStringLiteral("/chat/completions").size());
    norm.url = QUrl(s);
    norm.apiFormat = ILlmHttpClient::ApiFormat::Anthropic;
    out.url = LlmHttpClient::normalizeUrl(norm);

    out.headers.append({QByteArray("content-type"), QByteArray("application/json")});
    out.headers.append({QByteArray("x-api-key"), apiKey.toUtf8()});
    out.headers.append({QByteArray("anthropic-version"), QByteArray("2023-06-01")});

    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(openaiBody, &jerr);
    if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
        out.error = QStringLiteral("Malformed chat-completions body");
        return out;
    }
    const QJsonObject src = doc.object();
    QJsonObject body;
    body.insert(QLatin1String("model"), src.value(QLatin1String("model")));
    body.insert(QLatin1String("stream"), false);
    const int maxTokens = src.value(QLatin1String("max_tokens")).toInt();
    body.insert(QLatin1String("max_tokens"), maxTokens > 0 ? maxTokens : 16000);
    if (src.contains(QLatin1String("temperature")))
        body.insert(QLatin1String("temperature"), src.value(QLatin1String("temperature")));

    QString system;
    QJsonArray messages;
    QJsonArray pendingToolResults;
    const auto flushToolResults = [&]() {
        if (pendingToolResults.isEmpty())
            return;
        QJsonObject user;
        user.insert(QLatin1String("role"), QLatin1String("user"));
        user.insert(QLatin1String("content"), pendingToolResults);
        messages.append(user);
        pendingToolResults = QJsonArray();
    };

    const QJsonArray srcMsgs = src.value(QLatin1String("messages")).toArray();
    for (const auto &v : srcMsgs) {
        const QJsonObject msg = v.toObject();
        const QString role = msg.value(QLatin1String("role")).toString();
        if (role == QLatin1String("system")) {
            if (!system.isEmpty())
                system += QLatin1Char('\n');
            system += contentToString(msg.value(QLatin1String("content")));
            continue;
        }
        if (role == QLatin1String("tool")) {
            QJsonObject result;
            result.insert(QLatin1String("type"), QLatin1String("tool_result"));
            result.insert(QLatin1String("tool_use_id"), msg.value(QLatin1String("tool_call_id")));
            result.insert(QLatin1String("content"), contentToString(msg.value(QLatin1String("content"))));
            pendingToolResults.append(result);
            continue;
        }
        flushToolResults();
        if (role == QLatin1String("assistant")) {
            messages.append(assistantToAnthropic(msg));
            continue;
        }
        QJsonObject user;
        user.insert(QLatin1String("role"), QLatin1String("user"));
        user.insert(QLatin1String("content"), contentToString(msg.value(QLatin1String("content"))));
        messages.append(user);
    }
    flushToolResults();
    body.insert(QLatin1String("messages"), messages);
    if (!system.isEmpty())
        body.insert(QLatin1String("system"), system);

    const QJsonArray tools = convertTools(src.value(QLatin1String("tools")).toArray());
    if (!tools.isEmpty()) {
        body.insert(QLatin1String("tools"), tools);
        if (src.contains(QLatin1String("tool_choice")))
            body.insert(QLatin1String("tool_choice"), convertToolChoice(src.value(QLatin1String("tool_choice"))));
    }

    out.body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    return out;
}

QByteArray OpenaiAnthropicBridge::translateResponse(const QByteArray &anthropicBody)
{
    const QJsonObject src = QJsonDocument::fromJson(anthropicBody).object();
    QString text;
    QJsonArray toolCalls;
    const QJsonArray content = src.value(QLatin1String("content")).toArray();
    for (const auto &v : content) {
        const QJsonObject block = v.toObject();
        const QString type = block.value(QLatin1String("type")).toString();
        if (type == QLatin1String("text")) {
            text += block.value(QLatin1String("text")).toString();
            continue;
        }
        if (type != QLatin1String("tool_use"))
            continue;
        QJsonObject fn;
        fn.insert(QLatin1String("name"), block.value(QLatin1String("name")));
        fn.insert(QLatin1String("arguments"),
                  QString::fromUtf8(QJsonDocument(block.value(QLatin1String("input")).toObject())
                                        .toJson(QJsonDocument::Compact)));
        QJsonObject tc;
        tc.insert(QLatin1String("id"), block.value(QLatin1String("id")));
        tc.insert(QLatin1String("type"), QLatin1String("function"));
        tc.insert(QLatin1String("function"), fn);
        toolCalls.append(tc);
    }

    QString finish = QStringLiteral("stop");
    const QString stop = src.value(QLatin1String("stop_reason")).toString();
    if (stop == QLatin1String("tool_use") || !toolCalls.isEmpty())
        finish = QStringLiteral("tool_calls");
    else if (stop == QLatin1String("max_tokens"))
        finish = QStringLiteral("length");

    QJsonObject message;
    message.insert(QLatin1String("role"), QLatin1String("assistant"));
    message.insert(QLatin1String("content"), text);
    if (!toolCalls.isEmpty())
        message.insert(QLatin1String("tool_calls"), toolCalls);

    QJsonObject choice;
    choice.insert(QLatin1String("index"), 0);
    choice.insert(QLatin1String("message"), message);
    choice.insert(QLatin1String("finish_reason"), finish);

    QJsonObject out;
    QJsonArray choices;
    choices.append(choice);
    out.insert(QLatin1String("choices"), choices);
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QByteArray OpenaiAnthropicBridge::translateError(int status, const QByteArray &anthropicBody)
{
    QJsonObject err;
    err.insert(QLatin1String("message"), LlmHttpClient::httpErrorText(status, anthropicBody));
    QJsonObject out;
    out.insert(QLatin1String("error"), err);
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

} // namespace ai
