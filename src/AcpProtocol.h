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
constexpr const char *kMethodRequestPermission   = "request_permission";
constexpr const char *kMethodSessionUpdate       = "session/update";
constexpr const char *kMethodExtMethod           = "ext_method";

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
    QString title;
    QString status;
    QJsonArray content;
    QJsonObject rawInput;
    int groupId{0};
};

struct AcpToolCallUpdate
{
    QString id;
    std::optional<QString> status;
    std::optional<QJsonArray> content;
    std::optional<QJsonObject> rawInput;
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

struct AcpCapabilities
{
    bool fsReadTextFile{false};
    bool fsWriteTextFile{false};
    bool terminal{false};
    bool sessionLoad{false};
};

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

// Produces `(program, args)` for spawning an ACP agent following the
// design.md D2 spawn policy. POSIX collapses to `sh -lc "<cmd> 'args'..."`.
// On Windows: `.cmd`/`.bat` → `cmd /C ...`; `.ps1` → `powershell -NoProfile -File ...`;
// otherwise direct. `resolvedWindowsPath` is the PATH-resolved absolute path
// (caller's responsibility); when empty in Windows mode falls back to `command`.
QPair<QString, QStringList> buildSpawnArgv(const QString &command,
                                           const QStringList &args,
                                           bool isPosix,
                                           const QString &resolvedWindowsPath = QString());

// JSON (de)serialization helpers — free functions so unit tests don't need
// to spin up an AcpConnection.
QJsonObject contentBlockToJson(const AcpContentBlock &block);
AcpContentBlock contentBlockFromJson(const QJsonObject &obj);

QJsonObject permissionOptionToJson(const AcpPermissionOption &opt);
AcpPermissionOption permissionOptionFromJson(const QJsonObject &obj);

QJsonObject permissionRequestToJson(const AcpPermissionRequest &req);
AcpPermissionRequest permissionRequestFromJson(const QJsonObject &obj);

} // namespace AcpProtocol

#endif // ACP_PROTOCOL_H
