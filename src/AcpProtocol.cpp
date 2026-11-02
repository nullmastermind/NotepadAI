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

#include "AcpProtocol.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringView>
#include <QtGlobal>
#include <QStringList>

namespace AcpProtocol {

bool isPermissionRequestMethod(const QString &method)
{
    return method == QLatin1String(kMethodSessionRequestPermission)
        || method == QLatin1String(kMethodRequestPermission);
}

AcpCapabilities parseAgentCapabilities(const QJsonObject &initializeResult)
{
    const QJsonValue specValue = initializeResult.value(QStringLiteral("agentCapabilities"));
    const QJsonObject caps = specValue.isObject()
        ? specValue.toObject()
        : initializeResult.value(QStringLiteral("capabilities")).toObject();
    AcpCapabilities out;
    const QJsonObject fs = caps.value(QStringLiteral("fs")).toObject();
    out.fsReadTextFile = fs.value(QStringLiteral("readTextFile")).toBool();
    out.fsWriteTextFile = fs.value(QStringLiteral("writeTextFile")).toBool();
    out.terminal = caps.value(QStringLiteral("terminal")).toBool();
    out.sessionLoad = caps.value(QStringLiteral("loadSession")).toBool()
        || caps.value(QStringLiteral("sessionLoad")).toBool();
    // Spec: an object means supported; absent or null means not.
    out.sessionResume = caps.value(QStringLiteral("sessionCapabilities")).toObject()
                            .value(QStringLiteral("resume")).isObject();
    return out;
}

QStringList acpExtractFrames(QByteArray &buffer)
{
    QStringList out;
    int searchStart = 0;
    while (true) {
        const int nl = buffer.indexOf('\n', searchStart);
        if (nl < 0) {
            break;
        }
        int lineEnd = nl;
        // Trim trailing \r for CR-LF tolerance.
        if (lineEnd > 0 && buffer.at(lineEnd - 1) == '\r') {
            lineEnd -= 1;
        }
        const int lineLen = lineEnd - 0; // from index 0
        QByteArray line = buffer.mid(0, lineLen);
        // Strip the consumed bytes from the buffer (including the \n).
        buffer.remove(0, nl + 1);
        searchStart = 0;
        if (line.isEmpty()) {
            // Skip empty lines between frames.
            continue;
        }
        out.append(QString::fromUtf8(line));
    }
    return out;
}

std::optional<QString> pickAutoApproveOptionId(const QList<AcpPermissionOption> &options)
{
    for (const AcpPermissionOption &o : options) {
        if (o.kind == QLatin1String("allow_once")) {
            return o.id;
        }
    }
    for (const AcpPermissionOption &o : options) {
        if (o.kind == QLatin1String("allow_always")) {
            return o.id;
        }
    }
    return std::nullopt;
}

bool pathIsInsideWorkingDir(const QString &canonicalPath, const QString &canonicalWorkingDir)
{
    if (canonicalPath.isEmpty() || canonicalWorkingDir.isEmpty()) {
        return false;
    }
    if (canonicalPath == canonicalWorkingDir) {
        return true;
    }
    // Try with both possible separators since callers may hand us either.
    const QChar nativeSep = QDir::separator();
    QString prefixNative = canonicalWorkingDir;
    if (!prefixNative.endsWith(nativeSep)) {
        prefixNative.append(nativeSep);
    }
    if (canonicalPath.startsWith(prefixNative)) {
        return true;
    }
    QString prefixSlash = canonicalWorkingDir;
    if (!prefixSlash.endsWith(QLatin1Char('/'))) {
        prefixSlash.append(QLatin1Char('/'));
    }
    if (canonicalPath.startsWith(prefixSlash)) {
        return true;
    }
    return false;
}

namespace {

QString posixSingleQuote(const QString &s)
{
    // Wrap in single quotes; replace ' with '\'' .
    QString escaped = s;
    escaped.replace(QLatin1String("'"), QLatin1String("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

// Quote a single token for inclusion on a Windows command line (the form
// CommandLineToArgvW expects). Implements the canonical algorithm from
// MSDN "Everyone quotes command line arguments the wrong way" — backslashes
// before a double-quote are doubled, the inner quote is escaped, and the
// whole thing is wrapped in double quotes if it contains whitespace, a quote,
// a tab, or is empty. Used for assembling the argument tail of `cmd /D /S /C
// "..."` so the inner program path's spaces don't fragment the parse.
QString windowsCommandLineQuote(const QString &arg)
{
    if (!arg.isEmpty()
        && !arg.contains(QLatin1Char(' '))
        && !arg.contains(QLatin1Char('\t'))
        && !arg.contains(QLatin1Char('\n'))
        && !arg.contains(QLatin1Char('\v'))
        && !arg.contains(QLatin1Char('"'))) {
        return arg;
    }

    QString out;
    out.reserve(arg.size() + 2);
    out.append(QLatin1Char('"'));
    int backslashes = 0;
    for (const QChar c : arg) {
        if (c == QLatin1Char('\\')) {
            ++backslashes;
        } else if (c == QLatin1Char('"')) {
            // Backslashes preceding a quote must be doubled, then escape the quote.
            out.append(QString(backslashes * 2 + 1, QLatin1Char('\\')));
            out.append(QLatin1Char('"'));
            backslashes = 0;
        } else {
            if (backslashes) {
                out.append(QString(backslashes, QLatin1Char('\\')));
                backslashes = 0;
            }
            out.append(c);
        }
    }
    // Trailing backslashes before the closing quote must be doubled too.
    out.append(QString(backslashes * 2, QLatin1Char('\\')));
    out.append(QLatin1Char('"'));
    return out;
}

} // namespace

SpawnArgv buildSpawnArgv(const QString &command,
                         const QStringList &args,
                         bool isPosix,
                         const QString &resolvedWindowsPath)
{
    SpawnArgv out;

    if (isPosix) {
        QString line = command;
        for (const QString &a : args) {
            line.append(QLatin1Char(' '));
            line.append(posixSingleQuote(a));
        }
        // Per spec ("Process spawning policy"): argv[0] is the user's login
        // shell. Resolve from $SHELL at call time so we honour the user's
        // chosen shell (zsh, fish, bash, ...). Fall back to /bin/sh only when
        // $SHELL is unset/empty.
        QString loginShell = qEnvironmentVariable("SHELL");
        if (loginShell.isEmpty()) {
            loginShell = QStringLiteral("/bin/sh");
        }
        out.program = loginShell;
        out.arguments = QStringList{ QStringLiteral("-lc"), line };
        return out;
    }

    const QString resolved = resolvedWindowsPath.isEmpty() ? command : resolvedWindowsPath;
    const QString lower = resolved.toLower();
    if (lower.endsWith(QLatin1String(".cmd")) || lower.endsWith(QLatin1String(".bat"))) {
        // Path may contain spaces (e.g. "C:/Program Files/nodejs/npx.cmd"). The
        // only reliable cmd.exe form is `cmd /D /S /C "<command line>"` where
        // the entire command line is wrapped in a single pair of double quotes
        // and /S tells cmd to strip exactly the outer pair (so quotes inside
        // the path are preserved). QProcess's default arg quoting can't
        // produce that shape, so we hand-build the line and ask the caller
        // to feed it via setNativeArguments.
        QString line = windowsCommandLineQuote(resolved);
        for (const QString &a : args) {
            line.append(QLatin1Char(' '));
            line.append(windowsCommandLineQuote(a));
        }
        out.program = QStringLiteral("cmd");
        out.nativeArgumentsLine =
            QStringLiteral("/D /S /C \"") + line + QLatin1Char('"');
        return out;
    }
    if (lower.endsWith(QLatin1String(".ps1"))) {
        QStringList outArgs;
        outArgs.reserve(args.size() + 3);
        outArgs.append(QStringLiteral("-NoProfile"));
        outArgs.append(QStringLiteral("-File"));
        outArgs.append(resolved);
        outArgs.append(args);
        out.program = QStringLiteral("powershell");
        out.arguments = outArgs;
        return out;
    }
    out.program = resolved;
    out.arguments = args;
    return out;
}

QJsonObject contentBlockToJson(const AcpContentBlock &block)
{
    QJsonObject obj;
    if (block.kind == AcpContentBlock::Kind::Image) {
        obj.insert(QStringLiteral("type"), QStringLiteral("image"));
        obj.insert(QStringLiteral("data"),
                   QString::fromLatin1(block.imageData.toBase64()));
        obj.insert(QStringLiteral("mimeType"), block.mimeType);
    } else {
        obj.insert(QStringLiteral("type"), QStringLiteral("text"));
        obj.insert(QStringLiteral("text"), block.text);
    }
    return obj;
}

AcpContentBlock contentBlockFromJson(const QJsonObject &obj)
{
    AcpContentBlock block;
    const QString type = obj.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("image")) {
        block.kind = AcpContentBlock::Kind::Image;
        const QString b64 = obj.value(QStringLiteral("data")).toString();
        block.imageData = QByteArray::fromBase64(b64.toLatin1());
        block.mimeType = obj.value(QStringLiteral("mimeType")).toString();
    } else {
        block.kind = AcpContentBlock::Kind::Text;
        block.text = obj.value(QStringLiteral("text")).toString();
    }
    return block;
}

QString contentBlockToChunkText(const QJsonObject &content)
{
    const QString type = content.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("resource_link")) {
        QString uri = content.value(QStringLiteral("uri")).toString();
        QString label = content.value(QStringLiteral("name")).toString();
        if (label.isEmpty()) {
            label = content.value(QStringLiteral("title")).toString();
        }
        if (uri.isEmpty()) {
            return label;
        }
        if (label.isEmpty() || label == uri) {
            return uri;
        }
        return QLatin1Char('[') + label + QLatin1String("](") + uri + QLatin1Char(')');
    }
    return content.value(QStringLiteral("text")).toString();
}

QString stripPiAcpQueueNotices(QString text)
{
    // Fast path: pi-acp's queue notices always contain "queued".
    if (!text.contains(QLatin1String("queued"), Qt::CaseInsensitive))
        return text;
    static const QRegularExpression re(
        QStringLiteral(
            R"((?:Queued message \(position \d+\)\.)|(?:Starting queued message\. \(\d+ remaining\))|(?:Cleared queued prompts\.))"));
    const int before = text.size();
    text.remove(re);
    return text.size() == before ? text : text.trimmed();
}

QString terminalOutputDeltaFromMeta(const QJsonObject &meta)
{
    return meta.value(QStringLiteral("terminal_output"))
        .toObject()
        .value(QStringLiteral("data"))
        .toString();
}

void stripTerminalContentBlocks(QJsonArray &content)
{
    bool stripped = false;
    QJsonArray kept;
    for (const auto &v : content) {
        if (v.toObject().value(QStringLiteral("type")).toString() == QLatin1String("terminal")) {
            stripped = true;
            continue;
        }
        kept.append(v);
    }
    if (stripped) {
        content = kept;
    }
}

void appendToolCallTextDelta(QJsonArray &content, const QString &delta)
{
    if (delta.isEmpty()) {
        return;
    }
    stripTerminalContentBlocks(content);
    if (!content.isEmpty()) {
        const int last = content.size() - 1;
        QJsonObject block = content.at(last).toObject();
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("text")) {
            block.insert(QStringLiteral("text"),
                         block.value(QStringLiteral("text")).toString() + delta);
            content.replace(last, block);
            return;
        }
    }
    QJsonObject block;
    block.insert(QStringLiteral("type"), QStringLiteral("text"));
    block.insert(QStringLiteral("text"), delta);
    content.append(block);
}

namespace {

constexpr int kMaxPreservedDiffBlocks = 32;
// One ingest-time copy for write tools that never send type:diff. Not on the
// per-frame card path. 1 MiB stops a pathological paste from inflating session
// JSON; typical writes are KB.
constexpr int kMaxSynthesizedNewTextChars = 1024 * 1024;
constexpr int kMaxCompactDiffChars = 128 * 1024;

bool isHexTag(QStringView tag)
{
    if (tag.isEmpty()) {
        return false;
    }
    for (const QChar c : tag) {
        const ushort u = c.unicode();
        const bool digit = u >= '0' && u <= '9';
        const bool hexLo = u >= 'a' && u <= 'f';
        const bool hexHi = u >= 'A' && u <= 'F';
        if (!digit && !hexLo && !hexHi) {
            return false;
        }
    }
    return true;
}

QString pathFromHashlineInput(const QString &input)
{
    if (!input.startsWith(QLatin1Char('['))) {
        return {};
    }
    const int close = input.indexOf(QLatin1Char(']'));
    if (close <= 1) {
        return {};
    }
    QString inner = input.mid(1, close - 1).trimmed();
    if (inner.isEmpty()) {
        return {};
    }
    const int hash = inner.lastIndexOf(QLatin1Char('#'));
    if (hash > 0 && isHexTag(QStringView{inner}.mid(hash + 1))) {
        inner.truncate(hash);
    }
    return inner.trimmed();
}

QString firstLocationPath(const QJsonArray &locations)
{
    const int n = qMin(locations.size(), 64);
    for (int i = 0; i < n; ++i) {
        const QString path = locations.at(i).toObject().value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) {
            return path;
        }
    }
    return {};
}

QString firstDiffPath(const QJsonArray &content)
{
    const int n = qMin(content.size(), 256);
    for (int i = 0; i < n; ++i) {
        const QJsonObject obj = content.at(i).toObject();
        if (obj.value(QStringLiteral("type")).toString() != QLatin1String("diff")) {
            continue;
        }
        const QString path = obj.value(QStringLiteral("path")).toString();
        if (!path.isEmpty()) {
            return path;
        }
    }
    return {};
}

bool contentHasDiff(const QJsonArray &content)
{
    const int n = qMin(content.size(), 256);
    for (int i = 0; i < n; ++i) {
        if (content.at(i).toObject().value(QStringLiteral("type")).toString()
            == QLatin1String("diff")) {
            return true;
        }
    }
    return false;
}

QJsonObject detailsObject(const QJsonObject &rawOutput)
{
    if (rawOutput.isEmpty()) {
        return {};
    }
    const QJsonValue details = rawOutput.value(QStringLiteral("details"));
    if (details.isObject()) {
        return details.toObject();
    }
    // Some adapters flatten EditToolDetails onto rawOutput (no nested `details`).
    if (rawOutput.contains(QStringLiteral("diff"))
        || rawOutput.contains(QStringLiteral("oldText"))
        || rawOutput.contains(QStringLiteral("newText"))
        || rawOutput.contains(QStringLiteral("perFileResults"))
        || rawOutput.contains(QStringLiteral("resolvedPath"))) {
        return rawOutput;
    }
    return {};
}

QString compactDiffString(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    if (value.isString()) {
        QString s = value.toString();
        if (s.size() > kMaxCompactDiffChars) {
            s.truncate(kMaxCompactDiffChars);
        }
        return s;
    }
    if (!value.isArray()) {
        return {};
    }
    const QJsonArray arr = value.toArray();
    const int n = qMin(arr.size(), 4000);
    QString out;
    out.reserve(qMin(kMaxCompactDiffChars, n * 32));
    for (int i = 0; i < n; ++i) {
        const QJsonValue line = arr.at(i);
        if (!line.isString()) {
            continue;
        }
        if (!out.isEmpty()) {
            out.append(QLatin1Char('\n'));
        }
        out.append(line.toString());
        if (out.size() >= kMaxCompactDiffChars) {
            out.truncate(kMaxCompactDiffChars);
            break;
        }
    }
    return out;
}

QJsonObject diffBlockFromEntry(const QJsonObject &entry)
{
    if (entry.value(QStringLiteral("isError")).toBool()) {
        return {};
    }
    const QString path = entry.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        return {};
    }
    const QJsonValue oldText = entry.value(QStringLiteral("oldText"));
    const QJsonValue newText = entry.value(QStringLiteral("newText"));
    const QString compact = compactDiffString(entry.value(QStringLiteral("diff")));
    if (oldText.isUndefined() && newText.isUndefined() && compact.isEmpty()) {
        return {};
    }
    QJsonObject block;
    block.insert(QStringLiteral("type"), QStringLiteral("diff"));
    block.insert(QStringLiteral("path"), path);
    if (oldText.isString() || oldText.isNull()) {
        block.insert(QStringLiteral("oldText"), oldText);
    }
    if (newText.isString()) {
        block.insert(QStringLiteral("newText"), newText);
    }
    if (!compact.isEmpty()) {
        block.insert(QStringLiteral("diff"), compact);
    }
    return block;
}

} // namespace

QString toolCallStatusForUi(const QString &status)
{
    if (status == QLatin1String("pending") || status == QLatin1String("in_progress")) {
        return QStringLiteral("running");
    }
    return status;
}

bool toolCallStatusIsTerminal(const QString &status)
{
    return status == QLatin1String("completed")
        || status == QLatin1String("failed")
        || status == QLatin1String("cancelled")
        || status == QLatin1String("canceled");
}

std::optional<AcpAsyncTaskEvent> asyncTaskEventFromUpdate(const QString &kind,
                                                          const QJsonObject &update)
{
    AcpAsyncTaskEvent ev;
    if (kind == QLatin1String("async_task_spawned")) {
        ev.kind = AcpAsyncTaskEvent::Kind::Spawned;
    } else if (kind == QLatin1String("async_task_progress")) {
        ev.kind = AcpAsyncTaskEvent::Kind::Progress;
    } else if (kind == QLatin1String("async_task_state_update")) {
        ev.kind = AcpAsyncTaskEvent::Kind::StateChanged;
    } else {
        return std::nullopt;
    }
    ev.taskId = update.value(QStringLiteral("asyncTaskId")).toString();
    if (ev.taskId.isEmpty()) {
        return std::nullopt;
    }
    ev.name = update.value(QStringLiteral("name")).toString();
    ev.taskType = update.value(QStringLiteral("taskType")).toString();
    ev.description = update.value(QStringLiteral("description")).toString();
    ev.toolCallId = update.value(QStringLiteral("toolCallId")).toString();
    ev.state = update.value(QStringLiteral("state")).toString();
    ev.summary = update.value(QStringLiteral("summary")).toString();
    ev.canStop = update.value(QStringLiteral("canStop")).toBool(false);
    return ev;
}

bool asyncTaskStateIsLive(const QString &state)
{
    return state == QLatin1String("running") || state == QLatin1String("paused");
}

bool toolCallUpdateIsBackgrounded(const QJsonObject &update)
{
    return update.value(QStringLiteral("_meta")).toObject()
        .value(QStringLiteral("jetbrains")).toObject()
        .value(QStringLiteral("air")).toObject()
        .value(QStringLiteral("asyncTasks")).toObject()
        .value(QStringLiteral("backgrounded")).toBool(false);
}

bool toolCallUpdateIsAsyncSubagentLaunch(const QJsonObject &update)
{
    const QJsonValue metaValue = update.value(QStringLiteral("_meta"));
    if (!metaValue.isObject())
        return false;
    const QJsonObject response = metaValue.toObject()
        .value(QStringLiteral("claudeCode")).toObject()
        .value(QStringLiteral("toolResponse")).toObject();
    return response.value(QStringLiteral("isAsync")).toBool(false)
        || response.value(QStringLiteral("status")).toString() == QLatin1String("async_launched");
}

QString subagentParentToolCallId(const QJsonObject &update)
{
    const QJsonValue metaValue = update.value(QStringLiteral("_meta"));
    if (!metaValue.isObject())
        return {}; // the main agent's updates: no lookups past this
    const QJsonObject meta = metaValue.toObject();
    QString parent = meta.value(QStringLiteral("parentToolCallId")).toString();
    if (parent.isEmpty()) {
        parent = meta.value(QStringLiteral("claudeCode")).toObject()
                     .value(QStringLiteral("parentToolUseId")).toString();
    }
    if (parent.isEmpty())
        parent = meta.value(QStringLiteral("parentToolUseId")).toString();
    if (!parent.isEmpty() && parent == update.value(QStringLiteral("toolCallId")).toString())
        return {};
    return parent;
}

namespace {

constexpr int kMaxHistoryInstructionChars = 32000;

bool isOmpTaskInput(const QJsonObject &rawInput)
{
    const QJsonValue tasks = rawInput.value(QLatin1String("tasks"));
    if (!tasks.isArray()) return false;
    const QJsonArray arr = tasks.toArray();
    return !arr.isEmpty() && arr.first().toObject().value(QLatin1String("task")).isString();
}

} // namespace

bool isSubagentToolInput(const QJsonObject &rawInput)
{
    if (rawInput.contains(QLatin1String("subagent_type")))
        return true;
    if (rawInput.contains(QLatin1String("description"))
        && !rawInput.value(QLatin1String("prompt")).toString().isEmpty())
        return true; // WebFetch's prompt comes with a url, not a description
    return isOmpTaskInput(rawInput);
}

int subagentToolCount(const QJsonObject &rawInput)
{
    return isOmpTaskInput(rawInput)
        ? static_cast<int>(rawInput.value(QLatin1String("tasks")).toArray().size())
        : 1;
}

QString subagentInstructions(const QJsonObject &rawInput)
{
    if (!isOmpTaskInput(rawInput)) {
        if (!isSubagentToolInput(rawInput)) return {};
        return rawInput.value(QLatin1String("prompt")).toString().trimmed();
    }
    QString md = rawInput.value(QLatin1String("context")).toString().trimmed();
    const QJsonArray tasks = rawInput.value(QLatin1String("tasks")).toArray();
    for (const auto &v : tasks) {
        const QJsonObject task = v.toObject();
        const QString text = task.value(QLatin1String("task")).toString().trimmed();
        const QString name = task.value(QLatin1String("name")).toString().trimmed();
        if (text.isEmpty() && name.isEmpty()) continue;
        if (!md.isEmpty()) md += QLatin1String("\n\n");
        if (!name.isEmpty())
            md += QLatin1String("**") + name + QLatin1String("**\n\n");
        md += text;
    }
    return md;
}

QJsonObject subagentInputForHistory(const QJsonObject &rawInput)
{
    auto capped = [](QString text) {
        if (text.size() > kMaxHistoryInstructionChars) text.truncate(kMaxHistoryInstructionChars);
        return text;
    };
    QJsonObject out;
    if (!isOmpTaskInput(rawInput)) {
        const QString prompt = rawInput.value(QLatin1String("prompt")).toString();
        if (!prompt.isEmpty()) out.insert(QStringLiteral("prompt"), capped(prompt));
        return out;
    }
    for (const QLatin1String key : {QLatin1String("i"), QLatin1String("agent")}) {
        const QString value = rawInput.value(key).toString();
        if (!value.isEmpty()) out.insert(key, value.left(200));
    }
    // One budget across the context and every task.
    int budget = kMaxHistoryInstructionChars;
    const QString context = rawInput.value(QLatin1String("context")).toString().left(budget);
    budget -= static_cast<int>(context.size());
    if (!context.isEmpty()) out.insert(QStringLiteral("context"), context);
    QJsonArray tasks;
    const QJsonArray source = rawInput.value(QLatin1String("tasks")).toArray();
    for (const auto &v : source) {
        const QJsonObject task = v.toObject();
        const QString text = task.value(QLatin1String("task")).toString().left(qMax(0, budget));
        budget -= static_cast<int>(text.size());
        tasks.append(QJsonObject{{QStringLiteral("name"), task.value(QLatin1String("name")).toString()},
                                 {QStringLiteral("task"), text}});
    }
    out.insert(QStringLiteral("tasks"), tasks);
    return out;
}

QString formatWaitDuration(qint64 ms)
{
    if (ms < 0) {
        ms = 0;
    }
    const qint64 totalSeconds = ms / 1000;
    if (totalSeconds < 60) {
        return QStringLiteral("%1.%2s").arg(totalSeconds).arg((ms % 1000) / 100);
    }
    const qint64 minutes = totalSeconds / 60;
    if (minutes < 60) {
        return QStringLiteral("%1m %2s").arg(minutes).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1h %2m").arg(minutes / 60).arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QString formatToolDuration(qint64 ms)
{
    const qint64 total = qMax<qint64>(ms, 0) / 1000;
    if (total == 0) {
        return QStringLiteral("<1s");
    }
    const qint64 h = total / 3600;
    const qint64 m = (total / 60) % 60;
    const qint64 s = total % 60;
    if (h > 0) {
        return QStringLiteral("%1h%2m%3s").arg(h).arg(m).arg(s);
    }
    if (m > 0) {
        return QStringLiteral("%1m%2s").arg(m).arg(s);
    }
    return QStringLiteral("%1s").arg(s);
}

QJsonObject clientCapabilitiesMeta()
{
    QJsonObject air;
    air.insert(QStringLiteral("version"), 1);
    air.insert(QStringLiteral("capabilities"), QJsonArray{QStringLiteral("asyncTasks")});
    QJsonObject jetbrains;
    jetbrains.insert(QStringLiteral("air"), air);
    QJsonObject meta;
    meta.insert(QStringLiteral("jetbrains"), jetbrains);
    return meta;
}

QString toolCallMutatedPath(const QJsonObject &rawInput,
                            const QJsonArray &content,
                            const QJsonObject &rawOutput,
                            const QJsonArray &locations)
{
    QString path = rawInput.value(QStringLiteral("file_path")).toString();
    if (path.isEmpty()) {
        path = rawInput.value(QStringLiteral("path")).toString();
    }
    if (path.isEmpty()) {
        path = pathFromHashlineInput(rawInput.value(QStringLiteral("input")).toString());
    }
    if (path.isEmpty()) {
        path = firstLocationPath(locations);
    }
    if (path.isEmpty()) {
        path = firstDiffPath(content);
    }
    if (path.isEmpty()) {
        const QJsonObject details = detailsObject(rawOutput);
        path = details.value(QStringLiteral("path")).toString();
        if (path.isEmpty()) {
            path = details.value(QStringLiteral("resolvedPath")).toString();
        }
    }
    return path;
}

void injectToolCallPath(QJsonObject &rawInput, const QJsonArray &locations)
{
    if (!rawInput.value(QStringLiteral("path")).toString().isEmpty()
        || !rawInput.value(QStringLiteral("file_path")).toString().isEmpty()) {
        return;
    }
    QString path = firstLocationPath(locations);
    if (path.isEmpty()) {
        path = pathFromHashlineInput(rawInput.value(QStringLiteral("input")).toString());
    }
    if (!path.isEmpty()) {
        rawInput.insert(QStringLiteral("path"), path);
    }
}

QJsonArray ensureDiffContent(const QJsonArray &content,
                             const QJsonObject &rawInput,
                             const QJsonObject &rawOutput)
{
    const QJsonObject details = detailsObject(rawOutput);
    const QString compact = compactDiffString(details.value(QStringLiteral("diff")));

    if (contentHasDiff(content)) {
        if (compact.isEmpty()) {
            return content;
        }
        QJsonArray out = content;
        const int n = qMin(out.size(), 256);
        for (int i = 0; i < n; ++i) {
            const QJsonValue item = out.at(i);
            if (!item.isObject()) {
                continue;
            }
            QJsonObject block = item.toObject();
            if (block.value(QStringLiteral("type")).toString() != QLatin1String("diff")) {
                continue;
            }
            if (block.value(QStringLiteral("diff")).toString().isEmpty()) {
                block.insert(QStringLiteral("diff"), compact);
                out.replace(i, block);
            }
        }
        return out;
    }

    QJsonArray out = content;
    const QJsonValue perFile = details.value(QStringLiteral("perFileResults"));
    if (perFile.isArray()) {
        const QJsonArray entries = perFile.toArray();
        const int n = qMin(entries.size(), kMaxPreservedDiffBlocks);
        for (int i = 0; i < n; ++i) {
            const QJsonValue entry = entries.at(i);
            if (!entry.isObject()) {
                continue;
            }
            const QJsonObject block = diffBlockFromEntry(entry.toObject());
            if (!block.isEmpty()) {
                out.append(block);
            }
        }
    } else {
        const QJsonObject block = diffBlockFromEntry(details);
        if (!block.isEmpty()) {
            out.append(block);
        }
    }

    if (contentHasDiff(out)) {
        return out;
    }

    QString path = rawInput.value(QStringLiteral("path")).toString();
    if (path.isEmpty()) {
        path = rawInput.value(QStringLiteral("file_path")).toString();
    }
    const QJsonValue writeBody = rawInput.value(QStringLiteral("content"));
    // omp's xd:// device writes are tool calls with JSON arguments, not files.
    if (!path.isEmpty()
        && !path.startsWith(QLatin1String("xd://"), Qt::CaseInsensitive)
        && writeBody.isString()
        && !rawInput.contains(QStringLiteral("input"))
        && !rawInput.contains(QStringLiteral("edits"))) {
        const QString newText = writeBody.toString();
        if (newText.size() <= kMaxSynthesizedNewTextChars) {
            QJsonObject block;
            block.insert(QStringLiteral("type"), QStringLiteral("diff"));
            block.insert(QStringLiteral("path"), path);
            block.insert(QStringLiteral("oldText"), QJsonValue::Null);
            block.insert(QStringLiteral("newText"), newText);
            out.append(block);
        }
    }
    return out;
}

QJsonArray preserveDiffBlocks(const QJsonArray &incoming, const QJsonArray &previous)
{
    if (contentHasDiff(incoming)) {
        return incoming;
    }
    QJsonArray out = incoming;
    int added = 0;
    const int n = qMin(previous.size(), 256);
    for (int i = 0; i < n && added < kMaxPreservedDiffBlocks; ++i) {
        const QJsonObject block = previous.at(i).toObject();
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("diff")) {
            out.append(block);
            ++added;
        }
    }
    return out;
}


QJsonObject permissionOptionToJson(const AcpPermissionOption &opt)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), opt.id);
    obj.insert(QStringLiteral("label"), opt.label);
    obj.insert(QStringLiteral("kind"), opt.kind);
    return obj;
}

AcpPermissionOption permissionOptionFromJson(const QJsonObject &obj)
{
    AcpPermissionOption o;
    o.id = obj.value(QStringLiteral("id")).toString();
    if (o.id.isEmpty()) {
        o.id = obj.value(QStringLiteral("optionId")).toString();
    }
    o.label = obj.value(QStringLiteral("label")).toString();
    if (o.label.isEmpty()) {
        o.label = obj.value(QStringLiteral("name")).toString();
    }
    o.kind = obj.value(QStringLiteral("kind")).toString();
    return o;
}

QJsonObject permissionResponseToJson(const QString &outcome,
                                     const QString &optionId,
                                     bool nestedOutcome)
{
    QJsonObject decision;
    decision.insert(QStringLiteral("outcome"), outcome);
    if (outcome == QLatin1String("selected")) {
        decision.insert(QStringLiteral("optionId"), optionId);
    }

    if (!nestedOutcome) {
        return decision;
    }

    QJsonObject result;
    result.insert(QStringLiteral("outcome"), decision);
    return result;
}

QJsonObject permissionRequestToJson(const AcpPermissionRequest &req)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("requestId"), req.requestId);
    obj.insert(QStringLiteral("title"), req.title);
    obj.insert(QStringLiteral("description"), req.description);
    QJsonArray opts;
    for (const AcpPermissionOption &o : req.options) {
        opts.append(permissionOptionToJson(o));
    }
    obj.insert(QStringLiteral("options"), opts);
    return obj;
}

AcpPermissionRequest permissionRequestFromJson(const QJsonObject &obj)
{
    AcpPermissionRequest r;
    r.requestId = obj.value(QStringLiteral("requestId")).toString();
    r.title = obj.value(QStringLiteral("title")).toString();
    r.description = obj.value(QStringLiteral("description")).toString();
    const QJsonArray opts = obj.value(QStringLiteral("options")).toArray();
    for (const auto &v : opts) {
        if (v.isObject()) {
            r.options.append(permissionOptionFromJson(v.toObject()));
        }
    }
    return r;
}

bool isGoalCommandName(const QString &name)
{
    QStringView v{name};
    while (!v.isEmpty() && v.front().isSpace())
        v = v.mid(1);
    while (!v.isEmpty() && v.back().isSpace())
        v.chop(1);
    while (!v.isEmpty() && v.front() == QLatin1Char('/'))
        v = v.mid(1);
    return v.compare(QLatin1String("goal"), Qt::CaseInsensitive) == 0;
}

bool commandsIncludeGoal(const QList<AcpCommandInfo> &commands)
{
    for (const AcpCommandInfo &cmd : commands) {
        if (isGoalCommandName(cmd.name))
            return true;
    }
    return false;
}

namespace {

const AcpCommandInfo *findAdvisorCommand(const QList<AcpCommandInfo> &commands)
{
    for (const AcpCommandInfo &cmd : commands) {
        QStringView v{cmd.name};
        v = v.trimmed();
        while (!v.isEmpty() && v.front() == QLatin1Char('/'))
            v = v.mid(1);
        if (v.compare(QLatin1String("advisor"), Qt::CaseInsensitive) == 0)
            return &cmd;
    }
    return nullptr;
}

} // namespace

bool commandsIncludeAdvisor(const QList<AcpCommandInfo> &commands)
{
    return findAdvisorCommand(commands) != nullptr;
}

bool advisorIsToggle(const QList<AcpCommandInfo> &commands)
{
    const AcpCommandInfo *cmd = findAdvisorCommand(commands);
    if (!cmd) return false;
    // "[on|off|status|dump [raw]|configure]": alternatives split by | [ ] and spaces.
    const QStringView hint{cmd->inputHint};
    qsizetype start = -1;
    for (qsizetype i = 0; i <= hint.size(); ++i) {
        const bool sep = i == hint.size() || hint[i] == QLatin1Char('|') || hint[i] == QLatin1Char('[')
            || hint[i] == QLatin1Char(']') || hint[i].isSpace();
        if (!sep) {
            if (start < 0) start = i;
            continue;
        }
        if (start >= 0 && hint.mid(start, i - start).compare(QLatin1String("on"), Qt::CaseInsensitive) == 0)
            return true;
        start = -1;
    }
    return false;
}

QString advisorModelFor(const QString &modelValue, const QString &modelLabel)
{
    // Weakest first: the first family found in the value, else in the label.
    struct Step { QLatin1String family; QLatin1String advisor; };
    static const Step kSteps[] = {
        {QLatin1String("haiku"), QLatin1String("sonnet")},
        {QLatin1String("sonnet"), QLatin1String("opus")},
        {QLatin1String("opus"), QLatin1String("fable")},
        {QLatin1String("fable"), QLatin1String("fable")},
    };
    for (const QString &text : {modelValue, modelLabel}) {
        for (const Step &s : kSteps) {
            if (text.contains(s.family, Qt::CaseInsensitive))
                return QString(s.advisor);
        }
    }
    return QStringLiteral("opus");
}

QString advisorCommandText(bool on, const QString &modelValue, const QString &modelLabel)
{
    return on ? QStringLiteral("/advisor ") + advisorModelFor(modelValue, modelLabel)
              : QStringLiteral("/advisor off");
}

QString advisorCommandText(bool on, const QList<AcpCommandInfo> &commands,
                           const QString &modelValue, const QString &modelLabel)
{
    if (advisorIsToggle(commands))
        return on ? QStringLiteral("/advisor on") : QStringLiteral("/advisor off");
    return advisorCommandText(on, modelValue, modelLabel);
}

std::optional<bool> parseAdvisorCommand(const QString &text)
{
    QStringView v = QStringView{text}.trimmed();
    constexpr QLatin1String kCmd("/advisor");
    if (!v.startsWith(kCmd, Qt::CaseInsensitive))
        return std::nullopt;
    v = v.mid(kCmd.size());
    // Reject "/advisorfoo"; the command must end or be followed by whitespace.
    if (!v.isEmpty() && !v.front().isSpace())
        return std::nullopt;
    v = v.trimmed();
    if (v.isEmpty())
        return std::nullopt;
    // omp's queries and setup leave the advisor as it is.
    QStringView verb = v;
    for (qsizetype i = 0; i < v.size(); ++i) {
        if (v[i].isSpace()) {
            verb = v.left(i);
            break;
        }
    }
    for (const QLatin1String query : {QLatin1String("status"), QLatin1String("dump"),
                                      QLatin1String("configure")}) {
        if (verb.compare(query, Qt::CaseInsensitive) == 0)
            return std::nullopt;
    }
    return v.compare(QLatin1String("off"), Qt::CaseInsensitive) != 0;
}

void ensureHostGoalCommand(QList<AcpCommandInfo> &commands)
{
    if (commandsIncludeGoal(commands))
        return;
    AcpCommandInfo goal;
    goal.name = QStringLiteral("goal");
    goal.description = QStringLiteral("Set a goal for this session");
    goal.inputHint = QStringLiteral("criterion");
    commands.prepend(goal);
}

bool rpcErrorIsSessionBusy(const QJsonValue &error)
{
    if (!error.isObject())
        return false;
    const QJsonObject o = error.toObject();
    const QJsonValue data = o.value(QStringLiteral("data"));
    if (data.isObject()
        && data.toObject().value(QStringLiteral("reason")).toString()
               == QLatin1String("session_busy")) {
        return true;
    }
    return o.value(QStringLiteral("message")).toString()
        .contains(QLatin1String("already processing"), Qt::CaseInsensitive);
}

QJsonObject rawOutputToObject(const QJsonValue &raw)
{
    if (raw.isObject())
        return raw.toObject();
    if (raw.isString() || raw.isArray()) {
        QJsonObject o;
        o.insert(QStringLiteral("output"), raw);
        return o;
    }
    return {};
}

bool isAdvisorToolName(const QString &name)
{
    return name == QLatin1String("advisor");
}

bool isServerToolCallId(const QString &id)
{
    return id.startsWith(QLatin1String("srvtoolu_"));
}

bool rpcErrorIsIncompleteServerToolCall(const QJsonValue &error)
{
    if (!error.isObject())
        return false;
    const QJsonObject o = error.toObject();
    if (o.value(QStringLiteral("data")).toObject().value(QStringLiteral("errorKind")).toString()
        != QLatin1String("incomplete_tool_call")) {
        return false;
    }
    const QString msg = o.value(QStringLiteral("message")).toString();
    const qsizetype colon = msg.lastIndexOf(QLatin1String(": "));
    if (colon < 0)
        return false;
    const QList<QStringView> ids = QStringView(msg).mid(colon + 2).split(QLatin1Char(','));
    for (const QStringView &id : ids) {
        if (!id.trimmed().startsWith(QLatin1String("srvtoolu_")))
            return false;
    }
    return !ids.isEmpty();
}

} // namespace AcpProtocol
