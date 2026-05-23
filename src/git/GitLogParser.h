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

#ifndef GIT_LOG_PARSER_H
#define GIT_LOG_PARSER_H

#include "GitCommitInfo.h"

#include <QByteArray>
#include <QVector>

#include <cstdint>

// Streaming parser for the output of
//   git log --pretty=format:'%H%x1f%P%x1f%an%x1f%ae%x1f%ct%x1f%s%x1e' ...
//
// Field separator US (0x1f). Record separator RS (0x1e). Both bytes are
// extremely rare in commit messages, never appear in SHAs / timestamps /
// emails, and unlike NUL play nicely with QProcess pipes on Windows.
//
// Calling feed() repeatedly with arbitrary chunks of the byte stream is
// safe — the parser keeps a pending tail across calls so a record split
// across two chunks is still emitted whole.
//
// finish() flushes any final record that may not be terminated by 0x1e
// (git's last record has no trailing RS).
//
// Records with malformed structure (wrong field count, non-numeric ctime,
// embedded 0x1f in subject) are dropped silently — defensive against
// corrupt commit messages. The parser does NOT throw / crash on bad input.
class GitLogParser
{
public:
    GitLogParser();

    // Feed a chunk of bytes from the running `git log` stdout. Appends any
    // commits whose record terminator was found inside (or carried over
    // from a previous feed) to `out`. The parser owns no QObject state —
    // safe to construct one per fetch.
    void feed(const QByteArray &chunk, QVector<GitCommitInfo> &out);

    // Flush the last record if it lacks a trailing RS. Idempotent.
    void finish(QVector<GitCommitInfo> &out);

    // Drop any pending buffered tail (used when caller abandons a stream).
    void reset();

    // Field / record separators (exposed for tests and producers).
    static constexpr char kFieldSep  = '\x1f';
    static constexpr char kRecordSep = '\x1e';

private:
    QByteArray m_tail;   // bytes after the last RS in the last feed
};

#endif // GIT_LOG_PARSER_H
