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

#ifndef GIT_REPO_MODEL_H
#define GIT_REPO_MODEL_H

#include "GitRepoInfo.h"

#include <QAbstractListModel>

#include <cstdint>

class GitRepoModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles : std::uint16_t {
        ToplevelRole = Qt::UserRole + 1,
        DepthRole,
        IsSubmoduleRole,
        IsWorktreeRole,
        BranchRole
    };

    explicit GitRepoModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void setRepos(GitRepoInfos repos);
    int indexOf(const QString &toplevel) const;
    const GitRepoInfo *infoAt(int row) const;
    GitRepoInfos repos() const { return m_repos; }
    bool isEmpty() const { return m_repos.isEmpty(); }

private:
    GitRepoInfos m_repos;
};

#endif // GIT_REPO_MODEL_H
