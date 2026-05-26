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

#include "LlmHttpClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace ai {

LlmHttpClient::LlmHttpClient(QObject *parent)
    : ILlmHttpClient(parent),
      m_nam(new QNetworkAccessManager(this))
{
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, &LlmHttpClient::onIdleTimeout);
}

LlmHttpClient::~LlmHttpClient() { cancel(); }

QUrl LlmHttpClient::normalizeChatCompletionsUrl(const QUrl &base)
{
    QString s = base.toString().trimmed();
    if (s.isEmpty()) return base;
    while (s.endsWith(QLatin1Char('/'))) s.chop(1);
    if (s.endsWith(QLatin1String("/chat/completions"))) return QUrl(s);
    return QUrl(s + QLatin1String("/chat/completions"));
}

QByteArray LlmHttpClient::buildPayload(const Request &req)
{
    QJsonArray messages;

    if (!req.systemPrompt.isEmpty()) {
        QJsonObject sysMsg;
        sysMsg.insert(QLatin1String("role"),    QLatin1String("system"));
        sysMsg.insert(QLatin1String("content"), req.systemPrompt);
        messages.append(sysMsg);
    }

    QJsonObject userMsg;
    userMsg.insert(QLatin1String("role"),    QLatin1String("user"));
    if (req.images.isEmpty()) {
        userMsg.insert(QLatin1String("content"), req.prompt);
    } else {
        QJsonArray contentArr;
        QJsonObject textPart;
        textPart.insert(QLatin1String("type"), QLatin1String("text"));
        textPart.insert(QLatin1String("text"), req.prompt);
        contentArr.append(textPart);
        for (const auto &img : req.images) {
            QJsonObject urlObj;
            urlObj.insert(QLatin1String("url"),
                          QStringLiteral("data:%1;base64,%2")
                              .arg(img.second,
                                   QString::fromLatin1(img.first.toBase64())));
            urlObj.insert(QLatin1String("detail"), QLatin1String("low"));
            QJsonObject imgPart;
            imgPart.insert(QLatin1String("type"), QLatin1String("image_url"));
            imgPart.insert(QLatin1String("image_url"), urlObj);
            contentArr.append(imgPart);
        }
        userMsg.insert(QLatin1String("content"), contentArr);
    }
    messages.append(userMsg);

    QJsonObject body;
    body.insert(QLatin1String("model"),    req.model);
    body.insert(QLatin1String("messages"), messages);
    body.insert(QLatin1String("stream"),   true);
    if (req.maxTokens > 0)
        body.insert(QLatin1String("max_tokens"), req.maxTokens);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

void LlmHttpClient::openStream(const Request &req)
{
    if (m_reply) {
        // Caller error — but be defensive: cancel previous, then proceed.
        cancel();
    }
    m_firstByteEmitted = false;
    m_anyTokenSeen = false;
    m_endedCleanly = false;
    m_parser.reset();

    QNetworkRequest httpReq(normalizeChatCompletionsUrl(req.url));
    httpReq.setHeader(QNetworkRequest::ContentTypeHeader, QLatin1String("application/json"));
    httpReq.setRawHeader("Accept", "text/event-stream");
    if (!req.apiKey.isEmpty()) {
        httpReq.setRawHeader("Authorization", ("Bearer " + req.apiKey).toUtf8());
    }
    httpReq.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    const QByteArray body = buildPayload(req);
    m_reply = m_nam->post(httpReq, body);

    connect(m_reply.data(), &QIODevice::readyRead,   this, &LlmHttpClient::onReadyRead);
    connect(m_reply.data(), &QNetworkReply::finished, this, &LlmHttpClient::onFinished);

    const int idleMs = qMax(5, req.idleTimeoutSec) * 1000;
    m_idleTimer.start(idleMs);
}

void LlmHttpClient::cancel()
{
    m_idleTimer.stop();
    if (m_reply) {
        QNetworkReply *r = m_reply.data();
        m_reply.clear();
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
}

void LlmHttpClient::onReadyRead()
{
    if (!m_reply) return;

    // Restart idle watchdog on every chunk.
    m_idleTimer.start();

    if (!m_firstByteEmitted) {
        // HTTP status check before emitting firstByteReceived.
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 400) {
            const QByteArray body = m_reply->readAll();
            const QString msg = QString::fromUtf8(body.left(512));
            emit errorOccurred(status, msg.isEmpty()
                ? QStringLiteral("HTTP %1").arg(status) : msg);
            cancel();
            return;
        }
        if (status > 0) {
            m_firstByteEmitted = true;
            emit firstByteReceived();
        }
    }

    const QByteArray bytes = m_reply->readAll();
    m_parser.feed(QByteArrayView(bytes),
                  [this](const SsePartialParser::Event &ev) {
        switch (ev.kind) {
        case SsePartialParser::EventKind::Token:
            m_anyTokenSeen = true;
            emit tokenReceived(ev.text);
            break;
        case SsePartialParser::EventKind::Done:
            m_endedCleanly = true;
            emit streamEnded();
            cancel();
            break;
        case SsePartialParser::EventKind::Error:
            emit errorOccurred(0, ev.text);
            cancel();
            break;
        }
    });
}

void LlmHttpClient::onFinished()
{
    if (!m_reply) return;
    m_idleTimer.stop();

    const QNetworkReply::NetworkError err = m_reply->error();
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QPointer<QNetworkReply> r = m_reply;
    m_reply.clear();
    if (r) r->deleteLater();

    if (m_endedCleanly) return;
    if (err == QNetworkReply::OperationCanceledError) return;
    if (err != QNetworkReply::NoError) {
        emit errorOccurred(status, QStringLiteral("Network error: ") + (r ? r->errorString() : QString()));
        return;
    }
    // Connection closed without [DONE]/finish_reason. If we got tokens, treat
    // as soft end; otherwise as error.
    if (m_anyTokenSeen) emit streamEnded();
    else emit errorOccurred(status, QStringLiteral("Stream closed before any token arrived"));
}

void LlmHttpClient::onIdleTimeout()
{
    if (!m_reply) return;
    const QString msg = m_anyTokenSeen
        ? QStringLiteral("No bytes from provider for the idle timeout window")
        : QStringLiteral("Provider did not respond before the idle timeout");
    emit errorOccurred(0, msg);
    cancel();
}

} // namespace ai
