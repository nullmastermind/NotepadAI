#include "GoalHttpJudge.h"

#include "GoalPromptRenderer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

bool GoalHttpJudge::isCustomApiAgent(const QString &agentId)
{
    return agentId == QLatin1String(kAgentId);
}

QUrl GoalHttpJudge::messagesUrl(const QString &baseUrl)
{
    QString s = baseUrl.trimmed();
    while (s.endsWith(QLatin1Char('/')))
        s.chop(1);
    if (s.endsWith(QLatin1String("/v1/messages")))
        return QUrl(s);
    return QUrl(s + QLatin1String("/v1/messages"));
}

bool GoalHttpJudge::parseResponse(const QByteArray &json, GoalAction *out, ParseError *error)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        if (error) *error = InvalidJson;
        return false;
    }

    const QJsonArray content = doc.object().value(QLatin1String("content")).toArray();
    if (content.isEmpty()) {
        if (error) *error = EmptyContent;
        return false;
    }
    QString prose;
    for (const auto &v : content) {
        const QJsonObject block = v.toObject();
        const QString type = block.value(QLatin1String("type")).toString();
        if (type == QLatin1String("text")) {
            prose += block.value(QLatin1String("text")).toString();
            continue;
        }
        if (type != QLatin1String("tool_use"))
            continue;
        if (block.value(QLatin1String("name")).toString() != QLatin1String(kToolName))
            continue;

        const QJsonObject input = block.value(QLatin1String("input")).toObject();
        const QString status = input.value(QLatin1String("status")).toString().toLower();
        const QString text = input.value(QLatin1String("text")).toString().trimmed();

        if (status != QLatin1String("continue") && status != QLatin1String("complete")) {
            if (error) *error = InvalidStatus;
            return false;
        }
        if (text.isEmpty()) {
            if (error) *error = EmptyText;
            return false;
        }

        if (out) {
            out->type = (status == QLatin1String("complete")) ? GoalAction::Complete
                                                              : GoalAction::Continue;
            out->text = text;
        }
        if (error) *error = NoError;
        return true;
    }

    if (GoalActionParser::parse(prose, out, nullptr)) {
        if (error) *error = NoError;
        return true;
    }

    if (error) *error = NoToolCall;
    return false;
}

GoalHttpJudge::Decision GoalHttpJudge::decide(ParseError parseError, GoalAction::Type type,
                                              bool alreadyRetried)
{
    if (parseError == NoError)
        return (type == GoalAction::Complete) ? ApplyComplete : ApplyContinue;
    if (parseError == NoToolCall)
        return alreadyRetried ? AssumeAchieved : RetryOnce;
    return FailClosed;
}

QString GoalHttpJudge::correctionPrompt()
{
    return QStringLiteral(
        "Your previous reply did not call submit_goal_verdict. "
        "You MUST call submit_goal_verdict now with status \"continue\" "
        "(guidance for the coding agent) or status \"complete\" "
        "(the success criterion is met). Do not reply in prose.");
}

QString GoalHttpJudge::judgePrompt(const QString &goal,
                                   const QString &conversation,
                                   int iteration,
                                   int maxIterations,
                                   int criterionIndex,
                                   int totalCriteria,
                                   const QString &originalUserMessage,
                                   const QString &templateContent)
{
    static const QString kHttpTemplate = QStringLiteral(
        "You are an automated goal evaluator. A developer has started a goal-driven session "
        "with a coding agent. Your job is to observe the conversation and decide whether "
        "the success criterion has been met.\n\n"
        "You are evaluating criterion {{criterionIndex}} of {{totalCriteria}}.\n\n"
        "The developer's original message (the request that started this session):\n"
        "{{originalUserMessage}}\n\n"
        "Success criterion:\n{{goal}}\n\n"
        "Iteration: {{iteration}} of {{maxIterations}}\n\n"
        "Conversation since last evaluation:\n"
        "{{conversation}}\n\n"
        "You MUST call submit_goal_verdict on this turn. "
        "Use status \"continue\" with a first-person follow-up to the coding agent if the "
        "criterion is not yet met. Match the language and tone of the developer's original "
        "message. Use status \"complete\" with a brief reason only if the conversation "
        "contains clear evidence the criterion is satisfied. If you are not sure, continue "
        "and nudge toward verification. Do not answer in prose.\n");

    QString core = GoalPromptRenderer::renderJudgePrompt(
        templateContent.isEmpty() ? kHttpTemplate : templateContent,
        goal,
        conversation,
        iteration,
        maxIterations,
        criterionIndex,
        totalCriteria,
        originalUserMessage);
    if (!templateContent.isEmpty() && !core.contains(QLatin1String("submit_goal_verdict"))) {
        return core + QStringLiteral(
            "\nYou MUST call submit_goal_verdict on this turn with status "
            "\"continue\" or \"complete\" and a text field. Do not answer in prose.\n");
    }
    return core;
}

QByteArray GoalHttpJudge::buildRequestBody(const QString &model, const QString &userPrompt)
{
    QJsonObject statusProp{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("enum"), QJsonArray{QStringLiteral("continue"), QStringLiteral("complete")}},
        {QStringLiteral("description"),
         QStringLiteral("continue if the criterion is not yet met; complete if it is met.")},
    };
    QJsonObject textProp{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("description"),
         QStringLiteral("If continue: a first-person follow-up to the coding agent. "
                        "If complete: brief reason the criterion is met.")},
    };
    QJsonObject schema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("status"), statusProp},
            {QStringLiteral("text"), textProp},
        }},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("status"), QStringLiteral("text")}},
    };
    QJsonObject tool{
        {QStringLiteral("name"), QLatin1String(kToolName)},
        {QStringLiteral("description"),
         QStringLiteral("Submit your evaluation of the current success criterion. "
                        "Call this on your first reply.")},
        {QStringLiteral("input_schema"), schema},
    };

    QJsonObject userMsg{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), userPrompt},
    };

    QJsonObject body{
        {QStringLiteral("model"), model},
        {QStringLiteral("max_tokens"), 4096},
        {QStringLiteral("system"),
         QStringLiteral("You are an automated goal evaluator. "
                        "You MUST call submit_goal_verdict on your first reply. "
                        "Do not answer in prose. "
                        "If and only if you cannot invoke tools, emit exactly one XML tag: "
                        "<action type=\"continue\">guidance</action> or "
                        "<action type=\"complete\">reason</action>.")},
        {QStringLiteral("tools"), QJsonArray{tool}},
        {QStringLiteral("messages"), QJsonArray{userMsg}},
    };
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QString GoalHttpJudge::assistantText(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    QString out;
    const QJsonArray content = doc.object().value(QLatin1String("content")).toArray();
    for (const auto &v : content) {
        const QJsonObject block = v.toObject();
        if (block.value(QLatin1String("type")).toString() == QLatin1String("text"))
            out += block.value(QLatin1String("text")).toString();
    }
    return out;
}

QByteArray GoalHttpJudge::buildRetryBody(const QString &model,
                                        const QString &originalUserPrompt,
                                        const QString &assistantText,
                                        const QString &correctionPrompt)
{
    QJsonObject body = QJsonDocument::fromJson(buildRequestBody(model, originalUserPrompt)).object();
    QJsonArray messages = body.value(QLatin1String("messages")).toArray();
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"),
         assistantText.isEmpty() ? QStringLiteral("(no text)") : assistantText},
    });
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), correctionPrompt},
    });
    body.insert(QStringLiteral("messages"), messages);
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

bool GoalHttpJudge::isUsableEndpointUrl(const QString &baseUrl)
{
    if (baseUrl.trimmed().isEmpty())
        return false;
    const QUrl u = messagesUrl(baseUrl);
    if (!u.isValid() || u.host().isEmpty())
        return false;
    const QString scheme = u.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}

QString GoalHttpJudge::apiKeyPlaceholder(bool keyStored)
{
    return keyStored ? QStringLiteral("Stored in keychain")
                     : QStringLiteral("Paste API key");
}

QString GoalHttpJudge::formatFailureTrace(const QString &reason, const QUrl &url, const QString &model)
{
    return QStringLiteral("%1 | url=%2 model=%3")
        .arg(reason, url.toString(), model);
}
