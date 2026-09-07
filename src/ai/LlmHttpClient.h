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

#ifndef AI_LLM_HTTP_CLIENT_H
#define AI_LLM_HTTP_CLIENT_H

#include "ILlmHttpClient.h"
#include "SsePartialParser.h"

#include <QPointer>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

namespace ai {

// Production HTTP streaming client. OpenAI-compatible chat-completions and
// Anthropic /v1/messages are selected by Request::apiFormat. POSTs JSON and
// parses the SSE response with SsePartialParser. Watches an idle timeout: if
// no bytes arrive within `idleTimeoutSec`, aborts the request and emits
// errorOccurred.
class LlmHttpClient : public ILlmHttpClient
{
    Q_OBJECT
public:
    explicit LlmHttpClient(QObject *parent = nullptr);
    ~LlmHttpClient() override;

    void openStream(const Request &req) override;
    void cancel() override;

    static QByteArray buildPayload(const Request &req);
    static QUrl normalizeUrl(const Request &req);
    static QVector<QPair<QByteArray, QByteArray>> authHeaders(const Request &req);
    static QString httpErrorText(int status, const QByteArray &body);
    static ApiFormat apiFormatFromStored(int stored);
    static QString rejectReason(const Request &req);

private slots:
    void onReadyRead();
    void onFinished();
    void onIdleTimeout();

private:
    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    SsePartialParser m_parser;
    QTimer m_idleTimer;
    bool m_firstByteEmitted = false;
    bool m_anyTokenSeen = false;
    bool m_endedCleanly = false;

    static QUrl normalizeChatCompletionsUrl(const QUrl &base);
};

} // namespace ai

#endif // AI_LLM_HTTP_CLIENT_H
