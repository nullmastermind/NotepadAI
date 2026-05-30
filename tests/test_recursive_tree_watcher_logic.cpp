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

// Unit tests for the pure decision logic behind the selective-watch tree
// watcher: isUnderIgnored() (per-event filter, incl. exact-file ignore) and
// deriveWatchedTopLevelDirs() (which top-level dirs get a recursive watch).
// Win32-free — compiles and runs on every platform.

#include <QtTest>

#include "git/RecursiveTreeWatcherLogic.h"

#include <algorithm>
#include <string>
#include <vector>

using rtwlogic::deriveWatchedTopLevelDirs;
using rtwlogic::isUnderIgnored;

namespace {
bool under(const std::wstring &name, const std::vector<std::wstring> &prefixes)
{
    return isUnderIgnored(name.c_str(), static_cast<int>(name.size()), prefixes);
}
} // namespace

class TestRecursiveTreeWatcherLogic : public QObject
{
    Q_OBJECT

private slots:
    // --- isUnderIgnored -------------------------------------------------

    void emptyPrefixesNeverMatches()
    {
        QVERIFY(!under(L"node_modules/x", {}));
        QVERIFY(!under(L"", {}));
    }

    void changeUnderIgnoredDirIsFiltered()
    {
        const std::vector<std::wstring> p = {L"node_modules"};
        QVERIFY(under(L"node_modules/foo/bar.js", p));
        QVERIFY(under(L"node_modules\\foo\\bar.js", p)); // back-slash form
    }

    void exactIgnoredFileIsFiltered()
    {
        // A single ignored file at repo root (e.g. ".env") — produced by
        // `git ls-files --others --ignored --directory` WITHOUT a trailing
        // slash. Must be filtered by the exact-match branch.
        const std::vector<std::wstring> p = {L".env"};
        QVERIFY(under(L".env", p));
        QVERIFY(!under(L".envrc", p));      // not an exact match, no separator
        QVERIFY(!under(L".environment", p));
    }

    void separatorGuardPreventsPrefixBleed()
    {
        // "build" must not swallow "buildsystem/..." — the char after the
        // matched prefix has to be a separator (or end-of-string).
        const std::vector<std::wstring> p = {L"build"};
        QVERIFY(under(L"build", p));
        QVERIFY(under(L"build/output.o", p));
        QVERIFY(!under(L"buildsystem/x", p));
        QVERIFY(!under(L"builds/x", p));
    }

    void caseInsensitiveMatch()
    {
        const std::vector<std::wstring> p = {L"node_modules"};
        QVERIFY(under(L"Node_Modules/x", p));
        QVERIFY(under(L"NODE_MODULES\\X", p));
    }

    void nonIgnoredPathPassesThrough()
    {
        const std::vector<std::wstring> p = {L"node_modules", L"build"};
        QVERIFY(!under(L"src/main.cpp", p));
        QVERIFY(!under(L"README.md", p));
    }

    void multiplePrefixesAnyMatch()
    {
        const std::vector<std::wstring> p = {L"node_modules", L"dist", L".cache"};
        QVERIFY(under(L"dist/app.js", p));
        QVERIFY(under(L".cache/x", p));
        QVERIFY(!under(L"distribute/x", p)); // separator guard again
    }

    // --- deriveWatchedTopLevelDirs --------------------------------------

    void derivesNonIgnoredDirsOnly()
    {
        const std::vector<std::wstring> dirs = {L"src", L"node_modules", L"build", L"docs"};
        const std::vector<std::wstring> prefixes = {L"node_modules", L"build"};
        const auto watched = deriveWatchedTopLevelDirs(dirs, prefixes);
        QCOMPARE(watched.size(), std::size_t{2});
        QVERIFY(std::find(watched.begin(), watched.end(), std::wstring(L"src")) != watched.end());
        QVERIFY(std::find(watched.begin(), watched.end(), std::wstring(L"docs")) != watched.end());
        QVERIFY(std::find(watched.begin(), watched.end(), std::wstring(L"node_modules")) == watched.end());
        QVERIFY(std::find(watched.begin(), watched.end(), std::wstring(L"build")) == watched.end());
    }

    void alwaysExcludesDotGit()
    {
        const std::vector<std::wstring> dirs = {L".git", L"src"};
        const auto watched = deriveWatchedTopLevelDirs(dirs, {});
        QCOMPARE(watched.size(), std::size_t{1});
        QCOMPARE(watched.front(), std::wstring(L"src"));
    }

    void normalisesResultNames()
    {
        const std::vector<std::wstring> dirs = {L"Src", L"DOCS"};
        const auto watched = deriveWatchedTopLevelDirs(dirs, {});
        QCOMPARE(watched.size(), std::size_t{2});
        // Result names are lowercased so later comparisons against the watched
        // set (also lowercased) line up.
        QCOMPARE(watched[0], std::wstring(L"src"));
        QCOMPARE(watched[1], std::wstring(L"docs"));
    }

    void emptyPrefixesWatchesEverythingButDotGit()
    {
        const std::vector<std::wstring> dirs = {L"a", L"b", L".git", L"c"};
        const auto watched = deriveWatchedTopLevelDirs(dirs, {});
        QCOMPARE(watched.size(), std::size_t{3});
    }
};

QTEST_MAIN(TestRecursiveTreeWatcherLogic)
#include "test_recursive_tree_watcher_logic.moc"
