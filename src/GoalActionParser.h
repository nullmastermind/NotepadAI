#ifndef GOAL_ACTION_PARSER_H
#define GOAL_ACTION_PARSER_H

#include <QString>

#include <cstdint>

struct GoalAction
{
    enum Type : std::uint8_t { Continue, Complete };
    Type type = Continue;
    QString text;
};

class GoalActionParser
{
public:
    enum ParseError : std::uint8_t { NoError, NoActionTag, InvalidType, EmptyBody };

    static bool parse(const QString &response, GoalAction *out, ParseError *error = nullptr);

    static QString correctionPrompt();
};

#endif // GOAL_ACTION_PARSER_H
