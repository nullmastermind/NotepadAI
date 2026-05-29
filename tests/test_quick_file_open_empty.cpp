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
#include <QStringList>
#include <QVector>

#include "FileIndexCache.h"
#include "dialogs/QuickFileOpenDialog.h"

// Pure unit tests for the empty-query ordering: MRU files (present in the
// snapshot) first in MRU order, then the remaining files in enumeration order,
// truncated to the limit. No scoring, no heap.
class TestQuickFileOpenEmpty : public QObject
{
    Q_OBJECT

private slots:
    void mruFirstThenEnumerationOrder();
    void mruEntriesNotInSnapshotAreSkipped();
    void emptyMru_isEnumerationOrderTruncated();
    void truncatedToLimit();

private:
    static QStringList toPaths(const FileIndexCache &cache,
                               const QVector<QuickFileOpenCandidate> &v);
};

QStringList TestQuickFileOpenEmpty::toPaths(const FileIndexCache &cache,
                                            const QVector<QuickFileOpenCandidate> &v)
{
    QStringList out;
    out.reserve(v.size());
    for (const QuickFileOpenCandidate &c : v)
        out.append(cache.displayPaths.at(c.index));
    return out;
}

void TestQuickFileOpenEmpty::mruFirstThenEnumerationOrder()
{
    const QStringList paths = {
        QStringLiteral("a.txt"),   // 0
        QStringLiteral("b.txt"),   // 1
        QStringLiteral("c.txt"),   // 2
        QStringLiteral("d.txt"),   // 3
    };
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    // MRU picks c then a (reverse of enumeration); the rest follow in
    // enumeration order (b, d).
    const QStringList mru = { QStringLiteral("c.txt"), QStringLiteral("a.txt") };
    const QVector<QuickFileOpenCandidate> out =
        QuickFileOpenDialog::computeEmpty(cache, mru, 200);

    const QStringList expected = {
        QStringLiteral("c.txt"), QStringLiteral("a.txt"),
        QStringLiteral("b.txt"), QStringLiteral("d.txt"),
    };
    QCOMPARE(toPaths(cache, out), expected);
}

void TestQuickFileOpenEmpty::mruEntriesNotInSnapshotAreSkipped()
{
    const QStringList paths = {
        QStringLiteral("a.txt"),   // 0
        QStringLiteral("b.txt"),   // 1
    };
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    // "ghost.txt" is not in the snapshot → skipped; "b.txt" surfaces first.
    const QStringList mru = { QStringLiteral("ghost.txt"), QStringLiteral("b.txt") };
    const QVector<QuickFileOpenCandidate> out =
        QuickFileOpenDialog::computeEmpty(cache, mru, 200);

    const QStringList expected = { QStringLiteral("b.txt"), QStringLiteral("a.txt") };
    QCOMPARE(toPaths(cache, out), expected);
}

void TestQuickFileOpenEmpty::emptyMru_isEnumerationOrderTruncated()
{
    const QStringList paths = {
        QStringLiteral("a.txt"), QStringLiteral("b.txt"),
        QStringLiteral("c.txt"), QStringLiteral("d.txt"),
    };
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    const QVector<QuickFileOpenCandidate> out =
        QuickFileOpenDialog::computeEmpty(cache, QStringList(), 2);

    // Pure enumeration order, truncated to the limit.
    const QStringList expected = { QStringLiteral("a.txt"), QStringLiteral("b.txt") };
    QCOMPARE(toPaths(cache, out), expected);
}

void TestQuickFileOpenEmpty::truncatedToLimit()
{
    QStringList paths;
    paths.reserve(50);
    for (int i = 0; i < 50; ++i)
        paths.append(QStringLiteral("f%1.txt").arg(i));
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    // MRU longer than limit: result is still capped at the limit, MRU-first.
    const QStringList mru = { QStringLiteral("f40.txt"), QStringLiteral("f41.txt"),
                              QStringLiteral("f42.txt") };
    const QVector<QuickFileOpenCandidate> out =
        QuickFileOpenDialog::computeEmpty(cache, mru, 2);

    QCOMPARE(out.size(), 2);
    QCOMPARE(cache.displayPaths.at(out.at(0).index), QStringLiteral("f40.txt"));
    QCOMPARE(cache.displayPaths.at(out.at(1).index), QStringLiteral("f41.txt"));
}

QTEST_APPLESS_MAIN(TestQuickFileOpenEmpty)

#include "test_quick_file_open_empty.moc"
