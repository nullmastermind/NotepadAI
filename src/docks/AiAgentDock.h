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

#ifndef AI_AGENT_DOCK_H
#define AI_AGENT_DOCK_H

#include <QDockWidget>
#include <QProcess>
#include <QString>
#include <QStringList>

class AcpAgentManager;
class AcpAgentRegistry;
class AcpConnection;
class AcpSessionModel;
class ApplicationSettings;
class GoalAgent;
class QCloseEvent;

// Dock widget hosting one ACP session. Group 4 ships with a placeholder
// content widget; the real chat view (AcpSessionView) lands in Group 5.
//
// Holds non-owning pointers to the session model and connection. Both are
// owned by AcpAgentManager. The dock auto-deletes on close
// (Qt::WA_DeleteOnClose); the manager observes destruction via the
// dock's destroyed() signal.
class AiAgentDock : public QDockWidget
{
    Q_OBJECT

public:
    // Default dock area for this dock when first attached to a QMainWindow.
    // Centralized here so MainWindow doesn't hard-code Qt::RightDockWidgetArea
    // at every call site.
    static constexpr Qt::DockWidgetArea defaultArea() { return Qt::RightDockWidgetArea; }

    AiAgentDock(QString sessionId,
                QString agentName,
                QString workingDirectory,
                AcpSessionModel *model,
                AcpConnection *connection,
                AcpAgentRegistry *registry,
                AcpAgentManager *agentManager,
                ApplicationSettings *appSettings,
                QWidget *parent = nullptr);
    ~AiAgentDock() override;

    QString sessionId() const { return m_sessionId; }
    AcpSessionModel *model() const { return m_model; }
    AcpConnection *connection() const { return m_connection; }
    QString workingDirectory() const { return m_workingDirectory; }
    const QStringList &goalDebugLog() const { return m_goalDebugLog; }
    void insertTextToInput(const QString &text);
    void setActivityIndicator(bool active);
    bool isBusy() const;

    // Open the Send with Goal dialog and start a goal on this session.
    // No-op if a goal is already active.
    void sendWithGoal();

    // Attach an externally-created GoalAgent and wire its signals for UI
    // feedback (system messages, status bar, debug log). Takes ownership.
    // Returns false if a goal is already active.
    bool attachGoalAgent(GoalAgent *goal);

    // Replace the dock's inner session model + connection without destroying
    // the dock itself. The dock widget, its dock area, and its on-screen
    // position survive. Used by AcpAgentManager::restartSession (W4) to wire
    // a freshly-allocated sessionId/connection into the same on-screen dock.
    void rebind(AcpConnection *connection,
                AcpSessionModel *model,
                QString newSessionId,
                QString newAgentName);

signals:
    // Emitted when the user clicks Restart on the "Agent exited" banner.
    // Carries the CURRENT (about-to-be-replaced) session id so the manager
    // can clean up history etc. Connected by AcpAgentManager.
    void restartRequested(const QString &oldSessionId);
    void inputFocused();

    // Emitted whenever a new entry is appended to the goal debug log. Lets
    // the per-session debug dialog stream goal events live without polling.
    void goalDebugLogAppended(const QString &entry);

protected:
    QSize sizeHint() const override;
    void closeEvent(QCloseEvent *event) override;

    // Test seam — override in tests to bypass the modal QMessageBox.
    // Returns true if the user confirmed closing while a prompt is running.
    virtual bool confirmCloseWhileRunning();

private slots:
    void onMetadataChanged();
    void onAgentExited(int exitCode, QProcess::ExitStatus status);
    void onRetryFromView();
    void onRestartFromView();
    // Stop the attached GoalAgent if (and only if) one is currently running.
    // Shared sink for the goal-status-row Stop button and the composer Cancel
    // button so both terminate the supervisor loop. No-op when no goal is
    // active. Must be a member function — see wireConnectionSignals() for why
    // a lambda cannot be used here.
    void stopGoalAgentIfActive();

private:
    void refreshTitle();
    void wireConnectionSignals();

    QString m_sessionId;
    QString m_agentName;
    QString m_workingDirectory;
    AcpSessionModel *m_model;       // non-owning
    AcpConnection *m_connection;    // non-owning
    AcpAgentRegistry *m_registry;   // non-owning
    AcpAgentManager *m_agentManager; // non-owning
    ApplicationSettings *m_appSettings; // non-owning
    GoalAgent *m_goalAgent = nullptr; // owned
    class AcpSessionView *m_view = nullptr; // owned via setWidget
    bool m_agentExited = false;
    bool m_restartDialogShowing = false;
    bool m_hasActivity = false;
    QStringList m_goalDebugLog;
};

#endif // AI_AGENT_DOCK_H
