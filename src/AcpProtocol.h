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

#ifndef ACP_PROTOCOL_H
#define ACP_PROTOCOL_H

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace AcpProtocol {

// Protocol version that we negotiate at `initialize`. Per the ACP spec this
// is a u16 integer (V1 is the current stable; V0 was pre-release). Sent as a
// JSON number — sending a string makes strict agents (Zod-validated TS
// implementations) reject `initialize` with "expected number, received string".
// Bump in lockstep with the upstream `agent-client-protocol` crate's
// `ProtocolVersion::LATEST`.
constexpr int kProtocolVersion = 1;

// JSON-RPC method names — keep colocated so a protocol revision is one PR.
constexpr const char *kMethodInitialize          = "initialize";
constexpr const char *kMethodSessionNew          = "session/new";
constexpr const char *kMethodSessionResume       = "session/resume";
constexpr const char *kMethodSessionPrompt       = "session/prompt";
constexpr const char *kMethodSessionCancel       = "session/cancel";
constexpr const char *kMethodSessionSetMode      = "session/set_mode";
constexpr const char *kMethodSessionSetModel     = "session/set_model";
constexpr const char *kMethodSessionSetConfig    = "session/set_config_option";
constexpr const char *kMethodFsReadTextFile      = "fs/read_text_file";
constexpr const char *kMethodFsWriteTextFile     = "fs/write_text_file";
constexpr const char *kMethodTerminalCreate      = "terminal/create";
constexpr const char *kMethodTerminalOutput      = "terminal/output";
constexpr const char *kMethodTerminalWaitForExit = "terminal/wait_for_exit";
constexpr const char *kMethodTerminalKill        = "terminal/kill";
constexpr const char *kMethodTerminalRelease     = "terminal/release";
constexpr const char *kMethodSessionRequestPermission = "session/request_permission";
constexpr const char *kMethodRequestPermission   = "request_permission";
constexpr const char *kMethodSessionUpdate       = "session/update";
constexpr const char *kMethodExtMethod           = "ext_method";
// Never advertised: rejected as method-not-found (questions use grill_me).
constexpr const char *kMethodElicitationCreate   = "elicitation/create";
// claude-agent-acp AIR extension: stop one background (async) task.
constexpr const char *kMethodAsyncTaskStop       = "_session/async_task/stop";

bool isPermissionRequestMethod(const QString &method);

// ----- POD payloads shared by the engine and the session model -----------

struct AcpContentBlock
{
    enum class Kind : std::uint8_t { Text, Image };

    Kind kind = Kind::Text;
    QString text;
    QByteArray imageData; // raw bytes (not base64); serializer encodes
    QString mimeType;
};

struct AcpUsage
{
    std::optional<int> inputTokens;
    std::optional<int> outputTokens;
    std::optional<int> totalTokens;
    std::optional<int> maxTokens;
    std::optional<double> costAmount;
    std::optional<QString> costCurrency;
};

struct AcpToolCall
{
    QString id;
    QString title; // human-readable; adapters often rewrite (e.g. "Write path")
    QString name;  // wire tool name when sent (e.g. Write / Read / apply_patch)
    QString kind;
    QString status;
    QJsonArray content;
    QJsonObject rawInput;
    QJsonObject rawOutput;
    int groupId{0};
    // Claude Code moved this tool call to the background (the call itself is
    // reported `completed`). `backgroundRunning` is live-only state: the async
    // task it started is still alive.
    bool backgrounded{false};
    bool backgroundRunning{false};
    // Host-side run time. startedAtMs is live-only (epoch ms when first seen
    // non-terminal); durationMs is set once on the first terminal status and
    // persisted. -1 = unknown (restored/never observed running).
    qint64 startedAtMs{0};
    qint64 durationMs{-1};
    // Made by a subagent: the Agent/Task tool call that spawned it. Such calls
    // render nested under that card, never as transcript peers. Persisted.
    QString parentToolCallId;
};

// Needle match for ACP tool name or title. Prefer extra over dropping a write.
inline bool toolCallTitleLooksFileMutating(const QString &title)
{
    return title.contains(QLatin1String("write"), Qt::CaseInsensitive)
        || title.contains(QLatin1String("edit"), Qt::CaseInsensitive)
        || title.contains(QLatin1String("apply_patch"), Qt::CaseInsensitive)
        || title.contains(QLatin1String("apply-patch"), Qt::CaseInsensitive)
        || title.contains(QLatin1String("apply patch"), Qt::CaseInsensitive);
}

inline bool toolCallKindLooksFileMutating(const QString &kind)
{
    return kind.compare(QLatin1String("edit"), Qt::CaseInsensitive) == 0
        || kind.compare(QLatin1String("delete"), Qt::CaseInsensitive) == 0
        || kind.compare(QLatin1String("move"), Qt::CaseInsensitive) == 0;
}

inline bool toolCallLooksFileMutating(const AcpToolCall &tc)
{
    return toolCallTitleLooksFileMutating(tc.name)
        || toolCallTitleLooksFileMutating(tc.title)
        || toolCallKindLooksFileMutating(tc.kind);
}

struct AcpToolCallUpdate
{
    QString id;
    std::optional<QString> title;
    std::optional<QString> name;
    std::optional<QString> kind;
    std::optional<QString> status;
    std::optional<QJsonArray> content;
    std::optional<QJsonObject> rawInput;
    std::optional<QJsonObject> rawOutput;
    // Zed/PI execute-tool `_meta.terminal_output.data` — a stdout/stderr *delta*.
    // The session model appends this onto a text content block; the card stays
    // collapsed (title = command) until the user expands.
    std::optional<QString> terminalOutputDelta;
    // `_meta.jetbrains.air.asyncTasks.backgrounded` on the update.
    std::optional<bool> backgrounded;
    // Host-side (not wire): the session view mirrors AcpToolCall::backgroundRunning.
    std::optional<bool> backgroundRunning;
    // Host-side (not wire): mirrors AcpToolCall::durationMs once known.
    std::optional<qint64> durationMs;
    // Late subagent attribution (subagentParentToolCallId of the update): an
    // eager tool_call can miss it, the refining update carries it.
    std::optional<QString> parentToolCallId;
};

struct AcpPlanEntry
{
    QString text;
    QString status;
};

struct AcpPermissionOption
{
    QString id;
    QString label;
    QString kind;
};

struct AcpPermissionRequest
{
    QString requestId;
    QString title;
    QString description;
    QList<AcpPermissionOption> options;
};

struct AcpModeInfo
{
    QString id;
    QString name;
    QString description;
};

struct AcpModelInfo
{
    QString id;
    QString name;
    QString description;
};

struct AcpConfigOptionChoice
{
    QString value; // canonical id sent back via session/set_config_option
    QString name;  // user-facing label
};

struct AcpConfigOption
{
    QString id;
    QString name;
    QString description;
    QString category; // optional, e.g. "thought_level"
    QJsonValue currentValue;
    QList<AcpConfigOptionChoice> options;
};

struct AcpAgentInfo
{
    QString name;
    QString title;
    QString version;
};

struct AcpCommandInfo
{
    QString name;
    QString description;
    QString inputHint;
};

struct AcpCapabilities
{
    bool fsReadTextFile{false};
    bool fsWriteTextFile{false};
    bool terminal{false};
    bool sessionLoad{false};
    // `agentCapabilities.sessionCapabilities.resume`: session/resume restores
    // a session's context without replaying it (Switch agent, restored sessions).
    bool sessionResume{false};
};

// initialize result -> capabilities. Spec key `agentCapabilities` (loadSession,
// sessionCapabilities.resume), falling back to the legacy `capabilities`.
AcpCapabilities parseAgentCapabilities(const QJsonObject &initializeResult);

// ----- Free helpers (unit-testable in isolation) -------------------------

// Consumes complete `\n`-terminated frames from `buffer`, returning each
// frame's bytes (UTF-8) in order. Trailing partial line is left in the
// buffer for the next call. Empty lines between frames are skipped. CR-LF
// tolerated (trailing `\r` is stripped).
QStringList acpExtractFrames(QByteArray &buffer);

// Picks the first option whose `kind == "allow_once"`; otherwise the first
// whose `kind == "allow_always"`; otherwise nullopt. Pure function for the
// allowAll auto-approve policy.
std::optional<QString> pickAutoApproveOptionId(const QList<AcpPermissionOption> &options);

// Returns true iff `canonicalPath` is identical to `canonicalWorkingDir`
// or is a descendant. Inputs must already be canonicalized — this helper
// is a pure string-level boundary check so it is hermetic in tests.
bool pathIsInsideWorkingDir(const QString &canonicalPath, const QString &canonicalWorkingDir);

// Produces a spawn descriptor for an ACP agent following the design.md D2
// spawn policy. POSIX collapses to `sh -lc "<cmd> 'args'..."`.
// On Windows: `.cmd`/`.bat` → `cmd /D /S /C "<command line>"` via
// `nativeArgumentsLine` (callers must use QProcess::setNativeArguments to
// preserve our hand-built quoting — cmd /C won't survive QProcess's default
// arg-list quoting when the resolved path contains spaces);
// `.ps1` → `powershell -NoProfile -File ...`; otherwise invoke directly.
// `resolvedWindowsPath` is the PATH-resolved absolute path (caller's
// responsibility); when empty in Windows mode falls back to `command`.
struct SpawnArgv
{
    QString program;
    QStringList arguments;
    // Windows-only. When non-empty, the caller MUST use
    // QProcess::setNativeArguments(nativeArgumentsLine) instead of
    // setArguments(arguments) — arguments will be empty in that case.
    QString nativeArgumentsLine;
};

SpawnArgv buildSpawnArgv(const QString &command,
                         const QStringList &args,
                         bool isPosix,
                         const QString &resolvedWindowsPath = QString());

// JSON (de)serialization helpers — free functions so unit tests don't need
// to spin up an AcpConnection.
QJsonObject contentBlockToJson(const AcpContentBlock &block);
AcpContentBlock contentBlockFromJson(const QJsonObject &obj);

// Inbound session/update content → transcript chunk text.
// `text` blocks pass through `.text`. `resource_link` (e.g. PI `/export`) has
// no `.text`; fold name+uri into a markdown link so assistant bubbles (which
// already render markdown with open-external-links) show a clickable path.
QString contentBlockToChunkText(const QJsonObject &content);
// pi-acp emits queue depth as agent_message_chunk. Drop those notices so they
// are not assistant bubbles. Empty result = do not emit the chunk.
QString stripPiAcpQueueNotices(QString text);

// PI/Zed execute-tool `_meta.terminal_output.data` (empty if absent).
QString terminalOutputDeltaFromMeta(const QJsonObject &meta);
// Drop `{type:"terminal"}` placeholders so the card doesn't JSON-dump them.
void stripTerminalContentBlocks(QJsonArray &content);
// Append stdout/stderr onto the last text content block (creates one if needed).
void appendToolCallTextDelta(QJsonArray &content, const QString &delta);

// omp acp emits `pending` / `in_progress` instead of Claude/Codex `running`.
QString toolCallStatusForUi(const QString &status);

// Final tool-call states. `pending` / `in_progress` / empty are still open.
bool toolCallStatusIsTerminal(const QString &status);

// ----- AIR async tasks (claude-agent-acp background work) ------------------
// Emitted only when `initialize` declares the `asyncTasks` capability, see
// clientCapabilitiesMeta().
struct AcpAsyncTaskEvent
{
    enum class Kind : std::uint8_t { Spawned, Progress, StateChanged };

    Kind kind = Kind::Spawned;
    QString taskId;
    QString name;
    QString taskType;
    QString description;
    QString toolCallId; // may arrive late, on a later progress update
    QString state;      // StateChanged: running | paused | completed | failed | stopped
    QString summary;
    bool canStop = false; // Spawned: the agent accepts _session/async_task/stop for it
};

// A live background task as tracked by the connection (spawn order).
struct AcpBackgroundTask
{
    QString taskId;
    QString name;
    QString toolCallId; // may be empty until a later progress update
    bool canStop = false;
};

// `kind` is the `sessionUpdate` value. nullopt for any other kind or when the
// payload carries no `asyncTaskId`.
std::optional<AcpAsyncTaskEvent> asyncTaskEventFromUpdate(const QString &kind,
                                                          const QJsonObject &update);
bool asyncTaskStateIsLive(const QString &state);

// `_meta.jetbrains.air.asyncTasks.backgrounded == true` on a tool_call_update.
bool toolCallUpdateIsBackgrounded(const QJsonObject &update);

// `_meta.claudeCode.toolResponse` of an Agent/Task call whose subagent was
// launched in the background (`isAsync: true` / `status: "async_launched"`).
// claude-agent-acp reports such a call `completed` at once and publishes no
// async task for the subagent, so this marker is the only launch signal.
bool toolCallUpdateIsAsyncSubagentLaunch(const QJsonObject &update);

// The Agent/Task tool call whose subagent sent this session/update, from its
// `_meta`: `parentToolCallId`, then `claudeCode.parentToolUseId`
// (claude-agent-acp), then `parentToolUseId`. Empty for the main agent, and
// when the id names the update's own tool call.
QString subagentParentToolCallId(const QJsonObject &update);

// A tool call that runs subagents, from its rawInput: Claude Code's Agent/Task
// (`subagent_type`, or `description` + `prompt`) or omp's task
// (`tasks: [{name, task}]`, optional `context`, `agent`, intent `i`).
bool isSubagentToolInput(const QJsonObject &rawInput);
// Number of subagents the call starts: omp's task count, else 1.
int subagentToolCount(const QJsonObject &rawInput);
// The instructions those subagents get, as markdown (omp: the shared context,
// then each task under its bold name). Empty when the call carries none.
QString subagentInstructions(const QJsonObject &rawInput);
// The rawInput fields history keeps for such a call (instructions capped).
QJsonObject subagentInputForHistory(const QJsonObject &rawInput);

// Compact wait clock: "4.2s" under a minute, "2m 05s" under an hour, then "1h 02m".
QString formatWaitDuration(qint64 ms);
// Compact tool run time: "<1s", "40s", "1m5s", "1h1m5s".
QString formatToolDuration(qint64 ms);

// `initialize.clientCapabilities._meta`: opt in to the AIR async-task events.
// The agent requires `version` >= 1 and `capabilities` to be an array.
QJsonObject clientCapabilitiesMeta();

// Path for git refresh and titles. Claude uses `file_path`; omp uses `path`,
// `locations[]`, type:diff.path, or a hashline `input` (`[file#tag]`).
QString toolCallMutatedPath(const QJsonObject &rawInput,
                            const QJsonArray &content,
                            const QJsonObject &rawOutput,
                            const QJsonArray &locations = QJsonArray());

// Fold locations / hashline into `rawInput.path` when the agent omitted it.
void injectToolCallPath(QJsonObject &rawInput, const QJsonArray &locations);

// Normalize tool-call content to the Claude/Codex edit card: `type:diff`
// with oldText/newText, plus omp's compact numbered `diff` hunk when present.
QJsonArray ensureDiffContent(const QJsonArray &content,
                             const QJsonObject &rawInput,
                             const QJsonObject &rawOutput);

// Keep previously synthesized type:diff blocks when a later update omits them
// (omp write completes with a text status and no diff).
QJsonArray preserveDiffBlocks(const QJsonArray &incoming, const QJsonArray &previous);

// Host `/goal` — palette only. Agents (omp ACP) often omit it because their
// TUI handler has no text-mode `handle`. Composer send uses agent-advertised
// goal for side-prompt; host inject never counts as native.
bool isGoalCommandName(const QString &name);
bool commandsIncludeGoal(const QList<AcpCommandInfo> &commands);

// Claude Code `/advisor` toggle. The UI button exists only when the agent
// advertises the command (claude-agent-acp: hint "[fable|opus|sonnet|off]").
bool commandsIncludeAdvisor(const QList<AcpCommandInfo> &commands);
// Advisor = one level above the selected model: haiku -> sonnet -> opus -> fable
// (fable stays fable). The family is read from the model value ("opus",
// "claude-haiku-4-5-...") and, failing that, its label ("Sonnet 5.5"); an
// unrecognised model (e.g. "default") gets "opus".
QString advisorModelFor(const QString &modelValue, const QString &modelLabel = {});
// "/advisor <advisor model>" when on, "/advisor off" when off.
QString advisorCommandText(bool on, const QString &modelValue, const QString &modelLabel = {});
// True when the advertised `/advisor` is an on/off switch rather than a model
// pick: its hint lists "on" (omp: "[on|off|status|dump [raw]|configure]").
bool advisorIsToggle(const QList<AcpCommandInfo> &commands);
// The command in the form the agent advertises: "/advisor on" / "/advisor off"
// for a toggle, else advisorCommandText(on, modelValue, modelLabel).
QString advisorCommandText(bool on, const QList<AcpCommandInfo> &commands,
                           const QString &modelValue, const QString &modelLabel = {});
// Reads a typed `/advisor` command: "off" -> false, "on" or a model -> true.
// Bare `/advisor`, omp's status/dump/configure and any other text -> nullopt.
std::optional<bool> parseAdvisorCommand(const QString &text);
// Prepend `goal` when missing. O(n) over a few dozen commands; no heap if present.
void ensureHostGoalCommand(QList<AcpCommandInfo> &commands);

// omp ACP rejects session/prompt while an ownerless turn is streaming.
// Typed form (18.0.7+): data.reason == "session_busy". Older: message text.
bool rpcErrorIsSessionBusy(const QJsonValue &error);

// Anthropic server-side tools (advisor, web_search, ...) surface as tool calls
// whose ids start with `srvtoolu_`. claude-agent-acp never forwards their
// results, so it leaves them pending and, at end of turn, fails them and
// answers session/prompt with -32603 data.errorKind "incomplete_tool_call".
// Wire `rawOutput` is an object for most agents but a bare string/array for
// claude-agent-acp (Bash "(Bash completed with no output)", Grep "No files
// found"). Non-objects are wrapped as {"output": value} so the card can show
// them; an empty/null value yields an empty object.
QJsonObject rawOutputToObject(const QJsonValue &raw);
bool isAdvisorToolName(const QString &name);
bool isServerToolCallId(const QString &id);
// True only when every call listed in that error is a server tool call: the
// turn itself completed normally and the error carries no user-actionable fault.
bool rpcErrorIsIncompleteServerToolCall(const QJsonValue &error);


QJsonObject permissionOptionToJson(const AcpPermissionOption &opt);
AcpPermissionOption permissionOptionFromJson(const QJsonObject &obj);
QJsonObject permissionResponseToJson(const QString &outcome,
                                     const QString &optionId,
                                     bool nestedOutcome);

QJsonObject permissionRequestToJson(const AcpPermissionRequest &req);
AcpPermissionRequest permissionRequestFromJson(const QJsonObject &obj);

} // namespace AcpProtocol

#endif // ACP_PROTOCOL_H
