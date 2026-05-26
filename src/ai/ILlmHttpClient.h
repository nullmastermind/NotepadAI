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

#ifndef AI_I_LLM_HTTP_CLIENT_H
#define AI_I_LLM_HTTP_CLIENT_H

#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>
#include <QVector>

namespace ai {

// Abstract HTTP streaming-completion client. Concrete production impl wraps
// QNetworkAccessManager and SsePartialParser; the mock impl in tests scripts
// canned event sequences. DI pattern matches IGitProcessRunner.
class ILlmHttpClient : public QObject
{
    Q_OBJECT
public:
    struct Request {
        QUrl    url;             // base URL of OpenAI-compatible endpoint (e.g. https://api.openai.com/v1)
        QString model;
        QString apiKey;          // resolved at call time; passed in Authorization header
        QString systemPrompt;    // optional; sent as a system-role message before the user message
        QString prompt;          // already-assembled full prompt (user role)
        int     maxTokens = 0;   // 0 = omit from payload (provider default)
        int     idleTimeoutSec = 60;
        QVector<QPair<QByteArray, QString>> images;
    };

    explicit ILlmHttpClient(QObject *parent = nullptr) : QObject(parent) {}
    ~ILlmHttpClient() override = default;

    // Open a streaming completion against the configured endpoint. Subsequent
    // calls before streamEnded/errorOccurred fire are allowed only after the
    // previous one terminated.
    virtual void openStream(const Request &req) = 0;

    // Abort any in-flight stream. Idempotent.
    virtual void cancel() = 0;

signals:
    // First byte (or first header set) of the response received successfully.
    // Signals authentication accepted and stream is live.
    void firstByteReceived();

    // One token (or token chunk) parsed from the stream.
    void tokenReceived(const QString &token);

    // Stream ended cleanly (provider sent [DONE] / finish_reason / closed
    // gracefully after at least one token).
    void streamEnded();

    // Stream ended with an error. `httpStatus` is the HTTP status code if the
    // error originated from an HTTP-level failure, or 0 for transport / parser
    // errors.
    void errorOccurred(int httpStatus, const QString &message);
};

} // namespace ai

#endif // AI_I_LLM_HTTP_CLIENT_H
