/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
 *
 * SPDX short: GPL-3.0-or-later
 */

#include <QtTest>
#include <QSignalSpy>
#include <QBuffer>
#include <QImage>
#include <QImageWriter>

#include "AcpImageAttachmentList.h"

namespace {

QByteArray makeTinyPng()
{
    QImage img(2, 2, QImage::Format_ARGB32);
    img.fill(Qt::red);
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    QImageWriter w(&buf, "png");
    w.write(img);
    return data;
}

QByteArray makeLargeJpeg()
{
    // Build a >5 MiB JPEG by writing a 4000x4000 random image and padding.
    QImage img(4000, 4000, QImage::Format_RGB32);
    img.fill(Qt::white);
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    QImageWriter w(&buf, "jpeg");
    w.setQuality(100);
    w.write(img);
    // Pad to ensure > 5 MiB after JPEG compression.
    if (data.size() < AcpImageAttachmentList::kMaxItemBytes + 1) {
        data.append(QByteArray(static_cast<int>(AcpImageAttachmentList::kMaxItemBytes + 1024 - data.size()), '\0'));
    }
    return data;
}

QByteArray makeBmpBlob()
{
    // Minimal BMP header bytes — "BM" magic so QMimeDatabase resolves to image/bmp.
    QImage img(2, 2, QImage::Format_RGB32);
    img.fill(Qt::blue);
    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    QImageWriter w(&buf, "bmp");
    w.write(img);
    return data;
}

} // namespace

class TestAcpImageAttachmentLimits : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void accepts_twenty_pngs_then_rejects_twentyfirst();
    void rejects_oversize_jpeg();
    void rejects_unsupported_bmp();
    void takeAll_returns_pairs_and_clears();
    void png_bytes_roundtrip();

private:
    QByteArray m_png;
    QByteArray m_largeJpeg;
    QByteArray m_bmp;
};

void TestAcpImageAttachmentLimits::initTestCase()
{
    m_png = makeTinyPng();
    m_largeJpeg = makeLargeJpeg();
    m_bmp = makeBmpBlob();
    QVERIFY(!m_png.isEmpty());
    QVERIFY(m_largeJpeg.size() > AcpImageAttachmentList::kMaxItemBytes);
    QVERIFY(!m_bmp.isEmpty());
}

void TestAcpImageAttachmentLimits::accepts_twenty_pngs_then_rejects_twentyfirst()
{
    AcpImageAttachmentList list;
    QSignalSpy rejectSpy(&list, &AcpImageAttachmentList::imageRejected);

    for (int i = 0; i < AcpImageAttachmentList::kMaxItems; ++i) {
        QVERIFY(list.tryAddImage(m_png, QStringLiteral("a.png")));
    }
    QCOMPARE(list.count(), AcpImageAttachmentList::kMaxItems);

    QVERIFY(!list.tryAddImage(m_png, QStringLiteral("a.png")));
    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(rejectSpy.first().first().toString(), QStringLiteral("Maximum 20 images"));
}

void TestAcpImageAttachmentLimits::rejects_oversize_jpeg()
{
    AcpImageAttachmentList list;
    QSignalSpy rejectSpy(&list, &AcpImageAttachmentList::imageRejected);
    QVERIFY(!list.tryAddImage(m_largeJpeg, QStringLiteral("big.jpg")));
    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(rejectSpy.first().first().toString(), QStringLiteral("Image too large (max 5 MB)"));
}

void TestAcpImageAttachmentLimits::rejects_unsupported_bmp()
{
    AcpImageAttachmentList list;
    QSignalSpy rejectSpy(&list, &AcpImageAttachmentList::imageRejected);
    QVERIFY(!list.tryAddImage(m_bmp, QStringLiteral("x.bmp")));
    QCOMPARE(rejectSpy.count(), 1);
    QCOMPARE(rejectSpy.first().first().toString(), QStringLiteral("Unsupported image type"));
}

void TestAcpImageAttachmentLimits::takeAll_returns_pairs_and_clears()
{
    AcpImageAttachmentList list;
    QVERIFY(list.tryAddImage(m_png, QStringLiteral("a.png")));
    QVERIFY(list.tryAddImage(m_png, QStringLiteral("b.png")));
    QCOMPARE(list.count(), 2);

    auto items = list.takeAll();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items[0].second, QStringLiteral("image/png"));
    QCOMPARE(list.count(), 0);
    QVERIFY(!list.isNonEmpty());
}

void TestAcpImageAttachmentLimits::png_bytes_roundtrip()
{
    AcpImageAttachmentList list;
    QVERIFY(list.tryAddImage(m_png, QStringLiteral("a.png")));
    auto items = list.takeAll();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].first, m_png);
}

QTEST_MAIN(TestAcpImageAttachmentLimits)
#include "test_acp_image_attachment_limits.moc"
