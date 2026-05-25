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

#include "BranchPickerPopup.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace {
constexpr int RoleKind = Qt::UserRole + 1;    // 0 = local, 1 = remote, 2 = create-current, 3 = create-default
constexpr int RolePayload = Qt::UserRole + 2; // branch name or create base
}

QString BranchPickerPopup::sanitizeBranchName(const QString &raw)
{
    QString s = raw.trimmed();
    s.replace(QLatin1Char(' '), QLatin1Char('-'));
    // Strip characters git rejects (basic subset).
    static const QRegularExpression bad(QStringLiteral("[~^:?*\\[\\\\]"));
    s.remove(bad);
    return s;
}

BranchPickerPopup::BranchPickerPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup)
{
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::StrongFocus);

    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("Search branches…"));
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

    connect(m_filter, &QLineEdit::textChanged, this, &BranchPickerPopup::rebuild);
    connect(m_list, &QAbstractItemView::activated, this, &BranchPickerPopup::onActivated);
}

void BranchPickerPopup::setBranches(const QStringList &local, const QStringList &remote,
                                    const QString &current, const QString &defaultBranch)
{
    m_local = local;
    m_remote = remote;
    m_current = current;
    m_default = defaultBranch;
    m_filter->clear();
    rebuild();
}

void BranchPickerPopup::popupAt(const QPoint &globalPos)
{
    // Clamp inside the current screen so the popup is fully visible.
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

void BranchPickerPopup::rebuild()
{
    m_model->clear();
    const QString q = m_filter->text().trimmed();
    const QString qLower = q.toLower();

    auto addHeader = [&](const QString &text) {
        auto *it = new QStandardItem(text);
        it->setEnabled(false);
        auto f = it->font(); f.setBold(true); it->setFont(f);
        m_model->appendRow(it);
    };
    auto matches = [&](const QString &name) {
        return q.isEmpty() || name.toLower().contains(qLower);
    };

    bool sawAny = false;

    QStringList locals;
    for (const auto &b : m_local) if (matches(b)) locals.append(b);
    if (!locals.isEmpty()) {
        addHeader(tr("── Local ──"));
        for (const auto &b : locals) {
            QString display = b;
            if (b == m_current) display = QStringLiteral("✓ ") + b;
            else                display = QStringLiteral("   ") + b;
            auto *it = new QStandardItem(display);
            it->setData(0, RoleKind);
            it->setData(b, RolePayload);
            m_model->appendRow(it);
            sawAny = true;
        }
    }

    QStringList remotes;
    for (const auto &b : m_remote) if (matches(b)) remotes.append(b);
    if (!remotes.isEmpty()) {
        addHeader(tr("── Remote ──"));
        for (const auto &b : remotes) {
            auto *it = new QStandardItem(QStringLiteral("   ") + b);
            it->setData(1, RoleKind);
            it->setData(b, RolePayload);
            m_model->appendRow(it);
            sawAny = true;
        }
    }

    // "Create branch from query" — only when query has no exact local match.
    const QString sanitized = sanitizeBranchName(q);
    if (!sanitized.isEmpty() && !m_local.contains(sanitized)) {
        addHeader(tr("── Create ──"));
        QString fromCurrent = m_current.isEmpty() ? m_default : m_current;
        if (!fromCurrent.isEmpty()) {
            auto *it = new QStandardItem(tr("+ Create \"%1\" from %2").arg(sanitized, fromCurrent));
            it->setData(2, RoleKind);
            it->setData(sanitized, RolePayload);
            m_model->appendRow(it);
            sawAny = true;
        }
        if (!m_default.isEmpty() && m_default != fromCurrent) {
            auto *it = new QStandardItem(tr("+ Create \"%1\" from %2 (default)").arg(sanitized, m_default));
            it->setData(3, RoleKind);
            it->setData(sanitized, RolePayload);
            m_model->appendRow(it);
            sawAny = true;
        }
    }

    if (!sawAny) {
        auto *it = new QStandardItem(tr("No matches"));
        it->setEnabled(false);
        m_model->appendRow(it);
    }

    // Auto-select first enabled item.
    for (int r = 0; r < m_model->rowCount(); ++r) {
        if (m_model->item(r)->isEnabled()) {
            m_list->setCurrentIndex(m_model->index(r, 0));
            break;
        }
    }
}

void BranchPickerPopup::onActivated(const QModelIndex &index)
{
    if (!index.isValid()) return;
    auto *it = m_model->itemFromIndex(index);
    if (!it || !it->isEnabled()) return;
    const int kind = it->data(RoleKind).toInt();
    const QString payload = it->data(RolePayload).toString();
    if (kind == 0) {
        emit branchSelected(payload);
        close();
    } else if (kind == 1) {
        emit branchSelected(payload);
        close();
    } else if (kind == 2) {
        emit createBranchRequested(payload, m_current.isEmpty() ? m_default : m_current);
        close();
    } else if (kind == 3) {
        emit createBranchRequested(payload, m_default);
        close();
    }
}

bool BranchPickerPopup::eventFilter(QObject *o, QEvent *e)
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
        if (ke->key() == Qt::Key_Escape) {
            close();
            return true;
        }
    }
    return QWidget::eventFilter(o, e);
}
