/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
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

#include "AcpImageAttachmentList.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPixmap>
#include <QSet>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString humanSize(qint64 bytes)
{
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024);
    return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
}

} // namespace

AcpImageAttachmentList::AcpImageAttachmentList(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);
    m_layout->addStretch();

    setAcceptDrops(true);
    hide(); // shown only when items present
}

QString AcpImageAttachmentList::detectMimeType(const QByteArray &data) const
{
    QMimeDatabase db;
    const QMimeType type = db.mimeTypeForData(data);
    return type.name();
}

bool AcpImageAttachmentList::tryAddImage(const QByteArray &data, const QString &filenameHint)
{
    if (m_items.size() >= kMaxItems) {
        emit imageRejected(tr("Maximum 20 images"));
        return false;
    }
    if (static_cast<qint64>(data.size()) > kMaxItemBytes) {
        emit imageRejected(tr("Image too large (max 5 MB)"));
        return false;
    }

    const QString mime = detectMimeType(data);
    static const QSet<QString> allowed = {
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/png"),
        QStringLiteral("image/gif"),
        QStringLiteral("image/webp"),
    };
    if (!allowed.contains(mime)) {
        emit imageRejected(tr("Unsupported image type"));
        return false;
    }

    Item item;
    item.data = data;
    item.mimeType = mime;
    item.fileName = filenameHint.isEmpty() ? tr("image") : filenameHint;
    m_items.append(item);
    rebuildLayout();
    show();
    emit contentsChanged();
    return true;
}

bool AcpImageAttachmentList::addFileByPath(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        emit imageRejected(tr("Could not read file"));
        return false;
    }
    const QByteArray data = f.readAll();
    return tryAddImage(data, QFileInfo(filePath).fileName());
}

QVector<QPair<QByteArray, QString>> AcpImageAttachmentList::takeAll()
{
    QVector<QPair<QByteArray, QString>> out;
    out.reserve(m_items.size());
    for (const Item &item : m_items) {
        out.append({item.data, item.mimeType});
    }
    clear();
    return out;
}

QVector<QPair<QByteArray, QString>> AcpImageAttachmentList::peekAll() const
{
    QVector<QPair<QByteArray, QString>> out;
    out.reserve(m_items.size());
    for (const Item &item : m_items) {
        out.append({item.data, item.mimeType});
    }
    return out;
}

void AcpImageAttachmentList::clear()
{
    for (Item &item : m_items) {
        if (item.widget) {
            m_layout->removeWidget(item.widget);
            item.widget->deleteLater();
            item.widget = nullptr;
        }
    }
    m_items.clear();
    hide();
    emit contentsChanged();
}

void AcpImageAttachmentList::removeItemAt(int index)
{
    if (index < 0 || index >= m_items.size()) return;
    if (m_items[index].widget) {
        m_layout->removeWidget(m_items[index].widget);
        m_items[index].widget->deleteLater();
    }
    m_items.remove(index);
    rebuildLayout();
    if (m_items.isEmpty()) {
        hide();
    }
    emit contentsChanged();
}

void AcpImageAttachmentList::rebuildLayout()
{
    // Tear down stretch + any prior item widgets.
    while (m_layout->count() > 0) {
        QLayoutItem *li = m_layout->takeAt(0);
        if (li->widget()) {
            // Don't delete — Item owns the lifetime via deleteLater on removal.
            li->widget()->setParent(this);
        }
        delete li;
    }

    for (int i = 0; i < m_items.size(); ++i) {
        Item &item = m_items[i];
        if (!item.widget) {
            auto *itemWidget = new QWidget(this);
            auto *vl = new QVBoxLayout(itemWidget);
            vl->setContentsMargins(2, 2, 2, 2);
            vl->setSpacing(4);

            auto *thumb = new QLabel(itemWidget);
            QPixmap pix;
            pix.loadFromData(item.data);
            if (!pix.isNull()) {
                thumb->setPixmap(pix.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            } else {
                thumb->setText(QStringLiteral("[img]"));
            }
            thumb->setFixedSize(48, 48);
            thumb->setAlignment(Qt::AlignCenter);

            auto *nameLabel = new QLabel(itemWidget);
            const QFontMetrics fm(nameLabel->font());
            nameLabel->setText(fm.elidedText(item.fileName, Qt::ElideMiddle, 80));
            nameLabel->setToolTip(item.fileName);
            nameLabel->setAlignment(Qt::AlignHCenter);

            auto *sizeLabel = new QLabel(humanSize(item.data.size()), itemWidget);
            sizeLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
            sizeLabel->setAlignment(Qt::AlignHCenter);

            // Overlay remove button — small circular pill anchored to the
            // thumbnail's top-right corner. The thumb is over an arbitrary
            // image, so palette chrome doesn't apply: a semi-transparent
            // black pill with a white glyph reads on any image.
            auto *removeBtn = new QToolButton(itemWidget);
            removeBtn->setText(QStringLiteral("×"));
            removeBtn->setToolTip(tr("Remove"));
            removeBtn->setFixedSize(18, 18);
            removeBtn->setCursor(Qt::PointingHandCursor);
            removeBtn->setFocusPolicy(Qt::NoFocus);
            removeBtn->setStyleSheet(QStringLiteral(
                "QToolButton {"
                "  background: rgba(0,0,0,0.55);"
                "  color: white;"
                "  border: 1px solid rgba(255,255,255,0.25);"
                "  border-radius: 9px;"
                "  padding: 0 0 2px 0;"
                "  font-size: 12px;"
                "  font-weight: bold;"
                "}"
                "QToolButton:hover {"
                "  background: rgba(0,0,0,0.78);"
                "  border-color: rgba(255,255,255,0.45);"
                "}"
                "QToolButton:pressed {"
                "  background: rgba(0,0,0,0.92);"
                "}"));
            connect(removeBtn, &QToolButton::clicked, this, [this, itemWidget]() {
                for (int k = 0; k < m_items.size(); ++k) {
                    if (m_items[k].widget == itemWidget) {
                        removeItemAt(k);
                        break;
                    }
                }
            });

            auto *thumbHost = new QWidget(itemWidget);
            thumbHost->setFixedSize(48, 48);
            auto *thumbGrid = new QGridLayout(thumbHost);
            thumbGrid->setContentsMargins(0, 0, 0, 0);
            thumbGrid->setSpacing(0);
            thumbGrid->addWidget(thumb, 0, 0);
            // Same cell, second widget — aligned to the corner so it overlays
            // the thumbnail. raise() ensures paint order even when the layout
            // engine adds widgets in declaration order.
            thumbGrid->addWidget(removeBtn, 0, 0, Qt::AlignTop | Qt::AlignRight);
            removeBtn->raise();

            vl->addWidget(thumbHost, 0, Qt::AlignHCenter);
            vl->addWidget(nameLabel);
            vl->addWidget(sizeLabel);
            item.widget = itemWidget;
        }
        m_layout->addWidget(item.widget);
    }
    m_layout->addStretch();
}

void AcpImageAttachmentList::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) {
        event->acceptProposedAction();
    }
}

void AcpImageAttachmentList::dropEvent(QDropEvent *event)
{
    const QMimeData *md = event->mimeData();
    if (md->hasUrls()) {
        const QList<QUrl> urls = md->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile()) {
                addFileByPath(url.toLocalFile());
            }
        }
        event->acceptProposedAction();
        return;
    }
    if (md->hasImage()) {
        // Fallback: serialize via the raw image-data MIME if present.
        const QByteArray raw = md->data(QStringLiteral("image/png"));
        if (!raw.isEmpty()) {
            tryAddImage(raw, tr("pasted.png"));
        }
        event->acceptProposedAction();
    }
}
