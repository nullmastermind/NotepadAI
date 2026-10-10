#ifndef PI_ACP_JUDGE_CONFIG_H
#define PI_ACP_JUDGE_CONFIG_H

#include "AcpAgentDefinition.h"

#include <QString>

// Pi loads MCP from getAgentDir()/mcp.json, redirected by PI_CODING_AGENT_DIR.
// pi-acp does not wire ACP mcpServers, so the Goal judge stages a private
// agent dir and points only that child at it.
class PiAcpJudgeConfig
{
public:
    static constexpr const char *kEnvAgentDir = "PI_CODING_AGENT_DIR";

    static bool isPiAcpAgent(const AcpAgentDefinition &agent);
    // ACP Environment overlay first, then process env, then ~/.pi/agent.
    static QString sourceAgentDir(const AcpAgentDefinition &agent);
    // Copy source → dest (skip sessions/), merge goal-verdict into mcp.json.
    static bool stageJudgeAgentDir(const QString &sourceDir, const QString &destDir,
                                   const QString &verdictCommand);
};

#endif
