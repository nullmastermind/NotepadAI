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

#ifndef GIT_HISTORY_MODEL_H
#define GIT_HISTORY_MODEL_H

#include "GitCommitInfo.h"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

#include <cstdint>

// QAbstractListModel of GitCommitInfo rows. Append-only on the fetch path;
// clear() on repo / branch change.
//
// Custom roles avoid lazy UTF-8 conversion in the delegate: the delegate
// asks for QByteArray (raw) via *BytesRole, then does fromUtf8 on demand.
// Display data (QString) is only created when the delegate paints — Qt's
// view virtualizes so only visible rows pay the conversion cost.
class GitHistoryModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles : std::uint16_t {
        // Lazy QString-converted roles for the delegate.
        SubjectRole = Qt::UserRole + 1,
        AuthorNameRole,
        AuthorEmailRole,
        ShortShaRole,
        FullShaRole,
        // Raw bytes (zero-copy hand-off if the delegate needs them).
        SubjectBytesRole,
        // Raw types.
        CtimeRole,        // qint64 epoch seconds
        IsMergeRole,      // bool
        ParentCountRole,  // int
    };

    explicit GitHistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Append a chunk of newly-streamed commits. Dedupes by SHA (skips already-
    // present rows). beginInsertRows / endInsertRows around the appended span.
    void appendChunk(const QVector<GitCommitInfo> &chunk);

    // Clear all rows. Used on repo / branch-scope change.
    void clear();

    int count() const { return m_rows.size(); }

    // Row accessor by SHA (linear in deduper map; O(1) average).
    const GitCommitInfo *findBySha(const QByteArray &sha) const;

    // Const reference to row by index (bounds-checked).
    const GitCommitInfo *rowAt(int index) const {
        return (index >= 0 && index < m_rows.size()) ? &m_rows[index] : nullptr;
    }

private:
    QVector<GitCommitInfo>     m_rows;
    // SHA -> index in m_rows. Used for both dedup on append and findBySha().
    QHash<QByteArray, int>     m_indexBySha;
};

#endif // GIT_HISTORY_MODEL_H
