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


#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ai/LlmHttpClient.h"


class TestLlmHttpClient : public QObject
{
    Q_OBJECT

private slots:
    void anthropicPayload_usesTopLevelSystemAndDefaultMaxTokens();
    void anthropicPayload_omitsEmptySystem();
    void anthropicPayload_explicitMaxTokensHonored();
    void anthropicUrl_appendsV1Messages();
    void anthropicUrl_alreadyCompleteDoesNotDouble();
    void anthropicUrl_v1BaseAppendsMessagesOnly();
    void anthropicHeaders_useXApiKeyAndVersion();
    void anthropicHeaders_emptyKeyOmitsXApiKeyKeepsVersion();
    void openaiHeaders_useBearerNotXApiKey();
    void anthropicPayload_encodesImagesAsBase64Source();
    void anthropicHttpError_extractsErrorMessage();
    void httpErrorText_nonJsonReturnsHttpStatus();
    void httpErrorText_doesNotEchoSecretInRawBody();
    void rejectReason_emptyKey();
    void openaiUrl_appendsChatCompletionsNotMessages();
    void openaiPayload_keepsSystemRoleAndOmitsMaxTokensWhenZero();
    void anthropicPayload_neverPutsSystemRoleInMessages();
};

void TestLlmHttpClient::anthropicPayload_usesTopLevelSystemAndDefaultMaxTokens()
{
    ai::ILlmHttpClient::Request req;
    req.model = QStringLiteral("claude-opus-5");
    req.systemPrompt = QStringLiteral("Be terse");
    req.prompt = QStringLiteral("Hello");
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;

    const QJsonObject body = QJsonDocument::fromJson(ai::LlmHttpClient::buildPayload(req)).object();
    QCOMPARE(body.value(QLatin1String("model")).toString(), QStringLiteral("claude-opus-5"));
    QCOMPARE(body.value(QLatin1String("system")).toString(), QStringLiteral("Be terse"));
    QCOMPARE(body.value(QLatin1String("stream")).toBool(), true);
    QCOMPARE(body.value(QLatin1String("max_tokens")).toInt(), 16000);

    const QJsonArray messages = body.value(QLatin1String("messages")).toArray();
    QCOMPARE(messages.size(), 1);
    const QJsonObject user = messages.at(0).toObject();
    QCOMPARE(user.value(QLatin1String("role")).toString(), QStringLiteral("user"));
    QCOMPARE(user.value(QLatin1String("content")).toString(), QStringLiteral("Hello"));
}

void TestLlmHttpClient::anthropicPayload_omitsEmptySystem()
{
    ai::ILlmHttpClient::Request req;
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;
    req.prompt = QStringLiteral("Hello");
    const QJsonObject body = QJsonDocument::fromJson(ai::LlmHttpClient::buildPayload(req)).object();
    QVERIFY(!body.contains(QLatin1String("system")));
    QCOMPARE(body.value(QLatin1String("max_tokens")).toInt(), 16000);
}

void TestLlmHttpClient::anthropicPayload_explicitMaxTokensHonored()
{
    ai::ILlmHttpClient::Request req;
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;
    req.prompt = QStringLiteral("Hello");
    req.maxTokens = 4096;
    const QJsonObject body = QJsonDocument::fromJson(ai::LlmHttpClient::buildPayload(req)).object();
    QVERIFY(body.contains(QLatin1String("max_tokens")));
    QCOMPARE(body.value(QLatin1String("max_tokens")).toInt(), 4096);
}

void TestLlmHttpClient::anthropicUrl_appendsV1Messages()
{
    ai::ILlmHttpClient::Request req;
    req.url = QUrl(QStringLiteral("https://api.anthropic.com"));
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;
    QCOMPARE(ai::LlmHttpClient::normalizeUrl(req),
             QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
}

void TestLlmHttpClient::anthropicUrl_alreadyCompleteDoesNotDouble()
{
    ai::ILlmHttpClient::Request req;
    req.url = QUrl(QStringLiteral("https://api.anthropic.com/v1/messages"));
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;
    QCOMPARE(ai::LlmHttpClient::normalizeUrl(req),
             QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
}

void TestLlmHttpClient::anthropicUrl_v1BaseAppendsMessagesOnly()
{
    ai::ILlmHttpClient::Request req;
    req.url = QUrl(QStringLiteral("https://api.anthropic.com/v1"));
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;
    QCOMPARE(ai::LlmHttpClient::normalizeUrl(req),
             QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
}

void TestLlmHttpClient::anthropicHeaders_useXApiKeyAndVersion()
{
    ai::ILlmHttpClient::Request req;
    req.apiKey = QStringLiteral("sk-ant-test");
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;

    const auto headers = ai::LlmHttpClient::authHeaders(req);
    QByteArray apiKey;
    QByteArray version;
    bool sawAuthorization = false;
    for (const auto &h : headers) {
        if (h.first == "x-api-key") apiKey = h.second;
        else if (h.first == "anthropic-version") version = h.second;
        else if (h.first == "Authorization") sawAuthorization = true;
    }
    QCOMPARE(apiKey, QByteArray("sk-ant-test"));
    QCOMPARE(version, QByteArray("2023-06-01"));
    QVERIFY(!sawAuthorization);
}

void TestLlmHttpClient::anthropicHeaders_emptyKeyOmitsXApiKeyKeepsVersion()
{
    ai::ILlmHttpClient::Request req;
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;
    const auto headers = ai::LlmHttpClient::authHeaders(req);
    bool sawKey = false;
    bool sawVersion = false;
    bool sawAuthorization = false;
    for (const auto &h : headers) {
        if (h.first == "x-api-key") sawKey = true;
        else if (h.first == "anthropic-version") sawVersion = true;
        else if (h.first == "Authorization") sawAuthorization = true;
    }
    QVERIFY(!sawKey);
    QVERIFY(sawVersion);
    QVERIFY(!sawAuthorization);
}

void TestLlmHttpClient::openaiHeaders_useBearerNotXApiKey()
{
    ai::ILlmHttpClient::Request req;
    req.apiKey = QStringLiteral("sk-test");
    const auto headers = ai::LlmHttpClient::authHeaders(req);
    QCOMPARE(headers.size(), 1);
    QCOMPARE(headers.at(0).first, QByteArray("Authorization"));
    QCOMPARE(headers.at(0).second, QByteArray("Bearer sk-test"));
}

void TestLlmHttpClient::anthropicPayload_encodesImagesAsBase64Source()
{
    ai::ILlmHttpClient::Request req;
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;
    req.prompt = QStringLiteral("see this");
    req.images.append({QByteArray("PNGDATA"), QStringLiteral("image/png")});

    const QJsonObject body = QJsonDocument::fromJson(ai::LlmHttpClient::buildPayload(req)).object();
    const QJsonArray messages = body.value(QLatin1String("messages")).toArray();
    QCOMPARE(messages.size(), 1);
    const QJsonArray content = messages.at(0).toObject().value(QLatin1String("content")).toArray();
    QCOMPARE(content.size(), 2);
    QCOMPARE(content.at(0).toObject().value(QLatin1String("type")).toString(), QStringLiteral("text"));
    QCOMPARE(content.at(0).toObject().value(QLatin1String("text")).toString(), QStringLiteral("see this"));
    const QJsonObject img = content.at(1).toObject();
    QCOMPARE(img.value(QLatin1String("type")).toString(), QStringLiteral("image"));
    const QJsonObject source = img.value(QLatin1String("source")).toObject();
    QCOMPARE(source.value(QLatin1String("type")).toString(), QStringLiteral("base64"));
    QCOMPARE(source.value(QLatin1String("media_type")).toString(), QStringLiteral("image/png"));
    QCOMPARE(source.value(QLatin1String("data")).toString(),
             QString::fromLatin1(QByteArray("PNGDATA").toBase64()));
}

void TestLlmHttpClient::anthropicHttpError_extractsErrorMessage()
{
    const QByteArray body =
        "{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\","
        "\"message\":\"invalid x-api-key\"}}";
    QCOMPARE(ai::LlmHttpClient::httpErrorText(401, body),
             QStringLiteral("invalid x-api-key"));
}

void TestLlmHttpClient::httpErrorText_nonJsonReturnsHttpStatus()
{
    QCOMPARE(ai::LlmHttpClient::httpErrorText(401, QByteArray("not-json <html>")),
             QStringLiteral("HTTP 401"));
}

void TestLlmHttpClient::httpErrorText_doesNotEchoSecretInRawBody()
{
    const QByteArray body = "unauthorized sk-ant-leaked-secret";
    const QString msg = ai::LlmHttpClient::httpErrorText(401, body);
    QCOMPARE(msg, QStringLiteral("HTTP 401"));
    QVERIFY(!msg.contains(QLatin1String("sk-ant")));
}

void TestLlmHttpClient::rejectReason_emptyKey()
{
    ai::ILlmHttpClient::Request req;
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;
    req.prompt = QStringLiteral("hi");
    QCOMPARE(ai::LlmHttpClient::rejectReason(req), QStringLiteral("API key is empty"));
    req.apiKey = QStringLiteral("sk-ant");
    QVERIFY(ai::LlmHttpClient::rejectReason(req).isEmpty());
}

void TestLlmHttpClient::openaiUrl_appendsChatCompletionsNotMessages()
{
    ai::ILlmHttpClient::Request req;
    req.url = QUrl(QStringLiteral("https://api.openai.com/v1"));
    QCOMPARE(ai::LlmHttpClient::normalizeUrl(req),
             QUrl(QStringLiteral("https://api.openai.com/v1/chat/completions")));
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;
    QCOMPARE(ai::LlmHttpClient::normalizeUrl(req),
             QUrl(QStringLiteral("https://api.openai.com/v1/messages")));
}

void TestLlmHttpClient::openaiPayload_keepsSystemRoleAndOmitsMaxTokensWhenZero()
{
    ai::ILlmHttpClient::Request req;
    req.model = QStringLiteral("gpt-4o-mini");
    req.systemPrompt = QStringLiteral("Be terse");
    req.prompt = QStringLiteral("Hello");
    const QJsonObject body = QJsonDocument::fromJson(ai::LlmHttpClient::buildPayload(req)).object();
    QVERIFY(!body.contains(QLatin1String("system")));
    QVERIFY(!body.contains(QLatin1String("max_tokens")));
    QCOMPARE(body.value(QLatin1String("stream")).toBool(), true);
    const QJsonArray messages = body.value(QLatin1String("messages")).toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value(QLatin1String("role")).toString(), QStringLiteral("system"));
    QCOMPARE(messages.at(0).toObject().value(QLatin1String("content")).toString(), QStringLiteral("Be terse"));
    QCOMPARE(messages.at(1).toObject().value(QLatin1String("role")).toString(), QStringLiteral("user"));
}

void TestLlmHttpClient::anthropicPayload_neverPutsSystemRoleInMessages()
{
    ai::ILlmHttpClient::Request req;
    req.apiFormat = ai::ILlmHttpClient::ApiFormat::Anthropic;
    req.systemPrompt = QStringLiteral("Be terse");
    req.prompt = QStringLiteral("Hello");
    const QJsonArray messages = QJsonDocument::fromJson(ai::LlmHttpClient::buildPayload(req))
                                    .object().value(QLatin1String("messages")).toArray();
    for (const auto &v : messages)
        QVERIFY(v.toObject().value(QLatin1String("role")).toString() != QLatin1String("system"));
}

QTEST_APPLESS_MAIN(TestLlmHttpClient)

#include "test_llm_http_client.moc"
