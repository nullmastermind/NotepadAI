/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#include "AcpErrorClassifier.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace AcpErrorClassifier {

namespace {

bool contains(const QString &haystackLower, const char *needle)
{
    return haystackLower.contains(QLatin1String(needle));
}

} // namespace

AcpErrorKind classify(const QString &message)
{
    const QString lower = message.toLower();

    if (contains(lower, "failed to spawn agent process") || contains(lower, "spawn_blocking failed")) {
        return AcpErrorKind::SpawnFailed;
    }

    if (contains(lower, "authentication required")
        || contains(lower, "not logged in")
        || contains(lower, "login required")
        || contains(lower, "unauthorized")) {
        return AcpErrorKind::AuthRequired;
    }
    if (contains(lower, "auth")
        && (contains(lower, "required") || contains(lower, "failed"))) {
        return AcpErrorKind::AuthRequired;
    }

    return AcpErrorKind::InitFailed;
}

QString loginHint(const QString &command, const QStringList &args)
{
    QString combined = command;
    for (const QString &a : args) {
        combined.append(QLatin1Char(' '));
        combined.append(a);
    }
    const QString lower = combined.toLower();
    if (lower.contains(QLatin1String("claude"))) {
        return QStringLiteral("claude login");
    }
    if (lower.contains(QLatin1String("auggie"))) {
        return QStringLiteral("auggie login");
    }
    if (lower.contains(QLatin1String("gemini"))) {
        return QStringLiteral("gemini auth login");
    }
    QString base = QFileInfo(command).completeBaseName();
    if (base.isEmpty()) {
        base = command;
    }
    return base + QStringLiteral(" login");
}

QString friendlyMessage(AcpErrorKind kind, const QString &raw, const QString &loginHint)
{
    switch (kind) {
    case AcpErrorKind::AuthRequired:
        return QCoreApplication::translate(
                   "AcpErrorClassifier",
                   "Authentication required \xe2\x80\x94 run `%1` in your terminal")
            .arg(loginHint);
    case AcpErrorKind::SpawnFailed:
        return QCoreApplication::translate(
            "AcpErrorClassifier",
            "Could not start the agent \xe2\x80\x94 check that the command is installed");
    case AcpErrorKind::InitFailed:
        return QCoreApplication::translate(
                   "AcpErrorClassifier",
                   "Agent initialization failed \xe2\x80\x94 %1")
            .arg(raw);
    }
    return raw;
}

} // namespace AcpErrorClassifier
