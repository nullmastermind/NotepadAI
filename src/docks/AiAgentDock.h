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
#include <QString>
#include <QStringList>
#include <QVector>

class AcpAgentManager;
class AcpAgentRegistry;
class AcpConnection;
class AcpSessionModel;
class AcpSessionView;
class AiDockSessionStrip;
class ApplicationSettings;
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
    bool isSessionBusy(const QString &sessionId) const;

    int slotCount() const { return m_slots.size(); }
    int currentIndex() const { return m_current; }
    QString newestSessionId() const;
    QString sessionIdAt(int index) const;

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

signals:
    // Emitted when the user clicks Restart on the "Agent exited" banner.
    // Carries the CURRENT (about-to-be-replaced) session id so the manager
    // can clean up history etc. Connected by AcpAgentManager.
    void restartRequested(const QString &oldSessionId);
    void inputFocused();

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

    // Test seam — override in tests to bypass the modal QMessageBox.
    // Returns true if the user confirmed closing while a prompt is running.
    virtual bool confirmCloseWhileRunning();

private slots:
    void onMetadataChanged();
    void onAgentExited(int exitCode, QProcess::ExitStatus status);
    void onRetryFromView();
    void onRestartFromView();
    void onProcessingChanged(bool processing);
    void onMessageAppended(int idx);
    void generatePromptWithGoal();
    // Stop the attached GoalAgent if (and only if) one is currently running
    // on the sender view's slot. Shared sink for the goal-status-row Stop
    // button and the composer Cancel button. No-op when no goal is active.
    void stopGoalAgentIfActive();
    void onNativeGoalPromptEnded();

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
    };

    void buildUi();
    void refreshTitle();
    void refreshProjectTooltip();
    void wireSlotSignals(Slot &slot);
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
    bool slotBusy(const Slot &slot) const;
    void syncSlotBusy(Slot &slot);
    bool tryCloseSlot(int index);
    void detachSlotAt(int index);
    void ensureStrip();
    void salvageStrip();
    void scheduleStripReinstall();
    void reinstallStrip();
    void pushStripSnapshots();
    void showLocalTabBar();
    void hideLocalTabBar();
    void notifySiblingsRefreshStrip();
    void refreshActivityIcon();
    bool shouldShowActivityIcon() const;
    QTabBar *hostTabBar(int *tabIndex) const;

    QString m_workingDirectory;
    AcpAgentRegistry *m_registry;     // non-owning
    AcpAgentManager *m_agentManager;  // non-owning
    ApplicationSettings *m_appSettings; // non-owning

    QVector<Slot> m_slots;
    int m_current = 0;

    QWidget *m_header = nullptr;
    QHBoxLayout *m_headerLayout = nullptr;
    QStackedWidget *m_stack = nullptr;
    QTabBar *m_localTabBar = nullptr;
    QPointer<AiDockSessionStrip> m_strip;
    QPointer<QTabBar> m_tabBarForStrip;
    int m_stripTabIndex = -1;
    bool m_stripReinstallQueued = false;

    bool m_restartDialogShowing = false;
    bool m_acceptingWidgetClose = false;
    QStringList m_goalDebugLog;
};

#endif // AI_AGENT_DOCK_H
