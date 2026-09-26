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

#include "WorktreePickerPopup.h"

#include "GitRepoDiscovery.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QScreen>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace {
constexpr int RoleToplevel = Qt::UserRole + 1;
constexpr int RoleBranch = Qt::UserRole + 2;
constexpr int RoleParent = Qt::UserRole + 3;
}

WorktreePickerPopup::WorktreePickerPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::StrongFocus);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Search worktrees…"));
    m_filter->setClearButtonEnabled(true);
    m_filter->installEventFilter(this);

    m_model = new QStandardItemModel(this);
    m_list = new QListView(this);
    m_list->setModel(m_model);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    m_list->installEventFilter(this);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);
    lay->addWidget(m_filter);
    lay->addWidget(m_list, 1);

    setMinimumWidth(320);
    setMinimumHeight(360);

    connect(m_filter, &QLineEdit::textChanged, this, &WorktreePickerPopup::rebuild);
    connect(m_list, &QAbstractItemView::clicked, this, &WorktreePickerPopup::onActivated);
}

void WorktreePickerPopup::setWorktrees(const GitRepoInfos &repos, const QString &currentToplevel,
                                      const QString &fallbackMergeTarget)
{
    m_repos = repos;
    m_current = QDir::cleanPath(currentToplevel);
    m_fallbackMergeTarget = fallbackMergeTarget;
    m_filter->clear();
    rebuild();
}

void WorktreePickerPopup::popupAt(const QPoint &globalPos)
{
    QPoint pos = globalPos;
    if (auto *scr = QGuiApplication::screenAt(globalPos)) {
        const QRect g = scr->availableGeometry();
        pos.setX(qBound(g.left(), pos.x(), g.right() - width()));
        pos.setY(qBound(g.top(), pos.y(), g.bottom() - height()));
    }
    move(pos);
    show();
    m_filter->setFocus();
}

void WorktreePickerPopup::rebuild()
{
    m_model->clear();
    const GitRepoInfos hit = GitRepoDiscovery::filteredWorktrees(m_repos, m_filter->text());

    auto addHeader = [&](const QString &text) {
        auto *it = new QStandardItem(text);
        it->setEnabled(false);
        auto f = it->font();
        f.setBold(true);
        it->setFont(f);
        m_model->appendRow(it);
    };

    QString lastParent;
    bool sawAny = false;
    QModelIndex currentIdx;
    for (const auto &wt : hit) {
        const QString parent = QDir::cleanPath(wt.parentToplevel);
        if (parent != lastParent) {
            lastParent = parent;
            const QString header = parent.isEmpty()
                ? tr("── Worktrees ──")
                : tr("── %1 ──").arg(QFileInfo(parent).fileName());
            addHeader(header);
        }
        const QString name = wt.displayName.isEmpty() ? wt.toplevel : wt.displayName;
        const bool isCurrent = QDir::cleanPath(wt.toplevel) == m_current;
        QString display = isCurrent ? QStringLiteral("✓ ") : QStringLiteral("   ");
        display += name;
        if (!wt.branch.isEmpty() && wt.branch != name)
            display += QStringLiteral("  ·  ") + wt.branch;
        auto *it = new QStandardItem(display);
        it->setData(wt.toplevel, RoleToplevel);
        it->setData(wt.branch, RoleBranch);
        it->setData(wt.parentToplevel, RoleParent);
        it->setToolTip(wt.toplevel);
        m_model->appendRow(it);
        if (isCurrent)
            currentIdx = m_model->index(m_model->rowCount() - 1, 0);
        sawAny = true;
    }

    if (!sawAny) {
        auto *it = new QStandardItem(tr("No matches"));
        it->setEnabled(false);
        m_model->appendRow(it);
    }

    if (currentIdx.isValid()) {
        m_list->setCurrentIndex(currentIdx);
    } else {
        for (int r = 0; r < m_model->rowCount(); ++r) {
            if (m_model->item(r)->isEnabled()) {
                m_list->setCurrentIndex(m_model->index(r, 0));
                break;
            }
        }
    }
}

void WorktreePickerPopup::onActivated(const QModelIndex &index)
{
    showItemMenu(index);
}

void WorktreePickerPopup::showItemMenu(const QModelIndex &index)
{
    if (!index.isValid()) return;
    auto *it = m_model->itemFromIndex(index);
    if (!it || !it->isEnabled()) return;

    const QString toplevel = it->data(RoleToplevel).toString();
    const QString branch = it->data(RoleBranch).toString();
    GitRepoInfo info;
    info.toplevel = toplevel;
    info.parentToplevel = it->data(RoleParent).toString();
    info.branch = branch;
    info.isWorktree = true;
    const QString into = GitRepoDiscovery::worktreeMergeTargetName(info, m_fallbackMergeTarget);
    const bool isCurrent = QDir::cleanPath(toplevel) == m_current;

    QMenu menu(this);
    if (!isCurrent) {
        QAction *aSwitch = menu.addAction(tr("&Switch to this worktree"));
        connect(aSwitch, &QAction::triggered, this, [this, toplevel]() {
            emit switchRequested(toplevel);
            close();
        });
        menu.addSeparator();
    }
    QAction *aRemove = menu.addAction(tr("&Remove..."));
    connect(aRemove, &QAction::triggered, this, [this, toplevel]() {
        emit removeRequested(toplevel);
        close();
    });
    QAction *aMerge = menu.addAction(tr("&Merge into %1 and remove...").arg(into));
    aMerge->setEnabled(!branch.isEmpty());
    if (branch.isEmpty())
        aMerge->setToolTip(tr("Detached worktree has no branch to merge."));
    connect(aMerge, &QAction::triggered, this, [this, toplevel]() {
        emit mergeRemoveRequested(toplevel);
        close();
    });

    const QRect itemRect = m_list->visualRect(index);
    menu.exec(m_list->mapToGlobal(itemRect.topRight()));
}

bool WorktreePickerPopup::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (o == m_filter) {
            if (ke->key() == Qt::Key_Down) {
                m_list->setFocus();
                return false;
            }
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                onActivated(m_list->currentIndex());
                return true;
            }
        }
        if (o == m_list && ke->key() == Qt::Key_Delete) {
            const QModelIndex idx = m_list->currentIndex();
            if (idx.isValid()) {
                auto *it = m_model->itemFromIndex(idx);
                if (it && it->isEnabled()) {
                    emit removeRequested(it->data(RoleToplevel).toString());
                    close();
                    return true;
                }
            }
        }
        if (ke->key() == Qt::Key_Escape) {
            close();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}
