/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef PM2_PROCESSES_DIALOG_H
#define PM2_PROCESSES_DIALOG_H

#include <QDialog>
#include <QHash>
#include <QStringList>

#include <functional>
#include <optional>

#include "Pm2Client.h"
#include "Pm2Entrypoint.h"

class Pm2ProcessModel;
class TerminalDock;
class QFrame;
class QLabel;
class QMenu;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;
class QTimer;
class QToolButton;
class TerminalManager;

// Terminal > PM2 Processes: every process of the user's pm2 in a live table
// (status, uptime, restarts, CPU, memory), with start / stop / restart /
// delete / logs, and New / Edit for processes NotepadAI runs through an
// entrypoint. Modeless, one instance; the Node helper lives exactly as long
// as the dialog (ADR 0005).
class Pm2ProcessesDialog : public QDialog
{
    Q_OBJECT

public:
    // `defaultCwd` gives the directory a new process starts in (the active
    // local workspace, else home).
    Pm2ProcessesDialog(TerminalManager *terminals, std::function<QString()> defaultCwd,
                       QWidget *parent = nullptr);
    ~Pm2ProcessesDialog() override;

    using Done = std::function<void(bool ok, const QString &error)>;

    // --- What the buttons do, without asking (the Control API runs these) ---
    // The helper is ready and the process list has arrived.
    bool isReady() const;
    // The helper failed for good (Node missing, pm2 unusable) until retried.
    bool hasFailed() const;
    // What the banner says while not ready (connecting, installing, the error).
    QString statusText() const;
    const QVector<Pm2ProcessInfo> &processes() const;
    // Processes pm2 saved that it does not run (Resurrect starts them).
    QStringList savedNotRunning() const { return m_notRunning; }
    // The definition of a process NotepadAI created; nullopt for others.
    std::optional<Pm2Definition> definitionOf(int pmId) const;
    // Selects these processes' rows, so the table shows what a command acts on.
    void selectIds(const QList<int> &ids);
    void startIds(const QList<int> &ids, Done done = {});
    void stopIds(const QList<int> &ids, Done done = {});
    void restartIds(const QList<int> &ids, Done done = {});
    void deleteIds(const QList<int> &ids, Done done = {});
    // Checked like the New / Edit form; returns the problem, or empty once sent.
    QString create(const Pm2Definition &def, Done done = {});
    QString edit(int pmId, const Pm2Definition &def, Done done = {});
    // `pm2 logs` in a terminal task; null when pm2's directory is unknown.
    TerminalDock *showLogsFor(int pmId);
    void resurrectSaved(Done done = {});
    void discardSaved(Done done = {});

signals:
    // Readiness, the list or a banner state changed.
    void statusChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onStateChanged(Pm2Client::State state);
    void onList(const QList<Pm2ProcessInfo> &processes, const QStringList &dumpNames, bool hasDump);
    void onCommandFinished(int requestId, bool ok, const QString &error);
    // Tracks `requestId` (0 = not sent); a successful mutation is followed by
    // `pm2 save` unless the saved list still has processes pm2 lacks.
    // `removes` are the names the command takes out of pm2: until the save
    // after it lands, the dump still lists them, and they must not read as
    // "saved but not running" (that would pause the save and offer to
    // resurrect what the user just deleted).
    void track(int requestId, Done done = {}, bool saveAfter = true, const QStringList &removes = {});
    void refreshNotRunning();

    QList<int> selectedRows() const;
    QList<int> selectedIds(bool (*filter)(const Pm2ProcessInfo &) = nullptr) const;
    QStringList liveNames() const;
    QStringList takenNames() const;
    void updateActions();
    void updateBanner();
    void updateFooter();
    void showContextMenu(const QPoint &pos);

    void newProcess();
    void editProcess();
    void startSelected();
    void stopSelected();
    void restartSelected();
    void deleteSelected();
    void showLogs();
    void openDirectoryInTerminal();
    void restartAll();
    void stopAll();
    void resurrect();
    void discardSavedList();

    TerminalManager *m_terminals;
    std::function<QString()> m_defaultCwd;
    Pm2Client *m_client;
    Pm2ProcessModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    QTimer *m_clock;

    QFrame *m_banner;
    QLabel *m_bannerText;
    QPushButton *m_bannerPrimary;
    QPushButton *m_bannerSecondary;
    std::function<void()> m_bannerPrimaryAction;
    std::function<void()> m_bannerSecondaryAction;
    QLabel *m_footer;

    QToolButton *m_newButton;
    QToolButton *m_editButton;
    QToolButton *m_startButton;
    QToolButton *m_stopButton;
    QToolButton *m_restartButton;
    QToolButton *m_deleteButton;
    QToolButton *m_logsButton;
    QToolButton *m_restartAllButton;
    QToolButton *m_stopAllButton;
    QToolButton *m_resurrectButton;

    struct Pending
    {
        Done done;
        bool saveAfter = true;
        QStringList removes;
    };
    QHash<int, Pending> m_pending; // by request id
    QStringList m_dumpNames;
    QStringList m_notRunning; // saved in the dump, not in pm2 now
    bool m_hasDump = false;
    bool m_firstList = true;
    bool m_listLoaded = false; // a list arrived since the helper became ready
    QLabel *m_emptyLabel = nullptr;
    QString m_commandError;
};

#endif // PM2_PROCESSES_DIALOG_H
