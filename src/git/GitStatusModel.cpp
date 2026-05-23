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

#include "GitStatusModel.h"

#include "GitDiffPalette.h"

#include <QBrush>
#include <QCoreApplication>

namespace {
constexpr quintptr SECTION_PARENT = quintptr(-1);
}

GitStatusModel::GitStatusModel(QObject *parent) : QAbstractItemModel(parent)
{
    rebuildVisible();
}

QString GitStatusModel::sectionTitle(GitStatusEntry::Section s)
{
    switch (s) {
        case GitStatusEntry::Conflicts: return QCoreApplication::translate("GitStatusModel", "Conflicts");
        case GitStatusEntry::Staged:    return QCoreApplication::translate("GitStatusModel", "Staged");
        case GitStatusEntry::Tracked:   return QCoreApplication::translate("GitStatusModel", "Tracked");
        case GitStatusEntry::Untracked: return QCoreApplication::translate("GitStatusModel", "Untracked");
        default: return QString();
    }
}

QString GitStatusModel::changeShort(GitStatusEntry::Change c)
{
    switch (c) {
        case GitStatusEntry::Added:        return QStringLiteral("A");
        case GitStatusEntry::Modified:     return QStringLiteral("M");
        case GitStatusEntry::Deleted:      return QStringLiteral("D");
        case GitStatusEntry::Renamed:      return QStringLiteral("R");
        case GitStatusEntry::Copied:       return QStringLiteral("C");
        case GitStatusEntry::TypeChanged:  return QStringLiteral("T");
        case GitStatusEntry::Untracked_:   return QStringLiteral("?");
        case GitStatusEntry::Unmerged:     return QStringLiteral("U");
    }
    return QStringLiteral("?");
}

void GitStatusModel::rebuildVisible()
{
    m_visibleSections.clear();
    for (int s = 0; s < GitStatusEntry::SectionCount; ++s) {
        if (!m_buckets[s].isEmpty()) m_visibleSections.append(static_cast<GitStatusEntry::Section>(s));
    }
}

void GitStatusModel::setEntries(const GitStatusEntries &entries)
{
    beginResetModel();
    for (int s = 0; s < GitStatusEntry::SectionCount; ++s) m_buckets[s].clear();
    for (const GitStatusEntry &e : entries) m_buckets[e.section].append(e);
    rebuildVisible();
    endResetModel();
}

int GitStatusModel::columnCount(const QModelIndex &) const { return 1; }

int GitStatusModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) return m_visibleSections.size();
    if (parent.internalId() != SECTION_PARENT) return 0;
    const int row = parent.row();
    if (row < 0 || row >= m_visibleSections.size()) return 0;
    return m_buckets[m_visibleSections.at(row)].size();
}

QModelIndex GitStatusModel::index(int row, int col, const QModelIndex &parent) const
{
    if (col != 0) return {};
    if (!parent.isValid()) {
        if (row < 0 || row >= m_visibleSections.size()) return {};
        return createIndex(row, col, SECTION_PARENT);
    }
    if (parent.internalId() != SECTION_PARENT) return {};
    if (parent.row() < 0 || parent.row() >= m_visibleSections.size()) return {};
    const auto &bucket = m_buckets[m_visibleSections.at(parent.row())];
    if (row < 0 || row >= bucket.size()) return {};
    // store parent.row() in internalId so parent() can find back to section
    return createIndex(row, col, quintptr(parent.row()));
}

QModelIndex GitStatusModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) return {};
    if (child.internalId() == SECTION_PARENT) return {};
    const int sectionRow = int(child.internalId());
    if (sectionRow < 0 || sectionRow >= m_visibleSections.size()) return {};
    return createIndex(sectionRow, 0, SECTION_PARENT);
}

Qt::ItemFlags GitStatusModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled;
    if (index.internalId() != SECTION_PARENT) f |= Qt::ItemIsSelectable;
    return f;
}

bool GitStatusModel::isSection(const QModelIndex &i) const
{
    return i.isValid() && i.internalId() == SECTION_PARENT;
}

GitStatusEntry::Section GitStatusModel::sectionOf(const QModelIndex &i) const
{
    if (!i.isValid()) return GitStatusEntry::Tracked;
    const int row = isSection(i) ? i.row() : int(i.internalId());
    if (row < 0 || row >= m_visibleSections.size()) return GitStatusEntry::Tracked;
    return m_visibleSections.at(row);
}

int GitStatusModel::entriesInSection(GitStatusEntry::Section s) const
{
    return m_buckets[s].size();
}

int GitStatusModel::totalEntries() const
{
    int t = 0;
    for (int s = 0; s < GitStatusEntry::SectionCount; ++s) t += m_buckets[s].size();
    return t;
}

QVariant GitStatusModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};

    if (isSection(index)) {
        const auto sec = sectionOf(index);
        const int count = m_buckets[sec].size();
        if (role == Qt::DisplayRole)
            return QStringLiteral("%1 (%2)").arg(sectionTitle(sec)).arg(count);
        if (role == IsSectionRole) return true;
        if (role == SectionRole)   return int(sec);
        return {};
    }

    const auto sec = sectionOf(index);
    const auto &bucket = m_buckets[sec];
    if (index.row() < 0 || index.row() >= bucket.size()) return {};
    const GitStatusEntry &e = bucket.at(index.row());

    switch (role) {
        case Qt::DisplayRole: {
            QString prefix = changeShort(e.change);
            QString path = e.relPath;
            if (e.change == GitStatusEntry::Renamed && !e.origRelPath.isEmpty())
                path = QStringLiteral("%1 → %2").arg(e.origRelPath, e.relPath);
            return QStringLiteral("%1  %2").arg(prefix, path);
        }
        case Qt::ToolTipRole:    return e.relPath;
        case Qt::ForegroundRole: {
            const auto &pal = GitDiffPalette::current(m_isDark);
            if (sec == GitStatusEntry::Conflicts) return QBrush(pal.fgConflict);
            switch (e.change) {
                case GitStatusEntry::Added:        return QBrush(pal.fgAdded);
                case GitStatusEntry::Untracked_:   return QBrush(pal.fgUntracked);
                case GitStatusEntry::Modified:     return QBrush(pal.fgModified);
                case GitStatusEntry::TypeChanged:  return QBrush(pal.fgModified);
                case GitStatusEntry::Deleted:      return QBrush(pal.fgDeleted);
                case GitStatusEntry::Renamed:      return QBrush(pal.fgRenamed);
                case GitStatusEntry::Copied:       return QBrush(pal.fgRenamed);
                case GitStatusEntry::Unmerged:     return QBrush(pal.fgConflict);
            }
            return {};
        }
        case RelPathRole:                return e.relPath;
        case OrigPathRole:               return e.origRelPath;
        case ChangeRole:                 return int(e.change);
        case StagedSideRole:             return e.stagedSide;
        case SectionRole:                return int(e.section);
        case XyRole:                     return e.xy;
        case IsSectionRole:              return false;
        case AddedLinesRole:             return e.addedLines;
        case DeletedLinesRole:           return e.deletedLines;
        case IsBinaryRole:               return e.isBinary;
        case HasUnstableEncodingRole:    return e.hasUnstableEncoding;
        case OursShaRole:                return e.oursSha;
        case TheirsShaRole:              return e.theirsSha;
        case IsSubmoduleRole:            return e.isSubmodule;
        case SubAddedLinesRole:          return e.subAddedLines;
        case SubDeletedLinesRole:        return e.subDeletedLines;
        default: return {};
    }
}

void GitStatusModel::mergeNumstat(const QHash<QString, GitNumstatParser::Stat> &stats, bool stagedSide)
{
    if (stats.isEmpty()) return;
    // Update entries whose stagedSide matches. Emit per-row dataChanged so
    // QStyledItemDelegate repaints only affected rows.
    for (int s = 0; s < GitStatusEntry::SectionCount; ++s) {
        const bool sectionIsStaged = (s == GitStatusEntry::Staged);
        if (sectionIsStaged != stagedSide) continue;
        auto &bucket = m_buckets[s];
        for (int r = 0; r < bucket.size(); ++r) {
            const auto it = stats.constFind(bucket.at(r).relPath);
            if (it == stats.constEnd()) continue;
            GitStatusEntry &e = bucket[r];
            e.addedLines = it->added;
            e.deletedLines = it->deleted;
            e.isBinary = it->isBinary;

            // Locate visible row index for this bucket position.
            int visParent = -1;
            for (int v = 0; v < m_visibleSections.size(); ++v) {
                if (m_visibleSections.at(v) == s) { visParent = v; break; }
            }
            if (visParent < 0) continue;
            const QModelIndex parentIdx = createIndex(visParent, 0, quintptr(-1));
            const QModelIndex idx = index(r, 0, parentIdx);
            emit dataChanged(idx, idx, { Qt::DisplayRole, AddedLinesRole, DeletedLinesRole, IsBinaryRole });
        }
    }
}

void GitStatusModel::mergeSubmoduleStats(const QString &relPath, qint32 added, qint32 deleted)
{
    if (relPath.isEmpty()) return;
    // Apply to every section: a submodule with both pointer + content change
    // can appear in both Staged and Tracked, and we want both rows to show the
    // same inner stats.
    for (int s = 0; s < GitStatusEntry::SectionCount; ++s) {
        auto &bucket = m_buckets[s];
        for (int r = 0; r < bucket.size(); ++r) {
            if (bucket.at(r).relPath != relPath) continue;
            GitStatusEntry &e = bucket[r];
            if (e.subAddedLines == added && e.subDeletedLines == deleted) continue;
            e.subAddedLines = added;
            e.subDeletedLines = deleted;

            int visParent = -1;
            for (int v = 0; v < m_visibleSections.size(); ++v) {
                if (m_visibleSections.at(v) == s) { visParent = v; break; }
            }
            if (visParent < 0) continue;
            const QModelIndex parentIdx = createIndex(visParent, 0, quintptr(-1));
            const QModelIndex idx = index(r, 0, parentIdx);
            emit dataChanged(idx, idx, { Qt::DisplayRole, SubAddedLinesRole, SubDeletedLinesRole });
        }
    }
}

QStringList GitStatusModel::modifiedSubmodulePaths() const
{
    QStringList out;
    for (int s = 0; s < GitStatusEntry::SectionCount; ++s) {
        for (const GitStatusEntry &e : m_buckets[s]) {
            if (!e.isSubmodule || !e.subHasModifiedContent) continue;
            if (!out.contains(e.relPath)) out.append(e.relPath);
        }
    }
    return out;
}

void GitStatusModel::setDarkPalette(bool isDark)
{
    if (m_isDark == isDark) return;
    m_isDark = isDark;
    if (m_visibleSections.isEmpty()) return;
    const QModelIndex topLeft = index(0, 0);
    const QModelIndex botRight = index(m_visibleSections.size() - 1, 0);
    emit dataChanged(topLeft, botRight, { Qt::ForegroundRole });
    // Children rows too
    for (int v = 0; v < m_visibleSections.size(); ++v) {
        const QModelIndex secIdx = index(v, 0);
        const int rc = rowCount(secIdx);
        if (rc == 0) continue;
        emit dataChanged(index(0, 0, secIdx), index(rc - 1, 0, secIdx), { Qt::ForegroundRole });
    }
}

QStringList GitStatusModel::relPathsAtIndexes(const QModelIndexList &idxs) const
{
    QStringList out;
    for (const QModelIndex &i : idxs) {
        if (!i.isValid()) continue;
        if (isSection(i)) {
            const auto sec = sectionOf(i);
            for (const GitStatusEntry &e : m_buckets[sec])
                if (!out.contains(e.relPath)) out.append(e.relPath);
        } else {
            const auto sec = sectionOf(i);
            if (i.row() < m_buckets[sec].size()) {
                const QString p = m_buckets[sec].at(i.row()).relPath;
                if (!out.contains(p)) out.append(p);
            }
        }
    }
    return out;
}

QStringList GitStatusModel::stagedSelectionPaths(const QModelIndexList &idxs) const
{
    QStringList out;
    for (const QModelIndex &i : idxs) {
        if (!i.isValid()) continue;
        if (isSection(i)) {
            if (sectionOf(i) == GitStatusEntry::Staged)
                for (const GitStatusEntry &e : m_buckets[GitStatusEntry::Staged])
                    if (!out.contains(e.relPath)) out.append(e.relPath);
        } else {
            const auto sec = sectionOf(i);
            if (sec == GitStatusEntry::Staged && i.row() < m_buckets[sec].size()) {
                const QString p = m_buckets[sec].at(i.row()).relPath;
                if (!out.contains(p)) out.append(p);
            }
        }
    }
    return out;
}

QStringList GitStatusModel::unstagedSelectionPaths(const QModelIndexList &idxs) const
{
    QStringList out;
    for (const QModelIndex &i : idxs) {
        if (!i.isValid()) continue;
        if (isSection(i)) {
            const auto sec = sectionOf(i);
            if (sec == GitStatusEntry::Tracked || sec == GitStatusEntry::Untracked)
                for (const GitStatusEntry &e : m_buckets[sec])
                    if (!out.contains(e.relPath)) out.append(e.relPath);
        } else {
            const auto sec = sectionOf(i);
            if ((sec == GitStatusEntry::Tracked || sec == GitStatusEntry::Untracked) && i.row() < m_buckets[sec].size()) {
                const QString p = m_buckets[sec].at(i.row()).relPath;
                if (!out.contains(p)) out.append(p);
            }
        }
    }
    return out;
}
