#include "GoalActionParser.h"

#include <QRegularExpression>

bool GoalActionParser::parse(const QString &response, GoalAction *out, ParseError *error)
{
    static const QRegularExpression re(
        QString::fromLatin1(R"RE(<action\s+type\s*=\s*"(continue|complete)"\s*>([\s\S]*?)</action>)RE"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = re.match(response);
    if (!match.hasMatch()) {
        if (error) *error = NoActionTag;
        return false;
    }

    const QString type = match.captured(1).toLower();
    const QString body = match.captured(2).trimmed();

    if (type != QLatin1String("continue") && type != QLatin1String("complete")) {
        if (error) *error = InvalidType;
        return false;
    }
    if (body.isEmpty()) {
        if (error) *error = EmptyBody;
        return false;
    }

    if (out) {
        out->type = (type == QLatin1String("complete")) ? GoalAction::Complete : GoalAction::Continue;
        out->text = body;
    }
    if (error) *error = NoError;
    return true;
}

QString GoalActionParser::correctionPrompt()
{
    return QStringLiteral(
        "Your previous response did not contain a valid <action> tag. "
        "You MUST respond with exactly one of:\n\n"
        "  <action type=\"continue\">guidance text</action>\n\n"
        "OR\n\n"
        "  <action type=\"complete\">reason text</action>\n\n"
        "Nothing else. Try again now.");
}
