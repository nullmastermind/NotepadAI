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

#ifndef RECURSIVE_TREE_WATCHER_LOGIC_H
#define RECURSIVE_TREE_WATCHER_LOGIC_H

// Pure, Win32-free decision logic for the recursive working-tree watcher,
// split out so it can be unit-tested without pulling in <qt_windows.h>. Uses
// wchar_t/std::wstring (== WCHAR/std::wstring on Windows) so the worker can call
// these directly against FILE_NOTIFY_INFORMATION::FileName. Everything here is
// header-only and free of side effects.
//
// Normalisation contract (shared with RecursiveTreeWatcher::setIgnoredPrefixes):
// prefixes are lowercase, '/'-separated, with no leading/trailing slash. Inputs
// here are normalised on the fly to the same shape so matching is case- and
// separator-insensitive, matching Windows filesystem semantics.

#include <cwctype>
#include <string>
#include <vector>

namespace rtwlogic {

// True when the change path (root-relative, raw FILE_NOTIFY_INFORMATION name,
// back-slash separated) lies entirely within one of the gitignored prefixes,
// OR exactly equals an ignored entry (a single ignored file like ".env").
inline bool isUnderIgnored(const wchar_t *name, int len,
                           const std::vector<std::wstring> &prefixes)
{
    if (prefixes.empty() || len <= 0)
        return false;
    std::wstring norm;
    norm.reserve(static_cast<std::size_t>(len));
    for (int i = 0; i < len; ++i) {
        const wchar_t c = name[i];
        norm.push_back(c == L'\\' ? L'/' : static_cast<wchar_t>(std::towlower(c)));
    }
    for (const std::wstring &p : prefixes) {
        if (p.empty() || norm.size() < p.size())
            continue;
        if (norm.compare(0, p.size(), p) != 0)
            continue;
        // Exact match (ignored file) or the next char is a separator (the
        // change sits under an ignored directory). The separator guard stops
        // "build" from wrongly matching "buildsystem/".
        if (norm.size() == p.size() || norm[p.size()] == L'/')
            return true;
    }
    return false;
}

// Lowercase + '/'-normalise a single name component. Mirrors the producer-side
// normalisation so comparisons against the prefix set are apples-to-apples.
inline std::wstring normName(const std::wstring &raw)
{
    std::wstring out;
    out.reserve(raw.size());
    for (wchar_t c : raw)
        out.push_back(c == L'\\' ? L'/' : static_cast<wchar_t>(std::towlower(c)));
    return out;
}

// Given the names of the root's immediate child directories and the normalised
// ignored-prefix set, return the subset that SHOULD get a recursive watch:
// drop ".git" and any dir that is itself an ignored prefix (exact match) or
// sits under one. The result names are normalised (lowercased, '/'-separated).
inline std::vector<std::wstring> deriveWatchedTopLevelDirs(
    const std::vector<std::wstring> &topLevelDirNames,
    const std::vector<std::wstring> &prefixes)
{
    std::vector<std::wstring> out;
    out.reserve(topLevelDirNames.size());
    for (const std::wstring &raw : topLevelDirNames) {
        const std::wstring n = normName(raw);
        if (n.empty() || n == L".git")
            continue;
        if (isUnderIgnored(n.c_str(), static_cast<int>(n.size()), prefixes))
            continue;
        out.push_back(n);
    }
    return out;
}

} // namespace rtwlogic

#endif // RECURSIVE_TREE_WATCHER_LOGIC_H
