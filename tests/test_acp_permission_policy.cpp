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
#include <QJsonObject>

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
    void acceptsNamespacedAndLegacyRequestMethods();
    void parsesNamespacedPermissionOptions();
    void serializesNamespacedAndLegacyResponses();
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

void TestAcpPermissionPolicy::acceptsNamespacedAndLegacyRequestMethods()
{
    QVERIFY(AcpProtocol::isPermissionRequestMethod(QStringLiteral("session/request_permission")));
    QVERIFY(AcpProtocol::isPermissionRequestMethod(QStringLiteral("request_permission")));
    QVERIFY(!AcpProtocol::isPermissionRequestMethod(QStringLiteral("session/update")));
}

void TestAcpPermissionPolicy::parsesNamespacedPermissionOptions()
{
    QJsonObject json;
    json.insert(QStringLiteral("optionId"), QStringLiteral("allow-once"));
    json.insert(QStringLiteral("name"), QStringLiteral("Allow once"));
    json.insert(QStringLiteral("kind"), QStringLiteral("allow_once"));

    const AcpPermissionOption opt = AcpProtocol::permissionOptionFromJson(json);
    QCOMPARE(opt.id, QStringLiteral("allow-once"));
    QCOMPARE(opt.label, QStringLiteral("Allow once"));
    QCOMPARE(opt.kind, QStringLiteral("allow_once"));

    const QList<AcpPermissionOption> opts = {opt};
    const auto picked = AcpProtocol::pickAutoApproveOptionId(opts);
    QVERIFY(picked.has_value());
    QCOMPARE(picked.value(), QStringLiteral("allow-once"));
}

void TestAcpPermissionPolicy::serializesNamespacedAndLegacyResponses()
{
    const QJsonObject nested = AcpProtocol::permissionResponseToJson(QStringLiteral("selected"),
                                                                     QStringLiteral("allow-once"),
                                                                     true);
    const QJsonObject nestedOutcome = nested.value(QStringLiteral("outcome")).toObject();
    QCOMPARE(nestedOutcome.value(QStringLiteral("outcome")).toString(), QStringLiteral("selected"));
    QCOMPARE(nestedOutcome.value(QStringLiteral("optionId")).toString(), QStringLiteral("allow-once"));

    const QJsonObject legacy = AcpProtocol::permissionResponseToJson(QStringLiteral("selected"),
                                                                     QStringLiteral("allow-once"),
                                                                     false);
    QCOMPARE(legacy.value(QStringLiteral("outcome")).toString(), QStringLiteral("selected"));
    QCOMPARE(legacy.value(QStringLiteral("optionId")).toString(), QStringLiteral("allow-once"));

    const QJsonObject cancelled = AcpProtocol::permissionResponseToJson(QStringLiteral("cancelled"),
                                                                        QString(),
                                                                        true)
                                      .value(QStringLiteral("outcome")).toObject();
    QCOMPARE(cancelled.value(QStringLiteral("outcome")).toString(), QStringLiteral("cancelled"));
    QVERIFY(!cancelled.contains(QStringLiteral("optionId")));
}

QTEST_GUILESS_MAIN(TestAcpPermissionPolicy)
#include "test_acp_permission_policy.moc"
