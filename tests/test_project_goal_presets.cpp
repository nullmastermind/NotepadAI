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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "ProjectGoalPresets.h"

namespace {

QString goals(const QString &root)
{
    return QDir(root).filePath(QStringLiteral(".agents/.goals"));
}

void writeRaw(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(out.write(bytes), qint64(bytes.size()));
}

QByteArray readRaw(const QString &path)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return {};
    return in.readAll();
}

} // namespace

class TestProjectGoalPresets : public QObject
{
    Q_OBJECT

private slots:
    void saveOneCriterion_writesNameMdNotDirectory();
    void saveMany_writesDenseFilesAndRemovesLooseFile();
    void read_gapsStayAndSortNumerically();
    void read_ignoresEmptyZeroPaddedAndNotes();
    void read_doesNotCreateMissingNumbers();
    void saveOne_collapsesDirectory();
    void saveMany_keepsUnrelatedFileAndDropsStaleNumber();
    void list_directoryWinsOverLooseFile();
    void save_drops51stAndTruncates4001st();
    void read_skipsFileLargerThan256Kb();
    void save_rejectsIllegalNameAndWritesNothing();
    void save_caseInsensitiveOverwriteKeepsOnDiskStem();
    void saveMany_failedFirstWriteLeavesLooseFile();
    void remove_deletesFileOrEmptiedDirectory();
    void projectScope_requiresLocalExistingDirectory();
};

void TestProjectGoalPresets::saveOneCriterion_writesNameMdNotDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    const bool ok = ProjectGoalPresets::save(
        dir.path(), QStringLiteral("ship-login"),
        QStringList{QStringLiteral("User can sign in.")}, &error);
    QVERIFY2(ok, qPrintable(error));

    const QString file = QDir(goals(dir.path())).filePath(QStringLiteral("ship-login.md"));
    const QString folder = QDir(goals(dir.path())).filePath(QStringLiteral("ship-login"));
    QVERIFY(QFileInfo::exists(file));
    QVERIFY(!QFileInfo(folder).exists());
    QCOMPARE(QString::fromUtf8(readRaw(file)), QStringLiteral("User can sign in."));
}

void TestProjectGoalPresets::saveMany_writesDenseFilesAndRemovesLooseFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString error;
    QVERIFY2(ProjectGoalPresets::save(
                 dir.path(), QStringLiteral("ship"),
                 QStringList{QStringLiteral("only")}, &error),
             qPrintable(error));

    QVERIFY2(ProjectGoalPresets::save(
                 dir.path(), QStringLiteral("ship"),
                 QStringList{QStringLiteral("line1\n\nline2"), QStringLiteral("second")},
                 &error),
             qPrintable(error));

    const QDir folder(QDir(goals(dir.path())).filePath(QStringLiteral("ship")));
    QVERIFY(folder.exists());
    QVERIFY(!QFileInfo::exists(QDir(goals(dir.path())).filePath(QStringLiteral("ship.md"))));
    QCOMPARE(QString::fromUtf8(readRaw(folder.filePath(QStringLiteral("1.md")))),
             QStringLiteral("line1\n\nline2"));
    QCOMPARE(QString::fromUtf8(readRaw(folder.filePath(QStringLiteral("2.md")))),
             QStringLiteral("second"));
    QVERIFY(!QFileInfo::exists(folder.filePath(QStringLiteral("3.md"))));
}

void TestProjectGoalPresets::read_gapsStayAndSortNumerically()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = QDir(goals(dir.path())).filePath(QStringLiteral("ship"));
    writeRaw(QDir(folder).filePath(QStringLiteral("1.md")), "one");
    writeRaw(QDir(folder).filePath(QStringLiteral("3.md")), "three");
    writeRaw(QDir(folder).filePath(QStringLiteral("10.md")), "ten");
    writeRaw(QDir(folder).filePath(QStringLiteral("9.md")), "nine");

    QStringList criteria;
    QString error;
    QVERIFY2(ProjectGoalPresets::read(dir.path(), QStringLiteral("ship"), &criteria, &error),
             qPrintable(error));
    QCOMPARE(criteria, QStringList({QStringLiteral("one"), QStringLiteral("three"),
                                    QStringLiteral("nine"), QStringLiteral("ten")}));
}

void TestProjectGoalPresets::read_ignoresEmptyZeroPaddedAndNotes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = QDir(goals(dir.path())).filePath(QStringLiteral("ship"));
    writeRaw(QDir(folder).filePath(QStringLiteral("1.md")), "one");
    writeRaw(QDir(folder).filePath(QStringLiteral("2.md")), "  \n");
    writeRaw(QDir(folder).filePath(QStringLiteral("3.md")), "three");
    writeRaw(QDir(folder).filePath(QStringLiteral("0.md")), "zero");
    writeRaw(QDir(folder).filePath(QStringLiteral("01.md")), "padded");
    writeRaw(QDir(folder).filePath(QStringLiteral("notes.md")), "notes");

    QStringList criteria;
    QString error;
    QVERIFY2(ProjectGoalPresets::read(dir.path(), QStringLiteral("ship"), &criteria, &error),
             qPrintable(error));
    QCOMPARE(criteria, QStringList({QStringLiteral("one"), QStringLiteral("three")}));

    const QList<ProjectGoalPresets::Listed> listed = ProjectGoalPresets::list(dir.path());
    QCOMPARE(listed.size(), 1);
    QCOMPARE(listed.at(0).name, QStringLiteral("ship"));
    QCOMPARE(listed.at(0).count, 2);
}

void TestProjectGoalPresets::read_doesNotCreateMissingNumbers()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = QDir(goals(dir.path())).filePath(QStringLiteral("ship"));
    writeRaw(QDir(folder).filePath(QStringLiteral("1.md")), "one");
    writeRaw(QDir(folder).filePath(QStringLiteral("3.md")), "three");

    QStringList criteria;
    QString error;
    QVERIFY(ProjectGoalPresets::read(dir.path(), QStringLiteral("ship"), &criteria, &error));
    QVERIFY(!QFileInfo::exists(QDir(folder).filePath(QStringLiteral("2.md"))));
    QCOMPARE(criteria.size(), 2);
}

void TestProjectGoalPresets::saveOne_collapsesDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    QVERIFY2(ProjectGoalPresets::save(
                 dir.path(), QStringLiteral("ship"),
                 QStringList{QStringLiteral("a"), QStringLiteral("b")}, &error),
             qPrintable(error));
    QVERIFY2(ProjectGoalPresets::save(
                 dir.path(), QStringLiteral("ship"),
                 QStringList{QStringLiteral("only")}, &error),
             qPrintable(error));

    QVERIFY(QFileInfo::exists(QDir(goals(dir.path())).filePath(QStringLiteral("ship.md"))));
    QVERIFY(!QFileInfo(QDir(goals(dir.path())).filePath(QStringLiteral("ship"))).exists());
    QCOMPARE(QString::fromUtf8(readRaw(QDir(goals(dir.path())).filePath(QStringLiteral("ship.md")))),
             QStringLiteral("only"));
}

void TestProjectGoalPresets::saveMany_keepsUnrelatedFileAndDropsStaleNumber()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = QDir(goals(dir.path())).filePath(QStringLiteral("ship"));
    writeRaw(QDir(folder).filePath(QStringLiteral("1.md")), "old");
    writeRaw(QDir(folder).filePath(QStringLiteral("4.md")), "stale");
    writeRaw(QDir(folder).filePath(QStringLiteral("notes.txt")), "keep");

    QString error;
    QVERIFY2(ProjectGoalPresets::save(
                 dir.path(), QStringLiteral("ship"),
                 QStringList{QStringLiteral("a"), QStringLiteral("b")}, &error),
             qPrintable(error));

    QVERIFY(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("notes.txt"))));
    QVERIFY(!QFileInfo::exists(QDir(folder).filePath(QStringLiteral("4.md"))));
    QCOMPARE(QString::fromUtf8(readRaw(QDir(folder).filePath(QStringLiteral("1.md")))),
             QStringLiteral("a"));
    QCOMPARE(QString::fromUtf8(readRaw(QDir(folder).filePath(QStringLiteral("2.md")))),
             QStringLiteral("b"));
}

void TestProjectGoalPresets::list_directoryWinsOverLooseFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = goals(dir.path());
    writeRaw(QDir(root).filePath(QStringLiteral("gamma.md")), "loose");
    writeRaw(QDir(root).filePath(QStringLiteral("gamma/1.md")), "from-dir");
    writeRaw(QDir(root).filePath(QStringLiteral("gamma/2.md")), "second");
    writeRaw(QDir(root).filePath(QStringLiteral("alpha.md")), "a");

    const QList<ProjectGoalPresets::Listed> listed = ProjectGoalPresets::list(dir.path());
    QCOMPARE(listed.size(), 2);
    QCOMPARE(listed.at(0).name, QStringLiteral("alpha"));
    QCOMPARE(listed.at(0).count, 1);
    QCOMPARE(listed.at(1).name, QStringLiteral("gamma"));
    QCOMPARE(listed.at(1).count, 2);

    QStringList criteria;
    QString error;
    QVERIFY(ProjectGoalPresets::read(dir.path(), QStringLiteral("gamma"), &criteria, &error));
    QCOMPARE(criteria, QStringList({QStringLiteral("from-dir"), QStringLiteral("second")}));
}

void TestProjectGoalPresets::save_drops51stAndTruncates4001st()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QStringList many;
    for (int i = 0; i < 51; ++i)
        many.append(QStringLiteral("c%1").arg(i));

    QString error;
    QVERIFY2(ProjectGoalPresets::save(dir.path(), QStringLiteral("many"), many, &error),
             qPrintable(error));

    QStringList criteria;
    QVERIFY(ProjectGoalPresets::read(dir.path(), QStringLiteral("many"), &criteria, &error));
    QCOMPARE(criteria.size(), 50);
    QCOMPARE(criteria.first(), QStringLiteral("c0"));
    QCOMPARE(criteria.last(), QStringLiteral("c49"));

    const QString longText(4001, QLatin1Char('a'));
    QVERIFY(ProjectGoalPresets::save(
        dir.path(), QStringLiteral("long"), QStringList{longText}, &error));
    QCOMPARE(readRaw(QDir(goals(dir.path())).filePath(QStringLiteral("long.md"))).size(),
             qint64(4000));
}

void TestProjectGoalPresets::read_skipsFileLargerThan256Kb()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString folder = QDir(goals(dir.path())).filePath(QStringLiteral("ship"));
    writeRaw(QDir(folder).filePath(QStringLiteral("1.md")), "keep");
    writeRaw(QDir(folder).filePath(QStringLiteral("2.md")), QByteArray(256 * 1024 + 1, 'b'));

    QStringList criteria;
    QString error;
    QVERIFY(ProjectGoalPresets::read(dir.path(), QStringLiteral("ship"), &criteria, &error));
    QCOMPARE(criteria, QStringList{QStringLiteral("keep")});

    const QList<ProjectGoalPresets::Listed> listed = ProjectGoalPresets::list(dir.path());
    QCOMPARE(listed.size(), 1);
    QCOMPARE(listed.at(0).count, 1);
}

void TestProjectGoalPresets::save_rejectsIllegalNameAndWritesNothing()
{
    const QStringList bad = {
        QString(),
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("CON"),
        QStringLiteral("com1"),
        QStringLiteral("lpt9"),
        QStringLiteral("foo."),
        QStringLiteral("foo "),
        QStringLiteral("foo/bar"),
        QStringLiteral("foo\\bar"),
        QStringLiteral("a:b"),
        QStringLiteral("foo*"),
    };
    for (const QString &name : bad) {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString error;
        QVERIFY(!ProjectGoalPresets::save(
            dir.path(), name, QStringList{QStringLiteral("x")}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(goals(dir.path())));
    }
}

void TestProjectGoalPresets::save_caseInsensitiveOverwriteKeepsOnDiskStem()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeRaw(QDir(goals(dir.path())).filePath(QStringLiteral("Foo.md")), "old");

    QString error;
    QVERIFY2(ProjectGoalPresets::save(
                 dir.path(), QStringLiteral("foo"),
                 QStringList{QStringLiteral("new")}, &error),
             qPrintable(error));

    const QDir folder(goals(dir.path()));
    const QStringList md = folder.entryList(
        QStringList{QStringLiteral("*.md")}, QDir::Files);
    QCOMPARE(md.size(), 1);
    QCOMPARE(md.at(0), QStringLiteral("Foo.md"));
    QCOMPARE(QString::fromUtf8(readRaw(folder.filePath(md.at(0)))), QStringLiteral("new"));

    const QList<ProjectGoalPresets::Listed> listed = ProjectGoalPresets::list(dir.path());
    QCOMPARE(listed.size(), 1);
    QCOMPARE(listed.at(0).name, QStringLiteral("Foo"));
}

void TestProjectGoalPresets::saveMany_failedFirstWriteLeavesLooseFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = goals(dir.path());
    writeRaw(QDir(root).filePath(QStringLiteral("ship.md")), "original");
    QVERIFY(QDir().mkpath(QDir(root).filePath(QStringLiteral("ship/1.md"))));

    QString error;
    QVERIFY(!ProjectGoalPresets::save(
        dir.path(), QStringLiteral("ship"),
        QStringList{QStringLiteral("a"), QStringLiteral("b")}, &error));
    QCOMPARE(QString::fromUtf8(readRaw(QDir(root).filePath(QStringLiteral("ship.md")))),
             QStringLiteral("original"));
}

void TestProjectGoalPresets::remove_deletesFileOrEmptiedDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    QVERIFY(ProjectGoalPresets::save(
        dir.path(), QStringLiteral("one"), QStringList{QStringLiteral("a")}, &error));
    QVERIFY(ProjectGoalPresets::remove(dir.path(), QStringLiteral("one"), &error));
    QVERIFY(!QFileInfo::exists(QDir(goals(dir.path())).filePath(QStringLiteral("one.md"))));

    QVERIFY(ProjectGoalPresets::save(
        dir.path(), QStringLiteral("many"),
        QStringList{QStringLiteral("a"), QStringLiteral("b")}, &error));
    const QString notes = QDir(goals(dir.path())).filePath(QStringLiteral("many/notes.txt"));
    writeRaw(notes, "keep");
    QVERIFY(ProjectGoalPresets::remove(dir.path(), QStringLiteral("many"), &error));
    QVERIFY(!QFileInfo::exists(QDir(goals(dir.path())).filePath(QStringLiteral("many/1.md"))));
    QVERIFY(!QFileInfo::exists(QDir(goals(dir.path())).filePath(QStringLiteral("many/2.md"))));
    QVERIFY(QFileInfo::exists(notes));
}

void TestProjectGoalPresets::projectScope_requiresLocalExistingDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(ProjectGoalPresets::projectScopeAvailable(dir.path(), false));
    QVERIFY(!ProjectGoalPresets::projectScopeAvailable(dir.path(), true));
    QVERIFY(!ProjectGoalPresets::projectScopeAvailable(QString(), false));
    QVERIFY(!ProjectGoalPresets::projectScopeAvailable(
        QDir(dir.path()).filePath(QStringLiteral("missing")), false));
}

QTEST_MAIN(TestProjectGoalPresets)
#include "test_project_goal_presets.moc"
