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

#ifndef FILE_INDEX_CACHE_H
#define FILE_INDEX_CACHE_H

#include <QByteArray>
#include <QChar>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

// Immutable POD snapshot of a workspace's file list, laid out for a
// cache-friendly fuzzy-search hot path.
//
// Memory layout (the "arena"):
//   - foldedArena: every relative path, ASCII-lowercased, concatenated into
//     ONE contiguous byte buffer (1 byte per QChar). ASCII A-Z is folded to
//     a-z; non-ASCII QChars are folded via QChar::toLower() and truncated to
//     the low byte (deterministic, never dropped). The query is folded with
//     the same foldByte()/foldString() so a byte-level subsequence scan over
//     this buffer is a correct case-insensitive match.
//   - offsets: N+1 entries. Path i occupies foldedArena[offsets[i] ..
//     offsets[i+1]). offsets[0] == 0, offsets[N] == foldedArena.size().
//     A path's folded length is exactly its QChar count, so offsets bound
//     each path precisely.
//   - displayPaths: the original (un-folded) strings, used for display and
//     for case-SENSITIVE smart-case comparison (compared against the UTF-16
//     string, bypassing the byte arena).
//
// The struct is published as std::shared_ptr<const FileIndexCache> and never
// mutated in place; revalidation builds a fresh instance and swaps the pointer
// (RCU-lite), so search reads are lock-free.
struct FileIndexCache
{
    QByteArray foldedArena;       // all paths ASCII-lowercased, concatenated
    QVector<int32_t> offsets;     // N+1 entries; offsets[i]..offsets[i+1] = path i
    QStringList displayPaths;     // original strings (display + smart-case)
    bool isGitRepo = false;

    int32_t count() const { return static_cast<int32_t>(displayPaths.size()); }

    // Fold a single QChar to one arena byte. ASCII fast path (the common case
    // for source paths) is a branch-predictable add; non-ASCII falls through
    // to QChar::toLower() truncated to the low byte.
    static inline char foldByte(QChar c)
    {
        const char16_t u = c.unicode();
        if (u < 0x80) {
            // ASCII: lowercase A-Z, pass everything else through unchanged.
            char b = static_cast<char>(u);
            if (b >= 'A' && b <= 'Z')
                b = static_cast<char>(b - 'A' + 'a');
            return b;
        }
        return static_cast<char>(QChar::toLower(static_cast<uint>(u)) & 0xFF);
    }

    // Fold an entire string to its byte representation (used for the query).
    static QByteArray foldString(const QString &s)
    {
        QByteArray out;
        out.resize(s.size());
        const QChar *src = s.constData();
        char *dst = out.data();
        for (qsizetype i = 0, n = s.size(); i < n; ++i)
            dst[i] = foldByte(src[i]);
        return out;
    }

    // Build a populated cache from a list of workspace-relative paths.
    // ASCII fast path with on-the-fly fold for non-ASCII; callable from tests
    // without git. The arena holds exactly one byte per QChar of every path,
    // so offsets bound each path exactly.
    static FileIndexCache build(const QStringList &relativePaths, bool isGitRepo)
    {
        FileIndexCache cache;
        cache.isGitRepo = isGitRepo;
        cache.displayPaths = relativePaths;

        const int32_t n = static_cast<int32_t>(relativePaths.size());
        cache.offsets.resize(n + 1);

        qsizetype total = 0;
        for (const QString &p : relativePaths)
            total += p.size();

        cache.foldedArena.resize(total);
        char *dst = cache.foldedArena.data();

        int32_t cursor = 0;
        for (int32_t i = 0; i < n; ++i) {
            cache.offsets[i] = cursor;
            const QString &p = relativePaths.at(i);
            const QChar *src = p.constData();
            const qsizetype len = p.size();
            for (qsizetype j = 0; j < len; ++j)
                dst[cursor + j] = foldByte(src[j]);
            cursor += static_cast<int32_t>(len);
        }
        cache.offsets[n] = cursor;

        return cache;
    }
};

#endif // FILE_INDEX_CACHE_H
