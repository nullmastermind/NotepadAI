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
#include <QVector>

#include "ScrollbackBuffer.h"

#include <vterm.h>

#include <cstring>


// Helper: build a line of cells whose first character codepoint is the row tag
// and whose column index is encoded in attrs.bold so we can detect padding.
static QVector<VTermScreenCell> makeLine(int cols, uint32_t tag)
{
    QVector<VTermScreenCell> row(cols);
    for (int i = 0; i < cols; ++i) {
        std::memset(&row[i], 0, sizeof(VTermScreenCell));
        row[i].chars[0] = tag + static_cast<uint32_t>(i);
        row[i].width = 1;
    }
    return row;
}


class TestScrollbackBuffer : public QObject
{
    Q_OBJECT

private slots:
    void empty_sizeAndIsEmpty();
    void pushLine_appendsToBottom();
    void pushLine_overCapacity_dropsOldest();
    void popLine_takesMostRecent();
    void popLine_emptyReturnsFalse();
    void popLine_intoNarrowerCols_truncates();
    void popLine_intoWiderCols_padsBlankCells();
    void lineAt_outOfRange_returnsFalse();
    void lineAt_returnsByHistoryIndex();
    void lineAt_intoWiderCols_padsBlankCells();
    void lineAt_intoNarrowerCols_truncates();
    void clear_emptiesBuffer();
    void setMaxLines_shrinks_dropsOldest();
    void pushLine_returnsEvictionCount();
};


void TestScrollbackBuffer::empty_sizeAndIsEmpty()
{
    ScrollbackBuffer sb(100);
    QCOMPARE(sb.size(), 0);
    QVERIFY(sb.isEmpty());
    QCOMPARE(sb.maxLines(), 100);
}

void TestScrollbackBuffer::pushLine_appendsToBottom()
{
    ScrollbackBuffer sb(100);
    const auto line0 = makeLine(5, 100);
    const auto line1 = makeLine(5, 200);
    sb.pushLine(5, line0.constData());
    sb.pushLine(5, line1.constData());
    QCOMPARE(sb.size(), 2);
    QVERIFY(!sb.isEmpty());

    QVector<VTermScreenCell> out(5);
    QVERIFY(sb.lineAt(0, 5, out.data()));
    QCOMPARE(out[0].chars[0], 100u);
    QCOMPARE(out[2].chars[0], 102u);

    QVERIFY(sb.lineAt(1, 5, out.data()));
    QCOMPARE(out[0].chars[0], 200u);
    QCOMPARE(out[4].chars[0], 204u);
}

void TestScrollbackBuffer::pushLine_overCapacity_dropsOldest()
{
    ScrollbackBuffer sb(3);
    for (uint32_t i = 0; i < 5; ++i) {
        const auto row = makeLine(2, 1000u + i * 10);
        sb.pushLine(2, row.constData());
    }
    QCOMPARE(sb.size(), 3);

    // Oldest retained line should be the third push (tag 1020), then 1030, then 1040.
    QVector<VTermScreenCell> out(2);
    QVERIFY(sb.lineAt(0, 2, out.data()));
    QCOMPARE(out[0].chars[0], 1020u);
    QVERIFY(sb.lineAt(2, 2, out.data()));
    QCOMPARE(out[0].chars[0], 1040u);
}

void TestScrollbackBuffer::popLine_takesMostRecent()
{
    ScrollbackBuffer sb(10);
    sb.pushLine(3, makeLine(3, 10).constData());
    sb.pushLine(3, makeLine(3, 20).constData());

    QVector<VTermScreenCell> out(3);
    QVERIFY(sb.popLine(3, out.data()));
    QCOMPARE(out[0].chars[0], 20u);
    QCOMPARE(sb.size(), 1);

    QVERIFY(sb.popLine(3, out.data()));
    QCOMPARE(out[0].chars[0], 10u);
    QCOMPARE(sb.size(), 0);
}

void TestScrollbackBuffer::popLine_emptyReturnsFalse()
{
    ScrollbackBuffer sb(10);
    QVector<VTermScreenCell> out(3);
    QVERIFY(!sb.popLine(3, out.data()));
}

void TestScrollbackBuffer::popLine_intoNarrowerCols_truncates()
{
    ScrollbackBuffer sb(10);
    sb.pushLine(8, makeLine(8, 1).constData()); // chars 1..8 (tag 1)

    QVector<VTermScreenCell> out(3);
    QVERIFY(sb.popLine(3, out.data()));
    QCOMPARE(out[0].chars[0], 1u);
    QCOMPARE(out[1].chars[0], 2u);
    QCOMPARE(out[2].chars[0], 3u);
}

void TestScrollbackBuffer::popLine_intoWiderCols_padsBlankCells()
{
    ScrollbackBuffer sb(10);
    sb.pushLine(3, makeLine(3, 1).constData()); // chars 1..3

    QVector<VTermScreenCell> out(6);
    // pre-fill out with sentinel so we can detect that pad cells were actually written.
    for (int i = 0; i < 6; ++i) { out[i].chars[0] = 0xdeadbeef; }
    QVERIFY(sb.popLine(6, out.data()));
    QCOMPARE(out[0].chars[0], 1u);
    QCOMPARE(out[2].chars[0], 3u);
    QCOMPARE(out[3].chars[0], 0u); // blanked
    QCOMPARE(out[5].chars[0], 0u); // blanked
}

void TestScrollbackBuffer::lineAt_outOfRange_returnsFalse()
{
    ScrollbackBuffer sb(10);
    sb.pushLine(3, makeLine(3, 1).constData());
    QVector<VTermScreenCell> out(3);
    QVERIFY(!sb.lineAt(-1, 3, out.data()));
    QVERIFY(!sb.lineAt(1, 3, out.data())); // only index 0 exists
    QVERIFY(!sb.lineAt(100, 3, out.data()));
}

void TestScrollbackBuffer::lineAt_returnsByHistoryIndex()
{
    ScrollbackBuffer sb(10);
    for (uint32_t i = 0; i < 4; ++i) {
        sb.pushLine(2, makeLine(2, 100u + i * 10).constData());
    }
    QVector<VTermScreenCell> out(2);
    QVERIFY(sb.lineAt(0, 2, out.data()));
    QCOMPARE(out[0].chars[0], 100u);
    QVERIFY(sb.lineAt(3, 2, out.data()));
    QCOMPARE(out[0].chars[0], 130u);
}

void TestScrollbackBuffer::lineAt_intoWiderCols_padsBlankCells()
{
    ScrollbackBuffer sb(10);
    sb.pushLine(2, makeLine(2, 7).constData()); // chars 7..8

    QVector<VTermScreenCell> out(5);
    for (int i = 0; i < 5; ++i) { out[i].chars[0] = 0xcafef00d; }
    QVERIFY(sb.lineAt(0, 5, out.data()));
    QCOMPARE(out[0].chars[0], 7u);
    QCOMPARE(out[1].chars[0], 8u);
    QCOMPARE(out[2].chars[0], 0u);
    QCOMPARE(out[4].chars[0], 0u);
}

void TestScrollbackBuffer::lineAt_intoNarrowerCols_truncates()
{
    ScrollbackBuffer sb(10);
    sb.pushLine(6, makeLine(6, 1).constData()); // chars 1..6

    QVector<VTermScreenCell> out(2);
    QVERIFY(sb.lineAt(0, 2, out.data()));
    QCOMPARE(out[0].chars[0], 1u);
    QCOMPARE(out[1].chars[0], 2u);
}

void TestScrollbackBuffer::clear_emptiesBuffer()
{
    ScrollbackBuffer sb(10);
    sb.pushLine(2, makeLine(2, 1).constData());
    sb.pushLine(2, makeLine(2, 2).constData());
    QCOMPARE(sb.size(), 2);
    sb.clear();
    QCOMPARE(sb.size(), 0);
    QVERIFY(sb.isEmpty());
}

void TestScrollbackBuffer::setMaxLines_shrinks_dropsOldest()
{
    ScrollbackBuffer sb(10);
    for (uint32_t i = 0; i < 6; ++i) {
        sb.pushLine(2, makeLine(2, 100u + i * 10).constData());
    }
    QCOMPARE(sb.size(), 6);

    sb.setMaxLines(3);
    QCOMPARE(sb.size(), 3);
    QCOMPARE(sb.maxLines(), 3);

    // Oldest remaining should be the 4th push (tag 130), most recent the 6th (tag 150).
    QVector<VTermScreenCell> out(2);
    QVERIFY(sb.lineAt(0, 2, out.data()));
    QCOMPARE(out[0].chars[0], 130u);
    QVERIFY(sb.lineAt(2, 2, out.data()));
    QCOMPARE(out[0].chars[0], 150u);
}


void TestScrollbackBuffer::pushLine_returnsEvictionCount()
{
    ScrollbackBuffer sb(3);
    const auto row = makeLine(2, 1u);
    QCOMPARE(sb.pushLine(2, row.constData()), 0);
    QCOMPARE(sb.pushLine(2, row.constData()), 0);
    QCOMPARE(sb.pushLine(2, row.constData()), 0); // now at cap
    QCOMPARE(sb.pushLine(2, row.constData()), 1); // evicts oldest
    QCOMPARE(sb.size(), 3);
    QCOMPARE(sb.pushLine(2, row.constData()), 1);
    QCOMPARE(sb.size(), 3);

    QCOMPARE(sb.setMaxLines(1), 2);
    QCOMPARE(sb.size(), 1);
    QCOMPARE(sb.setMaxLines(10), 0);
}


QTEST_APPLESS_MAIN(TestScrollbackBuffer)

#include "test_scrollback_buffer.moc"
