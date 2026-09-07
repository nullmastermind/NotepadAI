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
#include <QApplication>
#include <QSpinBox>
#include <QWheelEvent>

#include "dialogs/UnfocusedWheelFilter.h"


static void sendWheelUp(QWidget *target)
{
    QWheelEvent event(QPointF(1, 1), QPointF(1, 1), QPoint(0, 0), QPoint(0, 120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(target, &event);
}


class TestUnfocusedWheelFilter : public QObject
{
    Q_OBJECT

private slots:
    void unfocusedSpinBox_wheelDoesNotChangeValue();
};

void TestUnfocusedWheelFilter::unfocusedSpinBox_wheelDoesNotChangeValue()
{
    QSpinBox spin;
    spin.setRange(0, 99);
    spin.setValue(10);
    QVERIFY(!spin.hasFocus());

    UnfocusedWheelFilter filter;
    spin.installEventFilter(&filter);

    sendWheelUp(&spin);
    QCOMPARE(spin.value(), 10);
}

QTEST_MAIN(TestUnfocusedWheelFilter)

#include "test_unfocused_wheel_filter.moc"
