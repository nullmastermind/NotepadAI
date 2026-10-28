/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "Pm2ProcessesDialog.h"

#include "Pm2Entrypoint.h"
#include "Pm2ProcessEditDialog.h"
#include "Pm2ProcessModel.h"
#include "TerminalManager.h"
#include "TerminalTaskLauncher.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSaveFile>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

using Column = Pm2ProcessModel::Column;

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

bool isRunning(const Pm2ProcessInfo &p)
{
    return pm2IsRunning(p.status);
}

bool isNotRunning(const Pm2ProcessInfo &p)
{
    return !pm2IsRunning(p.status);
}

// Hue carries the state (green / amber / red); lightness follows the live
// palette so the dot reads on light and dark bases alike.
QColor statusColor(Pm2Status status, const QPalette &palette)
{
    int hue = -1;
    switch (status) {
    case Pm2Status::Online: hue = 135; break;
    case Pm2Status::Launching:
    case Pm2Status::Stopping: hue = 40; break;
    case Pm2Status::Errored: hue = 5; break;
    case Pm2Status::Stopped:
    case Pm2Status::Other: break;
    }
    if (hue < 0)
        return palette.color(QPalette::PlaceholderText);
    const bool darkBase = palette.color(QPalette::Base).lightness() < 128;
    return QColor::fromHsl(hue, 150, darkBase ? 140 : 105);
}

// Status cell: a coloured dot before pm2's word (the word carries the
// meaning; the dot only makes it scannable).
class StatusDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QString text = opt.text;
        opt.text.clear();
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

        const auto status = Pm2Status(index.data(Pm2ProcessModel::StatusRole).toInt());
        const int d = dotSize(opt);
        const QRect r = opt.rect.adjusted(kPad, 0, -kPad, 0);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(statusColor(status, opt.palette));
        painter->drawEllipse(QRectF(r.left(), r.center().y() - d / 2.0 + 0.5, d, d));
        const bool selected = opt.state & QStyle::State_Selected;
        painter->setPen(opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        const QRect textRect = r.adjusted(d + kGap, 0, 0, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                          opt.fontMetrics.elidedText(text, Qt::ElideRight, textRect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.rwidth() += dotSize(option) + kGap + kPad;
        return s;
    }

private:
    static constexpr int kPad = 4;
    static constexpr int kGap = 6;
    static int dotSize(const QStyleOptionViewItem &opt) { return qMax(6, opt.fontMetrics.height() / 2); }
};

QToolButton *toolButton(const QString &text, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setAutoRaise(true);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return b;
}

QString entrypointPath(const QString &name)
{
    return Pm2Client::entrypointDir() + QLatin1Char('/') + name + QStringLiteral(".cjs");
}

bool writeEntrypoint(const Pm2Definition &def, QString *error)
{
    const QString path = entrypointPath(def.name);
    QSaveFile file(path);
    const QByteArray text = Pm2Entrypoint::generate(def);
    if (QDir().mkpath(Pm2Client::entrypointDir()) && file.open(QIODevice::WriteOnly)
        && file.write(text) == text.size() && file.commit()) {
        return true;
    }
    *error = QObject::tr("Could not write %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
    return false;
}

std::optional<Pm2Definition> readEntrypoint(const QString &script)
{
    QFile file(script);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return Pm2Entrypoint::parse(file.readAll());
}

} // namespace

Pm2ProcessesDialog::Pm2ProcessesDialog(TerminalManager *terminals, std::function<QString()> defaultCwd,
                                       QWidget *parent)
    : QDialog(parent)
    , m_terminals(terminals)
    , m_defaultCwd(std::move(defaultCwd))
    , m_client(new Pm2Client(this))
    , m_model(new Pm2ProcessModel(this))
    , m_proxy(new QSortFilterProxyModel(this))
    , m_table(new QTableView(this))
    , m_clock(new QTimer(this))
{
    setWindowTitle(tr("PM2 Processes"));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(false);
    resize(960, 440);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // Banner: hidden until there is something to say.
    m_banner = new QFrame(this);
    m_banner->setObjectName(QStringLiteral("pm2Banner"));
    auto *bannerLayout = new QHBoxLayout(m_banner);
    bannerLayout->setContentsMargins(8, 4, 4, 4);
    bannerLayout->setSpacing(6);
    m_bannerText = new QLabel(m_banner);
    m_bannerText->setWordWrap(true);
    m_bannerText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_bannerPrimary = new QPushButton(m_banner);
    m_bannerSecondary = new QPushButton(m_banner);
    bannerLayout->addWidget(m_bannerText, 1);
    bannerLayout->addWidget(m_bannerPrimary);
    bannerLayout->addWidget(m_bannerSecondary);
    m_banner->hide();
    layout->addWidget(m_banner);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(4);
    m_newButton = toolButton(tr("New…"), this);
    m_editButton = toolButton(tr("Edit…"), this);
    m_startButton = toolButton(tr("Start"), this);
    m_stopButton = toolButton(tr("Stop"), this);
    m_restartButton = toolButton(tr("Restart"), this);
    m_deleteButton = toolButton(tr("Delete"), this);
    m_logsButton = toolButton(tr("Logs"), this);
    m_restartAllButton = toolButton(tr("Restart all"), this);
    m_stopAllButton = toolButton(tr("Stop all"), this);
    m_resurrectButton = toolButton(tr("Resurrect"), this);
    m_newButton->setToolTip(tr("Run a command under pm2"));
    m_logsButton->setToolTip(tr("Follow the process's output in a terminal tab"));
    for (QToolButton *b : {m_newButton, m_editButton, m_startButton, m_stopButton, m_restartButton,
                           m_deleteButton, m_logsButton})
        bar->addWidget(b);
    bar->addStretch(1);
    for (QToolButton *b : {m_restartAllButton, m_stopAllButton, m_resurrectButton})
        bar->addWidget(b);
    layout->addLayout(bar);

    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Pm2ProcessModel::SortRole);
    m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_table->setModel(m_proxy);
    m_table->setItemDelegateForColumn(int(Column::Status), new StatusDelegate(m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(int(Column::Name), Qt::AscendingOrder);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(m_table->fontMetrics().height() + 8);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setHighlightSections(false);
    // Auto-size from a bounded sample of rows, never a full scan.
    m_table->horizontalHeader()->setResizeContentsPrecision(50);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->installEventFilter(this);
    layout->addWidget(m_table, 1);

    // Empty state, over the table's viewport (hidden while it has rows).
    m_emptyLabel = new QLabel(tr("No PM2 processes. Click New… to run a command under pm2."), m_table->viewport());
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setForegroundRole(QPalette::PlaceholderText);
    auto *emptyLayout = new QVBoxLayout(m_table->viewport());
    emptyLayout->addWidget(m_emptyLabel);
    m_emptyLabel->hide();

    auto *footer = new QHBoxLayout;
    m_footer = new QLabel(this);
    m_footer->setForegroundRole(QPalette::PlaceholderText);
    auto *close = new QPushButton(tr("Close"), this);
    footer->addWidget(m_footer, 1);
    footer->addWidget(close);
    layout->addLayout(footer);

    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_newButton, &QToolButton::clicked, this, &Pm2ProcessesDialog::newProcess);
    connect(m_editButton, &QToolButton::clicked, this, &Pm2ProcessesDialog::editProcess);
    connect(m_startButton, &QToolButton::clicked, this, &Pm2ProcessesDialog::startSelected);
    connect(m_stopButton, &QToolButton::clicked, this, &Pm2ProcessesDialog::stopSelected);
    connect(m_restartButton, &QToolButton::clicked, this, &Pm2ProcessesDialog::restartSelected);
    connect(m_deleteButton, &QToolButton::clicked, this, &Pm2ProcessesDialog::deleteSelected);
    connect(m_logsButton, &QToolButton::clicked, this, &Pm2ProcessesDialog::showLogs);
    connect(m_restartAllButton, &QToolButton::clicked, this, &Pm2ProcessesDialog::restartAll);
    connect(m_stopAllButton, &QToolButton::clicked, this, &Pm2ProcessesDialog::stopAll);
    connect(m_resurrectButton, &QToolButton::clicked, this, &Pm2ProcessesDialog::resurrect);
    // Run a copy: every action ends in updateBanner(), which replaces the
    // stored one while it is still running.
    connect(m_bannerPrimary, &QPushButton::clicked, this, [this]() {
        if (const std::function<void()> action = m_bannerPrimaryAction)
            action();
    });
    connect(m_bannerSecondary, &QPushButton::clicked, this, [this]() {
        if (const std::function<void()> action = m_bannerSecondaryAction)
            action();
    });
    connect(m_table, &QTableView::doubleClicked, this, &Pm2ProcessesDialog::showLogs);
    connect(m_table, &QTableView::customContextMenuRequested, this, &Pm2ProcessesDialog::showContextMenu);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &Pm2ProcessesDialog::updateActions);
    // Statuses changing under a selection are picked up by onList, once per
    // list (not per changed row).

    connect(m_client, &Pm2Client::stateChanged, this, &Pm2ProcessesDialog::onStateChanged);
    connect(m_client, &Pm2Client::listReceived, this, &Pm2ProcessesDialog::onList);
    connect(m_client, &Pm2Client::commandFinished, this, &Pm2ProcessesDialog::onCommandFinished);

    m_clock->setInterval(1000);
    connect(m_clock, &QTimer::timeout, this,
            [this]() { m_model->setNow(QDateTime::currentMSecsSinceEpoch()); });
    m_model->setNow(QDateTime::currentMSecsSinceEpoch());

    updateActions();
    updateFooter();
    m_client->start();
}

Pm2ProcessesDialog::~Pm2ProcessesDialog()
{
    disconnect(m_client, nullptr, this, nullptr);
    m_client->stop();
}

void Pm2ProcessesDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    m_model->setNow(QDateTime::currentMSecsSinceEpoch());
    m_clock->start();
}

void Pm2ProcessesDialog::hideEvent(QHideEvent *event)
{
    m_clock->stop();
    QDialog::hideEvent(event);
}

bool Pm2ProcessesDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Delete && m_deleteButton->isEnabled()) {
            deleteSelected();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && m_logsButton->isEnabled()) {
            showLogs();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void Pm2ProcessesDialog::onStateChanged(Pm2Client::State state)
{
    if (state != Pm2Client::State::Ready && !m_pending.isEmpty()) {
        // Commands sent to a helper that is gone never get a result: fail
        // them, so their cleanup (e.g. a new entrypoint) still runs.
        const QHash<int, Pending> pending = std::exchange(m_pending, {});
        for (const Pending &p : pending) {
            if (p.done)
                p.done(false, tr("The pm2 helper stopped."));
        }
        refreshNotRunning();
    }
    if (state != Pm2Client::State::Ready)
        m_listLoaded = false;
    m_table->setEnabled(state == Pm2Client::State::Ready);
    m_emptyLabel->setVisible(m_listLoaded && m_model->rowCount() == 0);
    updateActions();
    updateBanner();
    updateFooter();
    emit statusChanged();
}

void Pm2ProcessesDialog::onList(const QList<Pm2ProcessInfo> &processes, const QStringList &dumpNames,
                                bool hasDump)
{
    m_model->setProcesses(processes);
    m_hasDump = hasDump;
    m_dumpNames = dumpNames;
    m_listLoaded = true;
    m_emptyLabel->setVisible(processes.isEmpty());
    refreshNotRunning();
    if (m_firstList && !processes.isEmpty()) {
        m_firstList = false;
        m_table->resizeColumnsToContents();
    }
    updateActions();
    updateBanner();
    updateFooter();
    emit statusChanged();
}

void Pm2ProcessesDialog::refreshNotRunning()
{
    QList<Pm2ProcessInfo> live(m_model->processes().cbegin(), m_model->processes().cend());
    // Names a pending command removes count as live until its save lands.
    for (const Pending &p : std::as_const(m_pending)) {
        for (const QString &name : p.removes) {
            Pm2ProcessInfo leaving;
            leaving.name = name;
            live.append(leaving);
        }
    }
    m_notRunning = Pm2Protocol::missingFromDaemon(m_dumpNames, live);
}

void Pm2ProcessesDialog::track(int requestId, Done done, bool saveAfter, const QStringList &removes)
{
    if (requestId == 0) {
        if (done)
            done(false, QString());
        return;
    }
    m_commandError.clear();
    m_pending.insert(requestId, Pending{std::move(done), saveAfter, removes});
    refreshNotRunning();
    updateBanner();
    updateFooter();
}

void Pm2ProcessesDialog::onCommandFinished(int requestId, bool ok, const QString &error)
{
    const auto it = m_pending.find(requestId);
    if (it == m_pending.end())
        return;
    // Moved out: erasing the entry ends the iterator's life.
    const Pending finished = std::move(it.value());
    m_pending.erase(it);
    if (finished.done)
        finished.done(ok, error);
    if (!ok) {
        m_commandError = error;
        refreshNotRunning();
        updateActions();
        updateBanner();
        updateFooter();
        return;
    }
    // Keep pm2's saved list current, unless it still holds processes pm2
    // does not run: saving now would forget them (the banner says so). The
    // names this command removed stay excluded until that save lands.
    if (finished.saveAfter) {
        QStringList missing = m_notRunning;
        for (const QString &name : finished.removes)
            missing.removeAll(name);
        if (missing.isEmpty())
            track(m_client->save(), {}, false, finished.removes);
    }
    refreshNotRunning();
    updateActions();
    updateBanner();
    updateFooter();
}

QList<int> Pm2ProcessesDialog::selectedRows() const
{
    QList<int> rows;
    const QModelIndexList selected = m_table->selectionModel()->selectedRows();
    rows.reserve(selected.size());
    for (const QModelIndex &index : selected)
        rows.append(m_proxy->mapToSource(index).row());
    std::sort(rows.begin(), rows.end());
    return rows;
}

QList<int> Pm2ProcessesDialog::selectedIds(bool (*filter)(const Pm2ProcessInfo &)) const
{
    QList<int> ids;
    for (const int row : selectedRows()) {
        const Pm2ProcessInfo &p = m_model->processAt(row);
        if (!filter || filter(p))
            ids.append(p.pmId);
    }
    return ids;
}

QStringList Pm2ProcessesDialog::liveNames() const
{
    QStringList names;
    names.reserve(m_model->rowCount());
    for (const Pm2ProcessInfo &p : m_model->processes())
        names.append(p.name);
    return names;
}

QStringList Pm2ProcessesDialog::takenNames() const
{
    // A saved process that is not running still owns its name (and its
    // entrypoint file): Resurrect would bring it back.
    return liveNames() + m_notRunning;
}

void Pm2ProcessesDialog::updateActions()
{
    const bool ready = m_client->state() == Pm2Client::State::Ready;
    const QList<int> rows = ready ? selectedRows() : QList<int>();
    bool anyRunning = false;
    bool anyStopped = false;
    for (const int row : rows) {
        const bool running = isRunning(m_model->processAt(row));
        anyRunning = anyRunning || running;
        anyStopped = anyStopped || !running;
    }
    bool editable = false;
    if (rows.size() == 1)
        editable = Pm2Protocol::isOwnEntrypoint(m_model->processAt(rows.front()).script, Pm2Client::entrypointDir());

    bool anyRunningAtAll = false;
    for (const Pm2ProcessInfo &p : m_model->processes())
        anyRunningAtAll = anyRunningAtAll || isRunning(p);

    m_newButton->setEnabled(ready);
    m_editButton->setEnabled(editable);
    m_editButton->setToolTip(rows.size() == 1 && !editable ? tr("Created outside NotepadAI") : QString());
    m_startButton->setEnabled(anyStopped);
    m_stopButton->setEnabled(anyRunning);
    m_restartButton->setEnabled(!rows.isEmpty());
    m_deleteButton->setEnabled(!rows.isEmpty());
    m_logsButton->setEnabled(rows.size() == 1 && !Pm2Client::sessionPm2Dir().isEmpty());
    m_restartAllButton->setEnabled(ready && m_model->rowCount() > 0);
    m_stopAllButton->setEnabled(ready && anyRunningAtAll);
    m_resurrectButton->setEnabled(ready && m_hasDump);
    m_resurrectButton->setToolTip(m_notRunning.isEmpty()
                                      ? tr("Start the processes pm2 saved that are not running")
                                      : tr("Start %1").arg(m_notRunning.join(QStringLiteral(", "))));
}

void Pm2ProcessesDialog::updateBanner()
{
    enum class Tone : uint8_t { Neutral, Warning, Error };
    auto show = [this](Tone tone, const QString &text, const QString &primary = {},
                       std::function<void()> primaryAction = {}, const QString &secondary = {},
                       std::function<void()> secondaryAction = {}) {
        m_bannerText->setText(text);
        m_bannerPrimary->setText(primary);
        m_bannerPrimary->setVisible(!primary.isEmpty());
        m_bannerPrimaryAction = std::move(primaryAction);
        m_bannerSecondary->setText(secondary);
        m_bannerSecondary->setVisible(!secondary.isEmpty());
        m_bannerSecondaryAction = std::move(secondaryAction);
        switch (tone) {
        case Tone::Neutral:
            m_banner->setStyleSheet(QString());
            m_bannerText->setForegroundRole(QPalette::PlaceholderText);
            break;
        case Tone::Warning:
            m_banner->setStyleSheet(QStringLiteral(
                "QFrame#pm2Banner { background: #fff3cd; border: 1px solid #ffeeba; border-radius: 4px; }"
                "QFrame#pm2Banner > QLabel { color: #533f03; }"));
            break;
        case Tone::Error:
            m_banner->setStyleSheet(QStringLiteral(
                "QFrame#pm2Banner { background: #f8d7da; border: 1px solid #f5c6cb; border-radius: 4px; }"
                "QFrame#pm2Banner > QLabel { color: #58151c; }"));
            break;
        }
        m_banner->show();
    };

    auto retry = [this]() { m_client->start(); };
    switch (m_client->state()) {
    case Pm2Client::State::Idle:
        m_banner->hide();
        return;
    case Pm2Client::State::Connecting:
        show(Tone::Neutral, tr("Connecting to pm2…"));
        return;
    case Pm2Client::State::Installing:
        show(Tone::Neutral, tr("pm2 is not installed; getting it through npx (only the first time)…"));
        return;
    case Pm2Client::State::NodeMissing:
        show(Tone::Error, tr("Node.js was not found. Install it from nodejs.org, then retry."), tr("Retry"), retry);
        return;
    case Pm2Client::State::Failed:
        show(Tone::Error, m_client->errorText(), tr("Retry"), retry);
        return;
    case Pm2Client::State::Ready:
        break;
    }

    if (!m_listLoaded) {
        show(Tone::Neutral, tr("Loading processes…"));
        return;
    }
    if (!m_commandError.isEmpty()) {
        show(Tone::Error, tr("pm2: %1").arg(m_commandError), tr("Dismiss"), [this]() {
            m_commandError.clear();
            updateBanner();
        });
        return;
    }
    if (!m_notRunning.isEmpty()) {
        show(Tone::Warning,
             tr("Saved by pm2 but not running: %1. Changes are not saved until you resurrect them "
                "or discard the saved list.")
                 .arg(m_notRunning.join(QStringLiteral(", "))),
             tr("Resurrect"), [this]() { resurrect(); }, tr("Discard saved list…"),
             [this]() { discardSavedList(); });
        return;
    }
    m_banner->hide();
}

void Pm2ProcessesDialog::updateFooter()
{
    if (m_client->state() != Pm2Client::State::Ready) {
        m_footer->clear();
        return;
    }
    if (!m_listLoaded) {
        m_footer->setText(tr("pm2 %1 · loading…").arg(m_client->pm2Version()));
        return;
    }
    int online = 0;
    for (const Pm2ProcessInfo &p : m_model->processes())
        online += p.status == Pm2Status::Online ? 1 : 0;
    QString text = tr("pm2 %1 · %2 online · %3 total")
                       .arg(m_client->pm2Version())
                       .arg(online)
                       .arg(m_model->rowCount());
    // A command is in flight (pm2 can take seconds to stop or start one).
    if (!m_pending.isEmpty())
        text += tr(" · working…");
    m_footer->setText(text);
}

void Pm2ProcessesDialog::showContextMenu(const QPoint &pos)
{
    if (m_client->state() != Pm2Client::State::Ready)
        return;
    QMenu menu(this);
    QHash<QAction *, void (Pm2ProcessesDialog::*)()> handlers;
    auto add = [&](const QString &text, bool enabled, void (Pm2ProcessesDialog::*slot)()) {
        QAction *a = menu.addAction(text);
        a->setEnabled(enabled);
        handlers.insert(a, slot);
    };
    auto addButton = [&](QToolButton *button, void (Pm2ProcessesDialog::*slot)()) {
        add(button->text(), button->isEnabled(), slot);
    };
    addButton(m_logsButton, &Pm2ProcessesDialog::showLogs);
    add(tr("Open Directory in Terminal"), selectedRows().size() == 1, &Pm2ProcessesDialog::openDirectoryInTerminal);
    menu.addSeparator();
    addButton(m_startButton, &Pm2ProcessesDialog::startSelected);
    addButton(m_stopButton, &Pm2ProcessesDialog::stopSelected);
    addButton(m_restartButton, &Pm2ProcessesDialog::restartSelected);
    menu.addSeparator();
    addButton(m_editButton, &Pm2ProcessesDialog::editProcess);
    addButton(m_deleteButton, &Pm2ProcessesDialog::deleteSelected);
    menu.addSeparator();
    addButton(m_newButton, &Pm2ProcessesDialog::newProcess);
    // Run after the menu has closed: several open their own dialog.
    if (QAction *chosen = menu.exec(m_table->viewport()->mapToGlobal(pos)))
        (this->*handlers.value(chosen))();
}

// --- Commands -------------------------------------------------------------------

bool Pm2ProcessesDialog::isReady() const
{
    return m_client->state() == Pm2Client::State::Ready && m_listLoaded;
}

bool Pm2ProcessesDialog::hasFailed() const
{
    const Pm2Client::State state = m_client->state();
    return state == Pm2Client::State::NodeMissing || state == Pm2Client::State::Failed;
}

QString Pm2ProcessesDialog::statusText() const
{
    if (m_banner->isVisible())
        return m_bannerText->text();
    return m_footer->text();
}

const QVector<Pm2ProcessInfo> &Pm2ProcessesDialog::processes() const
{
    return m_model->processes();
}

std::optional<Pm2Definition> Pm2ProcessesDialog::definitionOf(int pmId) const
{
    const int row = m_model->rowForId(pmId);
    if (row < 0)
        return std::nullopt;
    const QString script = m_model->processAt(row).script;
    if (!Pm2Protocol::isOwnEntrypoint(script, Pm2Client::entrypointDir()))
        return std::nullopt;
    return readEntrypoint(script);
}

void Pm2ProcessesDialog::selectIds(const QList<int> &ids)
{
    QItemSelection selection;
    for (const int id : ids) {
        const int row = m_model->rowForId(id);
        if (row < 0)
            continue;
        const QModelIndex index = m_proxy->mapFromSource(m_model->index(row, 0));
        selection.select(index, index);
    }
    m_table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    if (!selection.isEmpty())
        m_table->scrollTo(selection.indexes().constFirst());
}

void Pm2ProcessesDialog::startIds(const QList<int> &ids, Done done)
{
    // pm2 restarts a stopped process by id; that is how `pm2 start <id>` works.
    track(m_client->restartProcesses(ids), std::move(done));
}

void Pm2ProcessesDialog::stopIds(const QList<int> &ids, Done done)
{
    track(m_client->stopProcesses(ids), std::move(done));
}

void Pm2ProcessesDialog::restartIds(const QList<int> &ids, Done done)
{
    track(m_client->restartProcesses(ids), std::move(done));
}

void Pm2ProcessesDialog::deleteIds(const QList<int> &ids, Done done)
{
    QList<int> stillThere;
    QStringList leaving;
    QStringList ownScripts;
    for (const int id : ids) {
        const int row = m_model->rowForId(id);
        if (row < 0)
            continue;
        const Pm2ProcessInfo &p = m_model->processAt(row);
        stillThere.append(id);
        leaving.append(p.name);
        if (Pm2Protocol::isOwnEntrypoint(p.script, Pm2Client::entrypointDir()))
            ownScripts.append(p.script);
    }
    track(m_client->deleteProcesses(stillThere),
          [ownScripts, done = std::move(done)](bool ok, const QString &error) {
              if (ok) {
                  for (const QString &script : ownScripts)
                      QFile::remove(script);
              }
              if (done)
                  done(ok, error);
          },
          true, leaving);
}

QString Pm2ProcessesDialog::create(const Pm2Definition &def, Done done)
{
    if (m_client->state() != Pm2Client::State::Ready)
        return tr("pm2 is not ready.");
    if (const QString problem = Pm2ProcessEditDialog::problemWith(def, takenNames()); !problem.isEmpty())
        return problem;
    QString error;
    if (!writeEntrypoint(def, &error))
        return error;
    const QString script = entrypointPath(def.name);
    track(m_client->startProcess(Pm2App{def.name, script, def.cwd, def.options}),
          [script, done = std::move(done)](bool ok, const QString &why) {
              if (!ok)
                  QFile::remove(script);
              if (done)
                  done(ok, why);
          });
    return QString();
}

QString Pm2ProcessesDialog::edit(int pmId, const Pm2Definition &def, Done done)
{
    if (m_client->state() != Pm2Client::State::Ready)
        return tr("pm2 is not ready.");
    const int row = m_model->rowForId(pmId);
    if (row < 0)
        return tr("Process %1 is no longer in pm2.").arg(pmId);
    const Pm2ProcessInfo now = m_model->processAt(row);
    const std::optional<Pm2Definition> original = readEntrypoint(now.script);
    if (!original)
        return tr("Could not read the entrypoint of '%1' (%2).").arg(now.name, QDir::toNativeSeparators(now.script));
    QStringList taken = takenNames();
    taken.removeAll(now.name); // keeping its own name is fine
    if (const QString problem = Pm2ProcessEditDialog::problemWith(def, taken); !problem.isEmpty())
        return problem;
    if (def == *original) {
        if (done)
            done(true, QString());
        return QString();
    }

    QString error;
    if (!Pm2ProcessEditDialog::needsRecreate(*original, def)) {
        // Same script path: pm2 reads the new file on its next start.
        if (!writeEntrypoint(def, &error))
            return error;
        if (now.status != Pm2Status::Stopped)
            track(m_client->restartProcesses({now.pmId}), std::move(done), false);
        else if (done)
            done(true, QString());
        return QString();
    }

    if (!writeEntrypoint(def, &error))
        return error;
    const QString oldScript = now.script;
    const QString newScript = entrypointPath(def.name);
    // A rename that only changes letter case is the same file on Windows and
    // macOS: removing the "old" one would delete the new entrypoint.
    const bool twoFiles = QDir::cleanPath(QDir::fromNativeSeparators(oldScript)).compare(newScript, kPathCase) != 0;
    track(m_client->recreateProcess(now.pmId, Pm2App{def.name, newScript, def.cwd, def.options}),
          [twoFiles, oldScript, newScript, done = std::move(done)](bool ok, const QString &why) {
              if (twoFiles)
                  QFile::remove(ok ? oldScript : newScript);
              if (done)
                  done(ok, why);
          },
          true, {now.name});
    return QString();
}

TerminalDock *Pm2ProcessesDialog::showLogsFor(int pmId)
{
    const int row = m_model->rowForId(pmId);
    if (row < 0 || !m_terminals || Pm2Client::sessionPm2Dir().isEmpty())
        return nullptr;
    const Pm2ProcessInfo &p = m_model->processAt(row);
    const QString cwd = QFileInfo(p.cwd).isDir() ? p.cwd : QDir::homePath();
    TerminalTask task;
    task.name = tr("pm2 logs: %1").arg(p.name);
    task.command = Pm2Protocol::logsCommand(terminalShellKind(m_terminals->resolveShellCommand()),
                                            Pm2Client::sessionNodePath(), Pm2Client::sessionPm2Dir(), p.pmId);
    return m_terminals->openTask(cwd, task);
}

void Pm2ProcessesDialog::resurrectSaved(Done done)
{
    // The saved list is what was just started: nothing to save.
    track(m_client->resurrect(), std::move(done), false);
}

void Pm2ProcessesDialog::discardSaved(Done done)
{
    track(m_client->save(), std::move(done), false);
}

// --- Buttons ----------------------------------------------------------------------

void Pm2ProcessesDialog::newProcess()
{
    const QString cwd = m_defaultCwd ? m_defaultCwd() : QDir::homePath();
    Pm2ProcessEditDialog form(std::nullopt, cwd, takenNames(), this);
    if (form.exec() != QDialog::Accepted || m_client->state() != Pm2Client::State::Ready)
        return;
    if (const QString error = create(form.definition()); !error.isEmpty())
        QMessageBox::warning(this, windowTitle(), error);
}

void Pm2ProcessesDialog::editProcess()
{
    const QList<int> rows = selectedRows();
    if (rows.size() != 1)
        return;
    const Pm2ProcessInfo p = m_model->processAt(rows.front());
    const std::optional<Pm2Definition> original = readEntrypoint(p.script);
    if (!original) {
        QMessageBox::warning(this, windowTitle(),
                             tr("Could not read the entrypoint of '%1' (%2).").arg(p.name, QDir::toNativeSeparators(p.script)));
        return;
    }
    Pm2ProcessEditDialog form(original, QString(), takenNames(), this);
    if (form.exec() != QDialog::Accepted || m_client->state() != Pm2Client::State::Ready)
        return;
    // The row may have changed while the form was open; edit() re-reads it.
    if (const QString error = edit(p.pmId, form.definition()); !error.isEmpty())
        QMessageBox::warning(this, windowTitle(), error);
}

void Pm2ProcessesDialog::startSelected()
{
    startIds(selectedIds(&isNotRunning));
}

void Pm2ProcessesDialog::stopSelected()
{
    stopIds(selectedIds(&isRunning));
}

void Pm2ProcessesDialog::restartSelected()
{
    restartIds(selectedIds());
}

void Pm2ProcessesDialog::deleteSelected()
{
    const QList<int> rows = selectedRows();
    if (rows.isEmpty())
        return;
    QStringList names;
    QList<int> ids;
    for (const int row : rows) {
        const Pm2ProcessInfo &p = m_model->processAt(row);
        names.append(p.name);
        ids.append(p.pmId);
    }
    const QString question = rows.size() == 1
        ? tr("Delete '%1'? pm2 stops it and forgets it.").arg(names.front())
        : tr("Delete %1? pm2 stops them and forgets them.").arg(names.join(QStringLiteral(", ")));
    if (QMessageBox::question(this, windowTitle(), question, QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel)
        != QMessageBox::Yes) {
        return;
    }
    // deleteIds re-reads the rows: the list may have changed while the question was open.
    deleteIds(ids);
}

void Pm2ProcessesDialog::showLogs()
{
    const QList<int> rows = selectedRows();
    if (rows.size() == 1)
        showLogsFor(m_model->processAt(rows.front()).pmId);
}

void Pm2ProcessesDialog::openDirectoryInTerminal()
{
    const QList<int> rows = selectedRows();
    if (rows.size() != 1 || !m_terminals)
        return;
    const QString cwd = m_model->processAt(rows.front()).cwd;
    if (QFileInfo(cwd).isDir())
        m_terminals->openTerminal(cwd);
}

void Pm2ProcessesDialog::restartAll()
{
    QList<int> ids;
    for (const Pm2ProcessInfo &p : m_model->processes())
        ids.append(p.pmId);
    restartIds(ids);
}

void Pm2ProcessesDialog::stopAll()
{
    QList<int> ids;
    for (const Pm2ProcessInfo &p : m_model->processes()) {
        if (isRunning(p))
            ids.append(p.pmId);
    }
    stopIds(ids);
}

void Pm2ProcessesDialog::resurrect()
{
    resurrectSaved();
}

void Pm2ProcessesDialog::discardSavedList()
{
    const QString question =
        tr("Forget %1? pm2's saved list becomes the processes it runs now.").arg(m_notRunning.join(QStringLiteral(", ")));
    if (QMessageBox::question(this, windowTitle(), question, QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel)
        != QMessageBox::Yes) {
        return;
    }
    discardSaved();
}
