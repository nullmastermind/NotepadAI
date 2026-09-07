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

#include "SsePartialParser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>

namespace ai {

namespace {

QString extractToken(const QJsonDocument &doc)
{
    if (!doc.isObject()) return {};
    const QJsonObject root = doc.object();

    // 1. choices[0].delta.content (OpenAI streaming canonical).
    const QJsonValue choicesV = root.value(QLatin1String("choices"));
    if (choicesV.isArray()) {
        const QJsonArray arr = choicesV.toArray();
        if (!arr.isEmpty() && arr.first().isObject()) {
            const QJsonObject c0 = arr.first().toObject();
            // delta.content
            const QJsonValue deltaV = c0.value(QLatin1String("delta"));
            if (deltaV.isObject()) {
                const QJsonValue contentV = deltaV.toObject().value(QLatin1String("content"));
                if (contentV.isString()) return contentV.toString();
            }
            // message.content (non-streaming fallback chunk)
            const QJsonValue msgV = c0.value(QLatin1String("message"));
            if (msgV.isObject()) {
                const QJsonValue contentV = msgV.toObject().value(QLatin1String("content"));
                if (contentV.isString()) return contentV.toString();
            }
        }
    }

    // 2. Anthropic streaming: type=content_block_delta, delta.text.
    const QJsonValue typeV = root.value(QLatin1String("type"));
    if (typeV.isString() && typeV.toString() == QLatin1String("content_block_delta")) {
        const QJsonValue deltaV = root.value(QLatin1String("delta"));
        if (deltaV.isObject()) {
            const QJsonValue textV = deltaV.toObject().value(QLatin1String("text"));
            if (textV.isString()) return textV.toString();
        }
    }

    // 3. content (string passthrough, or Anthropic non-stream content[].text).
    const QJsonValue contentV = root.value(QLatin1String("content"));
    if (contentV.isString()) return contentV.toString();
    if (contentV.isArray()) {
        QString acc;
        const QJsonArray arr = contentV.toArray();
        for (const auto &v : arr) {
            const QJsonObject block = v.toObject();
            if (block.value(QLatin1String("type")).toString() == QLatin1String("text"))
                acc += block.value(QLatin1String("text")).toString();
        }
        if (!acc.isEmpty()) return acc;
    }

    // 4. response (Ollama native fallback).
    const QJsonValue respV = root.value(QLatin1String("response"));
    if (respV.isString()) return respV.toString();

    return {};
}

bool hasFinishSignal(const QJsonDocument &doc)
{
    if (!doc.isObject()) return false;
    const QJsonObject root = doc.object();

    // OpenAI: choices[0].finish_reason != null.
    const QJsonValue choicesV = root.value(QLatin1String("choices"));
    if (choicesV.isArray()) {
        const QJsonArray arr = choicesV.toArray();
        if (!arr.isEmpty() && arr.first().isObject()) {
            const QJsonValue fr = arr.first().toObject().value(QLatin1String("finish_reason"));
            if (!fr.isUndefined() && !fr.isNull()) return true;
        }
    }
    // Ollama native: done == true.
    const QJsonValue doneV = root.value(QLatin1String("done"));
    if (doneV.isBool() && doneV.toBool()) return true;

    // Anthropic streaming: type == message_stop.
    if (root.value(QLatin1String("type")).toString() == QLatin1String("message_stop"))
        return true;

    // Anthropic non-stream: type == message with stop_reason set.
    if (root.value(QLatin1String("type")).toString() == QLatin1String("message")) {
        const QJsonValue fr = root.value(QLatin1String("stop_reason"));
        if (!fr.isUndefined() && !fr.isNull()) return true;
    }

    return false;
}

QString extractError(const QJsonDocument &doc)
{
    if (!doc.isObject()) return {};
    const QJsonValue errV = doc.object().value(QLatin1String("error"));
    if (errV.isString()) return errV.toString();
    if (errV.isObject()) {
        const QJsonValue msgV = errV.toObject().value(QLatin1String("message"));
        if (msgV.isString()) return msgV.toString();
    }
    return {};
}

} // namespace

void SsePartialParser::feed(QByteArrayView newBytes, const EventCallback &cb)
{
    m_buffer.append(newBytes);

    while (true) {
        const int lf = m_buffer.indexOf("\n\n");
        const int crlf = m_buffer.indexOf("\r\n\r\n");
        int endPos = -1;
        int delimLen = 0;
        if (lf >= 0 && (crlf < 0 || lf < crlf)) {
            endPos = lf; delimLen = 2;
        } else if (crlf >= 0) {
            endPos = crlf; delimLen = 4;
        }
        if (endPos < 0) break;

        QByteArray eventBytes = m_buffer.left(endPos);
        m_buffer.remove(0, endPos + delimLen);
        processEvent(eventBytes, cb);
    }
}

void SsePartialParser::processEvent(const QByteArray &eventBytes, const EventCallback &cb)
{
    // Concatenate `data:` lines (SSE spec: multi-line data is newline-joined).
    QByteArray dataAccum;
    bool sawData = false;

    int start = 0;
    while (start <= eventBytes.size()) {
        int nl = eventBytes.indexOf('\n', start);
        if (nl < 0) nl = eventBytes.size();
        QByteArray line = eventBytes.mid(start, nl - start);
        if (line.endsWith('\r')) line.chop(1);
        start = nl + 1;
        if (line.isEmpty()) continue;
        if (line.startsWith(':')) continue;                  // SSE comment

        if (line.startsWith("data:")) {
            QByteArray payload = line.mid(5);
            if (payload.startsWith(' ')) payload.remove(0, 1);
            if (sawData) dataAccum.append('\n');
            dataAccum.append(payload);
            sawData = true;
        }
        // Ignore other field types (event:, id:, retry:).
        if (nl >= eventBytes.size()) break;
    }

    if (!sawData) return;

    if (dataAccum == "[DONE]") {
        cb({ EventKind::Done, {} });
        return;
    }

    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(dataAccum, &jerr);
    if (jerr.error != QJsonParseError::NoError) {
        // Tolerant: skip malformed event.
        return;
    }

    const QString err = extractError(doc);
    if (!err.isEmpty()) {
        cb({ EventKind::Error, err });
        return;
    }

    const QString token = extractToken(doc);
    if (!token.isEmpty()) {
        cb({ EventKind::Token, token });
    }

    if (hasFinishSignal(doc)) {
        cb({ EventKind::Done, {} });
    }
}

} // namespace ai
