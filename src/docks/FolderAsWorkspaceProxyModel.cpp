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

#include "FolderAsWorkspaceProxyModel.h"

#include <QDir>
#include <QFileSystemModel>

// ---------------------------------------------------------------------------
// Core mapping overrides — O(1) hot path. No map consulted; every transform is
// at most two pointer/equality comparisons against the stored root plus one
// delegated source call.
// ---------------------------------------------------------------------------

int FolderAsWorkspaceProxyModel::columnCount(const QModelIndex & /*parent*/) const
{
    // Collapsed to 1 (see design.md D4): the tree needs only column 0 for
    // structure + decoration, and a single column makes rootPtr an unambiguous
    // node discriminator (all 4 source columns share one internalPointer).
    return 1;
}

QModelIndex FolderAsWorkspaceProxyModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid() || !m_root.isValid())
        return {};
    // The synthetic root maps back to the stored source root R.
    if (proxyIndex.internalPointer() == m_rootPtr)
        return m_root;
    // Passthrough region: row/internalPointer are already source-correct.
    return createSourceIndex(proxyIndex.row(), 0, proxyIndex.internalPointer());
}

QModelIndex FolderAsWorkspaceProxyModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid() || !m_root.isValid())
        return {};
    // R becomes the synthetic root PR at (row 0, the rootPtr discriminator).
    if (sourceIndex == m_root)
        return createIndex(0, 0, m_rootPtr);
    // Passthrough region: identity on row/internalPointer.
    return createIndex(sourceIndex.row(), 0, sourceIndex.internalPointer());
}

QModelIndex FolderAsWorkspaceProxyModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || !m_root.isValid())
        return {};
    // Top level holds exactly the synthetic root at row 0.
    if (!parent.isValid())
        return (row == 0) ? createIndex(0, 0, m_rootPtr) : QModelIndex();
    // Below the root: delegate to the source, then map the child back.
    return mapFromSource(sourceModel()->index(row, 0, mapToSource(parent)));
}

QModelIndex FolderAsWorkspaceProxyModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    // LOAD-BEARING guard: the synthetic root's parent is the invisible top.
    // Without it, parent(PR) would compute mapToSource(PR).parent() ==
    // R.parent() == the real filesystem parent directory and map that back to a
    // phantom top-level node, leaking the workspace's siblings into the tree.
    if (child.internalPointer() == m_rootPtr)
        return {};
    return mapFromSource(mapToSource(child).parent());
}

int FolderAsWorkspaceProxyModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_root.isValid() ? 1 : 0;
    return sourceModel()->rowCount(mapToSource(parent));
}

bool FolderAsWorkspaceProxyModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_root.isValid();
    // Delegates to the source — for a directory this is true BEFORE its children
    // load, so the expand affordance shows without forcing enumeration.
    return sourceModel()->hasChildren(mapToSource(parent));
}

bool FolderAsWorkspaceProxyModel::canFetchMore(const QModelIndex &parent) const
{
    return sourceModel() && sourceModel()->canFetchMore(mapToSource(parent));
}

void FolderAsWorkspaceProxyModel::fetchMore(const QModelIndex &parent)
{
    if (sourceModel())
        sourceModel()->fetchMore(mapToSource(parent));
}

// Note: data() is intentionally NOT overridden. QAbstractProxyModel::data
// auto-maps through mapToSource, so FolderAsWorkspaceFsModel's git-decoration
// colors and native-separator tooltips keep working unchanged.

// ---------------------------------------------------------------------------
// QFileSystemModel-specific helpers — map the proxy index to source first.
// ---------------------------------------------------------------------------

QString FolderAsWorkspaceProxyModel::filePath(const QModelIndex &proxyIdx) const
{
    auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
    if (!fs)
        return {};
    return fs->filePath(mapToSource(proxyIdx));
}

bool FolderAsWorkspaceProxyModel::isDir(const QModelIndex &proxyIdx) const
{
    auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
    if (!fs)
        return false;
    return fs->isDir(mapToSource(proxyIdx));
}

// ---------------------------------------------------------------------------
// R lifecycle — single reset-bracketed chokepoint.
// ---------------------------------------------------------------------------

void FolderAsWorkspaceProxyModel::setRootSourcePath(const QString &dir)
{
    m_pendingRootPath = dir;
    beginResetModel();
    auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
    // QFileSystemModel::index(path) is valid synchronously for an existing
    // directory; only rowCount()/children are async (the gatherer thread fills
    // them in and emits directoryLoaded). So R is set synchronously here.
    m_root = fs ? QPersistentModelIndex(fs->index(dir)) : QPersistentModelIndex();
    m_rootPtr = m_root.isValid() ? m_root.internalPointer() : nullptr;
    endResetModel();
}

void FolderAsWorkspaceProxyModel::setSourceModel(QAbstractItemModel *sourceModel)
{
    if (auto *old = this->sourceModel())
        old->disconnect(this);

    // Base wiring first (header bookkeeping + the 7 base-connected signals).
    QAbstractProxyModel::setSourceModel(sourceModel);

    if (!sourceModel)
        return;

    connect(sourceModel, &QAbstractItemModel::rowsAboutToBeInserted,
            this, &FolderAsWorkspaceProxyModel::onRowsAboutToBeInserted);
    connect(sourceModel, &QAbstractItemModel::rowsInserted,
            this, &FolderAsWorkspaceProxyModel::onRowsInserted);
    // rowsAboutToBeRemoved/rowsRemoved must be connected explicitly — the base
    // does not re-emit them to proxy consumers (see design.md D2/D5).
    connect(sourceModel, &QAbstractItemModel::rowsAboutToBeRemoved,
            this, &FolderAsWorkspaceProxyModel::onRowsAboutToBeRemoved);
    connect(sourceModel, &QAbstractItemModel::rowsRemoved,
            this, &FolderAsWorkspaceProxyModel::onRowsRemoved);
    connect(sourceModel, &QAbstractItemModel::dataChanged,
            this, &FolderAsWorkspaceProxyModel::onDataChanged);
    connect(sourceModel, &QAbstractItemModel::headerDataChanged,
            this, &FolderAsWorkspaceProxyModel::onHeaderDataChanged);
    connect(sourceModel, &QAbstractItemModel::layoutAboutToBeChanged,
            this, &FolderAsWorkspaceProxyModel::onLayoutAboutToBeChanged);
    connect(sourceModel, &QAbstractItemModel::layoutChanged,
            this, &FolderAsWorkspaceProxyModel::onLayoutChanged);
    connect(sourceModel, &QAbstractItemModel::modelAboutToBeReset,
            this, &FolderAsWorkspaceProxyModel::onModelAboutToBeReset);
    connect(sourceModel, &QAbstractItemModel::modelReset,
            this, &FolderAsWorkspaceProxyModel::onModelReset);

    // columnsInserted/columnsRemoved are deliberately NOT forwarded: the proxy
    // is fixed at 1 column and the source column set is static at 4.

    // Defensive fallback for a pathological slow/network root where index(dir)
    // was not synchronously resolvable: re-derive R on the first directoryLoaded
    // for the pending root path. Normal local directories never hit this.
    if (auto *fs = qobject_cast<QFileSystemModel *>(sourceModel)) {
        connect(fs, &QFileSystemModel::directoryLoaded,
                this, &FolderAsWorkspaceProxyModel::onSourceDirectoryLoaded);
    }
}

void FolderAsWorkspaceProxyModel::onSourceDirectoryLoaded(const QString &loadedPath)
{
    if (m_root.isValid())
        return;
    // Compare with cleanPath on BOTH sides so a trailing-separator mismatch
    // between the watcher's loadedPath and the requested root can't skip the
    // re-derive.
    if (m_pendingRootPath.isEmpty()
        || QDir::cleanPath(loadedPath) != QDir::cleanPath(m_pendingRootPath))
        return;
    auto *fs = qobject_cast<QFileSystemModel *>(sourceModel());
    if (!fs)
        return;
    const QModelIndex r = fs->index(loadedPath);
    if (!r.isValid())
        return;
    beginResetModel();
    m_root = QPersistentModelIndex(r);
    m_rootPtr = m_root.internalPointer();
    endResetModel();
}

// ---------------------------------------------------------------------------
// Subtree filter + hand-written signal forwarding (cold path).
// ---------------------------------------------------------------------------

bool FolderAsWorkspaceProxyModel::isInSubtree(const QModelIndex &s) const
{
    if (!m_root.isValid())
        return false;
    // Walk up via parent(): true iff s == R or some ancestor == R. O(depth),
    // and only ever invoked on watcher / directory-load events, never on paint.
    for (QModelIndex cur = s; cur.isValid(); cur = cur.parent()) {
        if (cur == m_root)
            return true;
    }
    return false;
}

void FolderAsWorkspaceProxyModel::onRowsAboutToBeInserted(const QModelIndex &parent, int first, int last)
{
    if (isInSubtree(parent))
        beginInsertRows(mapFromSource(parent), first, last);
}

void FolderAsWorkspaceProxyModel::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    // Re-test identically so a filtered-out begin is never left without its end
    // (parent membership cannot change between the paired signals).
    Q_UNUSED(first);
    Q_UNUSED(last);
    if (isInSubtree(parent))
        endInsertRows();
}

void FolderAsWorkspaceProxyModel::onRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last)
{
    if (isInSubtree(parent))
        beginRemoveRows(mapFromSource(parent), first, last);
}

void FolderAsWorkspaceProxyModel::onRowsRemoved(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(first);
    Q_UNUSED(last);
    if (isInSubtree(parent))
        endRemoveRows();
}

void FolderAsWorkspaceProxyModel::onDataChanged(const QModelIndex &topLeft,
                                                const QModelIndex &bottomRight,
                                                const QList<int> &roles)
{
    // topLeft and bottomRight share a parent; gate on either. Clamp columns to 0
    // (the proxy's only column).
    if (!isInSubtree(topLeft) && !isInSubtree(bottomRight))
        return;
    const QModelIndex tl = mapFromSource(topLeft.sibling(topLeft.row(), 0));
    const QModelIndex br = mapFromSource(bottomRight.sibling(bottomRight.row(), 0));
    if (tl.isValid() && br.isValid())
        emit dataChanged(tl, br, roles);
}

void FolderAsWorkspaceProxyModel::onHeaderDataChanged(Qt::Orientation orientation, int first, int last)
{
    emit headerDataChanged(orientation, first, last);
}

void FolderAsWorkspaceProxyModel::onLayoutAboutToBeChanged(
        const QList<QPersistentModelIndex> & /*parents*/,
        QAbstractItemModel::LayoutChangeHint /*hint*/)
{
    // Snapshot the proxy's persistent indices and their source equivalents so we
    // can remap them after the source finishes its layout change. Without this,
    // the view's selection/expansion QPersistentModelIndexes and the dock's
    // pendingTooltipIndex would dangle after a sort/refresh. The source side is
    // stored as QPersistentModelIndex (mirroring QIdentityProxyModel) so it
    // auto-tracks the source's row reorder — a plain QModelIndex would keep a
    // stale .row() and remap selections to the wrong items.
    m_layoutOldProxy = persistentIndexList();
    m_layoutOldSource.clear();
    m_layoutOldSource.reserve(m_layoutOldProxy.size());
    for (const QModelIndex &p : m_layoutOldProxy)
        m_layoutOldSource.append(QPersistentModelIndex(mapToSource(p)));
    emit layoutAboutToBeChanged();
}

void FolderAsWorkspaceProxyModel::onLayoutChanged(
        const QList<QPersistentModelIndex> & /*parents*/,
        QAbstractItemModel::LayoutChangeHint /*hint*/)
{
    QModelIndexList newProxy;
    newProxy.reserve(m_layoutOldSource.size());
    // Each source index is a QPersistentModelIndex that auto-tracked the reorder,
    // so it now yields the correct new source position → correct new proxy index.
    for (const QPersistentModelIndex &s : m_layoutOldSource)
        newProxy.append(mapFromSource(s));
    changePersistentIndexList(m_layoutOldProxy, newProxy);
    m_layoutOldProxy.clear();
    m_layoutOldSource.clear();
    emit layoutChanged();
}

void FolderAsWorkspaceProxyModel::onModelAboutToBeReset()
{
    beginResetModel();
}

void FolderAsWorkspaceProxyModel::onModelReset()
{
    endResetModel();
}
