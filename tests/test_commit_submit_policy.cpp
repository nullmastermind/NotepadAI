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

#include "CommitSubmitPolicy.h"

class TestCommitSubmitPolicy : public QObject
{
    Q_OBJECT

private slots:
    void submitEnabled_requiresRepoChangesAndNoConflicts();
    void agentPicker_onlyWhenMessageEmptyAndNotAmend();
};

void TestCommitSubmitPolicy::submitEnabled_requiresRepoChangesAndNoConflicts()
{
    QVERIFY(!commitSubmitEnabled(false, false, true));
    QVERIFY(!commitSubmitEnabled(true, true, true));
    QVERIFY(!commitSubmitEnabled(true, false, false));
    QVERIFY(commitSubmitEnabled(true, false, true));
}

void TestCommitSubmitPolicy::agentPicker_onlyWhenMessageEmptyAndNotAmend()
{
    QVERIFY(commitUsesAgentPicker(QString(), false));
    QVERIFY(!commitUsesAgentPicker(QStringLiteral("msg"), false));
    QVERIFY(!commitUsesAgentPicker(QString(), true));
    QVERIFY(!commitUsesAgentPicker(QStringLiteral("msg"), true));
}

QTEST_MAIN(TestCommitSubmitPolicy)
#include "test_commit_submit_policy.moc"
