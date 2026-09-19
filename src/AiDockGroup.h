/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef AI_DOCK_GROUP_H
#define AI_DOCK_GROUP_H

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include <chrono>

// Widget-free grouping helpers for AI project docks. One dock per
// aiDockGroupKey; titles and objectNames derive from these so they can be
// unit-tested without a QMainWindow.

inline constexpr char kAiDockGroupLocalPrefix[] = "local:";
inline constexpr char kAiDockGroupSshPrefix[] = "ssh:";
inline constexpr char kAiDockObjectNamePrefix[] = "AiAgentDock_";
constexpr int kAiDockObjectNameHexLength = 16;
// Max source chars kept in a session-number tooltip before U+2026.
constexpr int kAiDockSessionTooltipMaxChars = 80;

inline qint64 aiDockMonotonicNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline QString aiDockGroupKey(const QString &workingDirectory, const QString &sshProfileId)
{
    const QString path = QDir::cleanPath(workingDirectory);
    if (sshProfileId.isEmpty())
        return QLatin1String(kAiDockGroupLocalPrefix) + path;
    return QLatin1String(kAiDockGroupSshPrefix) + sshProfileId + QLatin1Char(':') + path;
}

inline QString aiDockProjectBasename(const QString &workingDirectory)
{
    const QString cleaned = QDir::cleanPath(workingDirectory);
    const QString name = QFileInfo(cleaned).fileName();
    return name.isEmpty() ? workingDirectory : name;
}

inline QString aiDockWindowTitle(const QString &basename, int slotCount)
{
    if (slotCount >= 2)
        return basename + QLatin1Char(':');
    return basename;
}

inline QString aiDockObjectName(const QString &groupKey)
{
    const QByteArray hex = QCryptographicHash::hash(
                               groupKey.toUtf8(), QCryptographicHash::Sha256)
                               .toHex()
                               .left(kAiDockObjectNameHexLength);
    return QLatin1String(kAiDockObjectNamePrefix) + QString::fromLatin1(hex);
}

// After removing `closed` from a vector of `count` slots, the index that
// should become current. `current`/`closed` are 0-based; `count` is the size
// before the removal. Closing the current slot prefers the left neighbor,
// else 0 (the old next, now at 0). Closing a non-current slot only shifts
// current down when the closed index was to its left.
inline int aiDockCompactAfterClose(int current, int closed, int count)
{
    if (count <= 1)
        return 0;
    int next = current;
    if (closed < current)
        --next;
    else if (closed == current)
        next = (closed > 0) ? closed - 1 : 0;
    const int maxIndex = count - 2;
    if (next > maxIndex)
        next = maxIndex;
    if (next < 0)
        next = 0;
    return next;
}

// Collapse whitespace then truncate for a session-number tooltip. Empty or
// whitespace-only input yields an empty string (no tooltip).
inline QString aiDockSessionTooltipPreview(const QString &raw)
{
    QString text = raw.simplified();
    if (text.isEmpty())
        return {};
    if (text.size() <= kAiDockSessionTooltipMaxChars)
        return text;
    text.truncate(kAiDockSessionTooltipMaxChars);
    if (!text.isEmpty() && text.back().isHighSurrogate())
        text.chop(1);
    text.append(QChar(0x2026));
    return text;
}

#endif // AI_DOCK_GROUP_H
