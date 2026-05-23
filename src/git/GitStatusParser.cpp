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

#include "GitStatusParser.h"

#include <QList>

#include <cstring>

namespace {

// Split on first N spaces, returning the trailing chunk verbatim. For porcelain
// v2 we count from the left because field counts are fixed per record type.
QList<QByteArray> splitFields(const QByteArray &line, int firstN)
{
    QList<QByteArray> out;
    int start = 0;
    int field = 0;
    for (int i = 0; i < line.size() && field < firstN; ++i) {
        if (line[i] == ' ') {
            out.append(line.mid(start, i - start));
            start = i + 1;
            ++field;
        }
    }
    out.append(line.mid(start));
    return out;
}

GitStatusEntry makeEntry(GitStatusEntry::Section sec,
                         GitStatusEntry::Change ch,
                         const QByteArray &rawPath,
                         const QByteArray &rawOrig,
                         bool stagedSide,
                         const QString &xy)
{
    GitStatusEntry e;
    e.section = sec;
    e.change = ch;
    e.relPath = QString::fromUtf8(rawPath);
    e.origRelPath = QString::fromUtf8(rawOrig);
    e.stagedSide = stagedSide;
    e.xy = xy;
    // Round-trip detect: if QString→UTF-8 doesn't round-trip the raw bytes, the
    // path was not valid UTF-8 (git emits it verbatim with core.quotepath=false).
    if (e.relPath.toUtf8() != rawPath) e.hasUnstableEncoding = true;
    if (!rawOrig.isEmpty() && e.origRelPath.toUtf8() != rawOrig) e.hasUnstableEncoding = true;
    return e;
}

// Decode the porcelain v2 `sub` field. Format: "<S|N><C|.><M|.><U|.>", where
// S=submodule, C=commit changed, M=tracked content modified, U=has untracked.
// For non-submodules ("N..."), all flags stay false.
void applySubFlags(GitStatusEntry &e, const QByteArray &subField)
{
    if (subField.size() < 1 || subField.at(0) != 'S') return;
    e.isSubmodule = true;
    if (subField.size() >= 3 && subField.at(2) == 'M') e.subHasModifiedContent = true;
}

} // namespace

bool GitStatusParser::isUnmerged(char x, char y)
{
    // From git status docs: any of DD AU UD UA DU AA UU
    if (x == 'U' || y == 'U') return true;
    if (x == 'A' && y == 'A') return true;
    if (x == 'D' && y == 'D') return true;
    return false;
}

GitStatusEntry::Change GitStatusParser::xyToChange(char x, char y, bool stagedSide)
{
    const char c = stagedSide ? x : y;
    switch (c) {
        case 'A': return GitStatusEntry::Added;
        case 'M': return GitStatusEntry::Modified;
        case 'D': return GitStatusEntry::Deleted;
        case 'R': return GitStatusEntry::Renamed;
        case 'C': return GitStatusEntry::Copied;
        case 'T': return GitStatusEntry::TypeChanged;
        case '?': return GitStatusEntry::Untracked_;
        case 'U': return GitStatusEntry::Unmerged;
        default:  return GitStatusEntry::Modified;
    }
}

GitStatusEntries GitStatusParser::parsePorcelainV2(const QByteArray &input,
                                                    Header *header)
{
    GitStatusEntries result;

    // Records are nul-separated. Renamed records contain TWO paths separated by nul,
    // so we walk byte-by-byte and parse record-by-record based on the leading marker.
    int i = 0;
    const int n = input.size();
    while (i < n) {
        // Locate end of this record's first segment (header + path), terminated by \0.
        int end = input.indexOf('\0', i);
        if (end < 0) end = n;
        QByteArray rec = input.mid(i, end - i);
        i = end + 1;
        if (rec.isEmpty()) continue;

        const char marker = rec[0];

        if (marker == '1') {
            // "1 XY sub mH mI mW hH hI path"
            // path is field index 8 (zero-based)
            QList<QByteArray> fields = splitFields(rec, 8);
            if (fields.size() < 9) continue;
            const QByteArray &xyB = fields.at(1);
            const QByteArray &subB = fields.at(2);
            const QByteArray &rawPath = fields.at(8);
            if (xyB.size() < 2) continue;
            const char x = xyB.at(0);
            const char y = xyB.at(1);
            const QString xy = QString::fromLatin1(xyB);

            if (x != '.' && x != ' ') {
                GitStatusEntry e = makeEntry(GitStatusEntry::Staged,
                                             xyToChange(x, y, true),
                                             rawPath, {}, true, xy);
                applySubFlags(e, subB);
                result.append(e);
            }
            if (y != '.' && y != ' ') {
                GitStatusEntry e = makeEntry(GitStatusEntry::Tracked,
                                             xyToChange(x, y, false),
                                             rawPath, {}, false, xy);
                applySubFlags(e, subB);
                result.append(e);
            }
        }
        else if (marker == '2') {
            // "2 XY sub mH mI mW hH hI X<score> path\0origPath"
            // path is field index 9; orig path follows after a nul terminator.
            QList<QByteArray> fields = splitFields(rec, 9);
            if (fields.size() < 10) continue;
            const QByteArray &xyB = fields.at(1);
            const QByteArray &subB = fields.at(2);
            const QByteArray &rawPath = fields.at(9);
            // Read the orig path (next nul-terminated segment).
            int origEnd = input.indexOf('\0', i);
            if (origEnd < 0) origEnd = n;
            const QByteArray rawOrig = input.mid(i, origEnd - i);
            i = origEnd + 1;
            if (xyB.size() < 2) continue;
            const char x = xyB.at(0);
            const char y = xyB.at(1);
            const QString xy = QString::fromLatin1(xyB);

            if (x != '.' && x != ' ') {
                GitStatusEntry e = makeEntry(GitStatusEntry::Staged,
                                             xyToChange(x, y, true),
                                             rawPath, rawOrig, true, xy);
                applySubFlags(e, subB);
                result.append(e);
            }
            if (y != '.' && y != ' ') {
                GitStatusEntry e = makeEntry(GitStatusEntry::Tracked,
                                             xyToChange(x, y, false),
                                             rawPath, rawOrig, false, xy);
                applySubFlags(e, subB);
                result.append(e);
            }
        }
        else if (marker == 'u') {
            // "u XY sub m1 m2 m3 mW h1 h2 h3 path"
            //   field index: 0  1  2  3  4  5  6  7  8  9  10
            // h2 = stage2 (ours), h3 = stage3 (theirs)
            QList<QByteArray> fields = splitFields(rec, 10);
            if (fields.size() < 11) continue;
            const QByteArray &xyB = fields.at(1);
            const QByteArray &rawPath = fields.at(10);
            const QString xy = QString::fromLatin1(xyB);
            GitStatusEntry e = makeEntry(GitStatusEntry::Conflicts,
                                         GitStatusEntry::Unmerged,
                                         rawPath, {}, false, xy);
            e.oursSha   = QString::fromLatin1(fields.at(8));
            e.theirsSha = QString::fromLatin1(fields.at(9));
            result.append(e);
        }
        else if (marker == '?') {
            // "? path"
            QByteArray rawPath = rec.mid(2);
            result.append(makeEntry(GitStatusEntry::Untracked,
                                    GitStatusEntry::Untracked_,
                                    rawPath, {}, false,
                                    QStringLiteral("??")));
        }
        else if (marker == '#' || marker == '!') {
            // '!' = ignored — drop. '#' = porcelain v2 branch header (only
            // present with --branch). Extract upstream + ahead/behind if a
            // caller asked for the header.
            if (marker == '#' && header) {
                // Field layout: "# branch.<key> <value...>"
                if (rec.startsWith(QByteArrayLiteral("# branch.upstream "))) {
                    header->hasUpstream = true;
                } else if (rec.startsWith(QByteArrayLiteral("# branch.ab "))) {
                    // "+<ahead> -<behind>" — parse two signed integers.
                    const QByteArray rest = rec.mid(qsizetype(strlen("# branch.ab ")));
                    int space = rest.indexOf(' ');
                    if (space > 0 && rest.at(0) == '+' && space + 1 < rest.size()
                        && rest.at(space + 1) == '-') {
                        const QByteArray ah = rest.mid(1, space - 1);
                        const QByteArray be = rest.mid(space + 2);
                        bool okA = false, okB = false;
                        const int a = ah.toInt(&okA);
                        const int b = be.toInt(&okB);
                        if (okA) header->ahead  = a;
                        if (okB) header->behind = b;
                        header->hasUpstream = true;
                    }
                }
            }
            continue;
        }
    }
    return result;
}
