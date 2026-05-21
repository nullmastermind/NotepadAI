/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtTest>

#include "AcpProtocol.h"

using AcpProtocol::AcpPermissionOption;

class TestAcpPermissionPolicy : public QObject
{
    Q_OBJECT

private slots:
    void prefersAllowOnce();
    void fallsBackToAllowAlways();
    void noneAcceptable();
    void emptyOptions();
    void priorityClassBeatsOrder();
};

static AcpPermissionOption mk(const QString &id, const QString &kind)
{
    AcpPermissionOption o;
    o.id = id;
    o.label = id;
    o.kind = kind;
    return o;
}

void TestAcpPermissionPolicy::prefersAllowOnce()
{
    QList<AcpPermissionOption> opts = {
        mk(QStringLiteral("deny1"), QStringLiteral("deny")),
        mk(QStringLiteral("once1"), QStringLiteral("allow_once")),
        mk(QStringLiteral("always1"), QStringLiteral("allow_always")),
    };
    auto picked = AcpProtocol::pickAutoApproveOptionId(opts);
    QVERIFY(picked.has_value());
    QCOMPARE(picked.value(), QStringLiteral("once1"));
}

void TestAcpPermissionPolicy::fallsBackToAllowAlways()
{
    QList<AcpPermissionOption> opts = {
        mk(QStringLiteral("deny1"), QStringLiteral("deny")),
        mk(QStringLiteral("always1"), QStringLiteral("allow_always")),
    };
    auto picked = AcpProtocol::pickAutoApproveOptionId(opts);
    QVERIFY(picked.has_value());
    QCOMPARE(picked.value(), QStringLiteral("always1"));
}

void TestAcpPermissionPolicy::noneAcceptable()
{
    QList<AcpPermissionOption> opts = {
        mk(QStringLiteral("deny1"), QStringLiteral("deny")),
    };
    auto picked = AcpProtocol::pickAutoApproveOptionId(opts);
    QVERIFY(!picked.has_value());
}

void TestAcpPermissionPolicy::emptyOptions()
{
    QList<AcpPermissionOption> opts;
    auto picked = AcpProtocol::pickAutoApproveOptionId(opts);
    QVERIFY(!picked.has_value());
}

void TestAcpPermissionPolicy::priorityClassBeatsOrder()
{
    QList<AcpPermissionOption> opts = {
        mk(QStringLiteral("always1"), QStringLiteral("allow_always")),
        mk(QStringLiteral("once1"), QStringLiteral("allow_once")),
    };
    auto picked = AcpProtocol::pickAutoApproveOptionId(opts);
    QVERIFY(picked.has_value());
    QCOMPARE(picked.value(), QStringLiteral("once1"));
}

QTEST_GUILESS_MAIN(TestAcpPermissionPolicy)
#include "test_acp_permission_policy.moc"
