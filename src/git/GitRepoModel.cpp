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

#include "GitRepoModel.h"

GitRepoModel::GitRepoModel(QObject *parent) : QAbstractListModel(parent) {}

int GitRepoModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_repos.size();
}

QVariant GitRepoModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_repos.size()) return {};
    const GitRepoInfo &r = m_repos.at(index.row());
    switch (role) {
        case Qt::DisplayRole: {
            if (r.isWorktree) {
                const QString name = r.displayName.isEmpty() ? r.toplevel : r.displayName;
                const QString label = QStringLiteral("%1 (worktree)").arg(name);
                if (r.depth <= 0) return label;
                return QStringLiteral("%1%2").arg(QString(r.depth * 2, QChar(0x2007)), label);
            }
            if (r.depth <= 0) return r.displayName;
            return QStringLiteral("%1%2").arg(QString(r.depth * 2, QChar(0x2007)), r.displayName);
        }
        case Qt::ToolTipRole: {
            if (r.isWorktree && !r.branch.isEmpty())
                return QStringLiteral("%1\n%2").arg(r.toplevel, r.branch);
            return r.toplevel;
        }
        case ToplevelRole:    return r.toplevel;
        case DepthRole:       return r.depth;
        case IsSubmoduleRole: return r.isSubmodule;
        case IsWorktreeRole:  return r.isWorktree;
        case BranchRole:      return r.branch;
        default: return {};
    }
}

void GitRepoModel::setRepos(GitRepoInfos repos)
{
    beginResetModel();
    m_repos = std::move(repos);
    endResetModel();
}

int GitRepoModel::indexOf(const QString &toplevel) const
{
    for (int i = 0; i < m_repos.size(); ++i)
        if (m_repos.at(i).toplevel == toplevel) return i;
    return -1;
}

const GitRepoInfo *GitRepoModel::infoAt(int row) const
{
    if (row < 0 || row >= m_repos.size()) return nullptr;
    return &m_repos.at(row);
}
