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

#ifndef GIT_HISTORY_ITEM_DELEGATE_H
#define GIT_HISTORY_ITEM_DELEGATE_H

#include <QStyledItemDelegate>
#include <QString>

// Two-line delegate for the history list:
//   line 1 (display font): commit subject (bold, search-match highlighted)
//   line 2 (muted, smaller): authorName • relativeTime • shortSha
//
// Uses palette(placeholder-text) for the muted line per ui-dna.md. Row
// height is uniform — caller sets QListView::setUniformItemSizes(true).
class GitHistoryItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit GitHistoryItemDelegate(QObject *parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    // Set the active filter query — substring is highlighted in the subject.
    // Empty = no highlight.
    void setFilterQuery(const QString &query);
    QString filterQuery() const { return m_filter; }

private:
    QString m_filter;

    // Format epoch seconds into a localized relative time string. Uses
    // tr("%n minute(s) ago", "", n) Qt plural syntax so Crowdin can pick
    // up the strings.
    static QString formatRelativeTime(qint64 ctime);
};

#endif // GIT_HISTORY_ITEM_DELEGATE_H
