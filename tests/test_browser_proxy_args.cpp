/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QtTest>

#include "BrowserProxyArgs.h"

class TestBrowserProxyArgs : public QObject
{
    Q_OBJECT

private slots:
    void proxyDisabled_returnsEmpty()
    {
        QCOMPARE(buildBrowserArgs(0, 0, QString(), 0, QString()), QString());
    }

    void proxyDisabled_debugPortOnly()
    {
        QCOMPARE(buildBrowserArgs(9222, 0, QString(), 0, QString()),
                 QStringLiteral("--remote-debugging-port=9222"));
    }

    void proxyEnabled_emptyHost_noProxyArgs()
    {
        QCOMPARE(buildBrowserArgs(0, 1, QString(), 8080, QString()), QString());
    }

    void proxyEnabled_httpType()
    {
        QCOMPARE(buildBrowserArgs(0, 1, QStringLiteral("proxy.example.com"), 8080, QString()),
                 QStringLiteral("--proxy-server=http://proxy.example.com:8080"));
    }

    void proxyEnabled_httpsType()
    {
        QCOMPARE(buildBrowserArgs(0, 2, QStringLiteral("secure.proxy.com"), 443, QString()),
                 QStringLiteral("--proxy-server=https://secure.proxy.com:443"));
    }

    void proxyEnabled_socks4Type()
    {
        QCOMPARE(buildBrowserArgs(0, 3, QStringLiteral("socks.local"), 1080, QString()),
                 QStringLiteral("--proxy-server=socks4://socks.local:1080"));
    }

    void proxyEnabled_socks5Type()
    {
        QCOMPARE(buildBrowserArgs(0, 4, QStringLiteral("socks5.proxy.net"), 1080, QString()),
                 QStringLiteral("--proxy-server=socks5://socks5.proxy.net:1080"));
    }

    void proxyEnabled_withBypassList()
    {
        QCOMPARE(buildBrowserArgs(0, 1, QStringLiteral("proxy.example.com"), 8080,
                                  QStringLiteral("localhost;127.0.0.1;[::1];<local>")),
                 QStringLiteral("--proxy-server=http://proxy.example.com:8080 --proxy-bypass-list=localhost;127.0.0.1;[::1];<local>"));
    }

    void proxyEnabled_withDebugPort()
    {
        QCOMPARE(buildBrowserArgs(9222, 4, QStringLiteral("myproxy"), 9050, QStringLiteral("*.local")),
                 QStringLiteral("--remote-debugging-port=9222 --proxy-server=socks5://myproxy:9050 --proxy-bypass-list=*.local"));
    }

    void proxyEnabled_hostWithWhitespace_trimmed()
    {
        QCOMPARE(buildBrowserArgs(0, 1, QStringLiteral("  proxy.example.com  "), 3128, QString()),
                 QStringLiteral("--proxy-server=http://proxy.example.com:3128"));
    }

    void proxyEnabled_portZero_omitted()
    {
        QCOMPARE(buildBrowserArgs(0, 1, QStringLiteral("proxy.example.com"), 0, QString()),
                 QStringLiteral("--proxy-server=http://proxy.example.com"));
    }

    void proxyEnabled_invalidTypeClampedToSocks5()
    {
        QCOMPARE(buildBrowserArgs(0, 99, QStringLiteral("proxy.example.com"), 8080, QString()),
                 QStringLiteral("--proxy-server=socks5://proxy.example.com:8080"));
    }
};

QTEST_GUILESS_MAIN(TestBrowserProxyArgs)
#include "test_browser_proxy_args.moc"