/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "BrowserProxyArgs.h"

#include <QStringList>
#include <QLatin1String>

QString buildBrowserArgs(int debugPort, int proxyType, const QString &proxyHost,
                         int proxyPort, const QString &proxyBypassList,
                         bool allowCrossOrigin)
{
    QStringList args;

    if (debugPort > 0)
        args << QStringLiteral("--remote-debugging-port=%1").arg(debugPort);

    if (proxyType > 0) {
        const QString host = proxyHost.trimmed();
        if (!host.isEmpty()) {
            static const char *schemes[] = {"http", "https", "socks4", "socks5"};
            const int idx = qBound(0, proxyType - 1, 3);

            QString proxyUrl = QStringLiteral("%1://%2")
                .arg(QLatin1String(schemes[idx]), host);
            if (proxyPort > 0 && proxyPort <= 65535)
                proxyUrl += QStringLiteral(":%1").arg(proxyPort);

            args << QStringLiteral("--proxy-server=%1").arg(proxyUrl);

            const QString bypass = proxyBypassList.trimmed();
            if (!bypass.isEmpty())
                args << QStringLiteral("--proxy-bypass-list=%1").arg(bypass);
        }
    }

    if (allowCrossOrigin) {
        args << QStringLiteral("--disable-web-security");
        args << QStringLiteral("--disable-site-isolation-trials");
    }

    return args.join(QLatin1Char(' '));
}
