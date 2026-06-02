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

#include "RemoteFileSystemModel.h"

#include "git/GitDiffPalette.h"
#include "git/GitStatusEntry.h"
#include "git/PathStatusIndex.h"

#include <QAbstractFileIconProvider>
#include <QDir>
#include <QFileInfo>
#include <QPointer>

#include <algorithm>
#include <utility>

namespace remote {

namespace {

QColor colourForChange(GitStatusEntry::Change c, const GitDiffPalette &p)
{
    switch (c) {
    case GitStatusEntry::Unmerged:    return p.fgConflict;
    case GitStatusEntry::Deleted:     return p.fgDeleted;
    case GitStatusEntry::Modified:    return p.fgModified;
    case GitStatusEntry::TypeChanged: return p.fgModified;
    case GitStatusEntry::Added:       return p.fgAdded;
    case GitStatusEntry::Renamed:     return p.fgRenamed;
    case GitStatusEntry::Copied:      return p.fgRenamed;
    case GitStatusEntry::Untracked_:  return p.fgUntracked;
    }
    return {};
}

// Stable ordering identical in spirit to QFileSystemModel's default: directories
// before files, then case-insensitive name, with a case-sensitive tiebreak so
// the order is deterministic (matters for the proxy/view + the diff path).
bool entryLess(bool aDir, const QString &aName, bool bDir, const QString &bName)
{
    if (aDir != bDir) {
        return aDir; // directories first
    }
    const int ci = aName.compare(bName, Qt::CaseInsensitive);
    if (ci != 0) {
        return ci < 0;
    }
    return aName.compare(bName, Qt::CaseSensitive) < 0;
}

// Lexically clean a remote (POSIX) path: normalize separators and collapse
// redundant slashes WITHOUT any disk access (the path lives on another host).
QString cleanRemote(const QString &path)
{
    QString p = path;
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    p = QDir::cleanPath(p);
    return p;
}

// Generic folder/file icon without touching any (local) filesystem. A single
// shared provider; QAbstractFileIconProvider::icon(IconType) is path-free.
QIcon genericIcon(bool isDir)
{
    static QAbstractFileIconProvider provider;
    return provider.icon(isDir ? QAbstractFileIconProvider::Folder
                               : QAbstractFileIconProvider::File);
}

} // namespace

RemoteFileSystemModel::RemoteFileSystemModel(ListFn lister, QObject *parent)
    : QAbstractItemModel(parent)
    , m_lister(std::move(lister))
{
}

RemoteFileSystemModel::~RemoteFileSystemModel() = default;

// --- node helpers ------------------------------------------------------------

RemoteFileSystemModel::Node *RemoteFileSystemModel::nodeFor(const QModelIndex &index) const
{
    return index.isValid() ? static_cast<Node *>(index.internalPointer()) : nullptr;
}

int RemoteFileSystemModel::rowOf(const Node *node) const
{
    if (!node) {
        return -1;
    }
    if (!node->parent) {
        return 0; // R sits at row 0 of the invisible top
    }
    const auto &sibs = node->parent->children;
    for (int i = 0; i < static_cast<int>(sibs.size()); ++i) {
        if (sibs[i].get() == node) {
            return i;
        }
    }
    return -1;
}

QModelIndex RemoteFileSystemModel::indexOfNode(Node *node) const
{
    if (!node) {
        return {};
    }
    return createIndex(rowOf(node), 0, node);
}

QString RemoteFileSystemModel::pathOf(const Node *node) const
{
    if (!node) {
        return {};
    }
    // Collect basenames from `node` up to (but not including) R, then prepend the
    // root path. R itself maps to exactly m_rootPath.
    QStringList parts;
    for (const Node *cur = node; cur && cur->parent; cur = cur->parent) {
        parts.prepend(cur->name);
    }
    if (parts.isEmpty()) {
        return m_rootPath;
    }
    const QString suffix = parts.join(QLatin1Char('/'));
    if (m_rootPath.endsWith(QLatin1Char('/'))) {
        return m_rootPath + suffix; // root is "/" — avoid a double slash
    }
    return m_rootPath + QLatin1Char('/') + suffix;
}

RemoteFileSystemModel::Node *RemoteFileSystemModel::nodeForPath(const QString &path) const
{
    if (!m_root) {
        return nullptr;
    }
    const QString cleaned = cleanRemote(path);
    if (cleaned == m_rootPath) {
        return m_root.get();
    }
    // Must be strictly under the root.
    const QString prefix = m_rootPath.endsWith(QLatin1Char('/'))
                               ? m_rootPath
                               : m_rootPath + QLatin1Char('/');
    if (!cleaned.startsWith(prefix)) {
        return nullptr;
    }
    const QString rel = cleaned.mid(prefix.size());
    const QStringList segs = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    Node *cur = m_root.get();
    for (const QString &seg : segs) {
        Node *next = nullptr;
        for (const auto &child : cur->children) {
            if (child->name == seg) {
                next = child.get();
                break;
            }
        }
        if (!next) {
            return nullptr; // segment not (yet) listed
        }
        cur = next;
    }
    return cur;
}

// --- QAbstractItemModel ------------------------------------------------------

QModelIndex RemoteFileSystemModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0) {
        return {};
    }
    if (!parent.isValid()) {
        // Invisible top holds exactly R at row 0.
        return (row == 0 && m_root) ? createIndex(0, 0, m_root.get()) : QModelIndex();
    }
    Node *p = nodeFor(parent);
    if (!p || row >= static_cast<int>(p->children.size())) {
        return {};
    }
    return createIndex(row, 0, p->children[row].get());
}

QModelIndex RemoteFileSystemModel::parent(const QModelIndex &child) const
{
    Node *n = nodeFor(child);
    if (!n || !n->parent) {
        return {}; // R's parent is the invisible top
    }
    return indexOfNode(n->parent);
}

int RemoteFileSystemModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return m_root ? 1 : 0;
    }
    Node *p = nodeFor(parent);
    return p ? static_cast<int>(p->children.size()) : 0;
}

int RemoteFileSystemModel::columnCount(const QModelIndex & /*parent*/) const
{
    return 1;
}

QVariant RemoteFileSystemModel::data(const QModelIndex &index, int role) const
{
    Node *n = nodeFor(index);
    if (!n) {
        return {};
    }
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        // R shows the workspace dir's basename (its name is empty); children show
        // their own basename.
        return n->parent ? n->name : QFileInfo(m_rootPath).fileName();
    case Qt::ToolTipRole:
        return pathOf(n);
    case Qt::DecorationRole:
        return genericIcon(n->isDir);
    case Qt::ForegroundRole:
        if (m_colorsEnabled && m_statusIndex) {
            const QString cleanPath = QDir::cleanPath(pathOf(n));
            const auto change = n->isDir
                ? m_statusIndex->folderChange(cleanPath)
                : m_statusIndex->fileChange(cleanPath);
            if (change.has_value()) {
                const QColor c = colourForChange(*change, GitDiffPalette::current(m_isDark));
                if (c.isValid()) return c;
            }
        }
        return {};
    case LoadingRole:
        return n->listing;
    default:
        return {};
    }
}

Qt::ItemFlags RemoteFileSystemModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool RemoteFileSystemModel::hasChildren(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return m_root != nullptr;
    }
    Node *p = nodeFor(parent);
    if (!p) {
        return false;
    }
    if (!p->isDir) {
        return false;
    }
    // A directory not yet listed reports children so the expand affordance shows
    // without forcing enumeration; once listed, it reports its real child count.
    return !p->listed || !p->children.empty();
}

bool RemoteFileSystemModel::canFetchMore(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        return false; // the invisible top's single row (R) exists immediately
    }
    Node *p = nodeFor(parent);
    return p && p->isDir && !p->listed && !p->listing;
}

void RemoteFileSystemModel::fetchMore(const QModelIndex &parent)
{
    Node *p = nodeFor(parent);
    if (p && p->isDir && !p->listed && !p->listing) {
        beginFetch(p);
    }
}

// --- remote::IWorkspaceFsModel -----------------------------------------------

QModelIndex RemoteFileSystemModel::indexForPath(const QString &path) const
{
    Node *n = nodeForPath(path);
    return n ? createIndex(rowOf(n), 0, n) : QModelIndex();
}

QString RemoteFileSystemModel::filePath(const QModelIndex &index) const
{
    Node *n = nodeFor(index);
    return n ? pathOf(n) : QString();
}

bool RemoteFileSystemModel::isDir(const QModelIndex &index) const
{
    Node *n = nodeFor(index);
    return n && n->isDir;
}

void RemoteFileSystemModel::setRootPath(const QString &path)
{
    // Reset-bracketed re-root. Frees the whole node tree (no index survives the
    // reset, so nothing dangles) and rebuilds R synchronously, then kicks off R's
    // child listing. The async listing emits directoryLoaded(root) when it lands
    // — same contract QFileSystemModel's gatherer gives, which the dock's
    // onDirectoryLoaded / m_pendingExpansion cascade relies on.
    beginResetModel();
    m_root.reset();
    m_rootPath = cleanRemote(path);
    if (!m_rootPath.isEmpty()) {
        m_root = std::make_unique<Node>();
        m_root->isDir = true; // a workspace root is always a directory
    }
    endResetModel();

    if (m_root) {
        beginFetch(m_root.get());
    }
}

QString RemoteFileSystemModel::rootPath() const
{
    return m_rootPath;
}

// --- listing / fetch ---------------------------------------------------------

void RemoteFileSystemModel::beginFetch(Node *node)
{
    if (!node || !node->isDir || node->listing || !m_lister) {
        return;
    }
    node->listing = true;
    // Repaint the node so a delegate can show the spinner (LoadingRole flipped).
    const QModelIndex idx = indexOfNode(node);
    if (idx.isValid()) {
        emit dataChanged(idx, idx, {LoadingRole});
    }

    const QString path = pathOf(node);
    QPointer<RemoteFileSystemModel> self(this);
    m_lister(path, [self, path](bool ok, const QList<Entry> &entries,
                                const QString &error) {
        if (self) {
            self->onListArrived(path, ok, entries, error);
        }
    });
}

void RemoteFileSystemModel::onListArrived(const QString &path, bool ok,
                                          const QList<Entry> &entries,
                                          const QString &error)
{
    Q_UNUSED(error);
    // Re-resolve by path: a re-root (or a parent removal) between request and
    // reply may have replaced/freed the original node.
    Node *node = nodeForPath(path);
    if (!node) {
        return;
    }
    node->listing = false;
    if (!ok) {
        // Leave `listed` false so a later expand / refresh retries; clear the
        // spinner so the node doesn't spin forever on an error.
        const QModelIndex idx = indexOfNode(node);
        if (idx.isValid()) {
            emit dataChanged(idx, idx, {LoadingRole});
        }
        emit directoryLoaded(path); // unblock the dock's pending-expansion wait
        return;
    }

    if (!node->listed) {
        populate(node, entries);
    } else {
        applyDiff(node, entries);
    }
    node->listed = true;

    const QModelIndex idx = indexOfNode(node);
    if (idx.isValid()) {
        emit dataChanged(idx, idx, {LoadingRole}); // spinner off
    }
    emit directoryLoaded(path);
}

void RemoteFileSystemModel::populate(Node *node, const QList<Entry> &entries)
{
    if (entries.isEmpty()) {
        return; // settles to expanded-but-childless (hasChildren now false)
    }
    QList<Entry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const Entry &a, const Entry &b) {
                  return entryLess(a.isDir, a.name, b.isDir, b.name);
              });

    const QModelIndex parentIdx = indexOfNode(node);
    beginInsertRows(parentIdx, 0, static_cast<int>(sorted.size()) - 1);
    node->children.reserve(sorted.size());
    for (const Entry &e : sorted) {
        auto child = std::make_unique<Node>();
        child->name = e.name;
        child->isDir = e.isDir;
        child->size = e.size;
        child->mtimeSecs = e.mtimeSecs;
        child->parent = node;
        node->children.push_back(std::move(child));
    }
    endInsertRows();
}

void RemoteFileSystemModel::applyDiff(Node *node, const QList<Entry> &entries)
{
    // Build a name → fresh-entry map for O(1) membership + metadata lookup.
    QHash<QString, Entry> fresh;
    fresh.reserve(entries.size());
    for (const Entry &e : entries) {
        fresh.insert(e.name, e);
    }

    // 1) Remove vanished rows, high-index first so earlier rows keep their index.
    for (int row = static_cast<int>(node->children.size()) - 1; row >= 0; --row) {
        const QString name = node->children[row]->name;
        if (!fresh.contains(name)) {
            const QModelIndex parentIdx = indexOfNode(node);
            beginRemoveRows(parentIdx, row, row);
            node->children.erase(node->children.begin() + row);
            endRemoveRows();
        }
    }

    // 2) Update metadata on survivors (dir-ness/size/mtime may have changed) and
    // collect the genuinely-new names.
    QHash<QString, const Node *> existing;
    for (const auto &child : node->children) {
        existing.insert(child->name, child.get());
        const Entry &e = fresh.value(child->name);
        if (child->isDir != e.isDir || child->size != e.size
            || child->mtimeSecs != e.mtimeSecs) {
            child->isDir = e.isDir;
            child->size = e.size;
            child->mtimeSecs = e.mtimeSecs;
            const QModelIndex idx = indexOfNode(child.get());
            if (idx.isValid()) {
                emit dataChanged(idx, idx);
            }
        }
    }

    // 3) Insert new entries, each at its sorted position so the row order stays
    // consistent with populate().
    for (const Entry &e : entries) {
        if (existing.contains(e.name)) {
            continue;
        }
        int pos = 0;
        while (pos < static_cast<int>(node->children.size())
               && entryLess(node->children[pos]->isDir, node->children[pos]->name,
                            e.isDir, e.name)) {
            ++pos;
        }
        const QModelIndex parentIdx = indexOfNode(node);
        beginInsertRows(parentIdx, pos, pos);
        auto child = std::make_unique<Node>();
        child->name = e.name;
        child->isDir = e.isDir;
        child->size = e.size;
        child->mtimeSecs = e.mtimeSecs;
        child->parent = node;
        node->children.insert(node->children.begin() + pos, std::move(child));
        endInsertRows();
        existing.insert(e.name, node->children[pos].get());
    }
}

void RemoteFileSystemModel::onDirectoryChanged(const QString &path)
{
    // Change-notification seam (Batch C poll-watcher + local watcher relay). Only
    // re-list a directory that is already loaded and visible; an unknown or
    // not-yet-listed dir is a no-op (it will fetch fresh when first expanded).
    Node *node = nodeForPath(path);
    if (!node || !node->isDir || !node->listed || node->listing) {
        return;
    }
    beginFetch(node);
}

void RemoteFileSystemModel::notifyPathsChanged(const QSet<QString> &cleanPaths)
{
    const QList<int> roles{ Qt::ForegroundRole };
    for (const QString &p : cleanPaths) {
        Node *n = nodeForPath(p);
        if (!n) continue;
        const QModelIndex idx = indexOfNode(n);
        if (!idx.isValid()) continue;
        emit dataChanged(idx, idx, roles);
    }
}

} // namespace remote