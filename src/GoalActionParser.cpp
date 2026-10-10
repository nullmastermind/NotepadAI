#include "GoalActionParser.h"

#include <QJsonObject>
#include <QRegularExpression>

bool GoalActionParser::isVerdictTool(const QString &name, const QString &title)
{
    auto hit = [](const QString &s) {
        if (s.isEmpty())
            return false;
        return s.compare(QLatin1String(kVerdictToolName), Qt::CaseInsensitive) == 0
            || s.endsWith(QLatin1String("__submit_goal_verdict"), Qt::CaseInsensitive)
            || s.endsWith(QLatin1String("_submit_goal_verdict"), Qt::CaseInsensitive);
    };
    return hit(name) || hit(title);
}

bool GoalActionParser::parseToolCall(const QString &name, const QString &title,
                                     const QJsonObject &rawInput, GoalAction *out,
                                     ParseError *error)
{
    if (!isVerdictTool(name, title)) {
        if (error) *error = NoToolCall;
        return false;
    }
    const QString status = rawInput.value(QLatin1String("status")).toString().trimmed().toLower();
    const QString text = rawInput.value(QLatin1String("text")).toString().trimmed();
    if (status != QLatin1String("continue") && status != QLatin1String("complete")
        && status != QLatin1String("restart")) {
        if (error) *error = InvalidType;
        return false;
    }
    if (text.isEmpty()) {
        if (error) *error = EmptyBody;
        return false;
    }
    if (out) {
        if (status == QLatin1String("complete"))
            out->type = GoalAction::Complete;
        else if (status == QLatin1String("restart"))
            out->type = GoalAction::Restart;
        else
            out->type = GoalAction::Continue;
        out->text = text;
    }
    if (error) *error = NoError;
    return true;
}

bool GoalActionParser::parse(const QString &response, GoalAction *out, ParseError *error)
{
    static const QRegularExpression re(
        QString::fromLatin1(R"RE(<action\s+type\s*=\s*"(continue|complete|restart)"\s*>([\s\S]*?)</action>)RE"),
        QRegularExpression::CaseInsensitiveOption);

    const auto match = re.match(response);
    if (!match.hasMatch()) {
        if (error) *error = NoActionTag;
        return false;
    }

    const QString type = match.captured(1).toLower();
    const QString body = match.captured(2).trimmed();

    if (type != QLatin1String("continue") && type != QLatin1String("complete")
        && type != QLatin1String("restart")) {
        if (error) *error = InvalidType;
        return false;
    }
    if (body.isEmpty()) {
        if (error) *error = EmptyBody;
        return false;
    }

    if (out) {
        if (type == QLatin1String("complete"))
            out->type = GoalAction::Complete;
        else if (type == QLatin1String("restart"))
            out->type = GoalAction::Restart;
        else
            out->type = GoalAction::Continue;
        out->text = body;
    }
    if (error) *error = NoError;
    return true;
}

QString GoalActionParser::displayText(const QString &raw)
{
    if (!raw.contains(QLatin1Char('<')))
        return raw;

    GoalAction parsed;
    if (parse(raw, &parsed))
        return parsed.text;

    const auto indexOfCi = [](const QString &hay, const QLatin1String needle, int from) {
        return hay.indexOf(needle, from, Qt::CaseInsensitive);
    };
    const int actionAt = indexOfCi(raw, QLatin1String("<action"), 0);
    if (actionAt < 0) {
        const QString trimmed = raw.trimmed();
        if (trimmed.startsWith(QLatin1Char('<'))) {
            const QLatin1String tag("<action");
            if (QString(tag).startsWith(trimmed, Qt::CaseInsensitive))
                return {};
        }
        return raw;
    }
    const int gt = raw.indexOf(QLatin1Char('>'), actionAt);
    if (gt < 0)
        return {};
    const int bodyStart = gt + 1;
    const int closeAt = indexOfCi(raw, QLatin1String("</action>"), bodyStart);
    const QString body = closeAt < 0
        ? raw.mid(bodyStart)
        : raw.mid(bodyStart, closeAt - bodyStart);
    return body.trimmed();
}

bool GoalActionParser::isMaxIterationsUnmet(const QString &text)
{
    return text.trimmed().startsWith(QLatin1String("max iterations reached"), Qt::CaseInsensitive);
}

bool GoalActionParser::isUnmetComplete(const QString &text)
{
    const QString trimmed = text.trimmed();
    return trimmed.startsWith(QLatin1String("need human-in-the-loop"), Qt::CaseInsensitive)
        || isMaxIterationsUnmet(trimmed);
}

QString GoalActionParser::correctionPrompt()
{
    return QStringLiteral(
        "Your previous reply did not call submit_goal_verdict. "
        "You MUST call submit_goal_verdict now with status \"continue\" "
        "(guidance for the coding agent), status \"complete\" "
        "(the success criterion is met), or status \"restart\" "
        "(the coding agent session must be restarted; text is the prompt "
        "to send after restart). Do not reply in prose. "
        "If and only if you cannot invoke tools, emit exactly one "
        "<action type=\"continue|complete|restart\">…</action> tag.");
}
