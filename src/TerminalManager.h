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

#ifndef TERMINALMANAGER_H
#define TERMINALMANAGER_H

#include <QList>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include "TerminalTaskRegistry.h"
#include "TerminalProcessStats.h"


class NotepadNextApplication;
class MainWindow;
class TerminalDock;
class TerminalAreaToolbar;
class TerminalEmptyPanel;
class AiAgentDock;
// Not included: TerminalWidget.h pulls in vterm.h, whose `small` bit-field
// collides with the Windows `small` macro in files that include windows.h.
struct TerminalAppearance;
struct TerminalColorScheme;
class ScintillaNext;
namespace ads { class CDockAreaWidget; class CDockWidget; }
namespace remote { class ExecutionContext; }

class TerminalManager : public QObject
{
    Q_OBJECT

public:
    TerminalManager(NotepadNextApplication *app, MainWindow *mainWindow);
    ~TerminalManager() override;

    QString resolveShellCommand() const;
    // The configured shell, resolved to an executable; empty (and `error`, or
    // a message box without one) when it or the PTY backend is missing.
    QString localShell(QString *error = nullptr) const;
    // Every open terminal, oldest first.
    QList<TerminalDock *> terminalDocks() const;
    // Closes a terminal without asking: false (and `error`) when it is busy
    // (TerminalDock::isBusy) and `force` is off; else stops it and closes.
    bool closeTerminal(TerminalDock *dock, bool force, QString *error);

    // Per-workspace task registry. workspacePath must be QDir::cleanPath'd.
    QList<TerminalTask> tasksForWorkspace(const QString &workspacePath) const;
    void addTask(const QString &workspacePath, const TerminalTask &task);
    void setTasks(const QString &workspacePath, const QList<TerminalTask> &tasks);

public slots:
    // Both return the new terminal, or nullptr when it could not be opened.
    // error: set instead of a message box when the terminal cannot start.
    TerminalDock *openTerminal(const QString &cwd, QString *error = nullptr);
    // Remote terminal: PTY over an SSH channel on `ctx` (captured at spawn into
    // the dock). `remoteCwd` is a POSIX path on the host; `shell` empty → the
    // remote default ($SHELL). No local PTY probe (the backend is remote).
    TerminalDock *openRemoteTerminal(remote::ExecutionContext *ctx, const QString &remoteCwd,
                                     const QString &shell = QString());
    // New terminal (over SSH when `ctx` is remote, else local) with `text`
    // typed at the prompt once the shell first prints, not entered: tasks
    // whose arguments the user still has to supply.
    TerminalDock *openTerminalWithInput(remote::ExecutionContext *ctx, const QString &cwd,
                                        const QString &text);
    TerminalDock *openTask(const QString &workspaceCwd, const TerminalTask &task, QString *error = nullptr);
    // Remote task execution: run `task` on the remote host via an SSH PTY channel.
    // `ctx` must be Connected. `remoteCwd` is the POSIX workspace root on the host.
    void openRemoteTask(remote::ExecutionContext *ctx, const QString &remoteCwd,
                        const TerminalTask &task);
    void runOrRestartTask(const QString &cwd, const TerminalTask &task,
                          remote::ExecutionContext *context = nullptr);
    void applyTheme();
    void applyFont();
    // Re-reads every Terminal/* appearance setting into all terminals.
    void applyAppearance();
    // Ctrl+wheel / Ctrl+=/-/0 in any terminal: persisted font size step.
    void zoomTerminals(int steps);
    // New terminal in `like`'s current directory and execution context
    // (workspace root when null).
    void openTerminalLike(TerminalDock *like);
    void renameTerminal(TerminalDock *dock);
    // Ctrl+click on a path in a terminal: open it at line/column.
    void openFileAt(const QString &path, int line, int column);
    // Ctrl+click on a directory: the workspace tree, else the file manager.
    void revealPath(const QString &path);
    // Selection → the terminal's project AI tab (or the active one, or a new
    // one in the terminal's directory).
    void sendSelectionToAi(TerminalDock *dock);
    void shutdown();

    TerminalDock *findTaskDock(const QString &command, const QString &cwd) const;

    void applyWorkspaceFilter(const QString &activeWorkspaceRoot, const QStringList &openWorkspaceRoots);
    void closeTerminalsForWorkspace(const QString &workspaceRoot, const QStringList &openWorkspaceRoots);

    // Re-reads the empty panel's configured tasks for the
    // active workspace. Called after Edit Tasks saves.
    void refreshEmptyPanel();

signals:
    // The empty panel's "Configure tasks…" — MainWindow owns the dialog.
    void editTasksRequested();

protected:
    // Editor tabs dropped on a terminal type their file path into it.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static constexpr int kMaxScrollbackLines = 100000;
    TerminalDock *terminalAt(const QPoint &globalPos) const;

    QFont terminalFont() const;
    TerminalColorScheme currentScheme() const;
    void updateLinkBaseDirs(TerminalDock *dock);
    void watchEditorTab(ScintillaNext *editor);
    TerminalAppearance appearance() const;
    void applyAppearanceTo(TerminalDock *dock, const TerminalAppearance &appearance);
    // Appearance + signal wiring for a dock's current TerminalWidget.
    void configureDock(TerminalDock *dock);
    void wireWidget(TerminalDock *dock);
    void wireContextMenu(TerminalDock *dock);
    AiAgentDock *aiDockFor(TerminalDock *dock) const;
    void installToolbar(ads::CDockAreaWidget *area);
    QIcon tabIcon(const TerminalDock *dock) const;
    void updateTab(TerminalDock *dock);
    // Runs the stats poll only while a local terminal is on screen.
    void updateStatsTimer();
    void placeInEditor(TerminalDock *dock);
    void rememberDock(TerminalDock *dock);
    bool forgetDock(QObject *obj);
    // Empty panel (bottom tool area placeholder when the active workspace has
    // no visible terminal). Created lazily by ensureEmptyPanel(); suppression
    // is keyed by TerminalEmptyPanelPolicy::workspaceKey and lives only while
    // the workspace stays open.
    void ensureEmptyPanel();
    void updateEmptyPanel();
    void hideEmptyPanel();
    void applyEmptyPanelColors();
    void refreshProcessStats();
    bool syncStatsPoll();


    NotepadNextApplication *m_app;
    MainWindow *m_mainWindow;
    QList<QPointer<TerminalDock>> m_docks;
    QList<QPointer<TerminalAreaToolbar>> m_toolbars;
    QString m_filterActiveRoot;
    QStringList m_filterOpenRoots;
    bool m_shuttingDown = false;
    QPointer<TerminalEmptyPanel> m_emptyPanel;
    QPointer<ads::CDockWidget> m_emptyDock;
    QSet<QString> m_emptySuppressed;
    QString m_emptyPanelKey;
    bool m_emptyPanelDirty = true;
    QTimer *m_statsTimer = nullptr;
    QVector<TerminalProcessStats::ProcessSample> m_processSnapshot;
    QVector<TerminalProcessStats::ProcessorTick> m_processorNow;
    QVector<TerminalProcessStats::ProcessorTick> m_processorPrev;
    QVector<double> m_processorPct;
    QHash<quint32, TerminalProcessStats::TreeCpuState> m_treeCpu;


};

#endif
