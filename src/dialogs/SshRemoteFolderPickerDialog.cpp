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

#include "SshRemoteFolderPickerDialog.h"

#include "remote/RemoteFsBackend.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

static const int ROLE_PATH = Qt::UserRole + 1;
static const int ROLE_IS_DIR = Qt::UserRole + 2;
static const int ROLE_FETCHED = Qt::UserRole + 3;

SshRemoteFolderPickerDialog::SshRemoteFolderPickerDialog(
    remote::RemoteFsBackend *backend, const QString &initialPath, QWidget *parent)
    : QDialog(parent)
    , m_backend(backend)
    , m_initialPath(initialPath.isEmpty() ? QStringLiteral("/") : initialPath)
{
    setWindowTitle(tr("Select Remote Folder"));
    setModal(true);
    resize(480, 420);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    m_statusLabel = new QLabel(tr("Loading..."), this);
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setFocusPolicy(Qt::StrongFocus);
    root->addWidget(m_tree, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_selectBtn = new QPushButton(tr("Select"), this);
    m_selectBtn->setDefault(true);
    m_selectBtn->setEnabled(false);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_selectBtn);
    root->addLayout(btnRow);

    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_selectBtn, &QPushButton::clicked, this, &SshRemoteFolderPickerDialog::onAccept);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &SshRemoteFolderPickerDialog::onItemExpanded);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &SshRemoteFolderPickerDialog::onSelectionChanged);
    // Double-click a directory = select it
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        if (item && item->data(0, ROLE_IS_DIR).toBool()) {
            m_selectedPath = pathForItem(item);
            accept();
        }
    });

    populateRoot();
}

void SshRemoteFolderPickerDialog::populateRoot()
{
    // Create the root "/" item and fetch its children.
    auto *rootItem = new QTreeWidgetItem(m_tree);
    rootItem->setText(0, m_initialPath);
    rootItem->setData(0, ROLE_PATH, m_initialPath);
    rootItem->setData(0, ROLE_IS_DIR, true);
    rootItem->setData(0, ROLE_FETCHED, false);
    rootItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    m_itemByPath.insert(m_initialPath, rootItem);

    // Expand the root immediately to trigger a fetch.
    m_tree->expandItem(rootItem);
    rootItem->setSelected(true);
}

void SshRemoteFolderPickerDialog::fetchChildren(QTreeWidgetItem *item)
{
    if (!item || !m_backend) return;

    const QString path = pathForItem(item);
    if (m_loading.value(path, false)) return; // already in flight
    m_loading.insert(path, true);

    item->setData(0, ROLE_FETCHED, false);

    // Show a "Loading..." placeholder child while fetching.
    auto *placeholder = new QTreeWidgetItem(item);
    placeholder->setText(0, tr("Loading..."));
    placeholder->setFlags(Qt::NoItemFlags); // not selectable

    m_backend->readdirAsync(path,
        [guard = QPointer<SshRemoteFolderPickerDialog>(this), path](
            bool ok, const QList<remote::RemoteDirEntry> &entries, const QString &error) {
            if (guard)
                guard->onReaddirResult(path, ok, entries, error);
        });
}

void SshRemoteFolderPickerDialog::onReaddirResult(
    const QString &path, bool ok,
    const QList<remote::RemoteDirEntry> &entries, const QString &error)
{
    m_loading.remove(path);

    QTreeWidgetItem *item = m_itemByPath.value(path, nullptr);
    if (!item) return; // item was removed while the fetch was in flight

    // Remove all existing children (the "Loading..." placeholder or stale data).
    while (item->childCount() > 0) {
        delete item->takeChild(0);
    }

    if (!ok) {
        // Error state: show the error inline as a non-selectable child.
        auto *errItem = new QTreeWidgetItem(item);
        errItem->setText(0, error.isEmpty() ? tr("Failed to list directory") : error);
        errItem->setFlags(Qt::NoItemFlags);
        m_statusLabel->setText(tr("Error: %1").arg(error));
        return;
    }

    // Filter to directories only (this is a folder picker).
    QList<remote::RemoteDirEntry> dirs;
    for (const auto &e : entries) {
        if (e.isDir) dirs.append(e);
    }

    // Sort alphabetically, case-insensitive.
    std::sort(dirs.begin(), dirs.end(), [](const remote::RemoteDirEntry &a, const remote::RemoteDirEntry &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    item->setData(0, ROLE_FETCHED, true);

    if (dirs.isEmpty()) {
        // Empty directory — expanded, no children, no spinner. The item stays
        // expanded with no child indicator.
        item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicator);
    } else {
        for (const auto &d : dirs) {
            const QString childPath = (path == QLatin1String("/"))
                ? QLatin1Char('/') + d.name
                : path + QLatin1Char('/') + d.name;

            auto *child = new QTreeWidgetItem(item);
            child->setText(0, d.name);
            child->setData(0, ROLE_PATH, childPath);
            child->setData(0, ROLE_IS_DIR, true);
            child->setData(0, ROLE_FETCHED, false);
            child->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            m_itemByPath.insert(childPath, child);
        }
    }

    m_statusLabel->setText(QString());
}

void SshRemoteFolderPickerDialog::onItemExpanded(QTreeWidgetItem *item)
{
    if (!item) return;
    // Only fetch if not already fetched.
    if (!item->data(0, ROLE_FETCHED).toBool() && !m_loading.value(pathForItem(item), false)) {
        // Remove any stale children before fetching (handles re-expand after collapse).
        while (item->childCount() > 0) {
            delete item->takeChild(0);
        }
        fetchChildren(item);
    }
}

void SshRemoteFolderPickerDialog::onSelectionChanged()
{
    const auto selected = m_tree->selectedItems();
    bool hasDir = false;
    if (!selected.isEmpty()) {
        QTreeWidgetItem *item = selected.first();
        hasDir = item->data(0, ROLE_IS_DIR).toBool();
    }
    m_selectBtn->setEnabled(hasDir);
}

void SshRemoteFolderPickerDialog::onAccept()
{
    const auto selected = m_tree->selectedItems();
    if (selected.isEmpty()) return;
    QTreeWidgetItem *item = selected.first();
    if (!item->data(0, ROLE_IS_DIR).toBool()) return;
    m_selectedPath = pathForItem(item);
    accept();
}

QString SshRemoteFolderPickerDialog::pathForItem(QTreeWidgetItem *item) const
{
    if (!item) return QString();
    return item->data(0, ROLE_PATH).toString();
}
