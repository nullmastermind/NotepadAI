/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "QuickBrowseUrl.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

static QString fileUrlFromPath(QString path, bool unc)
{
    path = QDir::fromNativeSeparators(path);
    QUrl url;
    url.setScheme(QStringLiteral("file"));
    if (unc) {
        if (path.startsWith(QLatin1String("//")))
            path = path.mid(2);
        const int slash = path.indexOf(QLatin1Char('/'));
        QString host;
        QString rest;
        if (slash < 0) {
            host = path;
        } else {
            host = path.left(slash);
            rest = path.mid(slash);
        }
        url.setHost(host);
        url.setPath(rest.isEmpty() ? QStringLiteral("/") : rest, QUrl::DecodedMode);
    } else {
        if (!path.startsWith(QLatin1Char('/')))
            path.prepend(QLatin1Char('/'));
        url.setPath(path, QUrl::DecodedMode);
    }
    return url.toString(QUrl::EncodeSpaces);
}

QString normalizeQuickBrowseInput(const QString &input)
{
    QString s = input.trimmed();
    if (s.size() >= 2) {
        const QChar a = s.front();
        const QChar b = s.back();
        if ((a == QLatin1Char('"') && b == QLatin1Char('"'))
            || (a == QLatin1Char('\'') && b == QLatin1Char('\''))) {
            s = s.mid(1, s.size() - 2).trimmed();
        }
    }
    if (s.isEmpty())
        return s;

    if (s.contains(QLatin1String("://")))
        return s;

    if (s.size() >= 3 && s.at(0).isLetter() && s.at(1) == QLatin1Char(':')
        && (s.at(2) == QLatin1Char('\\') || s.at(2) == QLatin1Char('/'))) {
        return fileUrlFromPath(s, false);
    }

    if (s.startsWith(QLatin1String("\\\\")))
        return fileUrlFromPath(s, true);

    if (s.startsWith(QLatin1Char('/')) && !s.startsWith(QLatin1String("//")))
        return fileUrlFromPath(s, false);

    return QStringLiteral("https://") + s;
}

bool isHtmlFilePath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("html") || suffix == QLatin1String("htm");
}
