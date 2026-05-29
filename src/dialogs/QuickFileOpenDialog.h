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

#pragma once

#include <QAbstractListModel>
#include <QDialog>
#include <QLineEdit>
#include <QListView>
#include <QStringList>
#include <QVarLengthArray>
#include <QVector>

#include <cstdint>
#include <memory>

#include "FileIndexCache.h"

class QuickFileOpenModel;

// One ranked search result. `index` recovers the original path from the
// snapshot's displayPaths (used for display + smart-case backtrace). positions
// is inline for patterns up to 32 chars (the common case), so highlight data
// for the ≤200 survivors needs no heap allocation.
struct QuickFileOpenCandidate
{
    qint32 score = 0;
    qint32 pathLen = 0;
    qint32 index = 0;
    QVarLengthArray<qint32, 32> positions;
};

class QuickFileOpenDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QuickFileOpenDialog(const QString &rootPath, QWidget *parent = nullptr);
    ~QuickFileOpenDialog() override;

    QString selectedFilePath() const;

    // Adopt a fresh immutable index snapshot (initial serve or revalidation
    // swap). Re-runs the current filter against the new snapshot.
    void adoptSnapshot(std::shared_ptr<const FileIndexCache> snapshot);

    // Workspace-relative most-recently-used files, for empty-query ordering.
    void setMruFiles(const QStringList &mru);

    static constexpr int MatchPositionsRole = Qt::UserRole + 1;
    static constexpr int kMaxResults = 200;

    // --- Pure, static search core (unit-testable without a live dialog/git) ---

    // True when the query contains at least one uppercase character (smart-case
    // decides case-sensitivity once, here).
    static bool isCaseSensitivePattern(const QString &pattern);

    // Convenience scorer for a single (pattern, candidate) pair. Returns the
    // fuzzy score (> 0 match, 0 no match). Folds + decides smart-case
    // internally. Used by the scoring unit test.
    static int score(const QString &pattern, const QString &candidate);

    // Two-phase top-K search over a snapshot. Phase 1 scores every candidate
    // with no per-candidate allocation; a bounded min-heap keeps the best
    // `limit`; Phase 2 backtraces match positions ONLY for the survivors.
    // Returns survivors sorted best-first (stable, deterministic).
    static QVector<QuickFileOpenCandidate> computeMatches(const FileIndexCache &snapshot,
                                                          const QString &pattern,
                                                          int limit);

    // Empty-query ordering: MRU files (present in the snapshot) first in MRU
    // order, then remaining files in enumeration order, truncated to `limit`.
    // No scoring, no heap. O(R + K) when MRU is empty; one early-exiting scan
    // to locate MRU entries otherwise.
    static QVector<QuickFileOpenCandidate> computeEmpty(const FileIndexCache &snapshot,
                                                        const QStringList &mru,
                                                        int limit);

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onTextChanged(const QString &text);
    void onItemActivated(const QModelIndex &index);

private:
    void applyFilter(const QString &pattern);

    QString m_rootPath;
    QLineEdit *m_lineEdit = nullptr;
    QListView *m_listView = nullptr;
    QuickFileOpenModel *m_model = nullptr;

    std::shared_ptr<const FileIndexCache> m_snapshot;
    QStringList m_mru;
    QString m_selectedFile;

    // Cached empty-query result; rebuilt only when the snapshot or MRU changes.
    QVector<QuickFileOpenCandidate> m_emptyResult;
    bool m_emptyDirty = true;
};

// Virtualized list model backed directly by the top-K candidate vector. Each
// keystroke does ONE beginResetModel/move/endResetModel (no clear()+appendRow
// churn). Match positions are cached per candidate, so repaints recompute
// nothing. QListView paints only the ~visible rows.
class QuickFileOpenModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit QuickFileOpenModel(QObject *parent = nullptr);

    void setSnapshot(std::shared_ptr<const FileIndexCache> snapshot);
    void setResults(QVector<QuickFileOpenCandidate> &&results);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    std::shared_ptr<const FileIndexCache> m_snapshot;
    QVector<QuickFileOpenCandidate> m_results;
};
