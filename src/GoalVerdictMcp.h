#ifndef GOAL_VERDICT_MCP_H
#define GOAL_VERDICT_MCP_H

#include <QByteArray>
#include <QJsonObject>

class GoalVerdictMcp
{
public:
    static constexpr const char *kCliFlag = "--goal-verdict-mcp";
    // Returned from tools/call. The judge must not implement the verdict itself.
    static constexpr const char *kStopGuide =
        "Verdict recorded. Stop immediately. Do not write another message, "
        "do not call another tool, and do not carry out the follow-up yourself. "
        "The host sends that text to the coding agent. End this turn now.";

    // Stdio MCP. Claude Code 2.1+ uses newline JSON; older clients use Content-Length.
    static int run();
    // One request or notification. Empty object = no response (notification).
    static QJsonObject handle(const QJsonObject &msg);
    // Encode handle() as newline JSON or Content-Length. Empty = no write.
    static QByteArray encodeReply(const QByteArray &jsonRequest, bool newlineFraming);
};

#endif
