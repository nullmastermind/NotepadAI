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

#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>

#include "EditTasksDialog.h"

class TestEditTasksDialog : public QObject
{
    Q_OBJECT

private slots:
    void moveUp_preservesNeighborFields();
    void moveDown_preservesNeighborFields();
    void moveDown_intoTail_preservesNeighborFields();
    void moveUp_commitsUnblurredFormToMovedTask();
    void moveUp_firstItem_isNoOp();
    void moveDown_lastItem_isNoOp();
    void singleItem_moveIsNoOp();

private:
    static QPushButton *buttonWithText(QWidget &root, const QString &text);
    static QList<TerminalTask> sampleTasks();
    static void assertTask(const TerminalTask &t, const char *name, const char *command,
                           const char *env, const char *cwd);
};

QPushButton *TestEditTasksDialog::buttonWithText(QWidget &root, const QString &text)
{
    for (QPushButton *button : root.findChildren<QPushButton *>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

QList<TerminalTask> TestEditTasksDialog::sampleTasks()
{
    return {
        {QStringLiteral("alpha"), QStringLiteral("cmd-a"), QStringLiteral("A=1"), QStringLiteral("dir-a")},
        {QStringLiteral("beta"), QStringLiteral("cmd-b"), QStringLiteral("B=1"), QStringLiteral("dir-b")},
        {QStringLiteral("gamma"), QStringLiteral("cmd-c"), QStringLiteral("C=1"), QStringLiteral("dir-c")},
    };
}

void TestEditTasksDialog::assertTask(const TerminalTask &t, const char *name, const char *command,
                                    const char *env, const char *cwd)
{
    QCOMPARE(t.name, QString::fromUtf8(name));
    QCOMPARE(t.command, QString::fromUtf8(command));
    QCOMPARE(t.env, QString::fromUtf8(env));
    QCOMPARE(t.cwd, QString::fromUtf8(cwd));
}

void TestEditTasksDialog::moveUp_preservesNeighborFields()
{
    EditTasksDialog dialog(QStringLiteral("/ws"), sampleTasks());
    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    list->setCurrentRow(1);

    QPushButton *up = buttonWithText(dialog, QString::fromUtf8("\xe2\x96\xb2"));
    QVERIFY(up);
    QTest::mouseClick(up, Qt::LeftButton);

    const QList<TerminalTask> out = dialog.tasks();
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[0].name, QStringLiteral("beta"));
    QCOMPARE(out[0].command, QStringLiteral("cmd-b"));
    QCOMPARE(out[0].env, QStringLiteral("B=1"));
    QCOMPARE(out[0].cwd, QStringLiteral("dir-b"));
    QCOMPARE(out[1].name, QStringLiteral("alpha"));
    QCOMPARE(out[1].command, QStringLiteral("cmd-a"));
    QCOMPARE(out[1].env, QStringLiteral("A=1"));
    QCOMPARE(out[1].cwd, QStringLiteral("dir-a"));
    QCOMPARE(out[2].name, QStringLiteral("gamma"));
    QCOMPARE(out[2].command, QStringLiteral("cmd-c"));
    QCOMPARE(out[2].env, QStringLiteral("C=1"));
    QCOMPARE(out[2].cwd, QStringLiteral("dir-c"));
}

void TestEditTasksDialog::moveDown_preservesNeighborFields()
{
    EditTasksDialog dialog(QStringLiteral("/ws"), sampleTasks());
    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    list->setCurrentRow(0);

    QPushButton *down = buttonWithText(dialog, QString::fromUtf8("\xe2\x96\xbc"));
    QVERIFY(down);
    QTest::mouseClick(down, Qt::LeftButton);

    const QList<TerminalTask> out = dialog.tasks();
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[0].name, QStringLiteral("beta"));
    QCOMPARE(out[0].command, QStringLiteral("cmd-b"));
    QCOMPARE(out[0].env, QStringLiteral("B=1"));
    QCOMPARE(out[0].cwd, QStringLiteral("dir-b"));
    QCOMPARE(out[1].name, QStringLiteral("alpha"));
    QCOMPARE(out[1].command, QStringLiteral("cmd-a"));
    QCOMPARE(out[1].env, QStringLiteral("A=1"));
    QCOMPARE(out[1].cwd, QStringLiteral("dir-a"));
    QCOMPARE(out[2].name, QStringLiteral("gamma"));
    QCOMPARE(out[2].command, QStringLiteral("cmd-c"));
    QCOMPARE(out[2].env, QStringLiteral("C=1"));
    QCOMPARE(out[2].cwd, QStringLiteral("dir-c"));
}

void TestEditTasksDialog::moveDown_intoTail_preservesNeighborFields()
{
    EditTasksDialog dialog(QStringLiteral("/ws"), sampleTasks());
    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    list->setCurrentRow(1);

    QPushButton *down = buttonWithText(dialog, QString::fromUtf8("\xe2\x96\xbc"));
    QVERIFY(down);
    QTest::mouseClick(down, Qt::LeftButton);

    const QList<TerminalTask> out = dialog.tasks();
    QCOMPARE(out.size(), 3);
    assertTask(out[0], "alpha", "cmd-a", "A=1", "dir-a");
    assertTask(out[1], "gamma", "cmd-c", "C=1", "dir-c");
    assertTask(out[2], "beta", "cmd-b", "B=1", "dir-b");
}

void TestEditTasksDialog::moveUp_commitsUnblurredFormToMovedTask()
{
    EditTasksDialog dialog(QStringLiteral("/ws"), sampleTasks());
    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    list->setCurrentRow(1);

    const auto edits = dialog.findChildren<QLineEdit *>();
    QCOMPARE(edits.size(), 3);
    edits.at(0)->setText(QStringLiteral("beta-dirty"));
    edits.at(1)->setText(QStringLiteral("cmd-b-dirty"));
    edits.at(2)->setText(QStringLiteral("dir-b-dirty"));
    auto *env = dialog.findChild<QPlainTextEdit *>();
    QVERIFY(env);
    env->setPlainText(QStringLiteral("B=dirty"));

    QPushButton *up = buttonWithText(dialog, QString::fromUtf8("\xe2\x96\xb2"));
    QVERIFY(up);
    QTest::mouseClick(up, Qt::LeftButton);

    const QList<TerminalTask> out = dialog.tasks();
    QCOMPARE(out.size(), 3);
    assertTask(out[0], "beta-dirty", "cmd-b-dirty", "B=dirty", "dir-b-dirty");
    assertTask(out[1], "alpha", "cmd-a", "A=1", "dir-a");
    assertTask(out[2], "gamma", "cmd-c", "C=1", "dir-c");
}

void TestEditTasksDialog::moveUp_firstItem_isNoOp()
{
    EditTasksDialog dialog(QStringLiteral("/ws"), sampleTasks());
    QPushButton *up = buttonWithText(dialog, QString::fromUtf8("\xe2\x96\xb2"));
    QVERIFY(up);
    QVERIFY(!up->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onMoveUpClicked"));

    const QList<TerminalTask> out = dialog.tasks();
    QCOMPARE(out.size(), 3);
    assertTask(out[0], "alpha", "cmd-a", "A=1", "dir-a");
    assertTask(out[1], "beta", "cmd-b", "B=1", "dir-b");
    assertTask(out[2], "gamma", "cmd-c", "C=1", "dir-c");
}

void TestEditTasksDialog::moveDown_lastItem_isNoOp()
{
    EditTasksDialog dialog(QStringLiteral("/ws"), sampleTasks());
    auto *list = dialog.findChild<QListWidget *>();
    QVERIFY(list);
    list->setCurrentRow(2);

    QPushButton *down = buttonWithText(dialog, QString::fromUtf8("\xe2\x96\xbc"));
    QVERIFY(down);
    QVERIFY(!down->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onMoveDownClicked"));

    const QList<TerminalTask> out = dialog.tasks();
    QCOMPARE(out.size(), 3);
    assertTask(out[0], "alpha", "cmd-a", "A=1", "dir-a");
    assertTask(out[1], "beta", "cmd-b", "B=1", "dir-b");
    assertTask(out[2], "gamma", "cmd-c", "C=1", "dir-c");
}

void TestEditTasksDialog::singleItem_moveIsNoOp()
{
    const QList<TerminalTask> one = {
        {QStringLiteral("only"), QStringLiteral("cmd-o"), QStringLiteral("O=1"), QStringLiteral("dir-o")},
    };
    EditTasksDialog dialog(QStringLiteral("/ws"), one);

    QPushButton *up = buttonWithText(dialog, QString::fromUtf8("\xe2\x96\xb2"));
    QPushButton *down = buttonWithText(dialog, QString::fromUtf8("\xe2\x96\xbc"));
    QVERIFY(up);
    QVERIFY(down);
    QVERIFY(!up->isEnabled());
    QVERIFY(!down->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onMoveUpClicked"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onMoveDownClicked"));

    const QList<TerminalTask> out = dialog.tasks();
    QCOMPARE(out.size(), 1);
    assertTask(out[0], "only", "cmd-o", "O=1", "dir-o");
}

QTEST_MAIN(TestEditTasksDialog)

#include "test_edit_tasks_dialog.moc"
