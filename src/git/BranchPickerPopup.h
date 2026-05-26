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

#ifndef BRANCH_PICKER_POPUP_H
#define BRANCH_PICKER_POPUP_H

#include <QStringList>
#include <QWidget>

class QLineEdit;
class QListView;
class QStandardItemModel;

class BranchPickerPopup : public QWidget
{
    Q_OBJECT
public:
    explicit BranchPickerPopup(QWidget *parent = nullptr);

    void setBranches(const QStringList &local,
                     const QStringList &remote,
                     const QString &current,
                     const QString &defaultBranch = QStringLiteral("main"),
                     bool detachedHead = false);
    void popupAt(const QPoint &globalPos);

    // In select-only mode, clicking a branch emits branchSelected and closes
    // immediately — no context menu with Checkout/New Branch/Set Upstream.
    // Use for merge/rebase target selection.
    void setSelectOnly(bool on, const QString &title = {});

    static QString sanitizeBranchName(const QString &raw);

signals:
    void checkoutRequested(const QString &name);
    void branchSelected(const QString &name);
    void createBranchRequested(const QString &name, const QString &base, bool setUpstream);
    void setUpstreamRequested(const QString &remoteBranch);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private slots:
    void rebuild();
    void onActivated(const QModelIndex &index);

private:
    void showItemMenu(const QModelIndex &index);
    void showNewBranchDialog(const QString &base);

    QLineEdit *m_filter;
    QListView *m_list;
    QStandardItemModel *m_model;
    QString m_current;
    QString m_default;
    QStringList m_local;
    QStringList m_remote;
    bool m_detachedHead = false;
    bool m_selectOnly = false;
};

#endif // BRANCH_PICKER_POPUP_H
