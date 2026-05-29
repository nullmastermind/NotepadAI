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

#include <QtTest>
#include <QByteArray>
#include <QString>
#include <QStringList>

#include "FileIndexCache.h"
#include "WorkspaceFileEnumerator.h"

// Pure unit tests for the two unit-testable cores that need no live git:
//   - WorkspaceFileEnumerator::mergeAndDedupe (three-stream merge)
//   - FileIndexCache::build (arena layout + ASCII / non-ASCII folding)
class TestWorkspaceEnumeratorMerge : public QObject
{
    Q_OBJECT

private slots:
    void merge_exactDedupe();
    void merge_dropsSubmoduleDirectoryPlaceholder();
    void merge_deterministicSortedOrder();

    void arena_offsetsBoundEachPath();
    void arena_asciiFoldLowercases();
    void arena_nonAsciiFoldedOnTheFly();
};

void TestWorkspaceEnumeratorMerge::merge_exactDedupe()
{
    // The same path appears in tracked and in the untracked superproject stream
    // (e.g. a race / overlap); it must collapse to a single entry.
    const QStringList tracked = { QStringLiteral("src/a.cpp"), QStringLiteral("src/b.cpp") };
    const QStringList othersSuper = { QStringLiteral("src/a.cpp"), QStringLiteral("README.md") };
    const QStringList othersSub;

    const QStringList merged =
        WorkspaceFileEnumerator::mergeAndDedupe(tracked, othersSuper, othersSub);

    const QStringList expected = {
        QStringLiteral("README.md"),
        QStringLiteral("src/a.cpp"),
        QStringLiteral("src/b.cpp"),
    };
    QCOMPARE(merged, expected);
}

void TestWorkspaceEnumeratorMerge::merge_dropsSubmoduleDirectoryPlaceholder()
{
    // `git ls-files --others` in the superproject reports an untracked submodule
    // as a single DIRECTORY placeholder ("sub"), while the recursive submodule
    // stream yields its actual contents ("sub/..."). The placeholder must be
    // dropped so the directory is not double-listed. After sorting, a descendant
    // of E is contiguous after E, and E is dropped iff the next entry begins
    // with E + "/".
    const QStringList tracked = { QStringLiteral("sub/tracked.txt") };
    const QStringList othersSuper = { QStringLiteral("sub"), QStringLiteral("top.txt") };
    const QStringList othersSub = { QStringLiteral("sub/untracked.txt") };

    const QStringList merged =
        WorkspaceFileEnumerator::mergeAndDedupe(tracked, othersSuper, othersSub);

    const QStringList expected = {
        QStringLiteral("sub/tracked.txt"),
        QStringLiteral("sub/untracked.txt"),
        QStringLiteral("top.txt"),
    };
    QCOMPARE(merged, expected);
    // The bare placeholder must NOT survive.
    QVERIFY(!merged.contains(QStringLiteral("sub")));
}

void TestWorkspaceEnumeratorMerge::merge_deterministicSortedOrder()
{
    // Inputs in scrambled order across the streams produce the same sorted,
    // deduped output every time.
    const QStringList tracked = { QStringLiteral("z.txt"), QStringLiteral("a.txt") };
    const QStringList othersSuper = { QStringLiteral("m.txt") };
    const QStringList othersSub = { QStringLiteral("a.txt") };

    const QStringList first =
        WorkspaceFileEnumerator::mergeAndDedupe(tracked, othersSuper, othersSub);
    const QStringList second =
        WorkspaceFileEnumerator::mergeAndDedupe(tracked, othersSuper, othersSub);

    const QStringList expected = {
        QStringLiteral("a.txt"), QStringLiteral("m.txt"), QStringLiteral("z.txt"),
    };
    QCOMPARE(first, expected);
    QCOMPARE(second, expected);
}

void TestWorkspaceEnumeratorMerge::arena_offsetsBoundEachPath()
{
    const QStringList paths = {
        QStringLiteral("a.txt"),
        QStringLiteral("src/longer/name.cpp"),
        QStringLiteral("x"),
    };
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/true);

    const int n = cache.count();
    QCOMPARE(n, static_cast<int>(paths.size()));
    // N+1 offsets; first is 0, last is the arena size.
    QCOMPARE(static_cast<int>(cache.offsets.size()), n + 1);
    QCOMPARE(cache.offsets.first(), 0);
    QCOMPARE(cache.offsets.last(), static_cast<int32_t>(cache.foldedArena.size()));

    for (int i = 0; i < n; ++i) {
        const int start = cache.offsets.at(i);
        const int end = cache.offsets.at(i + 1);
        // A folded path is exactly its QChar count long.
        QCOMPARE(end - start, static_cast<int>(paths.at(i).size()));
        // The arena slice equals the folded form of the path.
        const QByteArray slice = cache.foldedArena.mid(start, end - start);
        QCOMPARE(slice, FileIndexCache::foldString(paths.at(i)));
    }
    QCOMPARE(cache.isGitRepo, true);
}

void TestWorkspaceEnumeratorMerge::arena_asciiFoldLowercases()
{
    const QStringList paths = { QStringLiteral("Src/Foo.CPP") };
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    // Arena is ASCII-lowercased; displayPaths preserves the original casing.
    QCOMPARE(cache.foldedArena, QByteArray("src/foo.cpp"));
    QCOMPARE(cache.displayPaths.at(0), QStringLiteral("Src/Foo.CPP"));
}

void TestWorkspaceEnumeratorMerge::arena_nonAsciiFoldedOnTheFly()
{
    // 'É' (U+00C9) and 'é' (U+00E9): both fold to the low byte of toLower (0xE9).
    // The path must be preserved in displayPaths and produce arena bytes
    // (never dropped/empty), one byte per QChar. Build the string from explicit
    // code units so the test does not depend on the source file's encoding or
    // any compiler /utf-8 flag.
    QString original;
    original.append(QChar(u'c'));
    original.append(QChar(u'a'));
    original.append(QChar(u'f'));
    original.append(QChar(char16_t(0x00C9)));   // 'É'
    original.append(QChar(u'/'));
    original.append(QChar(char16_t(0x00C9)));   // 'É'
    original.append(QChar(char16_t(0x00E9)));   // 'é'
    original.append(QChar(u'.'));
    original.append(QChar(u't'));
    original.append(QChar(u'x'));
    original.append(QChar(u't'));
    const QStringList paths = { original };
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    // Display preserved exactly.
    QCOMPARE(cache.displayPaths.at(0), original);

    // One arena byte per QChar; slice non-empty and equal to foldString().
    const int start = cache.offsets.at(0);
    const int end = cache.offsets.at(1);
    QCOMPARE(end - start, static_cast<int>(original.size()));
    QVERIFY(end > start);
    const QByteArray slice = cache.foldedArena.mid(start, end - start);
    QCOMPARE(slice, FileIndexCache::foldString(original));

    // ASCII region lowercased ("caf"), non-ASCII folded to toLower low byte.
    QCOMPARE(static_cast<unsigned char>(slice.at(0)), static_cast<unsigned char>('c'));
    QCOMPARE(static_cast<unsigned char>(slice.at(1)), static_cast<unsigned char>('a'));
    QCOMPARE(static_cast<unsigned char>(slice.at(2)), static_cast<unsigned char>('f'));
    QCOMPARE(static_cast<unsigned char>(slice.at(3)), static_cast<unsigned char>(0xE9));
    // The '/' separator stays put.
    QCOMPARE(static_cast<unsigned char>(slice.at(4)), static_cast<unsigned char>('/'));
    // 'É' and 'é' both fold to 0xE9.
    QCOMPARE(static_cast<unsigned char>(slice.at(5)), static_cast<unsigned char>(0xE9));
    QCOMPARE(static_cast<unsigned char>(slice.at(6)), static_cast<unsigned char>(0xE9));
}

QTEST_APPLESS_MAIN(TestWorkspaceEnumeratorMerge)

#include "test_workspace_enumerator_merge.moc"
