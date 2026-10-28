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

#ifndef ACP_SESSION_VIEW_H
#define ACP_SESSION_VIEW_H

#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QDateTime>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <cstdint>
#include <functional>

#include "AcpDiffStat.h"
#include "AcpErrorClassifier.h"
#include "AcpPrewalk.h"
#include "AcpPromptQueue.h"
#include "AcpProtocol.h"
#include "AcpTranscriptTruncation.h"
#include "AcpTurnFolding.h"
#include "GrillMeFormWidget.h"

class AcpAgentRegistry;
class AcpConnection;
class AcpImageAttachmentList;
class AcpMessageWidget;
class AcpPlanWidget;
class AcpPermissionPrompt;
class AcpPromptQueueStrip;
class AcpSessionModel;
class AcpToolCallCard;
class AcpTurnFoldRow;
class AcpUsageIndicator;

namespace ai { class PromptImprover; }

class QAction;
class QCheckBox;
class QComboBox;
class QDialog;
class QFrame;
class QLabel;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QShortcut;
class QTimer;
class QToolButton;
class QVBoxLayout;

// The chat-view widget hosting an ACP session. Non-owning pointers; the
// manager owns model/connection/registry.
class AcpSessionView : public QWidget
{
    Q_OBJECT

public:
    enum class BannerKind : std::uint8_t { Info, Warning, Error };

    AcpSessionView(AcpSessionModel *model,
                   AcpConnection *connection,
                   AcpAgentRegistry *registry,
                   QWidget *parent = nullptr);
    ~AcpSessionView() override;

    void setBanner(const QString &text, BannerKind kind);
    void clearBanner();
    void insertTextToInput(const QString &text);
    QString peekInputText() const;
    QString takeInputText();
    QVector<QPair<QByteArray, QString>> peekInputImages() const;
    QVector<QPair<QByteArray, QString>> takeInputImages();
    QStringList goalDebugLog() const;
    AcpConnection *connection() const { return m_connection; }

    // If "New worktree" is checked, return displayText plus a hidden instruction
    // for the agent. The transcript should keep displayText. Stays checked for
    // the life of this view (sends + rebind/restart); a new chat constructs a
    // new view, which starts unchecked. Never persisted.
    QString applyNewWorktreeInstruction(const QString &displayText) const;

    // Goal status feedback — called by the owning dock when goal state changes.
    void setGoalActive(int criterionIndex, int totalCriteria, int iteration, int maxIterations);
    void setGoalTerminal(const QString &statusText);
    void clearGoalStatus();
    // Same queue as a composer send while a turn is running. Does not
    // session/prompt now — that write stops agents that do not support /goal.
    void enqueueFollowUp(const QString &text, bool fromGoalAgent = false);
    int queuedPromptCount() const;
    // A prompt sent but not yet out to the agent: parked until a restored
    // session's agent connects, or held until its session is created.
    bool hasUnsentPrompt() const;

    // Detach from the current model + connection and re-attach to a new pair
    // (typically after AcpAgentManager::restartSession). Clears the
    // transcript and the exit banner; re-runs hydrateFromModel() against the
    // new model. The view widget itself survives.
    void rebind(AcpSessionModel *model, AcpConnection *connection);

    // Switch agent: the same session continues on `connection`. Transcript,
    // queue and draft state stay; the session's model / mode / effort are
    // re-applied once the new agent's catalogs arrive.
    void rebindConnection(AcpConnection *connection);
    // Composer locked while the session moves to `targetName` (empty = done).
    void setSwitching(const QString &targetName);

    // Started by a scheduled task: nobody answers forms until the user sends a
    // message here. A running goal makes the session unattended too.
    void setScheduledUnattended(bool on) { m_scheduledUnattended = on; }
    // A cancelled form's "ask in plain text" notice is still queued: the goal
    // judge must wait for the turn it starts.
    bool holdsGoalEvaluation() const;
    // The agent is blocked on the user: a question form is open or a
    // permission request is unanswered. A form cancelled because nobody is
    // there to answer (unattended session) does not count.
    bool isWaitingForUser() const;
    // The session shown (the Control API reads the transcript from it).
    const AcpSessionModel *sessionModel() const { return m_model; }

    // A draft session is one where the user has sent nothing yet. Only its
    // model / mode / effort choices become the agent's defaults for new
    // sessions; after the first user send they apply to this session only.
    // Goal, scheduled-task and host prompts do not end the draft. Called for
    // every composer send, and by the dock when Send with Goal sends one.
    void noteUserSend();
    bool isDraft() const { return m_draft; }

    // --- Control API (docs/adr/0007): what the composer's controls do -------
    // Types `text` into the composer and presses Send (queued behind a running
    // turn). Empty when it went out or was queued, else why it could not.
    QString sendFromComposer(const QString &text);
    // The Cancel button: stops the running turn (and a goal on it).
    void cancelFromComposer();
    enum class Selector : std::uint8_t { Model, Mode, Effort };
    struct Choice
    {
        QString id;
        QString name;
        bool current = false;
    };
    // The items of the model / mode / effort picker (empty until the agent
    // reports them).
    QVector<Choice> choices(Selector selector) const;
    // Picks an item as the user would; false when the picker does not offer it.
    bool choose(Selector selector, const QString &id);
    bool autoApprove() const;
    void setAutoApprove(bool on);
    // The permission request waiting for an answer, if any.
    const AcpProtocol::AcpPermissionRequest *pendingPermission() const;
    // Clicks the option with this id or kind; false when none is pending or matches.
    bool answerPermission(const QString &optionIdOrKind);
    // The question form waiting for answers, if any.
    const GrillMeForm::Spec *pendingFormSpec() const;
    // Fills the form in and submits it; returns the required question ids
    // still unanswered (nothing sent then).
    QStringList answerForm(const QVector<GrillMeForm::Answer> &answers, const QString &note);
    // The form's Dismiss button.
    bool dismissForm(const QString &note);
    // Attaches an image to the composer as a paste or drop does. Empty when
    // added, else why it was refused (type, size, count).
    QString attachImage(const QByteArray &data, const QString &filenameHint);
    QString attachImageFile(const QString &path);
    // The composer's "New worktree" toggle.
    bool newWorktree() const;
    void setNewWorktree(bool on);
    // Starts Improve prompt (the composer's sparkle button) on the composer
    // text. Empty when it started (improveSettled follows), else why it cannot.
    QString improveComposer();
    // The prompts the queue strip shows (host replies excluded), oldest first.
    QStringList queuedPromptTexts() const;
    // The strip's remove button on its `visibleIndex`-th row; false when none.
    bool removeQueuedPrompt(int visibleIndex);
    // The Prewalk submenu: a model id the model picker lists, or "" for Off.
    // Empty when picked, else why not (Claude Code only).
    QString pickPrewalk(const QString &modelId);
    QString prewalkTarget() const;

signals:
    // Improve prompt ended: the improved text (now in the composer), or the error.
    void improveSettled(bool ok, const QString &textOrError);
    void retryRequested();
    // Ctrl+N in the composer. The dock decides whether to confirm before
    // forwarding to the manager.
    void restartSessionRequested();
    void sendWithGoalRequested();
    void generatePromptWithGoalRequested();
    void goalStopRequested();
    // Emitted when the user clicks Cancel on the composer. The view already
    // cancels the in-flight ACP prompt itself (onCancelClicked); this lets the
    // owning dock also stop any concurrently-running GoalAgent so the two are
    // torn down together.
    void cancelRequested();
    void inputFocused();
    // A fromGoalAgent queue item was sent (turn now open) or removed.
    void deferredGoalDispatched();
    void deferredGoalRemoved();
    void promptQueueChanged();
    // isWaitingForUser() flipped.
    void waitingChanged(bool waiting);

private slots:
    void onShowDebugLogClicked();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void onMessageAppended(int idx);
    void onMessageChunkAppended(int idx, const QString &chunk);
    void onMessageReplaced(int idx, const QString &fullText);
    void onThoughtAppended(int idx);
    void onThoughtChunkAppended(int idx, const QString &chunk);
    void onToolCallAddedOrUpdated(const QString &toolCallId);
    void onPlanUpdated();
    void onPlanResumeRequested(const QString &prompt);
    void onUsageChanged();
    void onMetadataChanged();
    void onCurrentModeChanged(const QString &modeId);
    void onIsProcessingChanged(bool processing);
    void onTurnEnded(int groupId);
    void onGoalJudgeTurnRemoved(const QVector<int> &messageIndices,
                                const QStringList &toolCallIds);
    void onPermissionRequested(const AcpProtocol::AcpPermissionRequest &req);
    void onRequestFailed(const QString &message);
    void onErrorOccurred(AcpErrorClassifier::AcpErrorKind kind, const QString &friendly);
    // Session set up (new or resumed): sends a restored session's parked prompt.
    void onConnectionReady();
    void onResumeFailed(const QString &reason);

    void onSendClicked();
    void onCancelClicked();
    void onAttachClicked();
    void onAutoApproveToggled(bool checked);
    void onModelComboChanged(int index);
    void onModeComboChanged(int index);
    void onEffortComboChanged(int index);
    void onPrewalkPicked(const QString &modelId);
    void onAdvisorClicked(bool on);
    void onAutoApprovePolicyChanged(const QString &policy);
    void onJumpToBottomClicked();
    void onImproveClicked();
    void onImproveFinished(const QString &improvedText);
    void onImproveError(const QString &message);

private:
    void buildUi();
    void wireSignals();
    void rebuildTintedIcons();
    void hydrateFromModel();
    void appendMessageWidget(int idx);
    // Insert a widget into the transcript timeline at the tail, just above
    // the inline heartbeat indicator (if present) and the trailing stretch.
    // The plan widget is a viewport footer outside the scroll area.
    void insertTimelineWidget(QWidget *w, bool syncWidth = true);
    int timelineTailIndex(QWidget *exclude) const;
    void maybeTruncateTranscript();
    void removeTranscriptWidget(QWidget *w);
    QWidget *widgetForTimelineEntry(const AcpTimelineEntry &entry) const;
    QLabel *makeTruncationPlaceholder(int hiddenCount);
    void rebuildTruncationPlaceholders(const AcpTranscriptTruncation::Plan &plan);
    void recordTruncation(const AcpTranscriptTruncation::Plan &plan);

    // "Worked for Xs" folding of finished turns. Folded entries have NO widgets
    // (nothing to lay out or paint); expanding instantiates them, collapsing
    // destroys them. A running turn is not folded: it is headed by a live
    // ticking row (ensureLiveTurnRow) that foldTurn adopts when the turn ends,
    // so every finished turn, newest included, looks the same. A background
    // wake-up turn continues the newest turn: its fold row goes live again and
    // the turn refolds once the wake-up stays over (m_wakeFoldTimer).
    void foldCompletedTurns(bool includeNewest = false);
    void foldEndedTurn(qint64 endedMs = 0);
    void releaseFormHeldFold();
    void foldTurn(const AcpTurnFolding::Turn &turn);
    void ensureLiveTurnRow();
    void dropLiveTurnRow();
    // Background tasks or Monitor watches the agent started are still alive:
    // they will wake it again, so the conversation is not over.
    bool agentWorkPending() const;
    // Fold the turn held open by that work once it is over (or `force`).
    void releaseWorkHeldFold(bool force = false);
    void setTurnExpanded(int turnBegin, bool expanded);
    // Materializes one time-boxed slice of an expanding turn; true when nothing
    // is left (done, collapsed, or the turn is gone).
    bool materializeTurnSlice(int turnBegin, const QFont &font);
    void pumpExpandQueue();
    void removeEntryWidget(const AcpTimelineEntry &entry);
    // Subagent calls (tool calls with a parentToolCallId) nest inside their
    // Agent card: never a transcript widget, never counted by folding.
    void onSubagentToolCallChanged(const AcpProtocol::AcpToolCall &tc);
    AcpToolCallCard *toolCallCard(const QString &id, int depth = 0) const;
    void wireToolCallCard(AcpToolCallCard *card);
    void refreshSubagentActivity(AcpToolCallCard *card);
    void materializeSubagentChildren(const QString &parentId);
    // Copy buttons under each finished turn's final report.
    // includeNewest: the newest turn has ended.
    void refreshResponseActions(bool includeNewest);
    void attachResponseActions(int messageIndex, AcpMessageWidget *w);
    // font: the chat typeface, resolved once by batch callers (chatFont() walks
    // qApp's children and reads settings).
    QWidget *createEntryWidget(const AcpTimelineEntry &entry, const QFont &font);
    AcpTurnFoldRow *makeFoldRow(const AcpTurnFolding::Turn &turn, AcpTurnFoldRow *adopt = nullptr);
    // Lines changed by the turn's completed edits, its subagents' included.
    AcpDiffStat::Lines turnDiffStat(const AcpTurnFolding::Turn &turn) const;
    // Sums the completed calls among `pending` and their subagent children;
    // counted (optional) collects the completed ids.
    AcpDiffStat::Lines diffStatOf(QStringList pending, QSet<QString> *counted) const;
    // A call just completed in the running turn: its lines join the live row.
    void noteLiveEdit(const AcpProtocol::AcpToolCall &tc);
    void resetLiveDiff();
    void setFoldedMask(const QVector<int> &entries, bool folded);
    void setEntryFolded(int timelineIndex, bool folded);
    void pruneStaleFoldedTurns();
    void syncTranscriptHostWidth();
    void scrollToBottomDeferred();
    void updateJumpButtonVisibility();
    void positionJumpButton();
    void positionImproveButton();
    void updateImproveButtonState();
    void updateSendButton();
    void refreshQueueStrip();
    void scheduleFlushQueuedPrompt();
    void flushQueuedPrompt();
    // Claude Code `/advisor`: the button is shown only while the agent advertises
    // the command; toggling sends `/advisor <model one level up>` or `/advisor off`.
    void updateAdvisorButton();
    void sendAdvisorCommand(bool on);
    void submitSlashCommand(const QString &text);
    // `wireText` (when set) is sent instead of `text`, which stays the bubble.
    void dispatchPrompt(const QString &text,
                        const QVector<QPair<QByteArray, QString>> &images,
                        bool fromGoalAgent = false,
                        const QString &marker = {},
                        const QString &wireText = {});

    // Question forms (grill_me ask tool calls).
    void trackGrillMeToolCall(const AcpProtocol::AcpToolCall &tc);
    void showGrillMeToolCallForm(const AcpProtocol::AcpToolCall &tc);
    void presentForm(const GrillMeForm::Spec &spec);
    void onFormSubmitted(const QVector<GrillMeForm::Answer> &answers);
    void onFormDismissed();
    // Leave the pending form in `state` (read-only) and forget it.
    void settlePendingForm(GrillMeFormWidget::State state, const QString &detail = {});
    // Send (or queue behind the running turn) a host-built prompt.
    void sendHostPrompt(const QString &text, const QString &marker, const QString &wireText = {},
                        bool fromGoalAgent = false);
    bool isUnattended() const;
    // Emits waitingChanged when isWaitingForUser() differs from the last report.
    void refreshWaiting();
    void refreshSelectorTooltips();
    void restoreExpiredForm();
    // Scroll so the form's first question is at the top; stops following the stream.
    void scrollToFormTop(QWidget *form);
    void applyMessageMarker(AcpMessageWidget *w, int messageIndex);
    bool inputKeyEventIsSubmit(QKeyEvent *ke) const;
    // Heartbeat indicator at the tail of the transcript: shows time since the
    // last structural event (new message / thought / tool call / plan /
    // permission). Text/thought chunk streaming intentionally does NOT
    // reset — during steady streaming the counter grows so the user
    // sees "still working, last new event was X.Xs ago".
    void resetElapsed();
    void onElapsedTick();
    // Heartbeat row text + visibility: timer while the agent is processing,
    // "waiting background (N)" to its right while Claude Code background tasks
    // are alive (also after the turn that spawned them has ended).
    void updateElapsedLabel();
    void onBackgroundTaskCountChanged(int count);
    // Wake-ups / cron jobs the agent scheduled: "⏰ Scheduled (N)" on the row.
    void onScheduledJobsChanged();
    // "Stop" link on the waiting-background row: pick one task to stop.
    void onBackgroundStopRequested();

    // Input-placeholder busy clock. Distinct from the transcript-tail
    // heartbeats above: those reset on every structural event, whereas this
    // measures the *whole* wall span during which any agent is working. The
    // union of "the ACP turn is processing" OR "a goal is active" drives it —
    // the clock starts on the idle→busy edge and only stops (restoring the
    // resting "Send a message" placeholder) once BOTH are idle again, so a
    // second agent starting, or one finishing mid-run, never restarts it.
    void refreshBusyPlaceholder();
    void updateBusyPlaceholderText();

    // Single chokepoint for every chat-input placeholder change. Sets the text
    // AND invalidates the viewport when the placeholder is on screen (empty
    // document) — QPlainTextEdit::setPlaceholderText() does not reliably
    // repaint on a pure text change, so routing all transitions through here
    // makes it impossible for a future placeholder change to silently freeze.
    void setInputPlaceholder(const QString &text);

    // Push the user's saved per-agent preferences (model/mode/effort) into
    // the running session — or the session's own carried selection when it
    // continues on a new agent process. Called once per session after
    // metadata first populates the available catalogs.
    void applySavedPreferences();
    // Remember the model's current model / mode / effort for applySavedPreferences.
    void captureSelection();
    // The model and effort the agent reports now.
    AcpPrewalk::Selection currentSelection() const;
    // Model change that is not the user's pick (Prewalk): no advisor re-point,
    // no agent default.
    void sendModelChange(const QString &modelId);

    // Prewalk (GLOSSARY). Claude Code only: it applies a model change from the
    // next request of the running turn.
    bool prewalkAvailable() const;
    // The session's prewalk model: its own choice, else the agent default.
    void loadPrewalkTarget();
    void refreshPrewalk();
    // The "Prewalk: …" submenu at the top of the model picker's menu.
    void addPrewalkMenu(QMenu *menu);
    // What Prewalk will do or is doing now.
    QString prewalkStatus() const;
    void notePrewalkToolCall(const AcpProtocol::AcpToolCall &tc);
    void notePrewalkTurnEnded();
    // After the agent reported its selection: finish a restore. True when it
    // sent the effort back (the reported one is the agent's reset, not a pick).
    bool notePrewalkSelectionReported();
    // History keeps the session's own selection while switched.
    void syncPrewalkOverride();
    QString modelLabel(const QString &modelId) const;
    // Sending is impossible right now: switching agents, resuming, or resume failed.
    bool composerLocked() const;
    // A banner button (e.g. Restart); setBanner() clears it.
    void setBannerAction(const QString &label, std::function<void()> action);

    void applyChatFont();

    // Build the chat (Default Font) QFont from settings, with the hinting
    // policy applied. Used to stamp every bubble explicitly (styled widgets
    // don't inherit setFont) and the input. Returns a sensible fallback when
    // settings are unavailable (test harness).
    QFont chatFont() const;

    // Slash-command / skill completion popup.
    void showCommandPopup();
    void hideCommandPopup();
    void resizeCommandPopup();
    void filterCommandPopup();
    void acceptCommandCompletion();
    void appendCommandItem(const AcpProtocol::AcpCommandInfo &cmd);
    void appendCompletionItem(const QString &label, const QString &value);
    QString owningDockWorkingDirectory() const;
    QString resolveSkillsDirectory(const QString &workingDirectory) const;
    QStringList cachedSkillNames();
    QStringList scanSkillNames(const QString &skillsDirectory) const;

    AcpSessionModel *m_model = nullptr;       // non-owning
    AcpConnection *m_connection = nullptr;    // non-owning
    AcpAgentRegistry *m_registry = nullptr;   // non-owning

    // Status banner
    QFrame *m_banner = nullptr;
    QLabel *m_bannerLabel = nullptr;
    QToolButton *m_bannerAction = nullptr;
    std::function<void()> m_bannerActionFn;
    QToolButton *m_bannerClose = nullptr;

    // ACP debug log popup (non-modal, lazily created on first click).
    QPointer<QDialog> m_debugDialog;
    QPlainTextEdit *m_debugDialogText = nullptr;
    QCheckBox *m_debugDialogOnlyGoal = nullptr;

    // Transcript
    QScrollArea *m_scroll = nullptr;
    QWidget *m_transcriptHost = nullptr;
    QVBoxLayout *m_transcriptLayout = nullptr;
    QToolButton *m_jumpToBottom = nullptr;

    // Selectors row
    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_effortCombo = nullptr;
    QString m_effortConfigOptionId;
    // Non-empty when the agent exposes the model picker as a config option
    // (e.g. Claude Code's `model` select) rather than the top-level models
    // array. Drives whether model changes dispatch via session/set_config.
    QString m_modelConfigOptionId;

    // Prewalk: a submenu of the model picker, absent unless available.
    AcpPrewalk m_prewalk;
    bool m_prewalkAvailable = false; // prewalkAvailable() as of the last catalog

    // Auto-approve, in the "More options" menu (vertical dots) after Advisor.
    QToolButton *m_moreBtn = nullptr;
    QAction *m_autoApproveAction = nullptr;
    // Session-local; never persisted. New views start unchecked.
    QCheckBox *m_newWorktreeCheck = nullptr;

    // Attachments
    AcpImageAttachmentList *m_attachmentList = nullptr;

    // Goal status row (visible only while a goal is active)
    QFrame *m_goalStatusRow = nullptr;
    QLabel *m_goalStatusLabel = nullptr;
    QToolButton *m_goalStopBtn = nullptr;

    // Input + buttons
    QPlainTextEdit *m_input = nullptr;
    AcpPromptQueue m_promptQueue;
    AcpPromptQueueStrip *m_queueStrip = nullptr;
    bool m_flushQueuedScheduled = false;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QToolButton *m_attachBtn = nullptr;
    QCheckBox *m_advisorBtn = nullptr;
    QToolButton *m_sendWithGoalBtn = nullptr;

    // Usage
    AcpUsageIndicator *m_usageIndicator = nullptr;

    // Heartbeat indicator shown only while the agent is processing.
    QLabel *m_elapsedLabel = nullptr;
    QTimer *m_elapsedTimer = nullptr;
    int m_elapsedMs = 0;
    bool m_elapsedTipIsBackground = false;
    int m_scheduledRev = 0;            // bumped per scheduledJobsChanged
    int m_elapsedTipScheduledRev = -1; // revision the cached tooltip reflects
    QElapsedTimer m_bgWaitClock; // valid while background tasks are alive

    // Goal heartbeat — shown at the transcript tail while a goal is active.
    QLabel *m_goalElapsedLabel = nullptr;
    QTimer *m_goalElapsedTimer = nullptr;
    int m_goalElapsedMs = 0;
    int m_goalTerminalGeneration = 0;

    // Input-placeholder busy clock (see refreshBusyPlaceholder). m_busyClock is
    // monotonic and only re-started on the idle→busy edge; the 1 s timer repaints
    // the placeholder. m_goalRunning mirrors the attached goal's active state
    // (the view has no direct GoalAgent pointer, so the dock-driven
    // setGoalActive/setGoalTerminal/clearGoalStatus calls toggle it).
    QTimer *m_busyPlaceholderTimer = nullptr;
    QElapsedTimer m_busyClock;
    bool m_busyPlaceholderActive = false;
    bool m_goalRunning = false;

    // Tracking
    QHash<int, AcpMessageWidget *> m_messageWidgets;
    QHash<QString, AcpToolCallCard *> m_toolCallCards;
    QVector<AcpToolCallCard *> m_currentGroupCards;
    QSet<int> m_truncatedMessageIndices;
    QSet<QString> m_truncatedToolCallIds;
    QVector<QLabel *> m_truncationPlaceholders;
    QSet<int> m_reportMessages; // message indices of final reports (copy buttons)

    struct FoldedTurn
    {
        QPointer<AcpTurnFoldRow> row;
        QVector<int> entries; // timeline indices hidden while collapsed
        bool expanded = false;
        // Expanding builds widgets in time-boxed slices: next `entries` position
        // to materialize, -1 when nothing is pending.
        int nextEntry = -1;
    };
    QHash<int, FoldedTurn> m_foldedTurns; // key: first timeline index of the turn
    QVector<int> m_expandQueue;           // turn begins with slices still pending, FIFO
    QTimer *m_expandPump = nullptr;       // zero-interval: one slice per event-loop pass
    QVector<std::uint8_t> m_foldedMask;   // per timeline index; 1 = currently folded (no widget)
    int m_foldedCount = 0;
    QPointer<AcpTurnFoldRow> m_liveRow; // heads the running turn; ticks once per second
    int m_liveTurnBegin = -1;           // timeline index of that turn's anchor
    int m_liveToolCalls = 0;            // tool calls so far in that turn
    AcpDiffStat::Lines m_liveDiff;      // lines its completed calls changed so far
    QSet<QString> m_liveDiffCounted;    // completed calls already in m_liveDiff
    // The live row is a folded turn's row gone live again: a wake-up turn
    // (background task result, no new prompt) continues the newest turn.
    bool m_liveRowReopened = false;
    // The newest turn ended while background work it started (tasks, Monitor
    // watches) still runs: that work wakes the agent again, so the turn stays
    // open, its row ticking and its cards untouched, until agentWorkPending()
    // clears (releaseWorkHeldFold) or a new prompt folds it.
    bool m_foldHeldByWork = false;
    QTimer *m_wakeFoldTimer = nullptr;  // single-shot: fold once a wake-up turn stays over
    qint64 m_wakeTurnEndedMs = 0;       // when that wait began (the turn's end)
    // The newest turn ended while a question form waited on the user: it stays
    // unfolded (its text is what the questions are about) under a frozen row
    // until the form settles or the next prompt folds it.
    bool m_foldHeldByForm = false;
    AcpPlanWidget *m_planWidget = nullptr;
    QPointer<AcpMessageWidget> m_activeThought;
    QPointer<AcpPermissionPrompt> m_activePermissionPrompt;

    // At most one pending form per session; settled forms stay in the
    // transcript as standalone widgets (never model entries).
    QPointer<GrillMeFormWidget> m_pendingForm;
    // grill_me ask calls seen but not yet shown (waiting for completion / turn end).
    QSet<QString> m_grillMeCandidates;
    QSet<QString> m_grillMeShown;
    // Answers of a form that was superseded or sent for clarification.
    QVector<GrillMeForm::Answer> m_carryAnswers;
    // Form note of a superseded form (never sent). A sent note is not carried.
    QString m_carryNote;
    bool m_scheduledUnattended = false;
    bool m_lastWaiting = false; // what waitingChanged last reported

    bool m_updatingSelectors = false;

    // Per-session one-shot: after metadata first arrives we push the user's
    // saved selections (model/mode/effort) back into the agent. Cleared on
    // rebind() so a restarted session re-applies preferences.
    bool m_savedPrefsApplied = false;
    // True until the user sends something (see noteUserSend()); reset by rebind.
    bool m_draft = true;

    struct Selection
    {
        QString model;
        QString mode;
        QString effort;
        bool valid = false;
    };
    // The session's own selection to re-apply on its next agent process
    // (Switch agent, restored session); consumed by applySavedPreferences.
    Selection m_carry;
    // Restored session: the first prompt waits here while its agent resumes.
    struct ParkedSend
    {
        QString text;
        QVector<QPair<QByteArray, QString>> images;
        bool fromGoalAgent = false;
        QString marker;
        QString wireText;
        bool active = false;
    };
    ParkedSend m_parked;
    bool m_resumeBlocked = false; // resume failed: only Restart helps
    QString m_switchingTo;        // agent the session is moving to

    // Auto-scroll state. The transcript is "stuck to the bottom" when the
    // user is parked at (or within a few px of) the maximum scroll value.
    // While stuck, content growth triggers auto-scroll; otherwise we leave
    // the user where they are and reveal the ↓ jump button. The flag flips
    // on when the user scrolls back to the bottom themselves, or when they
    // press Send / the ↓ button. m_programmaticScroll guards against our
    // own setValue() round-tripping through valueChanged and clobbering the
    // user-derived flag.
    bool m_stickToBottom = true;
    bool m_programmaticScroll = false;

    // Slash-command / skill completion
    QListWidget *m_commandPopup = nullptr;
    QChar m_completionTrigger = QLatin1Char('/');
    QString m_skillCacheCwd;
    QString m_skillCacheDir;
    QDateTime m_skillCacheMtime;
    QStringList m_skillCacheNames;

    // Prompt improve button (floating inside m_input)
    QToolButton *m_improveBtn = nullptr;
    ai::PromptImprover *m_promptImprover = nullptr;
    QShortcut *m_improveShortcut = nullptr;
    QString m_originalDraftBeforeImprove;
};

#endif // ACP_SESSION_VIEW_H
