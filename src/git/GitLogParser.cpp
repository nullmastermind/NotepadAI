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

#include "GitLogParser.h"

#include "ProfileScope.h"

namespace {

// Parse one record (bytes between two RSes, without the RS itself). Returns
// true and fills `info` on success; false on malformed input (dropped).
//
// Expected layout: SHA \x1f PARENTS \x1f AUTHOR_NAME \x1f AUTHOR_EMAIL \x1f
//                  CTIME \x1f SUBJECT
// Each %xx token expands to exactly one separator, so a valid record has
// exactly 5 field separators. Any deviation = malformed → drop.
//
// Defensive against:
//   - Trailing \n (git ends each format expansion with format-spec literals
//     only; an explicit %x1e RS terminates the record. We add it ourselves
//     to the format string).
//   - Leading newlines accidentally inserted between records by some shells.
//   - Subject containing \x1f (extremely rare but possible in malicious /
//     corrupt commits) — detected by extra field count → drop.
bool parseRecord(const char *data, qsizetype size, GitCommitInfo &info)
{
    // Skip any leading \r / \n / RS noise (defensive — first record can have
    // a stale newline before it on some platforms).
    qsizetype start = 0;
    while (start < size && (data[start] == '\n' || data[start] == '\r')) ++start;
    if (start >= size) return false;

    // Find 5 separators. Use raw byte loop — no QByteArray::indexOf overhead
    // (single function call vs five).
    qsizetype seps[5] = {-1, -1, -1, -1, -1};
    int sepN = 0;
    for (qsizetype i = start; i < size; ++i) {
        if (data[i] == GitLogParser::kFieldSep) {
            if (sepN >= 5) return false;   // too many separators
            seps[sepN++] = i;
        }
    }
    if (sepN != 5) return false;

    // SHA: must be 40 hex chars (defensive — short SHA never appears in
    // --pretty=format:%H, but bound check anyway).
    const qsizetype shaLen = seps[0] - start;
    if (shaLen != 40) return false;
    // ctime: must be parseable as positive int64
    const qsizetype ctStart = seps[3] + 1;
    const qsizetype ctEnd = seps[4];
    if (ctEnd <= ctStart) return false;
    qint64 ctime = 0;
    for (qsizetype i = ctStart; i < ctEnd; ++i) {
        const char c = data[i];
        if (c < '0' || c > '9') return false;
        ctime = ctime * 10 + (c - '0');
    }

    // All checks passed — fill struct.
    info.sha         = QByteArray(data + start,       seps[0] - start);
    info.parents     = QByteArray(data + seps[0] + 1, seps[1] - seps[0] - 1);
    info.authorName  = QByteArray(data + seps[1] + 1, seps[2] - seps[1] - 1);
    info.authorEmail = QByteArray(data + seps[2] + 1, seps[3] - seps[2] - 1);
    info.ctime       = ctime;
    // Subject runs from after the 5th separator to the end of the record.
    const qsizetype subjStart = seps[4] + 1;
    qsizetype subjEnd = size;
    // Trim a trailing \r (Windows git can emit CRLF for the subject line if
    // core.autocrlf is set on output paths — rare, but cheap to guard).
    if (subjEnd > subjStart && data[subjEnd - 1] == '\r') --subjEnd;
    info.subject = QByteArray(data + subjStart, subjEnd - subjStart);
    return true;
}

} // namespace

GitLogParser::GitLogParser() = default;

void GitLogParser::feed(const QByteArray &chunk, QVector<GitCommitInfo> &out)
{
    PROFILE_SCOPE("GitLogParser::parseChunk");
    if (chunk.isEmpty()) return;

    // Append to pending tail and walk forward, slicing on RS.
    // The slice contract: between m_tail.size() before the append and the
    // last RS found in the combined buffer, every RS marks the end of a
    // record we can parse. Whatever remains after the last RS goes back
    // into m_tail.
    m_tail.append(chunk);
    const char *data = m_tail.constData();
    const qsizetype n = m_tail.size();
    qsizetype recordStart = 0;

    // Pre-reserve a conservative estimate so successive emplace doesn't
    // reallocate on the hot path. ~120 bytes/commit average.
    if (out.capacity() < out.size() + n / 100) {
        out.reserve(out.size() + n / 100);
    }

    for (qsizetype i = 0; i < n; ++i) {
        if (data[i] != kRecordSep) continue;
        // Got a record [recordStart, i).
        const qsizetype recLen = i - recordStart;
        if (recLen > 0) {
            GitCommitInfo info;
            if (parseRecord(data + recordStart, recLen, info)) {
                out.append(std::move(info));
            }
            // else: malformed record, dropped silently.
        }
        recordStart = i + 1;
    }

    // Carry over the residual after the last RS.
    if (recordStart == 0) {
        // No RS seen yet — keep m_tail as-is.
        return;
    }
    if (recordStart >= n) {
        m_tail.clear();
    } else {
        // Shift the residual to the start of m_tail. QByteArray::remove(0, k)
        // is O(n) memmove but acceptable here — residual is small (< 1 record).
        m_tail.remove(0, static_cast<int>(recordStart));
    }
}

void GitLogParser::finish(QVector<GitCommitInfo> &out)
{
    if (m_tail.isEmpty()) return;
    // Treat the tail as a final record (git's last line has no trailing RS
    // if we omit it — but we always include %x1e in the format, so the tail
    // SHOULD be empty here in practice. Defensive flush nonetheless.)
    GitCommitInfo info;
    if (parseRecord(m_tail.constData(), m_tail.size(), info)) {
        out.append(std::move(info));
    }
    m_tail.clear();
}

void GitLogParser::reset()
{
    m_tail.clear();
}
