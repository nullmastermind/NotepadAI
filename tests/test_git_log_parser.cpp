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

#include "GitLogParser.h"

namespace {

// Helper: build one valid record (without the trailing RS so caller can
// concatenate freely).
QByteArray rec(const QByteArray &sha, const QByteArray &parents,
                const QByteArray &an, const QByteArray &ae,
                qint64 ct, const QByteArray &subject)
{
    QByteArray r;
    r.append(sha);
    r.append('\x1f');
    r.append(parents);
    r.append('\x1f');
    r.append(an);
    r.append('\x1f');
    r.append(ae);
    r.append('\x1f');
    r.append(QByteArray::number(ct));
    r.append('\x1f');
    r.append(subject);
    return r;
}

QByteArray oneFullRecord(const QByteArray &sha, const QByteArray &parents,
                          const QByteArray &an, const QByteArray &ae,
                          qint64 ct, const QByteArray &subject)
{
    return rec(sha, parents, an, ae, ct, subject) + QByteArray("\x1e");
}

QByteArray validSha40()
{
    return QByteArray("0123456789abcdef0123456789abcdef01234567");
}

QByteArray sha40(char c)
{
    return QByteArray(40, c);
}

} // namespace

class TestGitLogParser : public QObject
{
    Q_OBJECT
private slots:
    void empty_noOutput();
    void singleRecord_parsed();
    void multipleRecords_inOrder();
    void chunkSplitMidRecord_stillParses();
    void chunkSplitMidField_stillParses();
    void mergeCommit_multipleParents();
    void rootCommit_emptyParents();
    void utf8AuthorName_preserved();
    void malformedSha_dropped();
    void malformedCtime_dropped();
    void recordWithExtraUnitSeparator_dropped();
    void emptySubject_accepted();
    void finishFlushes();
    void reset_clearsPendingTail();
    void noTrailingRS_finishStillEmpts();
};

void TestGitLogParser::empty_noOutput()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    p.feed(QByteArray(), out);
    p.finish(out);
    QCOMPARE(out.size(), 0);
}

void TestGitLogParser::singleRecord_parsed()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    QByteArray in = oneFullRecord(validSha40(), QByteArray("aabbccddeeff00112233445566778899aabbccdd"),
                                   "Alice", "alice@example.com", 1716000000, "feat: hello world");
    p.feed(in, out);
    QCOMPARE(out.size(), 1);
    const auto &c = out[0];
    QCOMPARE(c.sha, validSha40());
    QCOMPARE(c.authorName, QByteArrayLiteral("Alice"));
    QCOMPARE(c.authorEmail, QByteArrayLiteral("alice@example.com"));
    QCOMPARE(c.ctime, qint64(1716000000));
    QCOMPARE(c.subject, QByteArrayLiteral("feat: hello world"));
    QCOMPARE(c.parentCount(), 1);
    QVERIFY(!c.isMerge());
}

void TestGitLogParser::multipleRecords_inOrder()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    QByteArray in;
    in.append(oneFullRecord(sha40('a'), "", "A", "a@a", 100, "first"));
    in.append(oneFullRecord(sha40('b'), sha40('a'), "B", "b@b", 200, "second"));
    in.append(oneFullRecord(sha40('c'), sha40('b'), "C", "c@c", 300, "third"));
    p.feed(in, out);
    QCOMPARE(out.size(), 3);
    QCOMPARE(out[0].subject, QByteArrayLiteral("first"));
    QCOMPARE(out[1].subject, QByteArrayLiteral("second"));
    QCOMPARE(out[2].subject, QByteArrayLiteral("third"));
}

void TestGitLogParser::chunkSplitMidRecord_stillParses()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    QByteArray full = oneFullRecord(sha40('a'), "", "Alice", "a@a", 100, "subject");
    full.append(oneFullRecord(sha40('b'), sha40('a'), "Bob", "b@b", 200, "sub two"));
    // Split somewhere mid-second-record but past the first RS.
    const qsizetype firstRs = full.indexOf('\x1e');
    QVERIFY(firstRs > 0);
    const qsizetype splitPos = firstRs + 30;
    QVERIFY(splitPos < full.size());
    p.feed(full.left(splitPos), out);
    QCOMPARE(out.size(), 1);
    p.feed(full.mid(splitPos), out);
    QCOMPARE(out.size(), 2);
    QCOMPARE(out[1].subject, QByteArrayLiteral("sub two"));
}

void TestGitLogParser::chunkSplitMidField_stillParses()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    QByteArray full = oneFullRecord(sha40('a'), "", "Alice", "alice@example.com",
                                     1234567890, "the subject line");
    // Split right in the middle of the SHA — the parser's pending tail must
    // hold the partial bytes until the second feed completes them.
    p.feed(full.left(15), out);
    QCOMPARE(out.size(), 0);
    p.feed(full.mid(15), out);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].subject, QByteArrayLiteral("the subject line"));
}

void TestGitLogParser::mergeCommit_multipleParents()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    QByteArray parents = sha40('a') + " " + sha40('b');
    p.feed(oneFullRecord(sha40('m'), parents, "M", "m@m", 1, "merge"), out);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].parentCount(), 2);
    QVERIFY(out[0].isMerge());
}

void TestGitLogParser::rootCommit_emptyParents()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    p.feed(oneFullRecord(sha40('r'), "", "Root", "r@r", 1, "root"), out);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].parentCount(), 0);
    QVERIFY(!out[0].isMerge());
}

void TestGitLogParser::utf8AuthorName_preserved()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    const QByteArray name = QStringLiteral("Nguyễn Văn A").toUtf8();
    p.feed(oneFullRecord(sha40('u'), "", name, "a@example.com", 1, "vi"), out);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].authorName, name);
    QCOMPARE(QString::fromUtf8(out[0].authorName),
             QStringLiteral("Nguyễn Văn A"));
}

void TestGitLogParser::malformedSha_dropped()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    // SHA only 10 chars — record has 5 separators but field 0 is too short.
    QByteArray bad = "0123456789" "\x1f" "" "\x1f" "A" "\x1f" "a@a"
                     "\x1f" "100" "\x1f" "subj" "\x1e";
    QByteArray good = oneFullRecord(sha40('b'), "", "B", "b@b", 2, "ok");
    p.feed(bad + good, out);
    // Only the good one should be parsed.
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].sha, sha40('b'));
}

void TestGitLogParser::malformedCtime_dropped()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    // ctime contains a letter.
    QByteArray bad = validSha40() + "\x1f" "" "\x1f" "A" "\x1f" "a@a"
                     "\x1f" "12X4" "\x1f" "subj" "\x1e";
    p.feed(bad, out);
    QCOMPARE(out.size(), 0);
}

void TestGitLogParser::recordWithExtraUnitSeparator_dropped()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    // Subject contains a stray \x1f → record has 6 separators → drop.
    QByteArray bad = validSha40() + "\x1f" "" "\x1f" "A" "\x1f" "a@a"
                     "\x1f" "100" "\x1f" "trouble" "\x1f" "more" "\x1e";
    p.feed(bad, out);
    QCOMPARE(out.size(), 0);
}

void TestGitLogParser::emptySubject_accepted()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    p.feed(oneFullRecord(validSha40(), "", "A", "a@a", 1, ""), out);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].subject, QByteArrayLiteral(""));
}

void TestGitLogParser::finishFlushes()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    // Build a complete record WITHOUT a trailing RS, then call finish().
    p.feed(rec(validSha40(), "", "A", "a@a", 1, "no-RS"), out);
    QCOMPARE(out.size(), 0);
    p.finish(out);
    QCOMPARE(out.size(), 1);
    QCOMPARE(out[0].subject, QByteArrayLiteral("no-RS"));
}

void TestGitLogParser::reset_clearsPendingTail()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    p.feed(QByteArrayLiteral("partial_garbage" "\x1f" "" "\x1f" "A"), out);
    p.reset();
    QVector<GitCommitInfo> out2;
    p.feed(oneFullRecord(validSha40(), "", "A", "a@a", 1, "clean"), out2);
    QCOMPARE(out2.size(), 1);
    QCOMPARE(out2[0].subject, QByteArrayLiteral("clean"));
}

void TestGitLogParser::noTrailingRS_finishStillEmpts()
{
    GitLogParser p;
    QVector<GitCommitInfo> out;
    p.feed(oneFullRecord(validSha40(), "", "A", "a@a", 1, "ok"), out);
    QCOMPARE(out.size(), 1);
    p.finish(out);   // idempotent — nothing left to flush.
    QCOMPARE(out.size(), 1);
}

QTEST_GUILESS_MAIN(TestGitLogParser)
#include "test_git_log_parser.moc"
