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

#include "GitHistoryView.h"

#include "GitCommitInfo.h"
#include "GitHistoryFetcher.h"
#include "GitHistoryItemDelegate.h"
#include "GitHistoryModel.h"
#include "GitWatcher.h"

#include <QCheckBox>
#include <QClipboard>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>

namespace {
constexpr int kPageSize = 500;
} // namespace

// Filter proxy: matches subject (DisplayRole) + author + shortSha against
// the filter substring, case-insensitive.
class HistoryFilterProxy : public QSortFilterProxyModel
{
public:
    explicit HistoryFilterProxy(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}
    QString filterText() const { return m_filter; }
    void setFilterText(const QString &t) {
        if (t == m_filter) return;
        m_filter = t;
        invalidateRowsFilter();
    }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override {
        if (m_filter.isEmpty()) return true;
        const QModelIndex src = sourceModel()->index(row, 0, parent);
        const QString subject = src.data(GitHistoryModel::SubjectRole).toString();
        if (subject.contains(m_filter, Qt::CaseInsensitive)) return true;
        const QString author = src.data(GitHistoryModel::AuthorNameRole).toString();
        if (author.contains(m_filter, Qt::CaseInsensitive)) return true;
        const QString sha = src.data(GitHistoryModel::FullShaRole).toString();
        if (sha.contains(m_filter, Qt::CaseInsensitive)) return true;
        return false;
    }
private:
    QString m_filter;
};

GitHistoryView::GitHistoryView(QWidget *parent) : QWidget(parent)
{
    m_model    = new GitHistoryModel(this);
    m_proxy    = new HistoryFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_fetcher  = new GitHistoryFetcher(this);
    m_delegate = new GitHistoryItemDelegate(this);
    buildUi();

    connect(m_fetcher, &GitHistoryFetcher::commitsAppended,
            this, &GitHistoryView::onCommitsAppended);
    connect(m_fetcher, &GitHistoryFetcher::fetchStarted,
            this, &GitHistoryView::onFetchStarted);
    connect(m_fetcher, &GitHistoryFetcher::fetchFinished,
            this, &GitHistoryView::onFetchFinished);
}

GitHistoryView::~GitHistoryView() = default;

void GitHistoryView::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 6, 0, 0);
    root->setSpacing(4);

    // Search box (hidden by default — '/' shows it).
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Filter commits…"));
    m_search->setClearButtonEnabled(true);
    m_search->setVisible(false);
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged,
            this, &GitHistoryView::onSearchTextChanged);
    root->addWidget(m_search);

    // List view.
    m_list = new QListView(this);
    m_list->setModel(m_proxy);
    m_list->setItemDelegate(m_delegate);
    m_list->setUniformItemSizes(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setAlternatingRowColors(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->installEventFilter(this);
    connect(m_list, &QListView::clicked,
            this, &GitHistoryView::onRowActivated);
    connect(m_list, &QListView::activated,
            this, &GitHistoryView::onRowActivated);
    connect(m_list, &QListView::customContextMenuRequested,
            this, [this](const QPoint &pos) {
        const QModelIndex idx = m_list->indexAt(pos);
        if (!idx.isValid()) return;
        const QString fullSha = idx.data(GitHistoryModel::FullShaRole).toString();
        const QString shortSha = idx.data(GitHistoryModel::ShortShaRole).toString();
        const QString subject = idx.data(GitHistoryModel::SubjectRole).toString();
        QMenu menu(this);
        QAction *aCopy = menu.addAction(tr("Copy SHA"));
        QAction *aCopyShort = menu.addAction(tr("Copy short SHA"));
        QAction *aCopyMsg = menu.addAction(tr("Copy commit message"));
        connect(aCopy, &QAction::triggered, this, [fullSha]() {
            QGuiApplication::clipboard()->setText(fullSha);
        });
        connect(aCopyShort, &QAction::triggered, this, [shortSha]() {
            QGuiApplication::clipboard()->setText(shortSha);
        });
        connect(aCopyMsg, &QAction::triggered, this, [subject]() {
            QGuiApplication::clipboard()->setText(subject);
        });
        menu.exec(m_list->viewport()->mapToGlobal(pos));
    });
    root->addWidget(m_list, 1);

    // Empty / status label overlay (lives below the list).
    m_emptyLabel = new QLabel(tr("No commits yet"), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral(
        "color: palette(placeholder-text); padding: 24px;"));
    m_emptyLabel->hide();
    root->addWidget(m_emptyLabel);

    // Footer row: status + load-more button + all-branches toggle.
    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(6, 0, 6, 4);
    footer->setSpacing(6);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(QStringLiteral(
        "color: palette(placeholder-text); font-size: 11px;"));
    m_loadMoreBtn = new QPushButton(tr("Load more"), this);
    m_loadMoreBtn->setFlat(true);
    m_loadMoreBtn->setAutoDefault(false);
    m_loadMoreBtn->setDefault(false);
    m_loadMoreBtn->setVisible(false);
    connect(m_loadMoreBtn, &QPushButton::clicked,
            this, &GitHistoryView::onLoadMoreClicked);
    m_allBranchesBox = new QCheckBox(tr("All branches"), this);
    connect(m_allBranchesBox, &QCheckBox::toggled,
            this, &GitHistoryView::onAllBranchesToggled);
    footer->addWidget(m_statusLabel, 1);
    footer->addWidget(m_loadMoreBtn);
    footer->addWidget(m_allBranchesBox);
    root->addLayout(footer);
}

void GitHistoryView::setRepoRoot(const QString &repoToplevel)
{
    m_fetcher->setRepoRoot(repoToplevel);
    m_model->clear();
    m_reachedEnd = false;
    updateFooter();
    if (!repoToplevel.isEmpty()) {
        m_fetcher->refetch(kPageSize);
    }
}

void GitHistoryView::setWatcher(GitWatcher *watcher)
{
    m_watcher = watcher;
    m_fetcher->connectWatcher(watcher);
}

void GitHistoryView::setAllBranches(bool all)
{
    if (m_allBranchesBox->isChecked() == all) {
        m_fetcher->setAllBranches(all);
        return;
    }
    // Setter triggers onAllBranchesToggled via QCheckBox::toggled signal,
    // which handles refetch.
    QSignalBlocker block(m_allBranchesBox);
    m_allBranchesBox->setChecked(all);
    m_fetcher->setAllBranches(all);
}

bool GitHistoryView::allBranches() const
{
    return m_allBranchesBox->isChecked();
}

void GitHistoryView::onCommitsAppended(const QVector<GitCommitInfo> &chunk)
{
    m_model->appendChunk(chunk);
    updateFooter();
}

void GitHistoryView::onFetchStarted()
{
    m_loading = true;
    m_statusLabel->setText(tr("Loading…"));
    m_loadMoreBtn->setVisible(false);
}

void GitHistoryView::onFetchFinished(bool reachedEnd, const QString &errorMessage)
{
    m_loading = false;
    m_reachedEnd = reachedEnd;
    if (!errorMessage.isEmpty()) {
        m_statusLabel->setText(tr("Error: %1").arg(errorMessage));
    } else {
        m_statusLabel->clear();
    }
    updateFooter();
}

void GitHistoryView::onRowActivated(const QModelIndex &index)
{
    if (!index.isValid()) return;
    const QString sha = index.data(GitHistoryModel::FullShaRole).toString();
    if (sha.isEmpty()) return;
    emit openCommitDetailRequested(sha.toLatin1());
}

void GitHistoryView::onSearchTextChanged(const QString &text)
{
    static_cast<HistoryFilterProxy *>(m_proxy)->setFilterText(text);
    m_delegate->setFilterQuery(text);
    m_list->viewport()->update();
    updateFooter();
}

void GitHistoryView::onAllBranchesToggled(bool on)
{
    m_fetcher->setAllBranches(on);
    m_model->clear();
    m_reachedEnd = false;
    m_fetcher->refetch(kPageSize);
}

void GitHistoryView::onLoadMoreClicked()
{
    if (m_loading || m_reachedEnd) return;
    m_fetcher->loadMore(m_model->count(), kPageSize);
}

void GitHistoryView::updateFooter()
{
    const int total = m_model->count();
    if (total == 0 && !m_loading) {
        m_emptyLabel->setVisible(true);
        m_list->setVisible(false);
    } else {
        m_emptyLabel->setVisible(false);
        m_list->setVisible(true);
    }
    m_loadMoreBtn->setVisible(!m_reachedEnd && !m_loading && total > 0);
    if (m_loading) return;
    // If no filter, status reflects total + reachedEnd hint.
    const QString filter = static_cast<HistoryFilterProxy *>(m_proxy)->filterText();
    if (filter.isEmpty()) {
        if (m_reachedEnd && total > 0) {
            m_statusLabel->setText(tr("%n commit(s)", nullptr, total));
        } else if (total > 0) {
            m_statusLabel->setText(tr("%n loaded", nullptr, total));
        }
    } else {
        const int shown = m_proxy->rowCount();
        m_statusLabel->setText(tr("%1 of %n match(es) in loaded",
                                  nullptr, total).arg(shown));
    }
}

void GitHistoryView::showSearchBox(bool show)
{
    m_search->setVisible(show);
    if (show) {
        m_search->setFocus();
        m_search->selectAll();
    } else {
        m_search->clear();
        m_list->setFocus();
    }
}

bool GitHistoryView::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        // '/' from the list focuses the search box.
        if (watched == m_list && ke->key() == Qt::Key_Slash
            && !(ke->modifiers() & Qt::ControlModifier)) {
            showSearchBox(true);
            return true;
        }
        if (watched == m_search && ke->key() == Qt::Key_Escape) {
            showSearchBox(false);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void GitHistoryView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Slash) {
        showSearchBox(true);
        return;
    }
    QWidget::keyPressEvent(event);
}
