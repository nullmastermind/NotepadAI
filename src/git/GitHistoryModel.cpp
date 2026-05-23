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

#include "GitHistoryModel.h"

#include "ProfileScope.h"

GitHistoryModel::GitHistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int GitHistoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_rows.size();
}

QVariant GitHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};
    const int r = index.row();
    if (r < 0 || r >= m_rows.size()) return {};
    const GitCommitInfo &c = m_rows[r];

    switch (role) {
    case Qt::DisplayRole:
    case SubjectRole:
        return QString::fromUtf8(c.subject);
    case AuthorNameRole:
        return QString::fromUtf8(c.authorName);
    case AuthorEmailRole:
        return QString::fromUtf8(c.authorEmail);
    case ShortShaRole:
        return QString::fromLatin1(c.shortSha());
    case FullShaRole:
        return QString::fromLatin1(c.sha);
    case SubjectBytesRole:
        return c.subject;
    case CtimeRole:
        return QVariant::fromValue<qint64>(c.ctime);
    case IsMergeRole:
        return c.isMerge();
    case ParentCountRole:
        return c.parentCount();
    default:
        return {};
    }
}

QHash<int, QByteArray> GitHistoryModel::roleNames() const
{
    auto roles = QAbstractListModel::roleNames();
    roles[SubjectRole]      = "subject";
    roles[AuthorNameRole]   = "authorName";
    roles[AuthorEmailRole]  = "authorEmail";
    roles[ShortShaRole]     = "shortSha";
    roles[FullShaRole]      = "fullSha";
    roles[SubjectBytesRole] = "subjectBytes";
    roles[CtimeRole]        = "ctime";
    roles[IsMergeRole]      = "isMerge";
    roles[ParentCountRole]  = "parentCount";
    return roles;
}

void GitHistoryModel::appendChunk(const QVector<GitCommitInfo> &chunk)
{
    PROFILE_SCOPE("GitHistoryModel::appendChunk");
    if (chunk.isEmpty()) return;

    // First pass: drop dupes (a refetch can re-emit commits already in the
    // model; we want positional stability for the user's scroll, so dropping
    // dupes silently is correct). Build a filtered staging vector so we can
    // do a single beginInsertRows over a contiguous range.
    QVector<GitCommitInfo> staged;
    staged.reserve(chunk.size());
    for (const auto &c : chunk) {
        if (c.sha.isEmpty()) continue;
        if (m_indexBySha.contains(c.sha)) continue;
        staged.append(c);
    }
    if (staged.isEmpty()) return;

    const int first = m_rows.size();
    const int last  = first + staged.size() - 1;
    beginInsertRows(QModelIndex(), first, last);
    m_rows.reserve(m_rows.size() + staged.size());
    int idx = first;
    for (auto &c : staged) {
        m_indexBySha.insert(c.sha, idx);
        m_rows.append(std::move(c));
        ++idx;
    }
    endInsertRows();
}

void GitHistoryModel::clear()
{
    if (m_rows.isEmpty()) return;
    beginResetModel();
    m_rows.clear();
    m_indexBySha.clear();
    endResetModel();
}

const GitCommitInfo *GitHistoryModel::findBySha(const QByteArray &sha) const
{
    const auto it = m_indexBySha.constFind(sha);
    if (it == m_indexBySha.constEnd()) return nullptr;
    return &m_rows[*it];
}
