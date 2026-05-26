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

#ifndef ACP_IMAGE_ATTACHMENT_LIST_H
#define ACP_IMAGE_ATTACHMENT_LIST_H

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QDragEnterEvent;
class QDropEvent;

// Horizontal queue of image attachments awaiting send. Validates MIME,
// size, and queue length. Emits imageRejected with a localized reason.
class AcpImageAttachmentList : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kMaxItems = 20;
    static constexpr qint64 kMaxItemBytes = 5LL * 1024 * 1024;

    explicit AcpImageAttachmentList(QWidget *parent = nullptr);

    // Try to enqueue raw image bytes. Returns false on validation failure
    // (also emits imageRejected with a translated message).
    bool tryAddImage(const QByteArray &data, const QString &filenameHint);

    // Convenience: read a file and forward to tryAddImage.
    bool addFileByPath(const QString &filePath);

    bool isNonEmpty() const { return !m_items.isEmpty(); }
    int count() const { return m_items.size(); }

    // Remove all items and return them in submission order.
    QVector<QPair<QByteArray, QString>> takeAll();

    // Return {data, mimeType} pairs without clearing the queue.
    QVector<QPair<QByteArray, QString>> peekAll() const;

    void clear();

signals:
    void imageRejected(const QString &reason);
    void contentsChanged();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct Item {
        QByteArray data;
        QString mimeType;
        QString fileName;
        QWidget *widget = nullptr;
    };

    QString detectMimeType(const QByteArray &data) const;
    void rebuildLayout();
    void removeItemAt(int index);

    QHBoxLayout *m_layout = nullptr;
    QVector<Item> m_items;
};

#endif // ACP_IMAGE_ATTACHMENT_LIST_H
