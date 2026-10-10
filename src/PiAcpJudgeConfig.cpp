#include "PiAcpJudgeConfig.h"

#include "GoalVerdictMcp.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>

namespace {

QString tokenBase(const QString &s)
{
    QString name = QFileInfo(s).fileName();
    static const QStringList suffixes = {
        QStringLiteral(".cmd"), QStringLiteral(".exe"), QStringLiteral(".bat"),
    };
    for (const QString &suf : suffixes) {
        if (name.endsWith(suf, Qt::CaseInsensitive)) {
            name.chop(suf.size());
            break;
        }
    }
    return name;
}

bool copyTreeSkippingSessions(const QString &srcPath, const QString &dstPath)
{
    QDir src(srcPath);
    if (!src.exists())
        return true;
    if (!QDir().mkpath(dstPath))
        return false;
    const QFileInfoList entries = src.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries | QDir::Hidden | QDir::System);
    for (const QFileInfo &fi : entries) {
        if (fi.fileName() == QLatin1String("sessions"))
            continue;
        const QString dest = dstPath + QLatin1Char('/') + fi.fileName();
        if (fi.isDir()) {
            if (!copyTreeSkippingSessions(fi.absoluteFilePath(), dest))
                return false;
            continue;
        }
        QFile::remove(dest);
        if (!QFile::copy(fi.absoluteFilePath(), dest))
            return false;
    }
    return true;
}

bool mergeGoalVerdictMcp(const QString &mcpPath, const QString &verdictCommand)
{
    QJsonObject root;
    QFile in(mcpPath);
    if (in.exists() && in.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
    }
    QJsonObject servers = root.value(QStringLiteral("mcpServers")).toObject();
    QJsonObject server;
    server.insert(QStringLiteral("command"), verdictCommand);
    server.insert(QStringLiteral("args"),
                  QJsonArray{QLatin1String(GoalVerdictMcp::kCliFlag)});
    server.insert(QStringLiteral("env"), QJsonObject{});
    // Pi defaults MCP servers to codemode, which wraps the call in a script
    // and emits a parent card plus a nested id/1 card. Direct is one tool call.
    server.insert(QStringLiteral("exposure"), QStringLiteral("direct"));
    servers.insert(QStringLiteral("goal-verdict"), server);
    root.insert(QStringLiteral("mcpServers"), servers);

    QDir().mkpath(QFileInfo(mcpPath).absolutePath());
    QFile out(mcpPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

// Pi restores the model from the newest session file. The staged dir skips
// sessions/, so without this it falls back to the first authed model
// (often anthropic/claude-opus-4-8 + thinking) instead of the user's last model.
void restoreLastSessionModel(const QString &sourceDir, const QString &destDir)
{
    QFileInfo newest;
    QDirIterator it(sourceDir + QStringLiteral("/sessions"),
                    QStringList{QStringLiteral("*.jsonl")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (!newest.exists() || fi.lastModified() > newest.lastModified()
            || (fi.lastModified() == newest.lastModified()
                && fi.absoluteFilePath() > newest.absoluteFilePath()))
            newest = fi;
    }
    if (!newest.exists())
        return;

    QFile session(newest.absoluteFilePath());
    if (!session.open(QIODevice::ReadOnly))
        return;
    QString provider;
    QString modelId;
    QString thinking;
    while (!session.atEnd() && session.pos() < 256 * 1024) {
        const QJsonObject obj = QJsonDocument::fromJson(session.readLine()).object();
        const QString type = obj.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("model_change")) {
            provider = obj.value(QStringLiteral("provider")).toString();
            modelId = obj.value(QStringLiteral("modelId")).toString();
        } else if (type == QLatin1String("thinking_level_change")) {
            thinking = obj.value(QStringLiteral("thinkingLevel")).toString();
        }
        if (!provider.isEmpty() && !modelId.isEmpty() && !thinking.isEmpty())
            break;
    }
    if (provider.isEmpty() || modelId.isEmpty())
        return;

    const QString settingsPath = destDir + QStringLiteral("/settings.json");
    QJsonObject settings;
    QFile in(settingsPath);
    if (in.exists() && in.open(QIODevice::ReadOnly)) {
        settings = QJsonDocument::fromJson(in.readAll()).object();
        in.close();
    }
    settings.insert(QStringLiteral("defaultProvider"), provider);
    settings.insert(QStringLiteral("defaultModel"), modelId);
    if (!thinking.isEmpty())
        settings.insert(QStringLiteral("defaultThinkingLevel"), thinking);

    QFile out(settingsPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    out.write(QJsonDocument(settings).toJson(QJsonDocument::Indented));
}

} // namespace

bool PiAcpJudgeConfig::isPiAcpAgent(const AcpAgentDefinition &agent)
{
    const auto hit = [](const QString &token) {
        return tokenBase(token).compare(QLatin1String("pi-acp"), Qt::CaseInsensitive) == 0
            || token.compare(QLatin1String("pi-acp"), Qt::CaseInsensitive) == 0;
    };
    if (hit(agent.command))
        return true;
    for (const QString &token : parseShellLikeArgs(agent.command)) {
        if (hit(token))
            return true;
    }
    for (const QString &a : agent.args) {
        if (hit(a))
            return true;
        for (const QString &token : parseShellLikeArgs(a)) {
            if (hit(token))
                return true;
        }
    }
    return false;
}

QString PiAcpJudgeConfig::sourceAgentDir(const AcpAgentDefinition &agent)
{
    const QString fromAgent = agent.env.value(QLatin1String(kEnvAgentDir)).trimmed();
    if (!fromAgent.isEmpty())
        return QDir::cleanPath(fromAgent);
    const QString fromProc = QProcessEnvironment::systemEnvironment()
                                 .value(QLatin1String(kEnvAgentDir))
                                 .trimmed();
    if (!fromProc.isEmpty())
        return QDir::cleanPath(fromProc);
    return QDir::cleanPath(QDir::homePath() + QStringLiteral("/.pi/agent"));
}

bool PiAcpJudgeConfig::stageJudgeAgentDir(const QString &sourceDir, const QString &destDir,
                                          const QString &verdictCommand)
{
    if (destDir.isEmpty() || verdictCommand.isEmpty())
        return false;
    if (!copyTreeSkippingSessions(sourceDir, destDir))
        return false;
    restoreLastSessionModel(sourceDir, destDir);
    return mergeGoalVerdictMcp(destDir + QStringLiteral("/mcp.json"), verdictCommand);
}
