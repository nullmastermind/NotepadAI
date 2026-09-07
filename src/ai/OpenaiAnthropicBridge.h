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


#ifndef AI_OPENAI_ANTHROPIC_BRIDGE_H
#define AI_OPENAI_ANTHROPIC_BRIDGE_H

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QUrl>
#include <QVector>

namespace ai {

// Translates OpenAI chat-completions wire format (used by Mini App page-agent)
// to Anthropic /v1/messages without modifying the vendor JS bundle.
struct BridgedRequest {
    QUrl url;
    QVector<QPair<QByteArray, QByteArray>> headers;
    QByteArray body;
    QString error;
};

class OpenaiAnthropicBridge
{
public:
    static BridgedRequest translateRequest(const QUrl &openaiUrl,
                                           const QString &apiKey,
                                           const QByteArray &openaiBody);
    static QByteArray translateResponse(const QByteArray &anthropicBody);
    static QByteArray translateError(int status, const QByteArray &anthropicBody);
};

} // namespace ai

#endif // AI_OPENAI_ANTHROPIC_BRIDGE_H
