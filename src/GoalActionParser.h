#ifndef GOAL_ACTION_PARSER_H
#define GOAL_ACTION_PARSER_H

#include <QJsonObject>
#include <QString>

#include <cstdint>

struct GoalAction
{
    enum Type : std::uint8_t { Continue, Complete, Restart };
    Type type = Continue;
    QString text;
};

class GoalActionParser
{
public:
    enum ParseError : std::uint8_t { NoError, NoActionTag, InvalidType, EmptyBody, NoToolCall };

    static constexpr const char *kVerdictToolName = "submit_goal_verdict";

    static bool parse(const QString &response, GoalAction *out, ParseError *error = nullptr);
    // ACP tool_call / Anthropic tool_use. Name may be prefixed (mcp__…__submit_goal_verdict).
    static bool isVerdictTool(const QString &name, const QString &title = {});
    static bool parseToolCall(const QString &name, const QString &title,
                              const QJsonObject &rawInput, GoalAction *out,
                              ParseError *error = nullptr);
    // Transcript text: drop <action type="...">…</action> so the bubble shows
    // the body. Incomplete opening tags (streaming) yield an empty string.
    static QString displayText(const QString &raw);

    // Complete reasons that stop the loop with the criterion still unmet.
    // Case-insensitive prefix match after trim, so a capitalized handback
    // is not treated as success.
    static bool isUnmetComplete(const QString &text);
    static bool isMaxIterationsUnmet(const QString &text);

    static QString correctionPrompt();
};

#endif // GOAL_ACTION_PARSER_H
