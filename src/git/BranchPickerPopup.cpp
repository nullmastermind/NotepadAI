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

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QStyle>
#include <QVBoxLayout>

namespace {
constexpr int RoleKind = Qt::UserRole + 1;    // 0 = local, 1 = remote
constexpr int RolePayload = Qt::UserRole + 2; // branch name
}

bool BranchPickerPopup::confirmForceAction(QWidget *parent,
                                           const QString &title,
                                           const QString &htmlMessage,
                                           const QString &okText,
                                           const QString &forceLabel,
                                           bool *force)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(320);

    auto *layout = new QVBoxLayout(&dlg);

    auto *topRow = new QHBoxLayout;
    auto *icon = new QLabel(&dlg);
    icon->setPixmap(dlg.style()->standardPixmap(QStyle::SP_MessageBoxWarning));
    auto *msg = new QLabel(htmlMessage, &dlg);
    msg->setWordWrap(true);
    msg->setTextFormat(Qt::RichText);
    topRow->addWidget(icon, 0, Qt::AlignTop);
    topRow->addWidget(msg, 1);
    layout->addLayout(topRow);

    auto *forceCheck = new QCheckBox(forceLabel, &dlg);
    forceCheck->setChecked(false);
    layout->addWidget(forceCheck);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(okText);
    layout->addWidget(buttons);

    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return false;
    if (force) *force = forceCheck->isChecked();
    return true;
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
    connect(m_list, &QAbstractItemView::clicked, this, &BranchPickerPopup::onActivated);
}

void BranchPickerPopup::setBranches(const QStringList &local, const QStringList &remote,
                                    const QString &current, const QString &defaultBranch,
                                    bool detachedHead)
{
    m_local = local;
    m_remote = remote;
    m_current = current;
    m_default = defaultBranch;
    m_detachedHead = detachedHead;
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

void BranchPickerPopup::setSelectOnly(bool on, const QString &title)
{
    m_selectOnly = on;
    if (on && !title.isEmpty())
        m_filter->setPlaceholderText(title);
    else
        m_filter->setPlaceholderText(tr("Search branches…"));
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
            QString display;
            if (m_selectOnly) {
                display = (b == m_current) ? QStringLiteral("✓ ") + b
                                           : QStringLiteral("   ") + b;
            } else {
                display = (b == m_current) ? QStringLiteral("✓ ") + b + QStringLiteral("  ›")
                                           : QStringLiteral("   ") + b + QStringLiteral("  ›");
            }
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
            QString display = m_selectOnly ? QStringLiteral("   ") + b
                                           : QStringLiteral("   ") + b + QStringLiteral("  ›");
            auto *it = new QStandardItem(display);
            it->setData(1, RoleKind);
            it->setData(b, RolePayload);
            m_model->appendRow(it);
            sawAny = true;
        }
    }

    if (!sawAny) {
        auto *it = new QStandardItem(tr("No matches"));
        it->setEnabled(false);
        m_model->appendRow(it);
    }

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

    if (m_selectOnly) {
        const QString payload = it->data(RolePayload).toString();
        emit branchSelected(payload);
        close();
        return;
    }
    showItemMenu(index);
}

void BranchPickerPopup::showItemMenu(const QModelIndex &index)
{
    auto *it = m_model->itemFromIndex(index);
    if (!it || !it->isEnabled()) return;
    const int kind = it->data(RoleKind).toInt();
    const QString payload = it->data(RolePayload).toString();
    const bool isRemote = (kind == 1);
    const bool isCurrent = (!isRemote && payload == m_current);

    QMenu menu(this);

    if (!isCurrent) {
        QAction *aCheckout = menu.addAction(tr("&Checkout"));
        connect(aCheckout, &QAction::triggered, this, [this, payload]() {
            emit checkoutRequested(payload);
            close();
        });
    }

    QAction *aNewBranch = menu.addAction(tr("&New Branch…"));
    connect(aNewBranch, &QAction::triggered, this, [this, payload]() {
        showNewBranchDialog(payload);
    });

    if (isRemote && !m_detachedHead) {
        QAction *aSetUpstream = menu.addAction(tr("&Set as Upstream"));
        connect(aSetUpstream, &QAction::triggered, this, [this, payload]() {
            emit setUpstreamRequested(payload);
            close();
        });
    }

    if (!isRemote) {
        menu.addSeparator();
        QAction *aRename = menu.addAction(tr("&Rename…"));
        connect(aRename, &QAction::triggered, this, [this, payload]() {
            showRenameBranchDialog(payload);
        });

        if (!isCurrent) {
            QAction *aDelete = menu.addAction(tr("&Delete…"));
            connect(aDelete, &QAction::triggered, this, [this, payload]() {
                showDeleteBranchDialog(payload);
            });
        }
    }

    const QRect itemRect = m_list->visualRect(index);
    const QPoint pos = m_list->mapToGlobal(itemRect.topRight());
    menu.exec(pos);
}

void BranchPickerPopup::showNewBranchDialog(const QString &base)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("New Branch from %1").arg(base));
    dlg.setMinimumWidth(320);

    auto *layout = new QVBoxLayout(&dlg);

    auto *nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(tr("Branch name"));

    auto *previewLabel = new QLabel(&dlg);
    previewLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));

    auto *upstreamCheck = new QCheckBox(tr("Set upstream tracking"), &dlg);
    upstreamCheck->setChecked(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

    layout->addWidget(new QLabel(tr("Create new branch from <b>%1</b>:").arg(base), &dlg));
    layout->addWidget(nameEdit);
    layout->addWidget(previewLabel);
    layout->addWidget(upstreamCheck);
    layout->addWidget(buttons);

    connect(nameEdit, &QLineEdit::textChanged, &dlg, [&](const QString &text) {
        const QString sanitized = sanitizeBranchName(text);
        if (sanitized.isEmpty()) {
            previewLabel->clear();
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        } else {
            previewLabel->setText(tr("→ %1").arg(sanitized));
            buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_local.contains(sanitized));
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    nameEdit->setFocus();

    if (dlg.exec() == QDialog::Accepted) {
        const QString name = sanitizeBranchName(nameEdit->text());
        if (!name.isEmpty()) {
            emit createBranchRequested(name, base, upstreamCheck->isChecked());
            close();
        }
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
        if (o == m_list) {
            if (ke->key() == Qt::Key_F2) {
                const QModelIndex idx = m_list->currentIndex();
                if (idx.isValid()) {
                    auto *it = m_model->itemFromIndex(idx);
                    if (it && it->data(RoleKind).toInt() == 0) {
                        showRenameBranchDialog(it->data(RolePayload).toString());
                        return true;
                    }
                }
            }
            if (ke->key() == Qt::Key_Delete) {
                const QModelIndex idx = m_list->currentIndex();
                if (idx.isValid()) {
                    auto *it = m_model->itemFromIndex(idx);
                    if (it && it->data(RoleKind).toInt() == 0) {
                        const QString payload = it->data(RolePayload).toString();
                        if (payload != m_current) {
                            showDeleteBranchDialog(payload);
                            return true;
                        }
                    }
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

void BranchPickerPopup::showRenameBranchDialog(const QString &branchName)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Rename Branch '%1'").arg(branchName));
    dlg.setMinimumWidth(320);

    auto *layout = new QVBoxLayout(&dlg);

    auto *nameEdit = new QLineEdit(&dlg);
    nameEdit->setText(branchName);

    auto *previewLabel = new QLabel(&dlg);
    previewLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));

    auto *remoteCheck = new QCheckBox(tr("Update remote tracking"), &dlg);
    remoteCheck->setChecked(false);
    remoteCheck->setVisible(m_hasRemote);

    auto *remoteHint = new QLabel(tr("(deletes old remote ref, pushes new)"), &dlg);
    remoteHint->setStyleSheet(QStringLiteral("color: gray; font-size: 10px;"));
    remoteHint->setVisible(m_hasRemote);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Rename"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);

    layout->addWidget(new QLabel(tr("Rename branch '<b>%1</b>' to:").arg(branchName.toHtmlEscaped()), &dlg));
    layout->addWidget(nameEdit);
    layout->addWidget(previewLabel);
    layout->addWidget(remoteCheck);
    layout->addWidget(remoteHint);
    layout->addWidget(buttons);

    connect(nameEdit, &QLineEdit::textChanged, &dlg, [&](const QString &text) {
        const QString sanitized = sanitizeBranchName(text);
        if (sanitized.isEmpty() || sanitized == branchName) {
            previewLabel->clear();
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
        } else {
            previewLabel->setText(tr("→ %1").arg(sanitized));
            buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_local.contains(sanitized));
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // Smart selection: select after last '/' or all if no '/'
    const int slashPos = branchName.lastIndexOf(QLatin1Char('/'));
    if (slashPos >= 0) {
        nameEdit->setSelection(slashPos + 1, branchName.length() - slashPos - 1);
    } else {
        nameEdit->selectAll();
    }
    nameEdit->setFocus();

    if (dlg.exec() == QDialog::Accepted) {
        const QString newName = sanitizeBranchName(nameEdit->text());
        if (!newName.isEmpty() && newName != branchName) {
            emit renameBranchRequested(branchName, newName, remoteCheck->isChecked());
            close();
        }
    }
}

void BranchPickerPopup::showDeleteBranchDialog(const QString &branchName)
{
    bool force = false;
    const QString html = tr("Are you sure you want to delete branch '<b>%1</b>'?")
                             .arg(branchName.toHtmlEscaped());
    if (!confirmForceAction(this, tr("Delete Branch"), html, tr("Delete"),
                            tr("Force delete (even if not fully merged)"), &force))
        return;
    emit deleteBranchRequested(branchName, force);
    close();
}
