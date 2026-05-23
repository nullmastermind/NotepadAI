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

#ifndef GIT_STATUS_MODEL_H
#define GIT_STATUS_MODEL_H

#include "GitNumstatParser.h"
#include "GitStatusEntry.h"

#include <QAbstractItemModel>
#include <QHash>

#include <cstdint>

// Two-level tree:
//   Section row (parent = invalid) holds title + count + child entries
//   File row (parent = section) holds one GitStatusEntry
//
// Sections with zero children are hidden.
class GitStatusModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Roles : std::uint16_t {
        RelPathRole = Qt::UserRole + 1,
        ChangeRole,
        OrigPathRole,
        StagedSideRole,
        SectionRole,
        XyRole,
        IsSectionRole,
        EntryRole,
        AddedLinesRole,
        DeletedLinesRole,
        IsBinaryRole,
        HasUnstableEncodingRole,
        OursShaRole,
        TheirsShaRole,
        IsSubmoduleRole,
        SubAddedLinesRole,
        SubDeletedLinesRole
    };

    explicit GitStatusModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QModelIndex index(int row, int col, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void setEntries(const GitStatusEntries &entries);
    // Merge numstat hash (keyed by relPath) into the matching side. Emits
    // dataChanged for affected rows; safe to call repeatedly.
    void mergeNumstat(const QHash<QString, GitNumstatParser::Stat> &stats, bool stagedSide);

    // Merge aggregated inner-submodule stats. Applies to ALL entries with the
    // matching relPath (a submodule can show in both Staged and Tracked).
    void mergeSubmoduleStats(const QString &relPath, qint32 added, qint32 deleted);

    // Enumerate unique relPaths of submodule entries with modified content
    // (sub field 'S.M.' / 'SCM.' / etc). Pointer-only changes are excluded —
    // their inner diff is empty anyway, so spawning a process would waste cost.
    QStringList modifiedSubmodulePaths() const;

    void setDarkPalette(bool isDark);

    bool isSection(const QModelIndex &i) const;
    GitStatusEntry::Section sectionOf(const QModelIndex &i) const;
    int entriesInSection(GitStatusEntry::Section s) const;
    int totalEntries() const;
    bool hasConflicts() const { return !m_buckets[GitStatusEntry::Conflicts].isEmpty(); }
    bool hasStaged() const   { return !m_buckets[GitStatusEntry::Staged].isEmpty(); }

    // Helpers for the view: pick the rel paths under a list of indexes
    // (sections expand to their children).
    QStringList relPathsAtIndexes(const QModelIndexList &idxs) const;
    QStringList stagedSelectionPaths(const QModelIndexList &idxs) const;
    QStringList unstagedSelectionPaths(const QModelIndexList &idxs) const;

    static QString sectionTitle(GitStatusEntry::Section s);
    static QString changeShort(GitStatusEntry::Change c);

private:
    GitStatusEntries m_buckets[GitStatusEntry::SectionCount];
    // Map from visible row index → real Section value. Sections with 0 entries are skipped.
    QVector<GitStatusEntry::Section> m_visibleSections;
    bool m_isDark = false;
    void rebuildVisible();
};

#endif // GIT_STATUS_MODEL_H
