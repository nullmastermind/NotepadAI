/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QBuffer>
#include <QImage>
#include <QtTest>

#include "FaviconIcon.h"

class TestFaviconIcon : public QObject
{
    Q_OBJECT

private slots:
    void pngBytes_becomeNonNullIcon()
    {
        QImage img(16, 16, QImage::Format_ARGB32);
        img.fill(QColor(0x22, 0x66, 0xee));
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        QVERIFY(img.save(&buf, "PNG"));
        buf.close();

        const QIcon icon = faviconIconFromData(png);
        QVERIFY(!icon.isNull());
    }

    void emptyData_returnsNullIcon()
    {
        QVERIFY(faviconIconFromData(QByteArray()).isNull());
    }

    void garbageData_returnsNullIcon()
    {
        QVERIFY(faviconIconFromData(QByteArray("not-an-image")).isNull());
    }
};

QTEST_MAIN(TestFaviconIcon)
#include "test_favicon_icon.moc"
