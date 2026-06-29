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

#ifndef REMOTE_REMOTEFILESYSTEMMODEL_H
#define REMOTE_REMOTEFILESYSTEMMODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

#include "IWorkspaceFsModel.h"

class PathStatusIndex;

namespace remote {

// SFTP-backed workspace file-tree model (D2). A QAbstractItemModel that ALSO
// implements IWorkspaceFsModel, so FolderAsWorkspaceDock + the synthetic-root
// FolderAsWorkspaceProxyModel drive it through the exact same path-based contract
// they use for the local QFileSystemModel — the proxy's invariants are unchanged.
//
// Shape (mirrors QFileSystemModel for the dock's purposes):
//   - There is ONE top-level row: the workspace root directory node R. Below it
//     the tree nests R's real children, lazily fetched.
//   - Listing is ASYNC and non-blocking on the UI thread: expanding (or rooting)
//     a directory calls the injected `ListFn`, which posts an SFTP `readdir` to
//     the connection's worker thread and returns immediately. When the entries
//     arrive the model inserts the child rows (rowsInserted) and emits
//     `directoryLoaded(path)` — identical contract to QFileSystemModel.
//   - While a directory is being fetched, `data(idx, LoadingRole)` is true so the
//     view can paint a spinner; an empty directory settles to an expanded,
//     childless, non-spinning state (not a perpetual spinner).
//
// Node model: an owned tree of Node (name, isDir, size, mtime, children, parent,
// listed/listing flags). internalPointer == Node* gives O(1) parent()/index()
// and stable indices per node (Nodes live in unique_ptrs and never move). Reset
// (re-root) frees the whole tree under begin/endResetModel, so no index dangles.
//
// Backend seam: the model takes a `ListFn` rather than a concrete RemoteFsBackend
// so it has ZERO compile/link dependency on the SSH stack and is unit-testable
// with a fake lister. FolderAsWorkspaceDock adapts RemoteFsBackend::readdirAsync
// to ListFn and relays RemoteFsBackend::directoryChanged into onDirectoryChanged.
class RemoteFileSystemModel : public QAbstractItemModel, public IWorkspaceFsModel
{
    Q_OBJECT

public:
    // Custom roles. LoadingRole reports whether a directory node is mid-fetch so
    // a delegate can show a spinner (Asynchronous remote directory listing).
    enum Roles : quint16
    {
        LoadingRole = Qt::UserRole + 1,
    };

    // One listed directory entry. A deliberately self-contained POD so the model
    // has ZERO dependency on the SSH stack (RemoteFsBackend / SshSessionWorker and
    // its Q_OBJECT) — that keeps the model unit-testable with a plain in-memory
    // lister and link-free of libssh2. FolderAsWorkspaceDock's adapter converts
    // RemoteFsBackend's RemoteDirEntry (same fields) to this on the cold,
    // network-bound listing path (O(entries), negligible).
    struct Entry
    {
        QString name;       // basename, no path
        bool    isDir = false;
        qint64  size = 0;
        qint64  mtimeSecs = 0;
    };

    // Per-directory listing primitive. Given an absolute remote path, fetch its
    // entries and invoke `done` exactly once on the UI thread. `ok=false` (with a
    // human `error`) leaves the node unlisted so a later expand/refresh retries.
    using ListDoneCallback =
        std::function<void(bool ok, const QList<Entry> &entries, const QString &error)>;
    using ListFn = std::function<void(const QString &path, ListDoneCallback done)>;

    explicit RemoteFileSystemModel(ListFn lister, QObject *parent = nullptr);
    ~RemoteFileSystemModel() override;

    // --- QAbstractItemModel --------------------------------------------------
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    // --- remote::IWorkspaceFsModel ------------------------------------------
    QModelIndex indexForPath(const QString &path) const override;
    QString filePath(const QModelIndex &index) const override;
    bool isDir(const QModelIndex &index) const override;
    void setRootPath(const QString &path) override;
    QString rootPath() const override;
    QAbstractItemModel *asModel() override { return this; }

public slots:
    // Change-notification seam consumed by the poll-watcher (Batch C) and the
    // local QFileSystemWatcher relay: re-list `path` if its node is already
    // loaded and diff the fresh listing against the cached children, emitting the
    // minimal row insert/remove. A no-op for an unknown / not-yet-listed dir.
    void onDirectoryChanged(const QString &path);

    // --- Git decoration (mirrors FolderAsWorkspaceFsModel) -------------------
    void setStatusIndex(PathStatusIndex *index) { m_statusIndex = index; }
    void setColorsEnabled(bool enabled) { m_colorsEnabled = enabled; }
    void setDarkPalette(bool dark) { m_isDark = dark; }
    void notifyPathsChanged(const QSet<QString> &cleanPaths);

signals:
    // Mirrors QFileSystemModel::directoryLoaded — fired once a directory's
    // children have materialized. The dock's path-space restore/expansion logic
    // (onDirectoryLoaded / m_pendingExpansion / pendingCurrentItem) waits on it.
    void directoryLoaded(const QString &path);

private:
    struct Node
    {
        QString name;                 // basename, no path ("" for the root node)
        bool    isDir = false;
        qint64  size = 0;
        qint64  mtimeSecs = 0;
        bool    listed = false;       // children have been fetched at least once
        bool    listing = false;      // a fetch is currently in flight
        Node   *parent = nullptr;     // nullptr only for the root node R
        std::vector<std::unique_ptr<Node>> children;
    };

    // Map a model index to its Node (root node for an invalid index is NOT
    // implied — invalid index == the invisible top above R).
    Node *nodeFor(const QModelIndex &index) const;
    // Row of `node` within its parent's children (0 for R). O(siblings).
    int rowOf(const Node *node) const;
    // The model index that denotes `node` (column 0). Invalid for nullptr.
    QModelIndex indexOfNode(Node *node) const;
    // Absolute remote path of `node` by walking parent links and prepending root.
    QString pathOf(const Node *node) const;
    // Resolve an absolute path to a Node, descending only through already-listed
    // dirs; nullptr if any segment is unknown. O(depth * siblings).
    Node *nodeForPath(const QString &path) const;

    // Kick a fetch for `node` (sets listing, emits dataChanged for the spinner,
    // calls m_lister). Re-listing an already-listed node is allowed (the
    // change-notification path); guarded only against a double in-flight fetch.
    void beginFetch(Node *node);
    // Resolve a fetch result on the UI thread. Re-resolves the node by path so a
    // re-root between request and reply can't dangle. First listing → populate;
    // a subsequent listing (watcher refresh) → applyDiff.
    void onListArrived(const QString &path, bool ok,
                       const QList<Entry> &entries, const QString &error);
    // First-time population of `node` from a fresh listing: insert all child rows.
    void populate(Node *node, const QList<Entry> &entries);
    // Re-list diff for an already-listed `node`: remove vanished rows, insert new
    // ones, update changed dir/size/mtime metadata. Minimal begin/endRemoveRows
    // + begin/endInsertRows so the view/proxy track precisely.
    void applyDiff(Node *node, const QList<Entry> &entries);

    // Drain deferred fetches up to the concurrency cap.
    void drainDeferred();

    ListFn m_lister;
    std::unique_ptr<Node> m_root;   // R, the workspace dir node (null if unrooted)
    QString m_rootPath;             // cleaned absolute root path

    // Concurrency limiter: at most kMaxInFlight readdir requests in the SFTP
    // meta-lane at once. Excess fetches are deferred (by path) and drained as
    // in-flight ones complete. Prevents session-restore cascades from flooding
    // the SFTP queue with 800+ readdir ops that block interactive use for minutes.
    static constexpr int kMaxInFlight = 8;
    int m_inFlight = 0;
    bool m_draining = false;
    QStringList m_deferredPaths;

    PathStatusIndex *m_statusIndex = nullptr;
    bool m_colorsEnabled = false;
    bool m_isDark = false;
};

} // namespace remote

#endif // REMOTE_REMOTEFILESYSTEMMODEL_H
