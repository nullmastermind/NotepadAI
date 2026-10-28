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
#include <QPointer>
#include <QProcess>
#include <QRgb>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

#include "AcpOpenSessions.h"
#include "AcpSwitchAgent.h"

class AcpAgentManager;
class AcpAgentRegistry;
class AcpConnection;
class AcpSessionModel;
class AcpSessionView;
class AiDockSessionStrip;
class AiDockTitleBar;
class ApplicationSettings;
struct SendWithGoalResult;
class GoalAgent;
class QCloseEvent;
class QHBoxLayout;
class QStackedWidget;
class QTabBar;
class QWidget;

// Dock widget hosting one project-group of ACP sessions. Qt's dock tab bar is
// one tab per QDockWidget, so N sessions of the same cwd/host share this dock
// as slots on a QStackedWidget. sessionId()/model()/connection()/isBusy()
// refer to the current slot.
//
// Holds non-owning pointers to each session's model and connection (owned by
// AcpAgentManager). The dock auto-deletes on close (Qt::WA_DeleteOnClose);
// the manager observes destruction via the dock's destroyed() signal.
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

    QString sessionId() const;
    AcpSessionModel *model() const;
    AcpConnection *connection() const;
    QString workingDirectory() const { return m_workingDirectory; }
    const QStringList &goalDebugLog() const { return m_goalDebugLog; }
    void insertTextToInput(const QString &text);
    void setActivityIndicator(bool active);
    bool isBusy() const;
    // A turn or goal is running. Scheduled tasks and the idle reaper go by this.
    bool isSessionBusy(const QString &sessionId) const;
    // Busy, or background work the agent started (tasks, Monitor watches) is
    // still alive: what the session number's working fill shows.
    bool isSessionWorking(const QString &sessionId) const;
    // A question form or permission request waits on the user, whether or not
    // the user can see it right now (the number's dot hides while they can).
    bool isSessionWaiting(const QString &sessionId) const;

    // What the dock's own tab says about its sessions, strongest first. Only a
    // dock that is not on screen needs to: a raised one shows its numbers.
    enum class ProjectMark : std::uint8_t { None, Activity, Working, Waiting };
    ProjectMark projectMark() const;

    int slotCount() const { return m_slots.size(); }
    int currentIndex() const { return m_current; }
    // The session added last, wherever the user has dragged it since.
    QString newestSessionId() const;
    // Started by a scheduled task: forms are cancelled until the user sends a
    // message in this session.
    void markSessionUnattended(const QString &sessionId);
    QString sessionIdAt(int index) const;
    // Drag-to-reorder from the session strip: session numbers are positions.
    // The open session stays open; false for a no-op or out-of-range move.
    bool moveSlot(int from, int to);

    void addSlot(QString sessionId,
                 QString agentName,
                 AcpSessionModel *model,
                 AcpConnection *connection,
                 bool makeCurrent = true);
    void switchTo(int index);
    void closeSlot(int index);
    // Manager-only: drop a slot without confirm / without calling closeSession.
    void detachSlot(const QString &sessionId);
    void stopGoalForSession(const QString &sessionId);

    // Open the Send with Goal dialog and start a goal on the sender's session
    // (or the current slot). Empty composer attaches to the existing
    // conversation (Send-then-Goal); otherwise sends the composer text as the
    // first prompt. No-op if a goal is already active on that slot.
    void sendWithGoal();
    // What Start in that dialog does, for a session: sends the composer text
    // with the goal, or attaches the goal to the conversation. Returns why it
    // could not start, empty when it did.
    QString startGoal(const QString &sessionId, const SendWithGoalResult &result);
    // The chat view of a session in this dock.
    AcpSessionView *viewFor(const QString &sessionId) const;
    QString agentNameFor(const QString &sessionId) const;
    // The goal running on a session, if any.
    GoalAgent *goalFor(const QString &sessionId) const;
    // The agent connection of a session in this dock (background tasks, jobs).
    AcpConnection *connectionFor(const QString &sessionId) const;
    // Whether a session's agent process has exited (Retry restarts it).
    bool hasAgentExited(const QString &sessionId) const;
    AcpSessionModel *modelFor(const QString &sessionId) const;

    // Attach an externally-created GoalAgent and wire its signals for UI
    // feedback (system messages, status bar, debug log). Takes ownership.
    // Empty sessionId targets the current slot. Returns false if that slot
    // already has an active goal.
    bool attachGoalAgent(GoalAgent *goal, const QString &sessionId = QString());

    // Replace one slot's inner session model + connection without destroying
    // the dock. objectName is unchanged. Used by AcpAgentManager::restartSession.
    void rebind(const QString &oldSessionId,
                AcpConnection *connection,
                AcpSessionModel *model,
                QString newSessionId,
                QString newAgentName);

    // Switch agent (AcpAgentManager::switchAgent): the session keeps its slot,
    // model and transcript and continues on `connection`.
    void rebindConnection(const QString &sessionId, AcpConnection *connection, QString agentName);
    // Lock the session while it moves to `targetName` (empty = done).
    void setSessionSwitching(const QString &sessionId, const QString &targetName);
    // What blocks a switch right now (Remote / SourceNotClaudeCode included).
    AcpSwitchAgent::SessionState switchState(const QString &sessionId) const;

    // Restored sessions: this dock's sessions in order, and its SSH profile.
    QVector<AcpOpenSessions::Session> openSessions() const;
    void setSshProfileId(const QString &id) { m_sshProfileId = id; }
    QString sshProfileId() const { return m_sshProfileId; }
    // NotepadAI is closing: sessions whose goal is running say it stopped.
    void noteAppClosing();

signals:
    // Emitted when the user clicks Restart on the "Agent exited" banner.
    // Carries the CURRENT (about-to-be-replaced) session id so the manager
    // can clean up history etc. Connected by AcpAgentManager.
    void restartRequested(const QString &oldSessionId);
    void inputFocused();

    // Sessions were added, closed, reordered, opened or moved to another
    // agent: the manager re-persists the open-session list.
    void slotsChanged();

    // Emitted whenever a new entry is appended to the goal debug log. Lets
    // the per-session debug dialog stream goal events live without polling.
    void goalDebugLogAppended(const QString &entry);

public slots:
    // Close every slot and destroy the dock. Used by dock-tab Close All /
    // Left / Right (those actions mean "remove these tabs").
    void closeGroup();
    // Reattach the session strip after Qt rebuilds or hides the area tab bar.
    void refreshSessionStripPlacement();

protected:
    QSize sizeHint() const override;
    void closeEvent(QCloseEvent *event) override;
    // The agent button's icon is tinted from the palette: re-push on a change.
    void changeEvent(QEvent *event) override;

    // Test seam — override in tests to bypass the modal QMessageBox.
    // Returns true if the user confirmed closing while a prompt is running.
    virtual bool confirmCloseWhileRunning();

private slots:
    void onMetadataChanged();
    void onAgentExited(int exitCode, QProcess::ExitStatus status);
    void onRetryFromView();
    void onRestartFromView();
    void onProcessingChanged(bool processing);
    // Background tasks / Monitor watches started or ended, or a session started
    // or stopped waiting on the user: repaint the marks.
    void onWorkChanged();
    void onMessageAppended(int idx);
    void generatePromptWithGoal();
    // Stop the attached GoalAgent if (and only if) one is currently running
    // on the sender view's slot. Shared sink for the goal-status-row Stop
    // button and the composer Cancel button. No-op when no goal is active.
    void stopGoalAgentIfActive();
    void onNativeGoalPromptEnded();
    void onNativeGoalBackgroundIdle();
    void onDeferredGoalDispatched();
    void onDeferredGoalRemoved();
    void onPromptQueueChanged();
    void onAgentMenuRequested(const QPoint &globalPos);

private:
    struct Slot {
        QString sessionId;
        QString agentName;
        AcpSessionModel *model = nullptr;
        AcpConnection *connection = nullptr;
        AcpSessionView *view = nullptr;
        GoalAgent *goal = nullptr;
        bool hasActivity = false;
        qint64 busySinceMs = 0;
        bool agentExited = false;
        bool nativeAutoCompact = false;
        QString switchingTo; // Switch agent in progress: the target's name
    };

    void buildUi();
    void refreshTitle();
    void refreshProjectTooltip();
    void wireSlotSignals(Slot &slot);
    void maybeSendNativeAutoCompact(Slot *slot);
    void unwireSlot(Slot &slot);
    void destroySlotGoal(Slot &slot);
    Slot *currentSlot();
    const Slot *currentSlot() const;
    Slot *slotById(const QString &sessionId);
    const Slot *slotById(const QString &sessionId) const;
    Slot *slotForView(const AcpSessionView *view);
    Slot *slotForModel(const QObject *model);
    Slot *slotForConnection(const QObject *connection);
    int indexOfSession(const QString &sessionId) const;
    QString resolvedAgentName(const Slot &slot) const;
    // The slot's agent as configured (registry entry when it still exists).
    AcpAgentDefinition slotAgent(const Slot &slot) const;
    AcpSwitchAgent::SessionState switchState(const Slot &slot) const;
    bool slotBusy(const Slot &slot) const;
    bool slotWorking(const Slot &slot) const;
    bool slotWaiting(const Slot &slot) const;
    void syncSlotBusy(Slot &slot);
    bool tryCloseSlot(int index);
    void detachSlotAt(int index);
    void ensureStrip();
    void salvageStrip();
    void scheduleStripReinstall();
    void reinstallStrip();
    void pushStripSnapshots();
    // The open session's agent on the title bar's agent button.
    void refreshAgentButton();
    void showLocalTabBar();
    void hideLocalTabBar();
    void notifySiblingsRefreshStrip();
    void refreshActivityIcon();
    QTabBar *hostTabBar(int *tabIndex) const;

    QString m_workingDirectory;
    QString m_sshProfileId;
    AcpAgentRegistry *m_registry;     // non-owning
    AcpAgentManager *m_agentManager;  // non-owning
    ApplicationSettings *m_appSettings; // non-owning

    QVector<Slot> m_slots;
    int m_current = 0;
    QString m_newestSessionId; // last addSlot; survives reordering
    ProjectMark m_iconMark = ProjectMark::None; // what the window icon shows now
    QRgb m_iconHighlight = 0;                   // ... and in which Highlight colour
    // The dock is on screen, from QDockWidget::visibilityChanged. Never
    // isVisible(): a tabbed dock whose tab is not current stays visible, Qt
    // only parks it off-screen.
    bool m_onScreen = false;

    QWidget *m_header = nullptr;
    QHBoxLayout *m_headerLayout = nullptr;
    QStackedWidget *m_stack = nullptr;
    QTabBar *m_localTabBar = nullptr;
    QPointer<AiDockSessionStrip> m_strip;
    AiDockTitleBar *m_titleBar = nullptr; // owned via setTitleBarWidget
    QPointer<QTabBar> m_tabBarForStrip;
    bool m_stripReinstallQueued = false;

    bool m_restartDialogShowing = false;
    bool m_acceptingWidgetClose = false;
    QStringList m_goalDebugLog;
};

#endif // AI_AGENT_DOCK_H
