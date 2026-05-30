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

#ifndef FOLDER_AS_WORKSPACE_PROXY_MODEL_H
#define FOLDER_AS_WORKSPACE_PROXY_MODEL_H

#include <QAbstractProxyModel>
#include <QModelIndexList>
#include <QPersistentModelIndex>
#include <QString>

// Synthetic-root proxy that presents the workspace directory itself as a single
// collapsible top-level node (JetBrains Project-tool-window style), with the
// directory's real children nested one level beneath it. It sits between the
// QTreeView and FolderAsWorkspaceFsModel (a QFileSystemModel subclass).
//
// Design invariants (see openspec/changes/workspace-root-top-node/design.md):
//   - O(1) state: one QPersistentModelIndex (the source index of the workspace
//     root, "R") plus its cached internalPointer ("rootPtr"). No per-node cache.
//   - Two O(1) discriminators distinguish the single synthetic-root
//     discontinuity from the pure-identity passthrough region below it:
//       * proxyIdx.internalPointer() == m_rootPtr  => this proxy index is R.
//       * sourceIdx == m_root                      => this source index is R.
//   - Columns collapse to 1 (the tree only needs column 0 for structure +
//     decoration; the dock already hides source columns 1..3).
//   - Hand-written, isInSubtree-filtered signal forwarding: out-of-subtree
//     watcher events (e.g. a sibling project under the root's real parent) are
//     dropped so the fixed single top row is never corrupted.
class QFileSystemModel;

class FolderAsWorkspaceProxyModel : public QAbstractProxyModel
{
    Q_OBJECT

public:
    using QAbstractProxyModel::QAbstractProxyModel;

    // Reset-bracketed chokepoint. Re-rooting the same dock flows through here so
    // the model reset cleanly replaces the stale R — no window where the proxy
    // serves the old root against a moved source.
    void setRootSourcePath(const QString &dir);

    // QFileSystemModel-specific helpers (not on QAbstractProxyModel). They map a
    // proxy index to source and delegate, so dock touch-points reading the file
    // path / dir-ness off a VIEW index call these instead of the source model.
    QString filePath(const QModelIndex &proxyIdx) const;
    bool isDir(const QModelIndex &proxyIdx) const;

    // Mapping + structure overrides (O(1) hot path).
    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    void setSourceModel(QAbstractItemModel *sourceModel) override;

private:
    // True iff `s` is the root or a descendant of the root. O(depth), cold path
    // only (filesystem-watcher / directory-load events), never on paint.
    bool isInSubtree(const QModelIndex &s) const;

    // Forwarding slots — re-emit source structural changes in proxy coordinates,
    // gated by isInSubtree. See design.md D5.
    void onRowsAboutToBeInserted(const QModelIndex &parent, int first, int last);
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void onRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last);
    void onRowsRemoved(const QModelIndex &parent, int first, int last);
    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                       const QList<int> &roles);
    void onHeaderDataChanged(Qt::Orientation orientation, int first, int last);
    void onLayoutAboutToBeChanged(const QList<QPersistentModelIndex> &parents,
                                  QAbstractItemModel::LayoutChangeHint hint);
    void onLayoutChanged(const QList<QPersistentModelIndex> &parents,
                         QAbstractItemModel::LayoutChangeHint hint);
    void onModelAboutToBeReset();
    void onModelReset();
    void onSourceDirectoryLoaded(const QString &loadedPath);

    QPersistentModelIndex m_root;       // R — source index of the workspace root
    void *m_rootPtr = nullptr;          // R.internalPointer(), the node discriminator
    QString m_pendingRootPath;          // path last handed to setRootSourcePath

    // layoutAboutToBeChanged → layoutChanged persistent-index remap snapshots.
    // The proxy-side list is what we hand to changePersistentIndexList as the OLD
    // list; the source-side list MUST be persistent so it auto-tracks the source's
    // row reorder during the layout change (mirrors QIdentityProxyModel). A plain
    // QModelIndexList would go stale on reorder and remap selections to the wrong
    // items.
    QModelIndexList m_layoutOldProxy;
    QList<QPersistentModelIndex> m_layoutOldSource;
};

#endif // FOLDER_AS_WORKSPACE_PROXY_MODEL_H
