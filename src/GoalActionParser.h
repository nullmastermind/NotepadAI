#ifndef GOAL_ACTION_PARSER_H
#define GOAL_ACTION_PARSER_H

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
    enum ParseError : std::uint8_t { NoError, NoActionTag, InvalidType, EmptyBody };

    static bool parse(const QString &response, GoalAction *out, ParseError *error = nullptr);

    // Complete reasons that stop the loop with the criterion still unmet.
    // Case-insensitive prefix match after trim, so a capitalized handback
    // is not treated as success.
    static bool isUnmetComplete(const QString &text);
    static bool isMaxIterationsUnmet(const QString &text);

    static QString correctionPrompt();
};

#endif // GOAL_ACTION_PARSER_H
