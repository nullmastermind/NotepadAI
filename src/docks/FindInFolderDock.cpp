#include "FindInFolderDock.h"
#include "ui_FindInFolderDock.h"
#include "FolderSearchEngine.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMutexLocker>
#include <QShortcut>
#include <QTimer>
#include <QTreeWidgetItem>

FindInFolderDock::FindInFolderDock(QWidget *parent)
    : QDockWidget(parent)
    , ui(new Ui::FindInFolderDock)
    , m_engine(new FolderSearchEngine(this))
{
    ui->setupUi(this);

    new QShortcut(QKeySequence::Cancel, this, this, &FindInFolderDock::close,
                  Qt::WidgetWithChildrenShortcut);

    connect(ui->btnSearch, &QPushButton::clicked, this, &FindInFolderDock::performSearch);
    connect(ui->searchInput, &QLineEdit::returnPressed, this, &FindInFolderDock::performSearch);
    connect(ui->resultsTree, &QTreeWidget::itemActivated, this, &FindInFolderDock::onItemActivated);
    connect(ui->resultsTree, &QTreeWidget::itemClicked, this, &FindInFolderDock::onItemClicked);
    connect(ui->resultsTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *current, QTreeWidgetItem *) {
        if (!current) return;
        if (!ui->resultsTree->hasFocus()) return;
        onItemClicked(current, 0);
    });

    m_drainTimer = new QTimer(this);
    m_drainTimer->setInterval(16);
    connect(m_drainTimer, &QTimer::timeout, this, &FindInFolderDock::drainResults);
    connect(m_drainTimer, &QTimer::timeout, m_engine, &FolderSearchEngine::pollEnumeration);
}

FindInFolderDock::~FindInFolderDock()
{
    m_drainTimer->stop();
    m_engine->cancel();
    delete ui;
}

void FindInFolderDock::setFolder(const QString &folderPath, const QString &workspaceRoot)
{
    m_folderPath = folderPath;
    m_workspaceRoot = workspaceRoot;

    QString displayPath = folderPath;
    if (!workspaceRoot.isEmpty() && displayPath.startsWith(workspaceRoot)) {
        displayPath = displayPath.mid(workspaceRoot.length());
        if (displayPath.startsWith(QLatin1Char('/')) || displayPath.startsWith(QLatin1Char('\\')))
            displayPath = displayPath.mid(1);
        if (displayPath.isEmpty())
            displayPath = QFileInfo(workspaceRoot).fileName();
    }
    ui->folderLabel->setText(tr("Folder: %1").arg(QDir::toNativeSeparators(displayPath)));

    show();
    raise();
    ui->searchInput->setFocus();
    ui->searchInput->selectAll();
}

void FindInFolderDock::performSearch()
{
    const QString query = ui->searchInput->text();
    if (query.isEmpty() || m_folderPath.isEmpty())
        return;

    m_drainTimer->stop();
    m_engine->cancel();

    ui->resultsTree->clear();
    m_totalFiles = 0;
    m_totalMatches = 0;
    ui->statusLabel->setText(tr("Searching..."));

    m_engine->startSearch(m_folderPath, m_workspaceRoot, query,
                          ui->btnCaseSensitive->isChecked(),
                          ui->btnWholeWord->isChecked(),
                          ui->btnRegex->isChecked());
    m_drainTimer->start();
}

void FindInFolderDock::drainResults()
{
    auto sink = m_engine->sink();
    if (!sink) return;

    QVector<FolderSearchFileBatch> batches;
    {
        QMutexLocker lock(&sink->mutex);
        if (sink->pending.isEmpty()) {
            if (sink->workersLaunched.load(std::memory_order_acquire)
                && sink->workersRemaining.load(std::memory_order_relaxed) == 0
                && m_drainTimer->isActive()) {
                m_drainTimer->stop();
                ui->statusLabel->setText(
                    tr("%L1 matches in %L2 files").arg(m_totalMatches).arg(m_totalFiles));
            }
            return;
        }
        batches.swap(sink->pending);
    }

    ui->resultsTree->setUpdatesEnabled(false);

    QElapsedTimer frameBudget;
    frameBudget.start();
    constexpr qint64 kMaxFrameMs = 8;
    int consumed = 0;

    for (const FolderSearchFileBatch &batch : batches) {
        QString displayPath = batch.filePath;
        if (!m_workspaceRoot.isEmpty() && displayPath.startsWith(m_workspaceRoot)) {
            displayPath = displayPath.mid(m_workspaceRoot.length());
            if (displayPath.startsWith(QLatin1Char('/')) || displayPath.startsWith(QLatin1Char('\\')))
                displayPath = displayPath.mid(1);
        }

        auto *fileItem = new QTreeWidgetItem(ui->resultsTree);
        fileItem->setText(0, QStringLiteral("%1 (%L2 matches)")
                             .arg(QDir::toNativeSeparators(displayPath))
                             .arg(batch.matches.size()));
        fileItem->setExpanded(true);

        const QPalette &pal = ui->resultsTree->palette();
        fileItem->setBackground(0, pal.color(QPalette::AlternateBase));

        for (const FolderSearchMatch &match : batch.matches) {
            auto *matchItem = new QTreeWidgetItem(fileItem);
            matchItem->setText(0, QStringLiteral("  %1: %2")
                                  .arg(match.lineNumber + 1)
                                  .arg(match.lineText));
            matchItem->setData(0, Qt::UserRole, batch.filePath);
            matchItem->setData(0, Qt::UserRole + 1, match.lineNumber);
            matchItem->setData(0, Qt::UserRole + 2, match.matchStart);
            matchItem->setData(0, Qt::UserRole + 3, match.matchEnd);
        }

        m_totalFiles++;
        m_totalMatches += batch.matches.size();
        consumed++;

        if (frameBudget.elapsed() >= kMaxFrameMs)
            break;
    }

    ui->resultsTree->setUpdatesEnabled(true);

    // Return unconsumed batches to the sink for the next tick
    if (consumed < batches.size()) {
        QMutexLocker lock(&sink->mutex);
        for (int i = batches.size() - 1; i >= consumed; --i) {
            sink->pending.prepend(std::move(batches[i]));
        }
    }

    ui->statusLabel->setText(tr("Searching... %L1 matches in %L2 files")
                             .arg(m_totalMatches).arg(m_totalFiles));
}

void FindInFolderDock::onItemActivated(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    const QString filePath = item->data(0, Qt::UserRole).toString();
    if (filePath.isEmpty()) return;

    const int lineNumber = item->data(0, Qt::UserRole + 1).toInt();
    const int matchStart = item->data(0, Qt::UserRole + 2).toInt();
    const int matchEnd = item->data(0, Qt::UserRole + 3).toInt();

    emit resultActivated(filePath, lineNumber, matchStart, matchEnd);
}

void FindInFolderDock::onItemClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);

    const QString filePath = item->data(0, Qt::UserRole).toString();
    if (filePath.isEmpty()) return;

    const int lineNumber = item->data(0, Qt::UserRole + 1).toInt();
    const int matchStart = item->data(0, Qt::UserRole + 2).toInt();
    const int matchEnd = item->data(0, Qt::UserRole + 3).toInt();

    emit resultClicked(filePath, lineNumber, matchStart, matchEnd);
}
