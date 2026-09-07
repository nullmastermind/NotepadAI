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

#include "ai/OpenaiAnthropicBridge.h"


class TestOpenaiAnthropicBridge : public QObject
{
    Q_OBJECT

private slots:
    void translateRequest_emptyKeyFailsWithoutUrl();
    void translateRequest_rewritesChatCompletionsUrlAndHeaders();
    void translateRequest_liftsSystemAndConvertsTools();
    void translateResponse_mapsToolUseToOpenAiToolCalls();
    void translateError_wrapsMessage();
};

void TestOpenaiAnthropicBridge::translateRequest_emptyKeyFailsWithoutUrl()
{
    const auto bridged = ai::OpenaiAnthropicBridge::translateRequest(
        QUrl(QStringLiteral("https://api.anthropic.com/v1/chat/completions")),
        QString(), QByteArray("{}"));
    QCOMPARE(bridged.error, QStringLiteral("API key is empty"));
    QVERIFY(bridged.body.isEmpty());
}

void TestOpenaiAnthropicBridge::translateRequest_rewritesChatCompletionsUrlAndHeaders()
{
    const auto bridged = ai::OpenaiAnthropicBridge::translateRequest(
        QUrl(QStringLiteral("https://api.anthropic.com/v1/chat/completions")),
        QStringLiteral("sk-ant"),
        QByteArray("{\"model\":\"claude-opus-5\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}"));
    QVERIFY(bridged.error.isEmpty());
    QCOMPARE(bridged.url, QUrl(QStringLiteral("https://api.anthropic.com/v1/messages")));
    QByteArray key;
    QByteArray version;
    bool sawBearer = false;
    for (const auto &h : bridged.headers) {
        if (h.first == "x-api-key") key = h.second;
        else if (h.first == "anthropic-version") version = h.second;
        else if (h.first == "Authorization") sawBearer = true;
    }
    QCOMPARE(key, QByteArray("sk-ant"));
    QCOMPARE(version, QByteArray("2023-06-01"));
    QVERIFY(!sawBearer);
}

void TestOpenaiAnthropicBridge::translateRequest_liftsSystemAndConvertsTools()
{
    const QByteArray openai =
        "{\"model\":\"claude-opus-5\",\"messages\":["
        "{\"role\":\"system\",\"content\":\"sys\"},"
        "{\"role\":\"user\",\"content\":\"hi\"}],"
        "\"tools\":[{\"type\":\"function\",\"function\":{"
        "\"name\":\"click\",\"description\":\"Click\",\"parameters\":{\"type\":\"object\"}}}],"
        "\"tool_choice\":\"required\"}";
    const auto bridged = ai::OpenaiAnthropicBridge::translateRequest(
        QUrl(QStringLiteral("https://api.anthropic.com/v1/chat/completions")),
        QStringLiteral("sk-ant"), openai);
    QVERIFY(bridged.error.isEmpty());
    const QJsonObject body = QJsonDocument::fromJson(bridged.body).object();
    QCOMPARE(body.value(QLatin1String("system")).toString(), QStringLiteral("sys"));
    QCOMPARE(body.value(QLatin1String("stream")).toBool(), false);
    QCOMPARE(body.value(QLatin1String("max_tokens")).toInt(), 16000);
    const QJsonArray messages = body.value(QLatin1String("messages")).toArray();
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.at(0).toObject().value(QLatin1String("role")).toString(), QStringLiteral("user"));
    const QJsonArray tools = body.value(QLatin1String("tools")).toArray();
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools.at(0).toObject().value(QLatin1String("name")).toString(), QStringLiteral("click"));
    QCOMPARE(body.value(QLatin1String("tool_choice")).toObject().value(QLatin1String("type")).toString(),
             QStringLiteral("any"));
}

void TestOpenaiAnthropicBridge::translateResponse_mapsToolUseToOpenAiToolCalls()
{
    const QByteArray anthropic =
        "{\"content\":[{\"type\":\"text\",\"text\":\"ok\"},"
        "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"click\","
        "\"input\":{\"x\":1}}],\"stop_reason\":\"tool_use\"}";
    const QJsonObject openai = QJsonDocument::fromJson(
        ai::OpenaiAnthropicBridge::translateResponse(anthropic)).object();
    const QJsonObject choice = openai.value(QLatin1String("choices")).toArray().at(0).toObject();
    QCOMPARE(choice.value(QLatin1String("finish_reason")).toString(), QStringLiteral("tool_calls"));
    const QJsonObject msg = choice.value(QLatin1String("message")).toObject();
    QCOMPARE(msg.value(QLatin1String("content")).toString(), QStringLiteral("ok"));
    const QJsonObject tc = msg.value(QLatin1String("tool_calls")).toArray().at(0).toObject();
    QCOMPARE(tc.value(QLatin1String("id")).toString(), QStringLiteral("toolu_1"));
    const QJsonObject fn = tc.value(QLatin1String("function")).toObject();
    QCOMPARE(fn.value(QLatin1String("name")).toString(), QStringLiteral("click"));
    QCOMPARE(fn.value(QLatin1String("arguments")).toString(), QStringLiteral("{\"x\":1}"));
}

void TestOpenaiAnthropicBridge::translateError_wrapsMessage()
{
    const QByteArray body =
        "{\"type\":\"error\",\"error\":{\"message\":\"invalid x-api-key\"}}";
    const QJsonObject err = QJsonDocument::fromJson(
        ai::OpenaiAnthropicBridge::translateError(401, body)).object();
    QCOMPARE(err.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString(),
             QStringLiteral("invalid x-api-key"));
}

QTEST_APPLESS_MAIN(TestOpenaiAnthropicBridge)

#include "test_openai_anthropic_bridge.moc"
