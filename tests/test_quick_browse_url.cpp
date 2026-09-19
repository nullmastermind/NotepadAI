/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QtTest>

#include "QuickBrowseUrl.h"

class TestQuickBrowseUrl : public QObject
{
    Q_OBJECT

private slots:
    void windowsDrivePath_becomesFileUrl()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral(R"(C:\Users\foo\page.html)")),
                 QStringLiteral("file:///C:/Users/foo/page.html"));
    }

    void windowsDrivePathForwardSlash_becomesFileUrl()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral("C:/Users/foo/page.html")),
                 QStringLiteral("file:///C:/Users/foo/page.html"));
    }

    void quotedWindowsPath_stripsQuotesAndBecomesFileUrl()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral(R"("C:\Users\foo\page.html")")),
                 QStringLiteral("file:///C:/Users/foo/page.html"));
    }

    void posixAbsolutePath_becomesFileUrl()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral("/home/foo/page.html")),
                 QStringLiteral("file:///home/foo/page.html"));
    }

    void uncPath_becomesFileUrl()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral(R"(\\server\share\page.html)")),
                 QStringLiteral("file://server/share/page.html"));
    }

    void pathWithSpaces_isPercentEncoded()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral(R"(C:\My Files\page.html)")),
                 QStringLiteral("file:///C:/My%20Files/page.html"));
    }

    void existingFileUrl_preserved()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral("file:///C:/Users/foo/page.html")),
                 QStringLiteral("file:///C:/Users/foo/page.html"));
    }

    void existingHttps_preserved()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral("https://secure.example.com")),
                 QStringLiteral("https://secure.example.com"));
    }

    void existingHttp_preserved()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral("http://insecure.example.com")),
                 QStringLiteral("http://insecure.example.com"));
    }

    void bareDomain_getsHttpsPrefix()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral("example.com")),
                 QStringLiteral("https://example.com"));
    }

    void localhostWithPort_getsHttpsPrefix()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral("localhost:3000")),
                 QStringLiteral("https://localhost:3000"));
    }

    void emptyInput_staysEmpty()
    {
        QCOMPARE(normalizeQuickBrowseInput(QString()), QString());
    }

    void whitespaceOnly_staysEmpty()
    {
        QCOMPARE(normalizeQuickBrowseInput(QStringLiteral("   ")), QString());
    }

    void htmlSuffix_isDetectedCaseInsensitive()
    {
        QVERIFY(isHtmlFilePath(QStringLiteral(R"(C:\site\index.html)")));
        QVERIFY(isHtmlFilePath(QStringLiteral("/tmp/Index.HTM")));
        QVERIFY(!isHtmlFilePath(QStringLiteral(R"(C:\site\readme.md)")));
        QVERIFY(!isHtmlFilePath(QStringLiteral(R"(C:\site)")));
    }
};

QTEST_GUILESS_MAIN(TestQuickBrowseUrl)
#include "test_quick_browse_url.moc"
