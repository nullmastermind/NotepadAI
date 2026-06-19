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
#include <QStringView>
#include <QtGlobal>

namespace AcpProtocol {

bool isPermissionRequestMethod(const QString &method)
{
    return method == QLatin1String(kMethodSessionRequestPermission)
        || method == QLatin1String(kMethodRequestPermission);
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

} // namespace AcpProtocol
