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
#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "AcpAgentDefinition.h"
#include "AcpAgentRegistry.h"
#include "ApplicationSettings.h"


class TestAcpAgentRegistry : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void firstLaunch_seedsBuiltinClaudeCode();
    void userAddRoundTrip_persistsAcrossInstances();
    void removeBuiltin_isRefused();
    void duplicateId_isRefused();
    void defaultAgentId_fallsBackToBuiltin();
    void autoApprovePolicy_defaultsToManual();
    void setAutoApprovePolicy_persistsAndEmits();

private:
    QTemporaryDir tempDir;
};

void TestAcpAgentRegistry::initTestCase()
{
    QVERIFY(tempDir.isValid());
    QCoreApplication::setOrganizationName("NotepadNextTest");
    QCoreApplication::setApplicationName("NotepadNextTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tempDir.path());
}

void TestAcpAgentRegistry::init()
{
    ApplicationSettings s;
    s.clear();
    s.sync();
}

void TestAcpAgentRegistry::firstLaunch_seedsBuiltinClaudeCode()
{
    ApplicationSettings s;
    AcpAgentRegistry registry(&s);

    const auto agents = registry.agents();
    QCOMPARE(agents.size(), 1);
    QCOMPARE(agents.first().id, QStringLiteral("builtin:claude-code"));
    QVERIFY(agents.first().builtin);
    QCOMPARE(agents.first().command, QStringLiteral("npx"));
}

void TestAcpAgentRegistry::userAddRoundTrip_persistsAcrossInstances()
{
    ApplicationSettings s;

    {
        AcpAgentRegistry registry(&s);
        AcpAgentDefinition def;
        def.id = QStringLiteral("user:foo");
        def.name = QStringLiteral("Foo");
        def.command = QStringLiteral("foo");
        def.builtin = false;
        QVERIFY(registry.addAgent(def));
    }
    s.sync();

    AcpAgentRegistry reloaded(&s);
    const auto agents = reloaded.agents();
    QCOMPARE(agents.size(), 2);
    QVERIFY(reloaded.contains(QStringLiteral("user:foo")));
    QVERIFY(reloaded.contains(QStringLiteral("builtin:claude-code")));
    QCOMPARE(reloaded.agent(QStringLiteral("user:foo")).name, QStringLiteral("Foo"));
}

void TestAcpAgentRegistry::removeBuiltin_isRefused()
{
    ApplicationSettings s;
    AcpAgentRegistry registry(&s);

    QVERIFY(!registry.removeAgent(QStringLiteral("builtin:claude-code")));
    QCOMPARE(registry.agents().size(), 1);
    QVERIFY(registry.contains(QStringLiteral("builtin:claude-code")));
}

void TestAcpAgentRegistry::duplicateId_isRefused()
{
    ApplicationSettings s;
    AcpAgentRegistry registry(&s);

    AcpAgentDefinition def;
    def.id = QStringLiteral("user:foo");
    def.name = QStringLiteral("Foo");
    def.command = QStringLiteral("foo");
    QVERIFY(registry.addAgent(def));

    AcpAgentDefinition dup;
    dup.id = QStringLiteral("user:foo");
    dup.name = QStringLiteral("Foo2");
    dup.command = QStringLiteral("bar");
    QVERIFY(!registry.addAgent(dup));
    QCOMPARE(registry.agents().size(), 2);

    // Built-in id should also be refused.
    AcpAgentDefinition collidesWithBuiltin;
    collidesWithBuiltin.id = QStringLiteral("builtin:claude-code");
    collidesWithBuiltin.name = QStringLiteral("Hijack");
    collidesWithBuiltin.command = QStringLiteral("evil");
    QVERIFY(!registry.addAgent(collidesWithBuiltin));
}

void TestAcpAgentRegistry::defaultAgentId_fallsBackToBuiltin()
{
    ApplicationSettings s;
    AcpAgentRegistry registry(&s);

    QCOMPARE(registry.defaultAgentId(), QStringLiteral("builtin:claude-code"));
}

void TestAcpAgentRegistry::autoApprovePolicy_defaultsToManual()
{
    ApplicationSettings s;
    AcpAgentRegistry registry(&s);

    QCOMPARE(registry.autoApprovePolicy(), QStringLiteral("manual"));
}

void TestAcpAgentRegistry::setAutoApprovePolicy_persistsAndEmits()
{
    ApplicationSettings s;
    AcpAgentRegistry registry(&s);

    QSignalSpy spy(&registry, &AcpAgentRegistry::autoApprovePolicyChanged);
    QVERIFY(spy.isValid());

    registry.setAutoApprovePolicy(QStringLiteral("allowAll"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("allowAll"));
    QCOMPARE(registry.autoApprovePolicy(), QStringLiteral("allowAll"));

    s.sync();
    AcpAgentRegistry reloaded(&s);
    QCOMPARE(reloaded.autoApprovePolicy(), QStringLiteral("allowAll"));
}

QTEST_MAIN(TestAcpAgentRegistry)

#include "test_acp_agent_registry.moc"
