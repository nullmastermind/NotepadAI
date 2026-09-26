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

#ifndef WORKTREE_PICKER_POPUP_H
#define WORKTREE_PICKER_POPUP_H

#include "GitRepoInfo.h"

#include <QWidget>

class QLineEdit;
class QListView;
class QStandardItemModel;

class WorktreePickerPopup : public QWidget
{
    Q_OBJECT
public:
    explicit WorktreePickerPopup(QWidget *parent = nullptr);

    void setWorktrees(const GitRepoInfos &repos, const QString &currentToplevel,
                      const QString &fallbackMergeTarget);
    void popupAt(const QPoint &globalPos);

signals:
    void switchRequested(const QString &toplevel);
    void removeRequested(const QString &toplevel);
    void mergeRemoveRequested(const QString &toplevel);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private slots:
    void rebuild();
    void onActivated(const QModelIndex &index);

private:
    void showItemMenu(const QModelIndex &index);

    QLineEdit *m_filter;
    QListView *m_list;
    QStandardItemModel *m_model;
    GitRepoInfos m_repos;
    QString m_current;
    QString m_fallbackMergeTarget;
};

#endif // WORKTREE_PICKER_POPUP_H
