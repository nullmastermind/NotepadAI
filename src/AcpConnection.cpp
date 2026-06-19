/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "AcpConnection.h"

#include "IAcpProcessChannel.h"
#include "LocalAcpProcessChannel.h"
#include "remote/ExecutionContext.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>
#include <QTime>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

Q_LOGGING_CATEGORY(lcAcp, "notepadnext.acp")

namespace {

constexpr int kJsonRpcInvalidParams = -32602;
constexpr int kJsonRpcMethodNotFound = -32601;

QJsonObject makeRpcEnvelope(const QJsonValue &id, const QString &method, const QJsonValue &params)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    if (!id.isUndefined()) {
        obj.insert(QStringLiteral("id"), id);
    }
    if (!method.isEmpty()) {
        obj.insert(QStringLiteral("method"), method);
    }
    if (!params.isUndefined() && !params.isNull()) {
        obj.insert(QStringLiteral("params"), params);
    }
    return obj;
}

QString rpcErrorMessage(const QJsonValue &error)
{
    if (error.isObject()) {
        const QJsonObject o = error.toObject();
        return o.value(QStringLiteral("message")).toString();
    }
    if (error.isString()) {
        return error.toString();
    }
    return QString();
}

QList<AcpProtocol::AcpConfigOption> parseConfigOptions(const QJsonArray &arr)
{
    QList<AcpProtocol::AcpConfigOption> out;
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        AcpProtocol::AcpConfigOption co;
        co.id = o.value(QStringLiteral("id")).toString();
        co.name = o.value(QStringLiteral("name")).toString();
        co.description = o.value(QStringLiteral("description")).toString();
        co.category = o.value(QStringLiteral("category")).toString();
        // Spec: `currentValue`. Tolerate legacy flat `value`.
        co.currentValue = o.contains(QStringLiteral("currentValue"))
            ? o.value(QStringLiteral("currentValue"))
            : o.value(QStringLiteral("value"));
        // Spec: `options` (array of {value, name}). Tolerate legacy
        // `choices` (array of strings).
        if (o.contains(QStringLiteral("options"))) {
            for (const auto &cv : o.value(QStringLiteral("options")).toArray()) {
                const QJsonObject co2 = cv.toObject();
                AcpProtocol::AcpConfigOptionChoice ch;
                ch.value = co2.value(QStringLiteral("value")).toString();
                ch.name = co2.value(QStringLiteral("name")).toString();
                if (ch.name.isEmpty()) ch.name = ch.value;
                if (!ch.value.isEmpty()) co.options.append(ch);
            }
        } else {
            for (const auto &cv : o.value(QStringLiteral("choices")).toArray()) {
                AcpProtocol::AcpConfigOptionChoice ch;
                ch.value = cv.toString();
                ch.name = cv.toString();
                if (!ch.value.isEmpty()) co.options.append(ch);
            }
        }
        out.append(co);
    }
    return out;
}

} // namespace

AcpConnection::AcpConnection(QObject *parent)
    : QObject(parent)
{
}

AcpConnection::~AcpConnection()
{
    cancelAllPendingPermissions();
    // Tear down terminals.
    for (auto it = m_terminals.begin(); it != m_terminals.end(); ++it) {
        if (it.value().proc) {
            it.value().proc->kill();
            it.value().proc->deleteLater();
        }
    }
    m_terminals.clear();

    // Kill the agent transport while this object is still whole, so a final
    // finished()/error chunk can't fire into a half-destroyed connection. The
    // channel is a child QObject and is deleted with us; the local adapter's
    // own destructor still does the kill+brief-join (behavior unchanged).
    if (m_channel) {
        m_channel->disconnect(this);
        m_channel->kill();
    }
}

void AcpConnection::setRemoteSpawn(remote::ExecutionContext *ctx, RemoteChannelBuilder builder)
{
    m_execContext = ctx;
    m_remoteChannelBuilder = std::move(builder);
}

void AcpConnection::setAutoApprovePolicyProvider(std::function<QString()> provider)
{
    m_autoApproveProvider = std::move(provider);
}

void AcpConnection::spawn(const AcpAgentDefinition &agent, const QString &workingDirectory)
{
    m_agent = agent;
    m_workingDir = workingDirectory;

    // Working-dir identity for the path sandbox. For a remote workspace the path
    // lives on another machine, so a local QFileInfo::canonicalFilePath would
    // return empty / a wrong local path — keep it lexical instead (the remote
    // agent enforces its own fs boundaries; our sandbox guards the local fs path
    // which is irrelevant remotely).
    const bool remote = m_execContext && m_execContext->isRemote() && m_remoteChannelBuilder;
    if (remote) {
        m_workingDirCanonical = QDir::cleanPath(workingDirectory);
    } else {
        m_workingDirCanonical = QFileInfo(workingDirectory).canonicalFilePath();
        if (m_workingDirCanonical.isEmpty()) {
            m_workingDirCanonical = QDir::cleanPath(workingDirectory);
        }
    }

    if (remote) {
        spawnRemote(agent, workingDirectory);
    } else {
        spawnLocal(agent, workingDirectory);
    }
}

void AcpConnection::spawnLocal(const AcpAgentDefinition &agent, const QString &workingDirectory)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = agent.env.constBegin(); it != agent.env.constEnd(); ++it) {
        env.insert(it.key(), it.value());
    }

#ifdef Q_OS_WIN
    const bool isPosix = false;
    QString resolved = QStandardPaths::findExecutable(agent.command);
    if (resolved.isEmpty()) {
        resolved = agent.command;
    }
    const auto argv = AcpProtocol::buildSpawnArgv(agent.command, agent.args, isPosix, resolved);
#else
    const bool isPosix = true;
    const auto argv = AcpProtocol::buildSpawnArgv(agent.command, agent.args, isPosix);
#endif

    {
        QStringList envOverrides;
        envOverrides.reserve(agent.env.size());
        for (auto it = agent.env.constBegin(); it != agent.env.constEnd(); ++it) {
            envOverrides.append(it.key() + QStringLiteral("=") + it.value());
        }
        appendDebugLog(QStringLiteral("spawn: program=%1").arg(argv.program));
        if (!argv.nativeArgumentsLine.isEmpty()) {
            appendDebugLog(QStringLiteral("spawn: nativeArguments=%1").arg(argv.nativeArgumentsLine));
        } else {
            appendDebugLog(QStringLiteral("spawn: args=[%1]").arg(argv.arguments.join(QStringLiteral(", "))));
        }
        appendDebugLog(QStringLiteral("spawn: cwd=%1").arg(workingDirectory));
        if (!envOverrides.isEmpty()) {
            appendDebugLog(QStringLiteral("spawn: env-overrides=[%1]").arg(envOverrides.join(QStringLiteral(", "))));
        }
#ifdef Q_OS_WIN
        appendDebugLog(QStringLiteral("spawn: PATH-resolved-command=%1").arg(resolved));
#endif
    }

    adoptChannel(new LocalAcpProcessChannel(argv, env, workingDirectory, this));
    m_channel->start();
}

void AcpConnection::spawnRemote(const AcpAgentDefinition &agent, const QString &workingDirectory)
{
    appendDebugLog(QStringLiteral("spawn: remote host=%1 cwd=%2")
                       .arg(m_execContext->displayName(), workingDirectory));

    // Binary availability probe (D8/10.3): three-tier check over a one-shot exec.
    // 1. `command -v <cmd>` in the default remote shell (covers binaries in the
    //    system PATH — works even when bash is absent).
    // 2. `bash -lc` — login shell sources /etc/profile + ~/.bash_profile (or
    //    ~/.profile). Covers nvm/volta installs that went into profile files.
    // 3. `bash -ic` — interactive non-login shell sources ~/.bashrc directly.
    //    Covers the common case where nvm's install script wrote to ~/.bashrc
    //    (its first choice when $SHELL is bash) and .bashrc has a non-interactive
    //    early-return guard (`case $- in *i*) ;; *) return;; esac`) that blocks
    //    `bash -lc` from reaching the nvm init lines.
    // SSH exec channels run a non-interactive non-login shell by default (often
    // dash on Debian/Ubuntu) which doesn't source any user startup files.
    // The command name is single-quoted and passed as $0 to avoid nested quoting.
    // Stdout from the -ic probe is suppressed via fd3 save/restore so startup
    // noise doesn't pollute the probe output.
    const QString qcmd = QLatin1Char('\'')
        + QString(agent.command).replace(QLatin1Char('\''), QLatin1String("'\\''"))
        + QLatin1Char('\'');
    const QString probeCmd = QStringLiteral(
        "if command -v %1 >/dev/null 2>&1; then command -v %1; "
        "elif bash -lc 'command -v \"$0\"' %1 >/dev/null 2>&1; then "
            "bash -lc 'exec 1>&3 3>&-; command -v \"$0\"' %1 3>&1 1>/dev/null; "
        "elif bash -ic 'command -v \"$0\"' %1 </dev/null >/dev/null 2>&1; then "
            "bash -ic 'exec 0<&4 4<&- 1>&3 3>&-; command -v \"$0\"' %1 4<&0 3>&1 0</dev/null 1>/dev/null; "
        "else exit 127; fi").arg(qcmd);
    const QStringList probeArgv{QStringLiteral("sh"), QStringLiteral("-c"), probeCmd};
    appendDebugLog(QStringLiteral("spawn: probing remote binary: %1").arg(probeCmd));

    QPointer<AcpConnection> self(this);
    const QString host = m_execContext->displayName();
    const QString agentName = agent.name.isEmpty() ? agent.command : agent.name;

    m_execContext->exec(
        QString(), probeArgv, QByteArray(), /*timeoutMs=*/15000,
        [self, host, agentName, agent, workingDirectory](
            int exitCode, const QByteArray &out, const QByteArray &err) {
            if (!self) {
                return;
            }
            if (exitCode != 0) {
                self->appendDebugLog(
                    QStringLiteral("spawn: remote binary probe failed (exit=%1) err=%2")
                        .arg(exitCode)
                        .arg(QString::fromUtf8(err).trimmed()));
                const QString friendly =
                    QStringLiteral("Agent '%1' not found on remote host %2. "
                                   "Install it on the remote or open a local workspace.")
                        .arg(agentName, host);
                emit self->errorOccurred(AcpErrorClassifier::AcpErrorKind::SpawnFailed, friendly);
                return;
            }
            self->appendDebugLog(
                QStringLiteral("spawn: remote binary found: %1").arg(QString::fromUtf8(out).trimmed()));

            IAcpProcessChannel *channel = self->m_remoteChannelBuilder(
                self->m_execContext, agent, workingDirectory, self.data());
            if (!channel) {
                emit self->errorOccurred(
                    AcpErrorClassifier::AcpErrorKind::SpawnFailed,
                    QStringLiteral("Could not open an SSH exec channel for agent '%1' on %2.")
                        .arg(agentName, host));
                return;
            }
            self->adoptChannel(channel);
            self->m_channel->start();
        });
}

void AcpConnection::adoptChannel(IAcpProcessChannel *channel)
{
    m_channel = channel;
    connect(m_channel, &IAcpProcessChannel::readyReadStdout,
            this, &AcpConnection::handleStdoutReady);
    connect(m_channel, &IAcpProcessChannel::readyReadStderr,
            this, &AcpConnection::handleStderrReady);
    connect(m_channel, &IAcpProcessChannel::errorOccurred,
            this, &AcpConnection::handleProcessError);
    connect(m_channel, &IAcpProcessChannel::finished,
            this, &AcpConnection::handleProcessFinished);
    connect(m_channel, &IAcpProcessChannel::started, this, &AcpConnection::initializeHandshake);
    connect(m_channel, &IAcpProcessChannel::started, this,
            [this]() { appendDebugLog(QStringLiteral("process: started")); });
}

void AcpConnection::initializeHandshake()
{
    sendInitialize();
}

void AcpConnection::sendInitialize()
{
    QJsonObject client;
    client.insert(QStringLiteral("name"), QStringLiteral("NotepadADE"));
    client.insert(QStringLiteral("version"), QString::fromLatin1(APP_VERSION));

    QJsonObject fs;
    fs.insert(QStringLiteral("readTextFile"), true);
    fs.insert(QStringLiteral("writeTextFile"), true);

    QJsonObject caps;
    caps.insert(QStringLiteral("fs"), fs);
    caps.insert(QStringLiteral("terminal"), true);

    QJsonObject params;
    params.insert(QStringLiteral("protocolVersion"),
                  AcpProtocol::kProtocolVersion);
    params.insert(QStringLiteral("client"), client);
    params.insert(QStringLiteral("capabilities"), caps);

    sendRequest(AcpProtocol::kMethodInitialize, params,
                [this](const QJsonValue &result, const QJsonValue &error) {
                    if (!error.isUndefined() && !error.isNull()) {
                        emitClassifiedError(rpcErrorMessage(error));
                        return;
                    }
                    const QJsonObject r = result.toObject();
                    const QJsonObject agentObj = r.value(QStringLiteral("agentInfo")).toObject();
                    m_agentInfo.name = agentObj.value(QStringLiteral("name")).toString();
                    m_agentInfo.title = agentObj.value(QStringLiteral("title")).toString();
                    m_agentInfo.version = agentObj.value(QStringLiteral("version")).toString();
                    const QJsonObject caps = r.value(QStringLiteral("capabilities")).toObject();
                    const QJsonObject fs = caps.value(QStringLiteral("fs")).toObject();
                    m_capabilities.fsReadTextFile = fs.value(QStringLiteral("readTextFile")).toBool();
                    m_capabilities.fsWriteTextFile = fs.value(QStringLiteral("writeTextFile")).toBool();
                    m_capabilities.terminal = caps.value(QStringLiteral("terminal")).toBool();
                    m_capabilities.sessionLoad = caps.value(QStringLiteral("sessionLoad")).toBool();
                    sendNewSession();
                });
}

void AcpConnection::sendNewSession()
{
    QJsonObject params;
    params.insert(QStringLiteral("cwd"), m_workingDir);
    // ACP requires `mcpServers` as an array — strict (Zod) agents reject the
    // request with "expected array, received undefined" if the key is absent.
    // We don't configure any MCP servers ourselves; send an empty array.
    params.insert(QStringLiteral("mcpServers"), QJsonArray{});

    sendRequest(AcpProtocol::kMethodSessionNew, params,
                [this](const QJsonValue &result, const QJsonValue &error) {
                    if (!error.isUndefined() && !error.isNull()) {
                        emitClassifiedError(rpcErrorMessage(error));
                        return;
                    }
                    const QJsonObject r = result.toObject();
                    m_sessionId = r.value(QStringLiteral("sessionId")).toString();

                    m_availableCommands.clear();
                    for (const auto &v : r.value(QStringLiteral("availableCommands")).toArray()) {
                        AcpProtocol::AcpCommandInfo cmd;
                        if (v.isObject()) {
                            const QJsonObject o = v.toObject();
                            cmd.name = o.value(QStringLiteral("name")).toString();
                            cmd.description = o.value(QStringLiteral("description")).toString();
                            const QJsonValue inp = o.value(QStringLiteral("input"));
                            if (inp.isObject())
                                cmd.inputHint = inp.toObject().value(QStringLiteral("hint")).toString();
                        } else {
                            cmd.name = v.toString();
                        }
                        if (!cmd.name.isEmpty())
                            m_availableCommands.append(cmd);
                    }

                    // ACP V1: modes/models are nested objects; older agents
                    // (pre-spec) emitted them flat at the top level. Accept
                    // either so we don't regress against existing agents.
                    const QJsonObject modesObj = r.value(QStringLiteral("modes")).toObject();
                    const QJsonArray modesArr = modesObj.contains(QStringLiteral("availableModes"))
                        ? modesObj.value(QStringLiteral("availableModes")).toArray()
                        : r.value(QStringLiteral("availableModes")).toArray();
                    m_modes.clear();
                    for (const auto &v : modesArr) {
                        const QJsonObject o = v.toObject();
                        AcpProtocol::AcpModeInfo m;
                        m.id = o.value(QStringLiteral("id")).toString();
                        m.name = o.value(QStringLiteral("name")).toString();
                        m.description = o.value(QStringLiteral("description")).toString();
                        m_modes.append(m);
                    }
                    m_currentMode = modesObj.contains(QStringLiteral("currentModeId"))
                        ? modesObj.value(QStringLiteral("currentModeId")).toString()
                        : r.value(QStringLiteral("currentMode")).toString();

                    const QJsonObject modelsObj = r.value(QStringLiteral("models")).toObject();
                    const QJsonArray modelsArr = modelsObj.contains(QStringLiteral("availableModels"))
                        ? modelsObj.value(QStringLiteral("availableModels")).toArray()
                        : r.value(QStringLiteral("availableModels")).toArray();
                    m_models.clear();
                    for (const auto &v : modelsArr) {
                        const QJsonObject o = v.toObject();
                        AcpProtocol::AcpModelInfo m;
                        // Spec calls the field modelId; tolerate plain id.
                        m.id = o.value(QStringLiteral("modelId")).toString();
                        if (m.id.isEmpty()) m.id = o.value(QStringLiteral("id")).toString();
                        m.name = o.value(QStringLiteral("name")).toString();
                        m.description = o.value(QStringLiteral("description")).toString();
                        m_models.append(m);
                    }
                    m_currentModel = modelsObj.contains(QStringLiteral("currentModelId"))
                        ? modelsObj.value(QStringLiteral("currentModelId")).toString()
                        : r.value(QStringLiteral("currentModel")).toString();

                    m_configOptions = parseConfigOptions(r.value(QStringLiteral("configOptions")).toArray());

                    // Emit BEFORE flushing any prompts queued during session/new.
                    // The view applies the user's saved per-agent preferences
                    // (model/mode/effort) on this signal, dispatching the
                    // corresponding set_* requests synchronously. Flushing the
                    // pending prompts afterwards keeps those config requests
                    // ahead of the prompt on the wire, so the deferred prompt
                    // is processed with the user's saved selection rather than
                    // the agent's session defaults.
                    emit initialized(m_agentInfo, m_availableCommands,
                                     m_modes, m_currentMode,
                                     m_models, m_currentModel,
                                     m_configOptions);

                    // Flush any prompts the user queued while session/new was
                    // still in flight. Move the queue aside first so a
                    // re-entrant sendPrompt (shouldn't happen, but be safe)
                    // doesn't infinite-loop.
                    if (!m_pendingPrompts.isEmpty()) {
                        const auto pending = std::move(m_pendingPrompts);
                        m_pendingPrompts.clear();
                        appendDebugLog(QStringLiteral("session/new: flushing %1 deferred prompt(s)")
                                           .arg(pending.size()));
                        for (const auto &p : pending) {
                            sendPrompt(p.text, p.images);
                        }
                    }
                });
}

// ---------- Outbound -----------------------------------------------------

int AcpConnection::sendRequest(const char *method, const QJsonValue &params, const Callback &cb)
{
    if (!m_channel || !m_channel->isRunning()) {
        appendDebugLog(QStringLiteral("send-request: dropped (process not running) method=%1")
                           .arg(QString::fromLatin1(method)));
        if (cb) {
            QJsonObject err;
            err.insert(QStringLiteral("message"), QStringLiteral("Process not running"));
            cb(QJsonValue(), err);
        }
        return -1;
    }
    const int id = m_nextRequestId++;
    m_pending.insert(id, cb);
    m_pendingMethod.insert(id, QString::fromLatin1(method));

    QJsonObject obj = makeRpcEnvelope(QJsonValue(id), QString::fromLatin1(method), params);
    QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    appendDebugFrameLog("→", bytes);
    m_channel->write(bytes);
    return id;
}

void AcpConnection::sendNotification(const char *method, const QJsonValue &params)
{
    if (!m_channel || !m_channel->isRunning()) {
        return;
    }
    QJsonObject obj = makeRpcEnvelope(QJsonValue(QJsonValue::Undefined), QString::fromLatin1(method), params);
    QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    appendDebugFrameLog("→", bytes);
    m_channel->write(bytes);
}

void AcpConnection::sendResponse(const QJsonValue &id, const QJsonValue &result)
{
    if (!m_channel || !m_channel->isRunning()) {
        return;
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("result"), result);
    QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    appendDebugFrameLog("→", bytes);
    m_channel->write(bytes);
}

void AcpConnection::sendErrorResponse(const QJsonValue &id, int code, const QString &message)
{
    if (!m_channel || !m_channel->isRunning()) {
        return;
    }
    QJsonObject err;
    err.insert(QStringLiteral("code"), code);
    err.insert(QStringLiteral("message"), message);
    QJsonObject obj;
    obj.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("error"), err);
    QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    appendDebugFrameLog("→", bytes);
    m_channel->write(bytes);
}

void AcpConnection::sendPrompt(const QString &text, const QList<QPair<QByteArray, QString>> &images)
{
    // The user may submit before `session/new` has returned; in that case
    // m_sessionId is still empty and sending now would produce a "Session not
    // found" error. Queue and flush from the session/new callback instead.
    if (m_sessionId.isEmpty()) {
        m_pendingPrompts.append({text, images});
        appendDebugLog(QStringLiteral("sendPrompt: deferred — session not yet established (queue=%1)")
                           .arg(m_pendingPrompts.size()));
        return;
    }

    QJsonArray content;
    {
        AcpProtocol::AcpContentBlock t;
        t.kind = AcpProtocol::AcpContentBlock::Kind::Text;
        t.text = text;
        content.append(AcpProtocol::contentBlockToJson(t));
    }
    for (const auto &img : images) {
        AcpProtocol::AcpContentBlock b;
        b.kind = AcpProtocol::AcpContentBlock::Kind::Image;
        b.imageData = img.first;
        b.mimeType = img.second;
        content.append(AcpProtocol::contentBlockToJson(b));
    }
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), m_sessionId);
    // Per the ACP schema PromptRequest requires `prompt` (array of
    // ContentBlock), not `content`.
    params.insert(QStringLiteral("prompt"), content);

    beginPrompt();
    sendRequest(AcpProtocol::kMethodSessionPrompt, params,
                [this](const QJsonValue &result, const QJsonValue &error) {
                    if (!error.isUndefined() && !error.isNull()) {
                        emit requestFailed(rpcErrorMessage(error));
                    } else if (result.isObject()) {
                        AcpProtocol::AcpUsage usage;
                        const QJsonObject usageObj =
                            result.toObject().value(QStringLiteral("usage")).toObject();
                        if (!usageObj.isEmpty()) {
                            if (usageObj.contains(QStringLiteral("inputTokens"))) {
                                usage.inputTokens = usageObj.value(QStringLiteral("inputTokens")).toInt();
                            }
                            if (usageObj.contains(QStringLiteral("outputTokens"))) {
                                usage.outputTokens = usageObj.value(QStringLiteral("outputTokens")).toInt();
                            }
                            if (usageObj.contains(QStringLiteral("totalTokens"))) {
                                usage.totalTokens = usageObj.value(QStringLiteral("totalTokens")).toInt();
                            }
                            emit usageUpdated(usage);
                        }
                        const bool hasUsage = usage.inputTokens.value_or(0) > 0
                            || usage.outputTokens.value_or(0) > 0
                            || usage.totalTokens.value_or(0) > 0;
                        if (!m_promptProducedOutput && !hasUsage) {
                            emit requestFailed(QStringLiteral("The agent ended the turn without a response. Open Debug for details."));
                        }
                    }
                    // session/prompt response is the authoritative end-of-turn
                    // — regardless of whether the agent also sent a
                    // session/update prompt_end notification.
                    endPrompt();
                });
}

void AcpConnection::cancelPrompt()
{
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), m_sessionId);
    sendNotification(AcpProtocol::kMethodSessionCancel, params);
}

void AcpConnection::setMode(const QString &id)
{
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), m_sessionId);
    params.insert(QStringLiteral("modeId"), id);
    sendRequest(AcpProtocol::kMethodSessionSetMode, params,
                [this](const QJsonValue &, const QJsonValue &error) {
                    if (!error.isUndefined() && !error.isNull()) {
                        emit requestFailed(rpcErrorMessage(error));
                    }
                });
}

void AcpConnection::setModel(const QString &id)
{
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), m_sessionId);
    params.insert(QStringLiteral("modelId"), id);
    sendRequest(AcpProtocol::kMethodSessionSetModel, params,
                [this](const QJsonValue &, const QJsonValue &error) {
                    if (!error.isUndefined() && !error.isNull()) {
                        emit requestFailed(rpcErrorMessage(error));
                    }
                });
}

void AcpConnection::setConfigOption(const QString &id, const QJsonValue &value)
{
    QJsonObject params;
    params.insert(QStringLiteral("sessionId"), m_sessionId);
    // Spec: configId. (Old field was `optionId`; agents validating against
    // the spec reject the request as invalid params.)
    params.insert(QStringLiteral("configId"), id);
    params.insert(QStringLiteral("value"), value);
    sendRequest(AcpProtocol::kMethodSessionSetConfig, params,
                [this](const QJsonValue &result, const QJsonValue &error) {
                    if (!error.isUndefined() && !error.isNull()) {
                        emit requestFailed(rpcErrorMessage(error));
                        return;
                    }
                    // Agents (e.g. Claude Code) return the refreshed
                    // configOptions inside the result rather than firing a
                    // separate session/update notification. Apply the result
                    // payload through the same path as the notification so the
                    // model and UI see the new currentValue.
                    const QJsonObject obj = result.toObject();
                    const QJsonValue opts = obj.value(QStringLiteral("configOptions"));
                    if (opts.isArray()) {
                        m_configOptions = parseConfigOptions(opts.toArray());
                        emit configOptionsUpdated(m_configOptions);
                    }
                });
}

// ---------- Stdio dispatch ----------------------------------------------

void AcpConnection::handleStdoutReady(const QByteArray &chunk)
{
    m_stdoutBuffer.append(chunk);
    const QStringList frames = AcpProtocol::acpExtractFrames(m_stdoutBuffer);
    for (const QString &frame : frames) {
        appendDebugFrameLog("←", frame.toUtf8());
        dispatchFrame(frame);
    }
}

void AcpConnection::handleStderrReady(const QByteArray &chunk)
{
    m_stderrBuffer.append(chunk);
    // Emit per-line via qWarning, line-buffered.
    while (true) {
        const int nl = m_stderrBuffer.indexOf('\n');
        if (nl < 0) {
            break;
        }
        QByteArray line = m_stderrBuffer.left(nl);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        m_stderrBuffer.remove(0, nl + 1);
        const QString text = QString::fromUtf8(line);
        appendDebugLog(QStringLiteral("stderr: %1").arg(text));
        qWarning("[acp:%s] %s",
                 qUtf8Printable(m_sessionId.isEmpty() ? QStringLiteral("?") : m_sessionId),
                 qUtf8Printable(text));
    }
}

void AcpConnection::dispatchFrame(const QString &frame)
{
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(frame.toUtf8(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        qCDebug(lcAcp) << "dropping malformed frame:" << frame;
        return;
    }
    const QJsonObject obj = doc.object();
    const QJsonValue idV = obj.value(QStringLiteral("id"));
    const bool hasId = !idV.isUndefined() && !idV.isNull();
    const QJsonValue methodV = obj.value(QStringLiteral("method"));
    const bool hasMethod = !methodV.isUndefined() && methodV.isString();

    if (hasId && hasMethod) {
        // Inbound request from agent.
        handleInboundRequest(idV, methodV.toString(), obj.value(QStringLiteral("params")).toObject());
        return;
    }
    if (hasId && !hasMethod) {
        // Response to one of ours.
        const int id = idV.toInt();
        auto cb = m_pending.take(id);
        m_pendingMethod.remove(id);
        if (cb) {
            cb(obj.value(QStringLiteral("result")), obj.value(QStringLiteral("error")));
        }
        return;
    }
    if (!hasId && hasMethod) {
        handleInboundNotification(methodV.toString(), obj.value(QStringLiteral("params")).toObject());
        return;
    }
    qCDebug(lcAcp) << "dropping frame with neither id nor method:" << frame;
}

void AcpConnection::handleInboundRequest(const QJsonValue &id, const QString &method, const QJsonObject &params)
{
    if (method == QLatin1String(AcpProtocol::kMethodFsReadTextFile)) {
        handleFsReadTextFile(id, params);
    } else if (method == QLatin1String(AcpProtocol::kMethodFsWriteTextFile)) {
        handleFsWriteTextFile(id, params);
    } else if (method == QLatin1String(AcpProtocol::kMethodTerminalCreate)) {
        handleTerminalCreate(id, params);
    } else if (method == QLatin1String(AcpProtocol::kMethodTerminalOutput)) {
        handleTerminalOutput(id, params);
    } else if (method == QLatin1String(AcpProtocol::kMethodTerminalWaitForExit)) {
        handleTerminalWaitForExit(id, params);
    } else if (method == QLatin1String(AcpProtocol::kMethodTerminalKill)) {
        handleTerminalKill(id, params);
    } else if (method == QLatin1String(AcpProtocol::kMethodTerminalRelease)) {
        handleTerminalRelease(id, params);
    } else if (AcpProtocol::isPermissionRequestMethod(method)) {
        handleRequestPermission(id, params,
                                method == QLatin1String(AcpProtocol::kMethodSessionRequestPermission));
    } else {
        // ext_method passthrough (and any other unknown method).
        handleExtMethod(id, params);
    }
}

void AcpConnection::handleInboundNotification(const QString &method, const QJsonObject &params)
{
    if (method != QLatin1String(AcpProtocol::kMethodSessionUpdate)) {
        qCDebug(lcAcp) << "unknown notification:" << method;
        return;
    }
    const QJsonObject update = params.value(QStringLiteral("update")).toObject();
    const QString kind = update.value(QStringLiteral("sessionUpdate")).toString();

    if (kind == QLatin1String("agent_message_chunk")) {
        m_promptProducedOutput = true;
        const QString text = update.value(QStringLiteral("content"))
                                 .toObject()
                                 .value(QStringLiteral("text"))
                                 .toString();
        emit messageChunk(text);
    } else if (kind == QLatin1String("agent_thought_chunk")) {
        m_promptProducedOutput = true;
        const QString text = update.value(QStringLiteral("content"))
                                 .toObject()
                                 .value(QStringLiteral("text"))
                                 .toString();
        emit thoughtChunk(text);
    } else if (kind == QLatin1String("tool_call")) {
        m_promptProducedOutput = true;
        AcpProtocol::AcpToolCall tc;
        tc.id = update.value(QStringLiteral("toolCallId")).toString();
        tc.title = update.value(QStringLiteral("title")).toString();
        tc.status = update.value(QStringLiteral("status")).toString();
        tc.content = update.value(QStringLiteral("content")).toArray();
        tc.rawInput = update.value(QStringLiteral("rawInput")).toObject();
        tc.groupId = update.value(QStringLiteral("groupId")).toInt(0);
        emit toolCallReceived(tc);
    } else if (kind == QLatin1String("tool_call_update")) {
        m_promptProducedOutput = true;
        AcpProtocol::AcpToolCallUpdate u;
        u.id = update.value(QStringLiteral("toolCallId")).toString();
        const QJsonValue st = update.value(QStringLiteral("status"));
        if (st.isString()) {
            u.status = st.toString();
        }
        const QJsonValue c = update.value(QStringLiteral("content"));
        if (c.isArray()) {
            u.content = c.toArray();
        }
        const QJsonValue ri = update.value(QStringLiteral("rawInput"));
        if (ri.isObject()) {
            u.rawInput = ri.toObject();
        }
        emit toolCallUpdated(u);
    } else if (kind == QLatin1String("plan")) {
        m_promptProducedOutput = true;
        QList<AcpProtocol::AcpPlanEntry> plan;
        for (const auto &v : update.value(QStringLiteral("entries")).toArray()) {
            const QJsonObject o = v.toObject();
            AcpProtocol::AcpPlanEntry e;
            // Agents use varying field names for the entry label.
            // Fallback chain mirrors spec-ade: title > name > description > content > subject > text
            QString label = o.value(QStringLiteral("title")).toString();
            if (label.isEmpty()) label = o.value(QStringLiteral("name")).toString();
            if (label.isEmpty()) label = o.value(QStringLiteral("description")).toString();
            if (label.isEmpty()) label = o.value(QStringLiteral("content")).toString();
            if (label.isEmpty()) label = o.value(QStringLiteral("subject")).toString();
            if (label.isEmpty()) label = o.value(QStringLiteral("text")).toString();
            e.text = label;
            e.status = o.value(QStringLiteral("status")).toString();
            plan.append(e);
        }
        emit planReceived(plan);
    } else if (kind == QLatin1String("available_commands_update")) {
        QList<AcpProtocol::AcpCommandInfo> cmds;
        for (const auto &v : update.value(QStringLiteral("availableCommands")).toArray()) {
            AcpProtocol::AcpCommandInfo cmd;
            if (v.isObject()) {
                const QJsonObject o = v.toObject();
                cmd.name = o.value(QStringLiteral("name")).toString();
                cmd.description = o.value(QStringLiteral("description")).toString();
                const QJsonValue inp = o.value(QStringLiteral("input"));
                if (inp.isObject())
                    cmd.inputHint = inp.toObject().value(QStringLiteral("hint")).toString();
            } else {
                cmd.name = v.toString();
            }
            if (!cmd.name.isEmpty())
                cmds.append(cmd);
        }
        emit availableCommandsUpdated(cmds);
    } else if (kind == QLatin1String("current_mode_update")) {
        // Spec field: currentModeId. Pre-spec agents emitted currentMode.
        const QString id = update.contains(QStringLiteral("currentModeId"))
            ? update.value(QStringLiteral("currentModeId")).toString()
            : update.value(QStringLiteral("currentMode")).toString();
        m_currentMode = id;
        emit currentModeChanged(id);
    } else if (kind == QLatin1String("config_option_update")) {
        m_configOptions = parseConfigOptions(update.value(QStringLiteral("configOptions")).toArray());
        emit configOptionsUpdated(m_configOptions);
    } else if (kind == QLatin1String("session_info_update")) {
        // const QJsonObject usageObj = update.value(QStringLiteral("usage")).toObject();
        // AcpProtocol::AcpUsage usage;
        // if (usageObj.contains(QStringLiteral("inputTokens"))) {
        //     usage.inputTokens = usageObj.value(QStringLiteral("inputTokens")).toInt();
        // }
        // if (usageObj.contains(QStringLiteral("outputTokens"))) {
        //     usage.outputTokens = usageObj.value(QStringLiteral("outputTokens")).toInt();
        // }
        // if (usageObj.contains(QStringLiteral("totalTokens"))) {
        //     usage.totalTokens = usageObj.value(QStringLiteral("totalTokens")).toInt();
        // }
        // if (usageObj.contains(QStringLiteral("maxTokens"))) {
        //     usage.maxTokens = usageObj.value(QStringLiteral("maxTokens")).toInt();
        // }
        // emit usageUpdated(usage);
    } else if (kind == QLatin1String("usage_update")) {
        // Flat shape: { sessionUpdate: "usage_update", used: N, size: N, cost: {...} }
        // The agent treats this as the authoritative running total, so it
        // replaces the snapshot (see AcpConnection::usageReplaced docs).
        AcpProtocol::AcpUsage usage;
        if (update.contains(QStringLiteral("used"))) {
            usage.totalTokens = update.value(QStringLiteral("used")).toInt();
        }
        if (update.contains(QStringLiteral("size"))) {
            usage.maxTokens = update.value(QStringLiteral("size")).toInt();
        }
        const QJsonValue costVal = update.value(QStringLiteral("cost"));
        if (costVal.isObject()) {
            const QJsonObject costObj = costVal.toObject();
            const QJsonValue amount = costObj.value(QStringLiteral("amount"));
            if (amount.isDouble()) {
                usage.costAmount = amount.toDouble();
            }
            const QJsonValue currency = costObj.value(QStringLiteral("currency"));
            if (currency.isString()) {
                usage.costCurrency = currency.toString();
            }
        }
        emit usageReplaced(usage);
    } else if (kind == QLatin1String("prompt_start")) {
        beginPrompt();
    } else if (kind == QLatin1String("prompt_end")) {
        endPrompt();
    } else {
        qCDebug(lcAcp) << "unknown session/update kind:" << kind;
    }
}

// ---------- Path sandbox -------------------------------------------------

bool AcpConnection::sandboxRead(const QString &rawPath, QString *canonicalOut, QString *errorOut) const
{
    const QString canonical = QFileInfo(rawPath).canonicalFilePath();
    if (canonical.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Path outside working directory: ") + rawPath;
        }
        return false;
    }
    if (!AcpProtocol::pathIsInsideWorkingDir(canonical, m_workingDirCanonical)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Path outside working directory: ") + rawPath;
        }
        return false;
    }
    if (canonicalOut) {
        *canonicalOut = canonical;
    }
    return true;
}

bool AcpConnection::sandboxWrite(const QString &rawPath, QString *canonicalOut, QString *errorOut) const
{
    QFileInfo fi(rawPath);
    QString canonicalDir = fi.dir().canonicalPath();
    if (canonicalDir.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Path outside working directory: ") + rawPath;
        }
        return false;
    }
    const QString canonical = QDir::cleanPath(canonicalDir + QLatin1Char('/') + fi.fileName());
    if (!AcpProtocol::pathIsInsideWorkingDir(canonical, m_workingDirCanonical)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Path outside working directory: ") + rawPath;
        }
        return false;
    }
    if (canonicalOut) {
        *canonicalOut = canonical;
    }
    return true;
}

// ---------- fs/* handlers ------------------------------------------------

void AcpConnection::handleFsReadTextFile(const QJsonValue &id, const QJsonObject &params)
{
    const QString rawPath = params.value(QStringLiteral("path")).toString();
    QString canonical;
    QString err;
    if (!sandboxRead(rawPath, &canonical, &err)) {
        sendErrorResponse(id, kJsonRpcInvalidParams, err);
        return;
    }
    QFile f(canonical);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        sendErrorResponse(id, kJsonRpcInvalidParams, f.errorString());
        return;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    QString contents;
    const QJsonValue lineV = params.value(QStringLiteral("line"));
    const QJsonValue limitV = params.value(QStringLiteral("limit"));
    if (lineV.isDouble() || limitV.isDouble()) {
        int startLine = lineV.isDouble() ? lineV.toInt() : 1;
        if (startLine < 1) {
            startLine = 1;
        }
        const int limit = limitV.isDouble() ? limitV.toInt() : -1;
        int current = 1;
        int produced = 0;
        QString line;
        while (in.readLineInto(&line)) {
            if (current >= startLine) {
                if (!contents.isEmpty()) {
                    contents.append(QLatin1Char('\n'));
                }
                contents.append(line);
                ++produced;
                if (limit > 0 && produced >= limit) {
                    break;
                }
            }
            ++current;
        }
    } else {
        contents = in.readAll();
    }
    QJsonObject result;
    result.insert(QStringLiteral("content"), contents);
    sendResponse(id, result);
}

void AcpConnection::handleFsWriteTextFile(const QJsonValue &id, const QJsonObject &params)
{
    const QString rawPath = params.value(QStringLiteral("path")).toString();
    QString canonical;
    QString err;
    if (!sandboxWrite(rawPath, &canonical, &err)) {
        sendErrorResponse(id, kJsonRpcInvalidParams, err);
        return;
    }
    const QString content = params.value(QStringLiteral("content")).toString();
    QFile f(canonical);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sendErrorResponse(id, kJsonRpcInvalidParams, f.errorString());
        return;
    }
    const QByteArray utf8 = content.toUtf8();
    if (f.write(utf8) != utf8.size()) {
        sendErrorResponse(id, kJsonRpcInvalidParams, f.errorString());
        return;
    }
    f.close();
    sendResponse(id, QJsonObject{});
    emit fileWrittenOnDisk(canonical);
}

// ---------- Terminal handlers --------------------------------------------

void AcpConnection::appendTerminalOutput(TerminalState &state, const QByteArray &chunk)
{
    state.buffer.append(chunk);
    if (state.buffer.size() > state.outputByteLimit) {
        const int overflow = state.buffer.size() - state.outputByteLimit;
        state.buffer.remove(0, overflow);
        state.truncated = true;
        // Trim incomplete leading multi-byte UTF-8 continuation bytes.
        while (!state.buffer.isEmpty()) {
            const unsigned char c = static_cast<unsigned char>(state.buffer.at(0));
            if ((c & 0xC0) == 0x80) {
                state.buffer.remove(0, 1);
            } else {
                break;
            }
        }
    }
}

void AcpConnection::handleTerminalCreate(const QJsonValue &id, const QJsonObject &params)
{
    const QString cmd = params.value(QStringLiteral("command")).toString();
    QStringList args;
    for (const auto &v : params.value(QStringLiteral("args")).toArray()) {
        args.append(v.toString());
    }
    QString cwd = params.value(QStringLiteral("cwd")).toString();
    if (cwd.isEmpty()) {
        cwd = m_workingDir;
    } else {
        QString canonical;
        QString err;
        if (!sandboxWrite(cwd, &canonical, &err)) {
            sendErrorResponse(id, kJsonRpcInvalidParams, err);
            return;
        }
        cwd = canonical;
    }

    const QString terminalId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    TerminalState state;
    state.proc = new QProcess(this);
    state.proc->setProcessChannelMode(QProcess::MergedChannels);
    state.proc->setWorkingDirectory(cwd);
    state.proc->setProgram(cmd);
    state.proc->setArguments(args);

    QProcess *proc = state.proc;
    QPointer<AcpConnection> self(this);

    connect(proc, &QProcess::readyReadStandardOutput, this, [self, termIdCopy = terminalId]() {
        if (!self) {
            return;
        }
        auto it = self->m_terminals.find(termIdCopy);
        if (it == self->m_terminals.end() || !it.value().proc) {
            return;
        }
        self->appendTerminalOutput(it.value(), it.value().proc->readAllStandardOutput());
    });
    connect(proc,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            [self, termIdCopy = terminalId](int exitCode, QProcess::ExitStatus status) {
                if (!self) {
                    return;
                }
                auto it = self->m_terminals.find(termIdCopy);
                if (it == self->m_terminals.end()) {
                    return;
                }
                const QString s = (status == QProcess::NormalExit)
                                      ? QStringLiteral("exited")
                                      : QStringLiteral("crashed");
                it.value().exitStatus = QPair<int, QString>(exitCode, s);
                // Flush waiters.
                for (auto &w : it.value().waiters) {
                    w();
                }
                it.value().waiters.clear();
            });

    m_terminals.insert(terminalId, state);
    proc->start();

    QJsonObject result;
    result.insert(QStringLiteral("terminalId"), terminalId);
    sendResponse(id, result);
}

void AcpConnection::handleTerminalOutput(const QJsonValue &id, const QJsonObject &params)
{
    const QString terminalId = params.value(QStringLiteral("terminalId")).toString();
    auto it = m_terminals.find(terminalId);
    if (it == m_terminals.end()) {
        sendErrorResponse(id, kJsonRpcInvalidParams,
                          QStringLiteral("Unknown terminal id: ") + terminalId);
        return;
    }
    QJsonObject result;
    result.insert(QStringLiteral("output"), QString::fromUtf8(it.value().buffer));
    result.insert(QStringLiteral("truncated"), it.value().truncated);
    if (it.value().exitStatus.has_value()) {
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) — guarded by has_value() one line up
        const auto &es0 = it.value().exitStatus.value();
        QJsonObject es;
        es.insert(QStringLiteral("exitCode"), es0.first);
        es.insert(QStringLiteral("status"), es0.second);
        result.insert(QStringLiteral("exitStatus"), es);
    } else {
        result.insert(QStringLiteral("exitStatus"), QJsonValue());
    }
    sendResponse(id, result);
}

void AcpConnection::handleTerminalWaitForExit(const QJsonValue &id, const QJsonObject &params)
{
    const QString terminalId = params.value(QStringLiteral("terminalId")).toString();
    auto it = m_terminals.find(terminalId);
    if (it == m_terminals.end()) {
        sendErrorResponse(id, kJsonRpcInvalidParams,
                          QStringLiteral("Unknown terminal id: ") + terminalId);
        return;
    }
    auto sendIt = [this, id, terminalId]() {
        auto it = m_terminals.find(terminalId);
        if (it == m_terminals.end() || !it.value().exitStatus.has_value()) {
            sendResponse(id, QJsonObject{});
            return;
        }
        QJsonObject es;
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) — guarded by has_value() above
        es.insert(QStringLiteral("exitCode"), it.value().exitStatus->first);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) — guarded by has_value() above
        es.insert(QStringLiteral("status"), it.value().exitStatus->second);
        sendResponse(id, es);
    };
    if (it.value().exitStatus.has_value()) {
        sendIt();
    } else {
        it.value().waiters.append(sendIt);
    }
}

void AcpConnection::handleTerminalKill(const QJsonValue &id, const QJsonObject &params)
{
    const QString terminalId = params.value(QStringLiteral("terminalId")).toString();
    auto it = m_terminals.find(terminalId);
    if (it == m_terminals.end()) {
        sendResponse(id, QJsonObject{});
        return;
    }
    if (it.value().proc && it.value().proc->state() != QProcess::NotRunning) {
#ifdef Q_OS_WIN
        it.value().proc->kill();
#else
        it.value().proc->terminate();
#endif
    }
    sendResponse(id, QJsonObject{});
}

void AcpConnection::handleTerminalRelease(const QJsonValue &id, const QJsonObject &params)
{
    const QString terminalId = params.value(QStringLiteral("terminalId")).toString();
    auto it = m_terminals.find(terminalId);
    if (it == m_terminals.end()) {
        sendResponse(id, QJsonObject{});
        return;
    }
    if (it.value().proc) {
        if (it.value().proc->state() != QProcess::NotRunning) {
            it.value().proc->kill();
            it.value().proc->waitForFinished(500);
        }
        it.value().proc->deleteLater();
    }
    m_terminals.erase(it);
    sendResponse(id, QJsonObject{});
}

// ---------- Permission ---------------------------------------------------

void AcpConnection::handleRequestPermission(const QJsonValue &id,
                                            const QJsonObject &params,
                                            bool nestedOutcome)
{
    AcpProtocol::AcpPermissionRequest req;
    req.title = params.value(QStringLiteral("title")).toString();
    req.description = params.value(QStringLiteral("description")).toString();
    const QJsonObject toolCall = params.value(QStringLiteral("toolCall")).toObject();
    if (req.title.isEmpty()) {
        req.title = toolCall.value(QStringLiteral("title")).toString();
    }
    for (const auto &v : params.value(QStringLiteral("options")).toArray()) {
        if (v.isObject()) {
            req.options.append(AcpProtocol::permissionOptionFromJson(v.toObject()));
        }
    }
    // Use the JSON-RPC id (stringified) as the requestId we expose externally.
    QString requestKey;
    if (id.isDouble()) {
        requestKey = QString::number(id.toInt());
    } else if (id.isString()) {
        requestKey = id.toString();
    } else {
        requestKey = QString::number(id.toVariant().toInt());
    }
    req.requestId = requestKey;

    const QString policy = m_autoApproveProvider ? m_autoApproveProvider() : QStringLiteral("manual");
    if (policy == QLatin1String("allowAll")) {
        const auto picked = AcpProtocol::pickAutoApproveOptionId(req.options);
        if (picked.has_value()) {
            sendResponse(id, AcpProtocol::permissionResponseToJson(QStringLiteral("selected"),
                                                                    picked.value(),
                                                                    nestedOutcome));
            return;
        }
        // No acceptable option — fall through to manual path.
    }

    const QJsonValue idCopy = id; // NOLINT(performance-unnecessary-copy-initialization) — captured by value in the resolver lambda below
    m_pendingPermissions.insert(requestKey,
        [this, idCopy, nestedOutcome](const QString &outcome, const QString &optionId) {
            sendResponse(idCopy, AcpProtocol::permissionResponseToJson(outcome,
                                                                        optionId,
                                                                        nestedOutcome));
        });
    emit permissionRequested(req);
}

void AcpConnection::respondToPermission(const QString &requestId, const QString &outcome, const QString &optionId)
{
    auto cb = m_pendingPermissions.take(requestId);
    if (!cb) {
        return;
    }
    cb(outcome, optionId);
}

void AcpConnection::cancelAllPendingPermissions()
{
    for (auto it = m_pendingPermissions.begin(); it != m_pendingPermissions.end(); ++it) {
        it.value()(QStringLiteral("cancelled"), QString());
    }
    m_pendingPermissions.clear();
}

// ---------- ext_method passthrough --------------------------------------

void AcpConnection::handleExtMethod(const QJsonValue &id, const QJsonObject &params)
{
    Q_UNUSED(params);
    qCDebug(lcAcp) << "ext_method or unknown inbound — replying empty success";
    sendResponse(id, QJsonObject{});
}

// ---------- Process errors ----------------------------------------------

void AcpConnection::handleProcessError(QProcess::ProcessError err, const QString &message)
{
    QString raw;
    switch (err) {
    case QProcess::FailedToStart:
        raw = QStringLiteral("failed to spawn agent process");
        break;
    case QProcess::Crashed:
        raw = QStringLiteral("Agent process crashed");
        break;
    default:
        raw = message.isEmpty() ? QStringLiteral("Process error") : message;
        break;
    }
    appendDebugLog(QStringLiteral("process: errorOccurred kind=%1 raw=%2").arg(int(err)).arg(raw));
    emitClassifiedError(raw);
}

void AcpConnection::handleProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    appendDebugLog(QStringLiteral("process: finished exitCode=%1 status=%2")
                       .arg(exitCode)
                       .arg(status == QProcess::NormalExit ? QStringLiteral("normal")
                                                           : QStringLiteral("crash")));
    cancelAllPendingPermissions();
    // If the process died mid-turn, close the turn so the UI re-enables Send.
    endPrompt();
    // Surface the exit so the dock can switch into the "Agent exited" state.
    // No auto-reconnect — the user must click Restart.
    emit agentExited(exitCode, status);
}

void AcpConnection::emitClassifiedError(const QString &raw)
{
    const auto kind = AcpErrorClassifier::classify(raw);
    const QString hint = AcpErrorClassifier::loginHint(m_agent.command, m_agent.args);
    const QString friendly = AcpErrorClassifier::friendlyMessage(kind, raw, hint);
    appendDebugLog(QStringLiteral("error: kind=%1 raw=%2").arg(int(kind)).arg(raw));
    appendDebugLog(QStringLiteral("error: friendly=%1").arg(friendly));
    emit errorOccurred(kind, friendly);
}

void AcpConnection::beginPrompt()
{
    if (m_promptInFlight) {
        return;
    }
    m_promptInFlight = true;
    m_promptProducedOutput = false;
    emit promptStarted();
}

void AcpConnection::endPrompt()
{
    if (!m_promptInFlight) {
        return;
    }
    m_promptInFlight = false;
    emit promptEnded();
}

void AcpConnection::clearDebugLog()
{
    m_debugLog.clear();
}

void AcpConnection::appendDebugLog(QString line)
{
    if (line.size() > kDebugLogLineMaxChars) {
        line.truncate(kDebugLogLineMaxChars);
        line.append(QStringLiteral("… [truncated]"));
    }
    const QString prefixed =
        QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")), line);
    m_debugLog.append(prefixed);
    if (m_debugLog.size() > kDebugLogMaxLines) {
        m_debugLog.erase(m_debugLog.begin(),
                         m_debugLog.begin() + (m_debugLog.size() - kDebugLogMaxLines));
    }
    qCDebug(lcAcp).noquote() << prefixed;
    emit debugLogAppended(prefixed);
}

void AcpConnection::appendDebugFrameLog(const char *direction, const QByteArray &bytes)
{
    QString text = QString::fromUtf8(bytes);
    // Trim trailing newline so the log line stays single-line.
    while (text.endsWith(QLatin1Char('\n')) || text.endsWith(QLatin1Char('\r'))) {
        text.chop(1);
    }
    appendDebugLog(QStringLiteral("%1 %2").arg(QString::fromLatin1(direction), text));
}
