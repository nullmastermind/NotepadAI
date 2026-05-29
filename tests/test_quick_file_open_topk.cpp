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

// Pure unit tests for the bounded top-K search: the min-heap keeps exactly
// `limit` survivors, ordering is stable/deterministic across runs, and match
// positions are computed only for survivors (and exactly pattern-length each).
class TestQuickFileOpenTopK : public QObject
{
    Q_OBJECT

private slots:
    void topK_keepsExactlyLimit();
    void tieBreak_deterministicAcrossRuns();
    void tieBreak_shorterPathThenSmallerIndex();
    void positions_presentAndPatternLengthForEverySurvivor();
};

void TestQuickFileOpenTopK::topK_keepsExactlyLimit()
{
    // 500 paths that all contain the subsequence "ab".
    QStringList paths;
    paths.reserve(500);
    for (int i = 0; i < 500; ++i)
        paths.append(QStringLiteral("dir%1/ab_file.txt").arg(i));
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    const QVector<QuickFileOpenCandidate> matches =
        QuickFileOpenDialog::computeMatches(cache, QStringLiteral("ab"), 200);
    QCOMPARE(matches.size(), 200);
}

void TestQuickFileOpenTopK::tieBreak_deterministicAcrossRuns()
{
    QStringList paths;
    paths.reserve(500);
    for (int i = 0; i < 500; ++i)
        paths.append(QStringLiteral("dir%1/ab_file.txt").arg(i));
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    const QVector<QuickFileOpenCandidate> a =
        QuickFileOpenDialog::computeMatches(cache, QStringLiteral("ab"), 200);
    const QVector<QuickFileOpenCandidate> b =
        QuickFileOpenDialog::computeMatches(cache, QStringLiteral("ab"), 200);

    QCOMPARE(a.size(), b.size());
    for (int i = 0; i < a.size(); ++i)
        QCOMPARE(a.at(i).index, b.at(i).index);
}

void TestQuickFileOpenTopK::tieBreak_shorterPathThenSmallerIndex()
{
    // All four match "z" at index 0 (same word-boundary + basename bonuses), so
    // their scores are identical. They differ only in pathLen and index:
    //   idx 0: "z.markdown"  len 10   (longest path, smallest index)
    //   idx 1: "z.txt"       len 5
    //   idx 2: "z.aaa"       len 5
    //   idx 3: "z.bbb"       len 5
    // Comparator = {score desc, shorter path, smaller index}, so the expected
    // best-first order is: idx1, idx2, idx3 (equal len → index asc), then idx0
    // last (longer path loses despite the smallest index — pathLen beats index).
    const QStringList paths = {
        QStringLiteral("z.markdown"),
        QStringLiteral("z.txt"),
        QStringLiteral("z.aaa"),
        QStringLiteral("z.bbb"),
    };
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    const QVector<QuickFileOpenCandidate> m =
        QuickFileOpenDialog::computeMatches(cache, QStringLiteral("z"), 200);
    QCOMPARE(m.size(), 4);

    // Shorter path first (z.txt over z.markdown), then smaller index.
    QCOMPARE(m.at(0).index, 1);   // z.txt
    QCOMPARE(m.at(1).index, 2);   // z.aaa
    QCOMPARE(m.at(2).index, 3);   // z.bbb
    QCOMPARE(m.at(3).index, 0);   // z.markdown (longest path, ranked last)
}

void TestQuickFileOpenTopK::positions_presentAndPatternLengthForEverySurvivor()
{
    QStringList paths;
    paths.reserve(300);
    for (int i = 0; i < 300; ++i)
        paths.append(QStringLiteral("pkg%1/abc_file.txt").arg(i));
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    const QString pattern = QStringLiteral("abc");
    const QVector<QuickFileOpenCandidate> m =
        QuickFileOpenDialog::computeMatches(cache, pattern, 200);

    // Bounded by limit; positions backtraced for every survivor, exactly
    // pattern-length each (one matched candidate index per pattern char).
    QVERIFY(m.size() <= 200);
    QVERIFY(!m.isEmpty());
    for (const QuickFileOpenCandidate &c : m) {
        QVERIFY(!c.positions.isEmpty());
        QCOMPARE(c.positions.size(), pattern.size());
    }
}

QTEST_APPLESS_MAIN(TestQuickFileOpenTopK)

#include "test_quick_file_open_topk.moc"
