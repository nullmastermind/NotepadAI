#ifndef GOAL_HTTP_JUDGE_H
#define GOAL_HTTP_JUDGE_H

#include "GoalActionParser.h"

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <cstdint>

class GoalHttpJudge
{
public:
    static constexpr const char *kAgentId = "custom-api";
    static constexpr const char *kToolName = "submit_goal_verdict";
    static constexpr const char *kCredentialKey = "goal-agent-custom-api";

    static bool isCustomApiAgent(const QString &agentId);

    static QUrl messagesUrl(const QString &baseUrl);

    enum ParseError : std::uint8_t {
        NoError,
        InvalidJson,
        NoToolCall,
        WrongTool,
        InvalidStatus,
        EmptyText,
        EmptyContent
    };

    static bool parseResponse(const QByteArray &json, GoalAction *out, ParseError *error = nullptr);

    enum Decision : std::uint8_t {
        ApplyContinue,
        ApplyComplete,
        RetryOnce,
        AssumeAchieved,
        FailClosed
    };

    static Decision decide(ParseError parseError, GoalAction::Type type, bool alreadyRetried);

    static QString correctionPrompt();
    static QString judgePrompt(const QString &goal,
                               const QString &conversation,
                               int iteration,
                               int maxIterations,
                               int criterionIndex,
                               int totalCriteria,
                               const QString &originalUserMessage,
                               const QString &templateContent = QString());
    static QByteArray buildRequestBody(const QString &model, const QString &userPrompt);
    static QByteArray buildRetryBody(const QString &model,
                                    const QString &originalUserPrompt,
                                    const QString &assistantText,
                                    const QString &correctionPrompt);
    static QString assistantText(const QByteArray &json);
    static bool isUsableEndpointUrl(const QString &baseUrl);
    static bool isSafeHeaderValue(const QString &value);
    static bool isTransportOutage(int httpStatus);
    static QString sanitizePlainText(const QString &text);
    static QString apiKeyPlaceholder(bool keyStored);
    static QString formatFailureTrace(const QString &reason, const QUrl &url, const QString &model);

    static constexpr int kCircuitFailureThreshold = 3;
    static constexpr qint64 kCircuitOpenMs = 30000;
    static constexpr const char *kUnavailableReason = "custom_api_unavailable";

    enum class Circuit : std::uint8_t { Closed, Open, HalfOpen };

    static bool circuitAllow();
    static void circuitRecordSuccess();
    static void circuitRecordTransportFailure();
    static Circuit circuitState();

    struct HttpMetrics {
        quint64 requests = 0;
        quint64 successes = 0;
        quint64 failures = 0;
        quint64 rejected = 0;
        quint64 retries = 0;
        quint64 lastTraceId = 0;
        quint64 lastLatencyNs = 0;
    };
    static HttpMetrics httpMetrics();
    static quint64 nextTraceId();
    static void recordLatencyNs(quint64 ns);
    static void recordRetry();
    static void resetHttpGuardForTesting();
    static void setNowMsForTesting(qint64 ms);
};

#endif // GOAL_HTTP_JUDGE_H
