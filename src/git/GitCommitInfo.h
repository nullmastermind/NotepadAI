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

#ifndef GIT_COMMIT_INFO_H
#define GIT_COMMIT_INFO_H

#include <QByteArray>
#include <QString>

#include <cstdint>

// Row-shaped record produced by GitLogParser from one `git log` output record.
//
// Strings stay as QByteArray (raw UTF-8 from git) until the delegate is about
// to draw them; lazy QString::fromUtf8 keeps the streaming path zero-copy and
// avoids paying the UTF-16 conversion for rows the user never scrolls to.
struct GitCommitInfo {
    QByteArray sha;             // 40 hex chars
    QByteArray parents;         // space-separated hex SHAs ("" for root commit)
    QByteArray authorName;
    QByteArray authorEmail;
    qint64     ctime = 0;       // committer epoch seconds (UTC)
    QByteArray subject;         // first line of message; trailing \r already stripped

    // Convenience: short SHA (first 7 bytes). Returns empty if sha is shorter.
    QByteArray shortSha() const {
        return sha.size() >= 7 ? sha.left(7) : sha;
    }

    // Count parents — 0 for root, 1 for normal, 2+ for merge.
    int parentCount() const {
        if (parents.isEmpty()) return 0;
        int n = 1;
        for (char c : parents) if (c == ' ') ++n;
        return n;
    }

    bool isMerge() const { return parentCount() >= 2; }

    // Stable equality for dedupe / model diff (only SHA matters).
    bool operator==(const GitCommitInfo &o) const { return sha == o.sha; }
};

#endif // GIT_COMMIT_INFO_H
