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

#ifndef GIT_HISTORY_VIEW_H
#define GIT_HISTORY_VIEW_H

#include <QByteArray>
#include <QPointer>
#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSortFilterProxyModel;

class GitHistoryFetcher;
class GitHistoryItemDelegate;
class GitHistoryModel;
class GitWatcher;

// Tab body for the "History" view in GitTabWidget.
//
// Composition:
//   [ optional search box (hidden, shown on '/') ]
//   [ QListView with custom delegate, uniform row sizes, virtualized ]
//   [ footer row: spinner/status + "Load more" link + AllBranches toggle ]
//
// Owns the GitHistoryModel and GitHistoryFetcher. The host (GitTabWidget)
// only calls setRepoRoot() and listens to openCommitDetailRequested.
class GitHistoryView : public QWidget
{
    Q_OBJECT
public:
    explicit GitHistoryView(QWidget *parent = nullptr);
    ~GitHistoryView() override;

    // Set the repo toplevel. Cancels any in-flight fetch, clears the model,
    // and issues a fresh git log if repoToplevel is non-empty.
    void setRepoRoot(const QString &repoToplevel);

    // Hook the watcher so refs/HEAD changes auto-refresh.
    void setWatcher(GitWatcher *watcher);

    // Initial "all branches" toggle state restoration. Re-fetches when changed.
    void setAllBranches(bool all);
    bool allBranches() const;

signals:
    // User clicked a commit row — host opens a commit detail tab.
    void openCommitDetailRequested(const QByteArray &sha);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onCommitsAppended(const QVector<struct GitCommitInfo> &chunk);
    void onFetchStarted();
    void onFetchFinished(bool reachedEnd, const QString &errorMessage);
    void onRowActivated(const QModelIndex &index);
    void onSearchTextChanged(const QString &text);
    void onAllBranchesToggled(bool on);
    void onLoadMoreClicked();

private:
    void buildUi();
    void showSearchBox(bool show);
    void updateFooter();

    GitHistoryModel        *m_model = nullptr;
    QSortFilterProxyModel  *m_proxy = nullptr;
    GitHistoryFetcher      *m_fetcher = nullptr;
    GitHistoryItemDelegate *m_delegate = nullptr;

    QListView   *m_list = nullptr;
    QLineEdit   *m_search = nullptr;
    QLabel      *m_emptyLabel = nullptr;
    QLabel      *m_statusLabel = nullptr;
    QPushButton *m_loadMoreBtn = nullptr;
    QCheckBox   *m_allBranchesBox = nullptr;

    bool m_loading = false;
    bool m_reachedEnd = false;

    QPointer<GitWatcher> m_watcher;
};

#endif // GIT_HISTORY_VIEW_H
