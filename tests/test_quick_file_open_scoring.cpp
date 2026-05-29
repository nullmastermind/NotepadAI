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

#include <limits>

#include "FileIndexCache.h"
#include "dialogs/QuickFileOpenDialog.h"

// Pure unit tests for the QuickFileOpenDialog scoring core. All methods under
// test are static and side-effect-free, so no live dialog / git is needed.
class TestQuickFileOpenScoring : public QObject
{
    Q_OBJECT

private slots:
    void consecutiveBonus_raisesScore();
    void wordBoundaryBonus_raisesScore();
    void camelCaseBonus_raisesScore();

    void patternLongerThanCandidate_isZero();

    void smartCase_detection();
    void smartCase_uppercaseMatchesCaseSensitively();

    void basenameBonus_filenameOutranksPathScattered();
    void basenameBonus_thresholdDominatesEveryNonBasename();
};

void TestQuickFileOpenScoring::consecutiveBonus_raisesScore()
{
    // Both candidates: length 4, no separator (identical basename region), so
    // the only difference is whether the matched chars are adjacent.
    QVERIFY(QuickFileOpenDialog::score(QStringLiteral("ab"), QStringLiteral("abxx"))
            > QuickFileOpenDialog::score(QStringLiteral("ab"), QStringLiteral("axbx")));
}

void TestQuickFileOpenScoring::wordBoundaryBonus_raisesScore()
{
    // '_' is a word-boundary separator but (unlike '/') does not move the
    // basename start, so the two candidates differ only by the boundary bonus.
    QVERIFY(QuickFileOpenDialog::score(QStringLiteral("b"), QStringLiteral("a_b"))
            > QuickFileOpenDialog::score(QStringLiteral("b"), QStringLiteral("axb")));
}

void TestQuickFileOpenScoring::camelCaseBonus_raisesScore()
{
    // Same candidate "FooBar"; an uppercase query matches the capitals
    // case-sensitively and earns the camelCase bonus on each, so it outscores
    // the lowercase (case-insensitive) query that matches the same positions
    // without the camel bonus.
    QVERIFY(QuickFileOpenDialog::score(QStringLiteral("FB"), QStringLiteral("FooBar"))
            > QuickFileOpenDialog::score(QStringLiteral("fb"), QStringLiteral("FooBar")));
}

void TestQuickFileOpenScoring::patternLongerThanCandidate_isZero()
{
    QCOMPARE(QuickFileOpenDialog::score(QStringLiteral("abcdef"), QStringLiteral("ab")), 0);
}

void TestQuickFileOpenScoring::smartCase_detection()
{
    QCOMPARE(QuickFileOpenDialog::isCaseSensitivePattern(QStringLiteral("abc")), false);
    QCOMPARE(QuickFileOpenDialog::isCaseSensitivePattern(QStringLiteral("Abc")), true);
    QCOMPARE(QuickFileOpenDialog::isCaseSensitivePattern(QStringLiteral("aBc")), true);
}

void TestQuickFileOpenScoring::smartCase_uppercaseMatchesCaseSensitively()
{
    // Uppercase query "FB" is case-sensitive: it matches the capitals in
    // "FooBar" (subsequence F..B) but NOT the all-lowercase "foobar".
    QVERIFY(QuickFileOpenDialog::score(QStringLiteral("FB"), QStringLiteral("FooBar")) > 0);
    QCOMPARE(QuickFileOpenDialog::score(QStringLiteral("FB"), QStringLiteral("foobar")), 0);

    // Lowercase query "fb" is case-insensitive and matches both.
    QVERIFY(QuickFileOpenDialog::score(QStringLiteral("fb"), QStringLiteral("FooBar")) > 0);
    QVERIFY(QuickFileOpenDialog::score(QStringLiteral("fb"), QStringLiteral("foobar")) > 0);
}

void TestQuickFileOpenScoring::basenameBonus_filenameOutranksPathScattered()
{
    // "bar" lands entirely inside the basename of the first path, but is
    // path-scattered (and only partially in the basename) for the second.
    QVERIFY(QuickFileOpenDialog::score(QStringLiteral("bar"), QStringLiteral("src/foo/bar.cpp"))
            > QuickFileOpenDialog::score(QStringLiteral("bar"), QStringLiteral("bar/x/y.cpp")));
}

void TestQuickFileOpenScoring::basenameBonus_thresholdDominatesEveryNonBasename()
{
    // A basename subsequence match must outrank ANY path-scattered match.
    // Build a snapshot containing one basename-match path and several
    // path-scattered ones, then assert via computeMatches that the basename
    // path ranks first AND that every basename match strictly beats every
    // non-basename match (the threshold dominates).
    const QStringList paths = {
        QStringLiteral("b/a/r/zzz.txt"),       // 'b','a','r' scattered across dirs
        QStringLiteral("alpha/beta/rome.h"),   // b..a..r scattered, not in basename
        QStringLiteral("src/deep/bar.cpp"),    // "bar" fully inside basename
        QStringLiteral("xbxaxrx/file.md"),     // scattered in a dir component
    };
    const FileIndexCache cache = FileIndexCache::build(paths, /*isGitRepo=*/false);

    const QVector<QuickFileOpenCandidate> matches =
        QuickFileOpenDialog::computeMatches(cache, QStringLiteral("bar"), 200);

    QVERIFY(!matches.isEmpty());
    // The basename match ("src/deep/bar.cpp") ranks first.
    QCOMPARE(cache.displayPaths.at(matches.first().index), QStringLiteral("src/deep/bar.cpp"));

    // Separate survivors into basename-matches vs the rest and assert the
    // threshold gives a hard min(basename) > max(non-basename) guarantee.
    qint32 minBasename = std::numeric_limits<qint32>::max();
    qint32 maxNonBasename = std::numeric_limits<qint32>::min();
    for (const QuickFileOpenCandidate &c : matches) {
        const QString &p = cache.displayPaths.at(c.index);
        const bool basenameMatch = (p == QStringLiteral("src/deep/bar.cpp"));
        if (basenameMatch)
            minBasename = qMin(minBasename, c.score);
        else
            maxNonBasename = qMax(maxNonBasename, c.score);
    }
    QVERIFY(minBasename != std::numeric_limits<qint32>::max());   // at least one basename match
    if (maxNonBasename != std::numeric_limits<qint32>::min())
        QVERIFY(minBasename > maxNonBasename);
}

QTEST_APPLESS_MAIN(TestQuickFileOpenScoring)

#include "test_quick_file_open_scoring.moc"
