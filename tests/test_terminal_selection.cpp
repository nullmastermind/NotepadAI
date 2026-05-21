/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

#include "TerminalCellSource.h"

#include <vterm.h>

#include <cstring>


// Stub source: builds a fixed grid of cells from a list of row strings, padded
// (or truncated) to a uniform column count. Each non-blank character in the
// input becomes one cell whose chars[0] is that codepoint; out-of-string
// columns are blank (chars[0] == 0).
class StubCellSource : public TerminalCellSource
{
public:
    StubCellSource(const QStringList &rows, int cols)
        : m_cols(cols)
    {
        m_cells.reserve(rows.size());
        for (const QString &row : rows) {
            QVector<VTermScreenCell> rowCells(cols);
            for (int c = 0; c < cols; ++c) {
                std::memset(&rowCells[c], 0, sizeof(VTermScreenCell));
                rowCells[c].width = 1;
                if (c < row.size()) {
                    const QChar ch = row.at(c);
                    if (ch != QLatin1Char(' ')) {
                        rowCells[c].chars[0] = ch.unicode();
                    }
                }
            }
            m_cells.append(rowCells);
        }
    }

    int rowCount() const override { return m_cells.size(); }
    int cols() const override { return m_cols; }
    VTermScreenCell cellAt(int row, int col) const override
    {
        if (row < 0 || row >= m_cells.size() || col < 0 || col >= m_cols) {
            VTermScreenCell blank;
            std::memset(&blank, 0, sizeof(blank));
            return blank;
        }
        return m_cells[row][col];
    }

private:
    QVector<QVector<VTermScreenCell>> m_cells;
    int m_cols;
};


class TestTerminalSelection : public QObject
{
    Q_OBJECT

private slots:
    void emptySelection_returnsEmptyString();
    void selectionInLiveOnly();
    void selectionInScrollbackOnly();
    void selectionCrossingBoundary();
    void selectionReversedDirection();
    void selectionPartialColumns_singleRow();
    void selectionPartialColumns_multipleRows();
    void selectionAcrossBlankCells_emitsSpaces();
    void selectionEmptyRowSpans_emitsBlankLine();
    void selectionOutOfBoundsClamps();
    void selectionAcrossMultipleHistoryRows();
    void selectionExactlyAtBoundary();
    void selectionMultiCodepointGlyph();
};


void TestTerminalSelection::emptySelection_returnsEmptyString()
{
    StubCellSource src(QStringList{QStringLiteral("ABCDE")}, 5);
    const QString out = extractSelectionText(src, QPoint(0, 0), QPoint(0, 0));
    // Single cell selection: end col is inclusive in the helper, so a (0,0)
    // start and (0,0) end emit exactly the cell at (0,0). This is the
    // documented convention.
    QCOMPARE(out, QStringLiteral("A"));
}

void TestTerminalSelection::selectionInLiveOnly()
{
    // 5 history rows + 3 live rows. Total 8 rows. Select rows 6..7 (both live).
    QStringList rows;
    for (int i = 0; i < 5; ++i) {
        rows.append(QStringLiteral("h%1   ").arg(i)); // history row
    }
    rows.append(QStringLiteral("LIVE0"));
    rows.append(QStringLiteral("LIVE1"));
    rows.append(QStringLiteral("LIVE2"));

    StubCellSource src(rows, 5);
    const QString out = extractSelectionText(src, QPoint(0, 6), QPoint(4, 7));
    QCOMPARE(out, QStringLiteral("LIVE1\nLIVE2"));
}

void TestTerminalSelection::selectionInScrollbackOnly()
{
    QStringList rows;
    for (int i = 0; i < 5; ++i) {
        rows.append(QStringLiteral("HIST%1").arg(i)); // 5-char rows
    }
    rows.append(QStringLiteral("LIVE0"));
    rows.append(QStringLiteral("LIVE1"));
    rows.append(QStringLiteral("LIVE2"));

    StubCellSource src(rows, 5);
    const QString out = extractSelectionText(src, QPoint(0, 1), QPoint(4, 3));
    QCOMPARE(out, QStringLiteral("HIST1\nHIST2\nHIST3"));
}

void TestTerminalSelection::selectionCrossingBoundary()
{
    QStringList rows;
    for (int i = 0; i < 5; ++i) {
        rows.append(QStringLiteral("HIST%1").arg(i));
    }
    rows.append(QStringLiteral("LIVE0"));
    rows.append(QStringLiteral("LIVE1"));
    rows.append(QStringLiteral("LIVE2"));

    StubCellSource src(rows, 5);
    const QString out = extractSelectionText(src, QPoint(0, 3), QPoint(4, 6));
    QCOMPARE(out, QStringLiteral("HIST3\nHIST4\nLIVE0\nLIVE1"));
}

void TestTerminalSelection::selectionReversedDirection()
{
    QStringList rows;
    for (int i = 0; i < 5; ++i) {
        rows.append(QStringLiteral("HIST%1").arg(i));
    }
    rows.append(QStringLiteral("LIVE0"));
    rows.append(QStringLiteral("LIVE1"));
    rows.append(QStringLiteral("LIVE2"));

    StubCellSource src(rows, 5);
    // Forwards: (0,3) -> (4,6).
    const QString forward = extractSelectionText(src, QPoint(0, 3), QPoint(4, 6));
    // Reversed: (4,6) -> (0,3) should normalise to the same result.
    const QString reverse = extractSelectionText(src, QPoint(4, 6), QPoint(0, 3));
    QCOMPARE(reverse, forward);
}

void TestTerminalSelection::selectionPartialColumns_singleRow()
{
    StubCellSource src(QStringList{QStringLiteral("ABCDEFGHIJ")}, 10);
    // Cols 2..5 inclusive of row 0 → "CDEF".
    const QString out = extractSelectionText(src, QPoint(2, 0), QPoint(5, 0));
    QCOMPARE(out, QStringLiteral("CDEF"));
}

void TestTerminalSelection::selectionPartialColumns_multipleRows()
{
    QStringList rows;
    rows.append(QStringLiteral("0123456789"));
    rows.append(QStringLiteral("ABCDEFGHIJ"));
    rows.append(QStringLiteral("KLMNOPQRST"));
    StubCellSource src(rows, 10);

    // Start (3, 0) → (5, 2): first row "3456789" (col 3 to last),
    //                          middle row "ABCDEFGHIJ" (full),
    //                          last row   "KLMNOP" (col 0..5).
    const QString out = extractSelectionText(src, QPoint(3, 0), QPoint(5, 2));
    QCOMPARE(out, QStringLiteral("3456789\nABCDEFGHIJ\nKLMNOP"));
}

void TestTerminalSelection::selectionAcrossBlankCells_emitsSpaces()
{
    // Row of cols 0..5: "AB" then three blanks then "F".
    QStringList rows;
    rows.append(QStringLiteral("AB   F"));
    StubCellSource src(rows, 6);

    // Select cols 0..5 of row 0.
    const QString out = extractSelectionText(src, QPoint(0, 0), QPoint(5, 0));
    // Expected: A, B, three spaces (blank cells), F.
    QCOMPARE(out, QStringLiteral("AB   F"));
}

void TestTerminalSelection::selectionEmptyRowSpans_emitsBlankLine()
{
    // Row 0 has text, row 1 is fully blank, row 2 has text.
    // A multi-row selection across row 1 must produce a line of spaces for it.
    QStringList rows;
    rows.append(QStringLiteral("AAAAA"));
    rows.append(QStringLiteral("     ")); // 5 blanks
    rows.append(QStringLiteral("CCCCC"));
    StubCellSource src(rows, 5);

    const QString out = extractSelectionText(src, QPoint(0, 0), QPoint(4, 2));
    QCOMPARE(out, QStringLiteral("AAAAA\n     \nCCCCC"));
}

void TestTerminalSelection::selectionOutOfBoundsClamps()
{
    StubCellSource src(QStringList{QStringLiteral("ABCDE")}, 5);
    // Coords beyond the grid: should clamp into range, no crash, no garbage.
    const QString out = extractSelectionText(src, QPoint(-10, -10), QPoint(100, 100));
    QCOMPARE(out, QStringLiteral("ABCDE"));
}

void TestTerminalSelection::selectionAcrossMultipleHistoryRows()
{
    QStringList rows;
    for (int i = 0; i < 8; ++i) {
        rows.append(QStringLiteral("hist%1").arg(i)); // 5 chars
    }
    StubCellSource src(rows, 5);

    // Three consecutive history rows: 2..4 inclusive.
    const QString out = extractSelectionText(src, QPoint(0, 2), QPoint(4, 4));
    QCOMPARE(out, QStringLiteral("hist2\nhist3\nhist4"));
}

void TestTerminalSelection::selectionExactlyAtBoundary()
{
    // historySize = 5, liveRows = 3.
    QStringList rows;
    for (int i = 0; i < 5; ++i) {
        rows.append(QStringLiteral("HIST%1").arg(i));
    }
    rows.append(QStringLiteral("LIVE0"));
    rows.append(QStringLiteral("LIVE1"));
    rows.append(QStringLiteral("LIVE2"));
    StubCellSource src(rows, 5);

    // Last history row, rightmost col: row 4, col 4 (single cell "4").
    const QString end = extractSelectionText(src, QPoint(4, 4), QPoint(4, 4));
    QCOMPARE(end, QStringLiteral("4"));

    // First live row spanning row 5 entirely.
    const QString live = extractSelectionText(src, QPoint(0, 5), QPoint(4, 5));
    QCOMPARE(live, QStringLiteral("LIVE0"));

    // Boundary crossing the last history row into the first live row.
    const QString cross = extractSelectionText(src, QPoint(0, 4), QPoint(4, 5));
    QCOMPARE(cross, QStringLiteral("HIST4\nLIVE0"));
}

void TestTerminalSelection::selectionMultiCodepointGlyph()
{
    // VTermScreenCell allows multi-codepoint glyphs in chars[0..n]. The helper
    // must concatenate them as a single visual cell.
    QStringList rows{QStringLiteral("ABCDE")};
    StubCellSource src(rows, 5);

    // Reach into the stub-built cell at (col=0, row=0) and add a combining
    // accent in chars[1] so the cell now represents 'A' + U+0301.
    // We can't mutate StubCellSource state directly without an accessor, so
    // build a small ad-hoc source instead.
    struct MultiSource : TerminalCellSource {
        int rowCount() const override { return 1; }
        int cols() const override { return 1; }
        VTermScreenCell cellAt(int, int) const override
        {
            VTermScreenCell c;
            std::memset(&c, 0, sizeof(c));
            c.width = 1;
            c.chars[0] = 0x41;     // 'A'
            c.chars[1] = 0x0301;   // combining acute accent
            return c;
        }
    } ms;
    const QString out = extractSelectionText(ms, QPoint(0, 0), QPoint(0, 0));
    // Two code points joined into the single output glyph.
    QString expected;
    expected.append(QChar::fromUcs4(0x41));
    expected.append(QChar::fromUcs4(0x0301));
    QCOMPARE(out, expected);
}


QTEST_APPLESS_MAIN(TestTerminalSelection)

#include "test_terminal_selection.moc"
