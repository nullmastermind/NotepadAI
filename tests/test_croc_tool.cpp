/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#include <QtTest>

#include "CrocTool.h"

class TestCrocTool : public QObject
{
    Q_OBJECT

private slots:
    void parse_plainPhrase();
    void parse_v11RunLine();
    void parse_v11Url();
    void parse_ansiWrapped();
    void parse_missing();
    void assetHint_nonEmpty();
    void validPhrase_requiresHyphen();
    void relay_emptyFallsBack();
    void relay_picksFirstIpv4();
    void relay_skipsLoopbackAndIpv6();
    void failure_includesCrocText();
    void failure_emptyOutputNotJustExit();
    void failure_redactsSecret();
};

void TestCrocTool::parse_plainPhrase()
{
    QCOMPARE(CrocTool::parseCodePhrase(QStringLiteral("Sending 'a.zip' (1 MB)\nCode is: 1234-sundial-horizon\n")),
             QStringLiteral("1234-sundial-horizon"));
}

void TestCrocTool::parse_v11RunLine()
{
    QCOMPARE(CrocTool::parseCodePhrase(
                 QStringLiteral("On the other computer, run:\n  croc nai-probe1 (code copied)\n")),
             QStringLiteral("nai-probe1"));
}

void TestCrocTool::parse_v11Url()
{
    QCOMPARE(CrocTool::parseCodePhrase(
                 QStringLiteral("https://getcroc.com/?code=nai-probe2\n")),
             QStringLiteral("nai-probe2"));
}

void TestCrocTool::parse_ansiWrapped()
{
    const QString s = QStringLiteral("Code is: \x1B[32malpha-bravo-charlie\x1B[0m\n");
    QCOMPARE(CrocTool::parseCodePhrase(s), QStringLiteral("alpha-bravo-charlie"));
}

void TestCrocTool::parse_missing()
{
    QVERIFY(CrocTool::parseCodePhrase(QStringLiteral("Sending...\n")).isEmpty());
}

void TestCrocTool::assetHint_nonEmpty()
{
    QVERIFY(!CrocTool::releaseAssetHint().isEmpty());
}

void TestCrocTool::validPhrase_requiresHyphen()
{
    QVERIFY(CrocTool::isValidCodePhrase(QStringLiteral("1234-sundial-horizon")));
    QVERIFY(CrocTool::isValidCodePhrase(QStringLiteral("ab-cde")));
    QVERIFY(!CrocTool::isValidCodePhrase(QString()));
    QVERIFY(!CrocTool::isValidCodePhrase(QStringLiteral("nope")));
    QVERIFY(!CrocTool::isValidCodePhrase(QStringLiteral("a-bcd")));
    QVERIFY(!CrocTool::isValidCodePhrase(QStringLiteral("has space-here")));
}

void TestCrocTool::relay_emptyFallsBack()
{
    QCOMPARE(CrocTool::pickRelayIPv4({}), CrocTool::fallbackRelayAddress());
    QVERIFY(CrocTool::fallbackRelayAddress().endsWith(QStringLiteral(":9009")));
}

void TestCrocTool::relay_picksFirstIpv4()
{
    QCOMPARE(CrocTool::pickRelayIPv4({QHostAddress(QStringLiteral("10.1.2.3"))}),
             QStringLiteral("10.1.2.3:9009"));
}

void TestCrocTool::relay_skipsLoopbackAndIpv6()
{
    const QList<QHostAddress> addrs{QHostAddress::LocalHost,
                                    QHostAddress(QStringLiteral("::1")),
                                    QHostAddress(QStringLiteral("8.8.8.8"))};
    QCOMPARE(CrocTool::pickRelayIPv4(addrs), QStringLiteral("8.8.8.8:9009"));
}

void TestCrocTool::failure_includesCrocText()
{
    const QString msg = CrocTool::formatProcessFailure(
        1, QStringLiteral("waiting for sender...\nflate: corrupt"), QString());
    QVERIFY(msg.contains(QLatin1String("flate: corrupt")));
    QVERIFY(msg.contains(QLatin1String("1")));
}

void TestCrocTool::failure_emptyOutputNotJustExit()
{
    const QString msg = CrocTool::formatProcessFailure(1, QString(), QString());
    QVERIFY(msg.contains(QLatin1String("no output")));
    QVERIFY(msg.contains(QLatin1String("1")));
}

void TestCrocTool::failure_redactsSecret()
{
    const QString msg = CrocTool::formatProcessFailure(
        1, QStringLiteral("code nai-secretxx failed"), QStringLiteral("nai-secretxx"));
    QVERIFY(!msg.contains(QLatin1String("nai-secretxx")));
    QVERIFY(msg.contains(QLatin1String("***")));
}

QTEST_APPLESS_MAIN(TestCrocTool)
#include "test_croc_tool.moc"
