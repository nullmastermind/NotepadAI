/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "AcpCatchUp.h"

namespace AcpCatchUp {

namespace {

const QString kKind = QStringLiteral("sessionUpdate");

bool isFinal(const QString &status)
{
    return status == QLatin1String("completed") || status == QLatin1String("failed")
        || status == QLatin1String("cancelled") || status == QLatin1String("canceled");
}

QString kindOf(const QJsonObject &update)
{
    return update.value(kKind).toString();
}

bool isToolCall(const QString &kind)
{
    return kind == QLatin1String("tool_call") || kind == QLatin1String("tool_call_update");
}

QString chunkText(const QJsonObject &update)
{
    const QJsonObject content = update.value(QStringLiteral("content")).toObject();
    if (content.value(QStringLiteral("type")).toString() != QLatin1String("text"))
        return {};
    return content.value(QStringLiteral("text")).toString();
}

} // namespace

QList<QJsonObject> missingUpdates(const QList<QJsonObject> &replay, const Seen &seen)
{
    qsizetype lastUser = -1;
    for (qsizetype i = 0; i < replay.size(); ++i) {
        if (kindOf(replay.at(i)) == QLatin1String("user_message_chunk"))
            lastUser = i;
    }

    // The replayed turn's text must start with what the client streamed;
    // otherwise the turns do not line up and no text is safe to add.
    QString replayText;
    for (qsizetype i = lastUser + 1; i < replay.size(); ++i) {
        if (kindOf(replay.at(i)) == QLatin1String("agent_message_chunk"))
            replayText += chunkText(replay.at(i));
    }
    const bool textExtends = replayText.startsWith(seen.turnText);
    const qsizetype seenLen = seen.turnText.size();

    QList<QJsonObject> out;
    QHash<QString, QString> known = seen.toolStatus; // grows with what we return
    qsizetype cursor = 0;
    for (qsizetype i = 0; i < replay.size(); ++i) {
        const QJsonObject &update = replay.at(i);
        const QString kind = kindOf(update);
        if (isToolCall(kind)) {
            const QString id = update.value(QStringLiteral("toolCallId")).toString();
            if (id.isEmpty())
                continue;
            const QString status = update.value(QStringLiteral("status")).toString();
            const auto it = known.constFind(id);
            if (it == known.constEnd()) {
                if (i <= lastUser)
                    continue; // an older turn's call
                QJsonObject call = update;
                call.insert(kKind, QStringLiteral("tool_call"));
                out.append(call);
                known.insert(id, status);
            } else if (isFinal(status) && !isFinal(it.value())) {
                QJsonObject done = update;
                done.insert(kKind, QStringLiteral("tool_call_update"));
                out.append(done);
                known.insert(id, status);
            }
        } else if (kind == QLatin1String("agent_message_chunk") && i > lastUser && textExtends) {
            const QString text = chunkText(update);
            const qsizetype len = text.size();
            if (cursor + len <= seenLen) {
                cursor += len;
                continue;
            }
            const qsizetype skip = qMax<qsizetype>(0, seenLen - cursor);
            cursor += len;
            QJsonObject chunk = update;
            QJsonObject content = chunk.value(QStringLiteral("content")).toObject();
            content.insert(QStringLiteral("text"), text.mid(skip));
            chunk.insert(QStringLiteral("content"), content);
            out.append(chunk);
        }
    }
    return out;
}

} // namespace AcpCatchUp
