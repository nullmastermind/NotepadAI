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

#ifndef GIT_COMMIT_DETAIL_H
#define GIT_COMMIT_DETAIL_H

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>

// Parsed output of `git -c color.ui=never show --first-parent --no-color -p <sha>`
// plus the numstat side-channel for binary detection.
//
// `diffBytes` is the unified diff portion only (everything after the message
// trailing blank line). The header fields (author, message, etc.) are extracted
// from `--no-patch --format=...` and stored as separate fields so the renderer
// doesn't have to re-parse the show preamble.
//
// `truncated` is true when the producing process exceeded the configured
// stdout cap (default 5 MB); the renderer surfaces a footer banner offering
// re-fetch without cap.
struct GitCommitDetail {
    QByteArray sha;                 // full 40 hex
    QByteArray parents;             // space-separated parent SHAs
    QByteArray authorName;
    QByteArray authorEmail;
    qint64     authorTime = 0;      // author epoch seconds
    QByteArray committerName;
    QByteArray committerEmail;
    qint64     commitTime = 0;      // committer epoch seconds
    QByteArray subject;             // first line of body
    QByteArray body;                // full message (subject + remainder)

    // Trailers parsed from the body cluster at the end of the message
    // (Linux-kernel convention: trailing lines matching ^[A-Z][A-Za-z-]+: .+).
    struct Trailer {
        QByteArray key;             // e.g. "Co-Authored-By"
        QByteArray value;           // e.g. "Alice <alice@example.com>"
    };
    QVector<Trailer> trailers;

    // Per-file numstat. isBinary populated from `-\t-\t<path>` records.
    struct FileStat {
        QByteArray path;
        qint32     added = -1;
        qint32     deleted = -1;
        bool       isBinary = false;
    };
    QVector<FileStat> fileStats;

    // Raw unified diff bytes (from --first-parent for merge commits).
    QByteArray diffBytes;
    bool       truncated = false;

    bool isMerge() const {
        if (parents.isEmpty()) return false;
        for (char c : parents) if (c == ' ') return true;
        return false;
    }

    bool isEmpty() const { return sha.isEmpty(); }
};

#endif // GIT_COMMIT_DETAIL_H
