/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtTest>

#include "GitHistoryModel.h"

namespace {

GitCommitInfo makeCommit(const QByteArray &sha, const QByteArray &subject = "subj",
                          const QByteArray &author = "Alice", qint64 ctime = 1)
{
    GitCommitInfo c;
    c.sha = sha.leftJustified(40, '0');
    c.authorName = author;
    c.authorEmail = "a@a";
    c.ctime = ctime;
    c.subject = subject;
    return c;
}

QByteArray sha(char c)
{
    return QByteArray(40, c);
}

} // namespace

class TestGitHistoryModel : public QObject
{
    Q_OBJECT
private slots:
    void empty_returnsZeroRows();
    void appendOne_oneRow();
    void appendChunk_multiple();
    void appendChunk_dedupesBySha();
    void appendChunk_emptyChunkNoChange();
    void clear_resetsModel();
    void findBySha_returnsRowOrNull();
    void rowAt_boundsChecked();
    void dataRoles_returnExpected();
    void appendThenClearThenAppend_clean();
};

void TestGitHistoryModel::empty_returnsZeroRows()
{
    GitHistoryModel m;
    QCOMPARE(m.rowCount(), 0);
    QCOMPARE(m.count(), 0);
}

void TestGitHistoryModel::appendOne_oneRow()
{
    GitHistoryModel m;
    m.appendChunk({makeCommit(sha('a'))});
    QCOMPARE(m.rowCount(), 1);
    QCOMPARE(m.count(), 1);
}

void TestGitHistoryModel::appendChunk_multiple()
{
    GitHistoryModel m;
    QVector<GitCommitInfo> chunk;
    chunk.append(makeCommit(sha('a'), "first"));
    chunk.append(makeCommit(sha('b'), "second"));
    chunk.append(makeCommit(sha('c'), "third"));
    m.appendChunk(chunk);
    QCOMPARE(m.rowCount(), 3);
    QCOMPARE(m.data(m.index(0), GitHistoryModel::SubjectRole).toString(),
             QStringLiteral("first"));
    QCOMPARE(m.data(m.index(2), GitHistoryModel::SubjectRole).toString(),
             QStringLiteral("third"));
}

void TestGitHistoryModel::appendChunk_dedupesBySha()
{
    GitHistoryModel m;
    m.appendChunk({makeCommit(sha('a'), "v1"),
                    makeCommit(sha('b'), "v1")});
    QCOMPARE(m.count(), 2);
    // Re-append same SHAs (e.g. fetch returns overlap with previous page).
    m.appendChunk({makeCommit(sha('b'), "v2"),
                    makeCommit(sha('c'), "v1")});
    QCOMPARE(m.count(), 3);
    // The duplicate 'b' must be ignored — subject stays at v1.
    const QByteArray secondSha =
        m.data(m.index(1), GitHistoryModel::FullShaRole)
            .toString().toLatin1();
    QCOMPARE(secondSha, sha('b'));
    QCOMPARE(m.data(m.index(1), GitHistoryModel::SubjectRole).toString(),
             QStringLiteral("v1"));
    // New 'c' appended at the end.
    QCOMPARE(m.data(m.index(2), GitHistoryModel::SubjectRole).toString(),
             QStringLiteral("v1"));
}

void TestGitHistoryModel::appendChunk_emptyChunkNoChange()
{
    GitHistoryModel m;
    m.appendChunk({makeCommit(sha('a'))});
    QCOMPARE(m.count(), 1);
    m.appendChunk({});
    QCOMPARE(m.count(), 1);
}

void TestGitHistoryModel::clear_resetsModel()
{
    GitHistoryModel m;
    m.appendChunk({makeCommit(sha('a')), makeCommit(sha('b'))});
    QCOMPARE(m.count(), 2);
    m.clear();
    QCOMPARE(m.count(), 0);
    QCOMPARE(m.rowCount(), 0);
    QVERIFY(m.findBySha(sha('a')) == nullptr);
}

void TestGitHistoryModel::findBySha_returnsRowOrNull()
{
    GitHistoryModel m;
    m.appendChunk({makeCommit(sha('a'), "subjA"),
                    makeCommit(sha('b'), "subjB")});
    const GitCommitInfo *byA = m.findBySha(sha('a'));
    QVERIFY(byA != nullptr);
    QCOMPARE(byA->subject, QByteArrayLiteral("subjA"));
    QVERIFY(m.findBySha(sha('z')) == nullptr);
}

void TestGitHistoryModel::rowAt_boundsChecked()
{
    GitHistoryModel m;
    m.appendChunk({makeCommit(sha('a'))});
    QVERIFY(m.rowAt(-1) == nullptr);
    QVERIFY(m.rowAt(0) != nullptr);
    QVERIFY(m.rowAt(1) == nullptr);
}

void TestGitHistoryModel::dataRoles_returnExpected()
{
    GitHistoryModel m;
    GitCommitInfo c = makeCommit(sha('a'), "msg", "Alice", 1716000000);
    c.parents = "deadbeef00000000000000000000000000000000";
    m.appendChunk({c});
    const QModelIndex idx = m.index(0);
    QCOMPARE(m.data(idx, GitHistoryModel::SubjectRole).toString(),
             QStringLiteral("msg"));
    QCOMPARE(m.data(idx, GitHistoryModel::AuthorNameRole).toString(),
             QStringLiteral("Alice"));
    QCOMPARE(m.data(idx, GitHistoryModel::CtimeRole).toLongLong(),
             qint64(1716000000));
    QCOMPARE(m.data(idx, GitHistoryModel::ShortShaRole).toString().length(), 7);
    QCOMPARE(m.data(idx, GitHistoryModel::FullShaRole).toString().length(), 40);
    QCOMPARE(m.data(idx, GitHistoryModel::IsMergeRole).toBool(), false);
    QCOMPARE(m.data(idx, GitHistoryModel::ParentCountRole).toInt(), 1);
}

void TestGitHistoryModel::appendThenClearThenAppend_clean()
{
    GitHistoryModel m;
    m.appendChunk({makeCommit(sha('a')), makeCommit(sha('b'))});
    m.clear();
    m.appendChunk({makeCommit(sha('c')), makeCommit(sha('d'))});
    QCOMPARE(m.count(), 2);
    // After clear, prior shas should not block re-insertion if re-streamed.
    m.appendChunk({makeCommit(sha('a'))});
    QCOMPARE(m.count(), 3);
}

QTEST_GUILESS_MAIN(TestGitHistoryModel)
#include "test_git_history_model.moc"
