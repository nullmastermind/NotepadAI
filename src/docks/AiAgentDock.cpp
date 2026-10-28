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

#include "AiAgentDock.h"

#include "AcpAgentIcons.h"
#include "AcpAgentManager.h"
#include "AcpAgentRegistry.h"
#include "AcpConnection.h"
#include "AcpSessionModel.h"
#include "AiDockGroup.h"
#include "ApplicationSettings.h"
#include "GoalActionParser.h"
#include "GoalAgent.h"
#include "ProjectGoalPresets.h"
#include "dialogs/GoalDraftDialog.h"
#include "dialogs/SendWithGoalDialog.h"
#include "widgets/AcpSessionView.h"
#include "widgets/AiDockSessionStrip.h"
#include "widgets/AiDockTitleBar.h"
#include "remote/ExecutionContext.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <Qt>

namespace {

QString lastUserPreviewFor(const AcpSessionModel *model)
{
    if (!model)
        return {};
    const auto &msgs = model->messages();
    for (int i = msgs.size() - 1; i >= 0; --i) {
        const AcpMessage &msg = msgs.at(i);
        if (msg.role != QLatin1String("user") || msg.fromGoalAgent)
            continue;
        QString text;
        for (const auto &block : msg.content) {
            if (block.kind == AcpProtocol::AcpContentBlock::Kind::Text)
                text += block.text;
        }
        return aiDockSessionTooltipPreview(text);
    }
    return {};
}

} // namespace

AiAgentDock::AiAgentDock(QString sessionId,
                         QString agentName,
                         QString workingDirectory,
                         AcpSessionModel *model,
                         AcpConnection *connection,
                         AcpAgentRegistry *registry,
                         AcpAgentManager *agentManager,
                         ApplicationSettings *appSettings,
                         QWidget *parent)
    : QDockWidget(parent)
    , m_workingDirectory(std::move(workingDirectory))
    , m_registry(registry)
    , m_agentManager(agentManager)
    , m_appSettings(appSettings)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setObjectName(aiDockObjectName(aiDockGroupKey(m_workingDirectory, QString())));
    // Spec ("Default dock area"): dock is unrestricted — user may move it to
    // any side. defaultArea() is only consulted on first attach.
    setAllowedAreas(Qt::AllDockWidgetAreas);

    buildUi();
    // The agent button lives in the title bar, not on the tab (which only
    // has room for the project name and session numbers).
    m_titleBar = new AiDockTitleBar(this);
    setTitleBarWidget(m_titleBar);
    connect(m_titleBar, &AiDockTitleBar::agentMenuRequested, this, &AiAgentDock::onAgentMenuRequested);
    addSlot(std::move(sessionId), std::move(agentName), model, connection, true);

    connect(this, &QDockWidget::topLevelChanged, this, [this](bool) {
        scheduleStripReinstall();
    });
    connect(this, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea) {
        scheduleStripReinstall();
    });
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        m_onScreen = visible;
        // Hidden docks must reinstall too: tabify rebuilds the area bar and
        // the inactive project's [1][2] live on that bar, not on this widget.
        scheduleStripReinstall();
        // Whether the open session's red dot shows depends on being on screen.
        pushStripSnapshots();
        refreshActivityIcon();
    });
}

void AiAgentDock::buildUi()
{
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_header = new QWidget(container);
    m_headerLayout = new QHBoxLayout(m_header);
    constexpr int kHeaderOuterPaddingPx = 8;
    constexpr int kHeaderVerticalPaddingPx = 4;
    m_headerLayout->setContentsMargins(kHeaderOuterPaddingPx,
                                       kHeaderVerticalPaddingPx,
                                       kHeaderOuterPaddingPx,
                                       kHeaderVerticalPaddingPx);
    m_headerLayout->setSpacing(AiDockSessionStrip::kSpacingPx);
    m_header->setObjectName(QStringLiteral("nn_aiSessionStripHeader"));
    m_header->hide();

    m_stack = new QStackedWidget(container);

    // QDockAreaLayoutInfo::updateTabBar returns false when count() <= 1, so
    // Qt never shows a dock-area tab bar for a lone ACP dock. This South bar
    // is the singleton stand-in; hidden again once a real area bar exists.
    m_localTabBar = new QTabBar(container);
    m_localTabBar->setObjectName(QStringLiteral("nn_aiLocalTabBar"));
    m_localTabBar->setDrawBase(true);
    m_localTabBar->setElideMode(Qt::ElideRight);
    m_localTabBar->setExpanding(false);
    m_localTabBar->setMovable(false);
    m_localTabBar->setShape(QTabBar::RoundedSouth);
    m_localTabBar->hide();

    layout->addWidget(m_header);
    layout->addWidget(m_stack, 1);
    layout->addWidget(m_localTabBar);
    setWidget(container);
}

void AiAgentDock::addSlot(QString sessionId,
                          QString agentName,
                          AcpSessionModel *model,
                          AcpConnection *connection,
                          bool makeCurrent)
{
    Slot slot;
    slot.sessionId = std::move(sessionId);
    m_newestSessionId = slot.sessionId;
    slot.agentName = std::move(agentName);
    slot.model = model;
    slot.connection = connection;
    slot.view = new AcpSessionView(model, connection, m_registry, m_stack);
    m_stack->addWidget(slot.view);
    wireSlotSignals(slot);
    m_slots.append(std::move(slot));

    const int index = m_slots.size() - 1;
    if (makeCurrent) {
        switchTo(index);
    } else {
        m_slots[index].hasActivity = true;
        refreshActivityIcon();
        pushStripSnapshots();
    }
    refreshTitle();
    refreshProjectTooltip();
    scheduleStripReinstall();
    emit slotsChanged();
}

void AiAgentDock::wireSlotSignals(Slot &slot)
{
    if (slot.model) {
        connect(slot.model, &AcpSessionModel::metadataChanged,
                this, &AiAgentDock::onMetadataChanged);
        connect(slot.model, &AcpSessionModel::isProcessingChanged,
                this, &AiAgentDock::onProcessingChanged);
        // User prompts only — assistant messageAppended is the first chunk of
        // a turn, not a last-user change. Never connect messageChunkAppended.
        connect(slot.model, &AcpSessionModel::messageAppended,
                this, &AiAgentDock::onMessageAppended);
    }
    if (slot.connection) {
        connect(slot.connection, &AcpConnection::agentExited,
                this, &AiAgentDock::onAgentExited);
        connect(slot.connection, &AcpConnection::promptEnded,
                this, &AiAgentDock::onNativeGoalPromptEnded,
                Qt::UniqueConnection);
        connect(slot.connection, &AcpConnection::backgroundIdle,
                this, &AiAgentDock::onNativeGoalBackgroundIdle,
                Qt::UniqueConnection);
        // Background tasks and Monitor watches keep the session number's working fill.
        connect(slot.connection, &AcpConnection::backgroundIdle,
                this, &AiAgentDock::onWorkChanged, Qt::UniqueConnection);
        connect(slot.connection, &AcpConnection::scheduledJobsChanged,
                this, &AiAgentDock::onWorkChanged, Qt::UniqueConnection);
        connect(slot.connection, &AcpConnection::asyncTasksChanged,
                this, &AiAgentDock::onWorkChanged, Qt::UniqueConnection);
    }
    if (slot.view) {
        connect(slot.view, &AcpSessionView::waitingChanged,
                this, &AiAgentDock::onWorkChanged, Qt::UniqueConnection);
    }
    if (slot.view) {
        connect(slot.view, &AcpSessionView::retryRequested,
                this, &AiAgentDock::onRetryFromView,
                Qt::UniqueConnection);
        connect(slot.view, &AcpSessionView::restartSessionRequested,
                this, &AiAgentDock::onRestartFromView,
                Qt::UniqueConnection);
        connect(slot.view, &AcpSessionView::sendWithGoalRequested,
                this, &AiAgentDock::sendWithGoal,
                Qt::UniqueConnection);
        connect(slot.view, &AcpSessionView::generatePromptWithGoalRequested,
                this, &AiAgentDock::generatePromptWithGoal,
                Qt::UniqueConnection);
        // NOTE: both of these MUST target a member-function slot, not a lambda.
        // Qt::UniqueConnection silently refuses functor/lambda slots
        // (QObject::connectImpl returns an empty Connection for them), so a
        // lambda here would never actually be wired up and the goal would keep
        // running. Route the goal-row Stop button and the composer Cancel
        // button into the same member slot.
        connect(slot.view, &AcpSessionView::goalStopRequested,
                this, &AiAgentDock::stopGoalAgentIfActive,
                Qt::UniqueConnection);
        connect(slot.view, &AcpSessionView::cancelRequested,
                this, &AiAgentDock::stopGoalAgentIfActive,
                Qt::UniqueConnection);
        connect(slot.view, &AcpSessionView::deferredGoalDispatched,
                this, &AiAgentDock::onDeferredGoalDispatched,
                Qt::UniqueConnection);
        connect(slot.view, &AcpSessionView::deferredGoalRemoved,
                this, &AiAgentDock::onDeferredGoalRemoved,
                Qt::UniqueConnection);
        connect(slot.view, &AcpSessionView::promptQueueChanged,
                this, &AiAgentDock::onPromptQueueChanged,
                Qt::UniqueConnection);
        connect(slot.view, &AcpSessionView::inputFocused,
                this, &AiAgentDock::inputFocused,
                Qt::UniqueConnection);
    }
}

void AiAgentDock::unwireSlot(Slot &slot)
{
    if (slot.model)
        disconnect(slot.model, nullptr, this, nullptr);
    if (slot.connection)
        disconnect(slot.connection, nullptr, this, nullptr);
    if (slot.view)
        disconnect(slot.view, nullptr, this, nullptr);
}

void AiAgentDock::destroySlotGoal(Slot &slot)
{
    // GoalAgent must die before its view: QWidget child order would otherwise
    // free the view first, then GoalAgent::~ emits statusChanged into UAF.
    if (slot.goal) {
        disconnect(slot.goal, nullptr, this, nullptr);
        delete slot.goal;
        slot.goal = nullptr;
    }
}

void AiAgentDock::stopGoalAgentIfActive()
{
    Slot *slot = slotForView(qobject_cast<AcpSessionView *>(sender()));
    if (!slot)
        slot = currentSlot();
    if (slot)
        slot->nativeAutoCompact = false;
    if (slot && slot->goal && slot->goal->status() == GoalAgent::Active)
        slot->goal->stop();
}

void AiAgentDock::onNativeGoalPromptEnded()
{
    maybeSendNativeAutoCompact(slotForConnection(sender()));
}

void AiAgentDock::onNativeGoalBackgroundIdle()
{
    maybeSendNativeAutoCompact(slotForConnection(sender()));
}

void AiAgentDock::onDeferredGoalDispatched()
{
    Slot *slot = slotForView(qobject_cast<AcpSessionView *>(sender()));
    if (slot && slot->goal)
        slot->goal->noteDeferredGoalDispatched();
}

void AiAgentDock::onDeferredGoalRemoved()
{
    Slot *slot = slotForView(qobject_cast<AcpSessionView *>(sender()));
    if (slot && slot->goal)
        slot->goal->noteDeferredGoalDropped();
}

void AiAgentDock::onPromptQueueChanged()
{
    maybeSendNativeAutoCompact(slotForView(qobject_cast<AcpSessionView *>(sender())));
}

void AiAgentDock::maybeSendNativeAutoCompact(Slot *slot)
{
    if (!slot || !slot->nativeAutoCompact || !slot->connection)
        return;
    if (slot->model && slot->model->isProcessing())
        return;
    // Never compact under a running background task or before the wake-up
    // turn that reads its result; backgroundIdle re-drives this.
    if (slot->connection->hasPendingBackgroundWork())
        return;
    if (slot->view && slot->view->queuedPromptCount() > 0)
        return;
    slot->nativeAutoCompact = false;
    GoalAgent::sendAutoCompactTo(slot->connection, slot->model);
}

void AiAgentDock::stopGoalForSession(const QString &sessionId)
{
    Slot *slot = slotById(sessionId);
    if (slot && slot->goal && slot->goal->status() == GoalAgent::Active)
        slot->goal->stop();
}

void AiAgentDock::generatePromptWithGoal()
{
    Slot *slot = slotForView(qobject_cast<AcpSessionView *>(sender()));
    if (!slot)
        slot = currentSlot();
    if (!slot || !slot->view)
        return;

    const QString id = slot->sessionId;
    GoalDraftDialog dlg(m_agentManager,
                        m_registry,
                        m_appSettings,
                        slot->model,
                        slot->connection ? slot->connection->workingDirectory() : m_workingDirectory,
                        slot->connection ? slot->connection->executionContext() : nullptr,
                        this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString generated = dlg.generatedText().trimmed();
    if (generated.isEmpty())
        return;
    slot = slotById(id);
    if (!slot || !slot->view)
        return;
    slot->view->insertTextToInput(generated);
}

void AiAgentDock::onAgentExited(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    Slot *slot = slotForConnection(sender());
    if (!slot)
        return;
    slot->agentExited = true;
    if (slot->view) {
        slot->view->setBanner(
            tr("Agent exited (code %1). Click Restart to start a new session.").arg(exitCode),
            AcpSessionView::BannerKind::Error);
    }
}

void AiAgentDock::onRetryFromView()
{
    Slot *slot = slotForView(qobject_cast<AcpSessionView *>(sender()));
    if (slot && slot->agentExited)
        emit restartRequested(slot->sessionId);
}

void AiAgentDock::onRestartFromView()
{
    if (m_restartDialogShowing)
        return;

    Slot *slot = slotForView(qobject_cast<AcpSessionView *>(sender()));
    if (!slot)
        slot = currentSlot();
    if (!slot)
        return;

    const QString id = slot->sessionId;
    // Mirror closeEvent's safety net: a restart is destructive to the running
    // turn, so confirm if the agent is mid-prompt. The manager tears down the
    // old connection during rebind, which would cancel the in-flight call.
    if (slot->model && slot->model->isProcessing()) {
        m_restartDialogShowing = true;
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this,
            tr("AI Agent"),
            tr("A prompt is still running. Restart the session anyway?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        m_restartDialogShowing = false;
        if (choice != QMessageBox::Yes)
            return;
        slot = slotById(id);
        if (!slot)
            return;
        if (slot->connection)
            slot->connection->cancelPrompt();
    }
    emit restartRequested(id);
}

void AiAgentDock::onProcessingChanged(bool processing)
{
    Q_UNUSED(processing);
    Slot *slot = slotForModel(sender());
    if (slot)
        syncSlotBusy(*slot);
}

void AiAgentDock::onMessageAppended(int idx)
{
    Slot *slot = slotForModel(sender());
    if (!slot || !slot->model)
        return;
    const auto &msgs = slot->model->messages();
    if (idx < 0 || idx >= msgs.size())
        return;
    const AcpMessage &msg = msgs.at(idx);
    if (msg.role != QLatin1String("user") || msg.fromGoalAgent)
        return;
    pushStripSnapshots();
}

void AiAgentDock::rebind(const QString &oldSessionId,
                         AcpConnection *connection,
                         AcpSessionModel *model,
                         QString newSessionId,
                         QString newAgentName)
{
    Slot *slot = slotById(oldSessionId);
    if (!slot)
        return;

    unwireSlot(*slot);

    if (m_newestSessionId == oldSessionId)
        m_newestSessionId = newSessionId;
    slot->sessionId = std::move(newSessionId);
    slot->agentName = std::move(newAgentName);
    slot->connection = connection;
    slot->model = model;
    slot->agentExited = false;

    if (slot->view) {
        slot->view->rebind(model, connection);
        // rebind() calls clearGoalStatus(). Goal-driven (and user) restart
        // keeps an Active GoalAgent on this slot — restore the banner.
        if (slot->goal && slot->goal->status() == GoalAgent::Active) {
            const int idx = slot->goal->currentCriterionIndex();
            const auto &crits = slot->goal->criteria();
            const int iter = (idx >= 0 && idx < crits.size()) ? crits.at(idx).iteration : 0;
            slot->view->setGoalActive(idx + 1, crits.size(), iter,
                                      slot->goal->maxIterations());
        }
    }

    wireSlotSignals(*slot);
    syncSlotBusy(*slot);
    refreshTitle();
    pushStripSnapshots();
    emit slotsChanged();
}

void AiAgentDock::rebindConnection(const QString &sessionId, AcpConnection *connection, QString agentName)
{
    Slot *slot = slotById(sessionId);
    if (!slot)
        return;
    unwireSlot(*slot);
    // A finished goal still points at the old connection; switching needs an
    // idle session, so it is never a running one.
    destroySlotGoal(*slot);
    slot->connection = connection;
    slot->agentName = std::move(agentName);
    slot->agentExited = false;
    slot->switchingTo.clear();
    if (slot->view) {
        slot->view->setSwitching({});
        slot->view->rebindConnection(connection);
    }
    wireSlotSignals(*slot);
    syncSlotBusy(*slot);
    pushStripSnapshots();
    emit slotsChanged();
}

void AiAgentDock::setSessionSwitching(const QString &sessionId, const QString &targetName)
{
    Slot *slot = slotById(sessionId);
    if (!slot)
        return;
    slot->switchingTo = targetName;
    if (slot->view)
        slot->view->setSwitching(targetName);
    pushStripSnapshots();
}

AcpSwitchAgent::SessionState AiAgentDock::switchState(const QString &sessionId) const
{
    const Slot *slot = slotById(sessionId);
    return slot ? switchState(*slot) : AcpSwitchAgent::SessionState{};
}

AcpSwitchAgent::SessionState AiAgentDock::switchState(const Slot &slot) const
{
    AcpSwitchAgent::SessionState st;
    if (slot.connection) {
        const remote::ExecutionContext *ctx = slot.connection->executionContext();
        st.remote = ctx && ctx->isRemote();
        st.sourceIsClaudeCode = AcpSwitchAgent::isClaudeCodeAgent(slot.connection->definition());
        st.backgroundWork = slot.connection->agentWorkPending()
            || slot.connection->backgroundTaskCount() > 0;
    }
    st.switching = !slot.switchingTo.isEmpty();
    st.turnRunning = slot.model && slot.model->isProcessing();
    st.goalActive = slot.goal && slot.goal->status() == GoalAgent::Active;
    st.waitingForUser = slotWaiting(slot);
    st.queuedPrompts = slot.view ? slot.view->queuedPromptCount() : 0;
    return st;
}

AcpAgentDefinition AiAgentDock::slotAgent(const Slot &slot) const
{
    AcpAgentDefinition def = slot.connection ? slot.connection->definition() : AcpAgentDefinition{};
    if (m_registry && !def.id.isEmpty()) {
        const AcpAgentDefinition live = m_registry->agent(def.id);
        if (!live.id.isEmpty())
            def = live;
    }
    if (def.name.isEmpty())
        def.name = slot.agentName;
    return def;
}

QVector<AcpOpenSessions::Session> AiAgentDock::openSessions() const
{
    QVector<AcpOpenSessions::Session> out;
    out.reserve(m_slots.size());
    for (const Slot &slot : m_slots) {
        const AcpAgentDefinition def = slotAgent(slot);
        out.append({slot.sessionId, def.id, def.name});
    }
    return out;
}

void AiAgentDock::noteAppClosing()
{
    for (Slot &slot : m_slots) {
        if (slot.goal && slot.goal->status() == GoalAgent::Active && slot.model)
            slot.model->appendSystemMessage(tr("Goal stopped when NotepadAI closed"));
    }
}

void AiAgentDock::onAgentMenuRequested(const QPoint &globalPos)
{
    Slot *slot = currentSlot();
    if (!slot || !slot->connection || !m_registry || !m_agentManager)
        return;
    const QString sessionId = slot->sessionId;
    const AcpSwitchAgent::SessionState st = switchState(*slot);
    const AcpSwitchAgent::Block block = AcpSwitchAgent::blockReason(st);
    const QString currentId = slot->connection->definition().id;
    const int backgroundCount = slot->connection->backgroundTaskCount();

    QMenu menu(this);
    menu.setToolTipsVisible(true);
    // Why nothing can be picked comes first.
    if (block != AcpSwitchAgent::Block::None)
        menu.addAction(AcpSwitchAgent::blockText(block, backgroundCount))->setEnabled(false);
    if (block != AcpSwitchAgent::Block::Remote && block != AcpSwitchAgent::Block::SourceNotClaudeCode) {
        menu.addSection(tr("Continue this session with"));
        const QString tip = tr("Copies the conversation into this agent's Claude Code folder and resumes it "
                               "there.\nThinking from Sonnet / Haiku 5.5 does not carry across accounts, and "
                               "the first reply starts without a cache.");
        const QList<AcpAgentDefinition> targets = AcpSwitchAgent::claudeCodeAgents(m_registry->agents());
        for (const AcpAgentDefinition &agent : targets) {
            QAction *action = menu.addAction(acpAgentIcon(agent, palette()), agent.name);
            if (agent.id == currentId) {
                action->setCheckable(true);
                action->setChecked(true);
                action->setEnabled(false);
                continue;
            }
            action->setData(agent.id);
            action->setToolTip(tip);
            action->setEnabled(block == AcpSwitchAgent::Block::None);
        }
    }
    const QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;
    const QString targetId = chosen->data().toString();
    // The menu ran a nested event loop: the slot may be gone.
    if (targetId.isEmpty() || !slotById(sessionId))
        return;
    m_agentManager->switchAgent(sessionId, targetId);
}

AiAgentDock::~AiAgentDock()
{
    // ~QWidget deletes the strip with our children after our members are
    // gone; its destroyed handler must not write them.
    if (m_strip)
        disconnect(m_strip, nullptr, this, nullptr);

    for (Slot &slot : m_slots)
        destroySlotGoal(slot);

    for (Slot &slot : m_slots)
        unwireSlot(slot);
}

void AiAgentDock::setActivityIndicator(bool active)
{
    if (active) {
        // The session added last, wherever the user has dragged it since.
        if (const int newest = indexOfSession(newestSessionId()); newest >= 0) {
            if (m_current != newest)
                m_slots[newest].hasActivity = true;
            else if (!m_onScreen)
                m_slots[m_current].hasActivity = true;
        }
    } else if (Slot *slot = currentSlot()) {
        slot->hasActivity = false;
    }
    refreshActivityIcon();
}

AiAgentDock::ProjectMark AiAgentDock::projectMark() const
{
    const bool raised = m_onScreen;
    bool activity = false;
    bool working = false;
    bool waiting = false;
    for (int i = 0; i < m_slots.size(); ++i) {
        const Slot &slot = m_slots.at(i);
        if (slot.hasActivity && (!raised || i != m_current))
            activity = true;
        // A raised dock already shows each number's own fill and dot.
        if (!raised && slotWorking(slot))
            working = true;
        if (!raised && slotWaiting(slot))
            waiting = true;
    }
    if (waiting)
        return ProjectMark::Waiting;
    if (working)
        return ProjectMark::Working;
    return activity ? ProjectMark::Activity : ProjectMark::None;
}

void AiAgentDock::refreshActivityIcon()
{
    constexpr int kSize = 8;
    const ProjectMark mark = projectMark();
    const QColor highlight = palette().color(QPalette::Highlight);
    // Called on every work/waiting/busy change: repaint (and make the tab bar
    // relayout) only when what the icon shows actually changed.
    if (mark == m_iconMark && highlight.rgba() == m_iconHighlight)
        return;
    m_iconMark = mark;
    m_iconHighlight = highlight.rgba();
    if (mark == ProjectMark::None) {
        setWindowIcon(QIcon());
        return;
    }
    QPixmap px(kSize, kSize);
    px.fill(Qt::transparent);
    QPainter painter(&px);
    painter.setRenderHint(QPainter::Antialiasing);
    if (mark == ProjectMark::Working) {
        // A ring, not a dot: still, so the tab never repaints on a timer.
        constexpr qreal kPen = 1.5;
        painter.setPen(QPen(highlight, kPen));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QRectF(kPen / 2, kPen / 2, kSize - kPen, kSize - kPen));
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(mark == ProjectMark::Waiting ? AiDockSessionStrip::attentionColor() : highlight);
        painter.drawEllipse(0, 0, kSize, kSize);
    }
    painter.end();
    setWindowIcon(QIcon(px));
}

bool AiAgentDock::isBusy() const
{
    const Slot *slot = currentSlot();
    return slot && slotBusy(*slot);
}

bool AiAgentDock::isSessionBusy(const QString &sessionId) const
{
    const Slot *slot = slotById(sessionId);
    return slot && slotBusy(*slot);
}

bool AiAgentDock::isSessionWorking(const QString &sessionId) const
{
    const Slot *slot = slotById(sessionId);
    return slot && slotWorking(*slot);
}

bool AiAgentDock::isSessionWaiting(const QString &sessionId) const
{
    const Slot *slot = slotById(sessionId);
    return slot && slotWaiting(*slot);
}

bool AiAgentDock::slotWaiting(const Slot &slot) const
{
    return slot.view && slot.view->isWaitingForUser();
}

bool AiAgentDock::slotWorking(const Slot &slot) const
{
    if (slotBusy(slot))
        return true;
    if (slot.connection)
        return slot.connection->agentWorkPending();
    return slot.model && slot.model->backgroundTaskCount() > 0;
}

void AiAgentDock::onWorkChanged()
{
    pushStripSnapshots();
    refreshActivityIcon();
}

bool AiAgentDock::slotBusy(const Slot &slot) const
{
    if (slot.model && slot.model->isProcessing())
        return true;
    if (slot.goal && slot.goal->status() == GoalAgent::Active)
        return true;
    return false;
}

void AiAgentDock::syncSlotBusy(Slot &slot)
{
    const bool busy = slotBusy(slot);
    if (busy) {
        if (slot.busySinceMs == 0)
            slot.busySinceMs = aiDockMonotonicNowMs();
    } else {
        slot.busySinceMs = 0;
    }
    pushStripSnapshots();
    refreshActivityIcon();
}

QSize AiAgentDock::sizeHint() const
{
    return QSize(600, 400);
}

void AiAgentDock::closeEvent(QCloseEvent *event)
{
    if (m_acceptingWidgetClose) {
        QDockWidget::closeEvent(event);
        if (event->isAccepted())
            notifySiblingsRefreshStrip();
        return;
    }
    if (m_slots.size() <= 1) {
        Slot *slot = currentSlot();
        const QString id = slot ? slot->sessionId : QString();
        if (slot && slot->model && slot->model->isProcessing()) {
            if (!confirmCloseWhileRunning()) {
                event->ignore();
                return;
            }
            slot = slotById(id);
            if (!slot) {
                event->accept();
                return;
            }
            if (slot->connection)
                slot->connection->cancelPrompt();
        }
        QDockWidget::closeEvent(event);
        if (event->isAccepted())
            notifySiblingsRefreshStrip();
        return;
    }

    if (!tryCloseSlot(m_current)) {
        event->ignore();
        return;
    }
    event->ignore();
}

void AiAgentDock::changeEvent(QEvent *event)
{
    QDockWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        pushStripSnapshots();
}

void AiAgentDock::refreshSessionStripPlacement()
{
    scheduleStripReinstall();
}

void AiAgentDock::closeGroup()
{
    bool anyBusy = false;
    for (const Slot &slot : m_slots) {
        if (slot.model && slot.model->isProcessing()) {
            anyBusy = true;
            break;
        }
    }
    if (anyBusy && !confirmCloseWhileRunning())
        return;

    for (Slot &slot : m_slots) {
        if (slot.connection)
            slot.connection->cancelPrompt();
    }

    m_acceptingWidgetClose = true;
    if (m_agentManager) {
        QStringList ids;
        ids.reserve(m_slots.size());
        for (const Slot &slot : m_slots)
            ids.append(slot.sessionId);
        for (const QString &id : ids)
            m_agentManager->closeSession(id);
    }
    close();
}

bool AiAgentDock::confirmCloseWhileRunning()
{
    const QMessageBox::StandardButton choice = QMessageBox::question(
        this,
        tr("AI Agent"),
        tr("A prompt is still running. Close anyway?"),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    return choice == QMessageBox::Yes;
}

void AiAgentDock::onMetadataChanged()
{
    Slot *slot = slotForModel(sender());
    if (!slot)
        return;
    const QString name = resolvedAgentName(*slot);
    if (slot->agentName != name)
        slot->agentName = name;
    pushStripSnapshots();
}

void AiAgentDock::refreshTitle()
{
    setWindowTitle(aiDockWindowTitle(aiDockProjectBasename(m_workingDirectory),
                                     m_slots.size()));
    if (m_localTabBar && m_localTabBar->isVisible() && m_localTabBar->count() > 0)
        m_localTabBar->setTabText(0, windowTitle());
    // QDockAreaLayoutInfo::updateTabBar copies windowTitle into tabToolTip
    // on every title change. Clear it — the tab already shows the title.
    refreshProjectTooltip();
}

void AiAgentDock::refreshProjectTooltip()
{
    setToolTip(QString());
    // Qt's dock tab bar copies windowTitle into tabToolTip on every title
    // change (qdockarealayout.cpp). Hover on the project name is the tab
    // text, so keep the tooltip empty after that copy.
    int tabIndex = -1;
    if (QTabBar *bar = hostTabBar(&tabIndex); bar && tabIndex >= 0)
        bar->setTabToolTip(tabIndex, QString());
    else if (m_localTabBar && m_localTabBar->isVisible() && m_localTabBar->count() > 0)
        m_localTabBar->setTabToolTip(0, QString());
}

void AiAgentDock::insertTextToInput(const QString &text)
{
    if (Slot *slot = currentSlot(); slot && slot->view)
        slot->view->insertTextToInput(text);
}

QString AiAgentDock::sessionId() const
{
    const Slot *slot = currentSlot();
    return slot ? slot->sessionId : QString();
}

AcpSessionModel *AiAgentDock::model() const
{
    const Slot *slot = currentSlot();
    return slot ? slot->model : nullptr;
}

AcpConnection *AiAgentDock::connection() const
{
    const Slot *slot = currentSlot();
    return slot ? slot->connection : nullptr;
}

QString AiAgentDock::newestSessionId() const
{
    if (indexOfSession(m_newestSessionId) >= 0)
        return m_newestSessionId;
    return m_slots.isEmpty() ? QString() : m_slots.last().sessionId;
}

QString AiAgentDock::sessionIdAt(int index) const
{
    if (index < 0 || index >= m_slots.size())
        return {};
    return m_slots.at(index).sessionId;
}

bool AiAgentDock::moveSlot(int from, int to)
{
    const int count = m_slots.size();
    if (from == to || from < 0 || to < 0 || from >= count || to >= count)
        return false;

    // The open session is whoever it is, not whichever number it had.
    const QString openId = m_slots.at(m_current).sessionId;
    m_slots.move(from, to);
    if (AcpSessionView *view = m_slots.at(to).view) {
        m_stack->removeWidget(view);
        m_stack->insertWidget(to, view);
    }

    m_current = indexOfSession(openId);
    if (AcpSessionView *open = m_slots.at(m_current).view)
        m_stack->setCurrentWidget(open);
    pushStripSnapshots();
    refreshActivityIcon();
    emit slotsChanged();
    return true;
}

void AiAgentDock::switchTo(int index)
{
    if (index < 0 || index >= m_slots.size())
        return;
    const bool changed = m_current != index;
    m_current = index;
    m_stack->setCurrentIndex(index);
    m_slots[index].hasActivity = false;
    pushStripSnapshots();
    refreshActivityIcon();
    if (changed)
        emit slotsChanged();
}

void AiAgentDock::closeSlot(int index)
{
    tryCloseSlot(index);
}

bool AiAgentDock::tryCloseSlot(int index)
{
    if (index < 0 || index >= m_slots.size())
        return false;

    const QString id = m_slots[index].sessionId;
    if (m_slots[index].model && m_slots[index].model->isProcessing()) {
        if (!confirmCloseWhileRunning())
            return false;
        Slot *slot = slotById(id);
        if (!slot)
            return true;
        if (slot->connection)
            slot->connection->cancelPrompt();
    }

    if (m_agentManager) {
        m_agentManager->closeSession(id);
        return true;
    }
    const int idx = indexOfSession(id);
    if (idx >= 0)
        detachSlotAt(idx);
    return true;
}

void AiAgentDock::detachSlot(const QString &sessionId)
{
    const int index = indexOfSession(sessionId);
    if (index >= 0)
        detachSlotAt(index);
}

void AiAgentDock::detachSlotAt(int index)
{
    if (index < 0 || index >= m_slots.size())
        return;

    const int nextCurrent = aiDockCompactAfterClose(m_current, index, m_slots.size());
    Slot slot = m_slots.takeAt(index);
    destroySlotGoal(slot);
    unwireSlot(slot);
    if (slot.view) {
        m_stack->removeWidget(slot.view);
        slot.view->deleteLater();
        slot.view = nullptr;
    }

    if (m_slots.isEmpty()) {
        emit slotsChanged();
        if (!m_acceptingWidgetClose) {
            m_acceptingWidgetClose = true;
            close();
        }
        return;
    }

    m_current = nextCurrent;
    m_stack->setCurrentIndex(m_current);
    refreshTitle();
    refreshProjectTooltip();
    pushStripSnapshots();
    scheduleStripReinstall();
    refreshActivityIcon();
    emit slotsChanged();
}

AiAgentDock::Slot *AiAgentDock::currentSlot()
{
    if (m_current < 0 || m_current >= m_slots.size())
        return nullptr;
    return &m_slots[m_current];
}

const AiAgentDock::Slot *AiAgentDock::currentSlot() const
{
    if (m_current < 0 || m_current >= m_slots.size())
        return nullptr;
    return &m_slots[m_current];
}

AiAgentDock::Slot *AiAgentDock::slotById(const QString &sessionId)
{
    const int index = indexOfSession(sessionId);
    return index < 0 ? nullptr : &m_slots[index];
}

const AiAgentDock::Slot *AiAgentDock::slotById(const QString &sessionId) const
{
    const int index = indexOfSession(sessionId);
    return index < 0 ? nullptr : &m_slots[index];
}

AcpSessionView *AiAgentDock::viewFor(const QString &sessionId) const
{
    const Slot *slot = slotById(sessionId);
    return slot ? slot->view : nullptr;
}

QString AiAgentDock::agentNameFor(const QString &sessionId) const
{
    const Slot *slot = slotById(sessionId);
    return slot ? slot->agentName : QString();
}

GoalAgent *AiAgentDock::goalFor(const QString &sessionId) const
{
    const Slot *slot = slotById(sessionId);
    return slot ? slot->goal : nullptr;
}

AcpConnection *AiAgentDock::connectionFor(const QString &sessionId) const
{
    const Slot *slot = slotById(sessionId);
    return slot ? slot->connection : nullptr;
}

bool AiAgentDock::hasAgentExited(const QString &sessionId) const
{
    const Slot *slot = slotById(sessionId);
    return slot && slot->agentExited;
}

AcpSessionModel *AiAgentDock::modelFor(const QString &sessionId) const
{
    const Slot *slot = slotById(sessionId);
    return slot ? slot->model : nullptr;
}

AiAgentDock::Slot *AiAgentDock::slotForView(const AcpSessionView *view)
{
    if (!view)
        return nullptr;
    for (Slot &slot : m_slots) {
        if (slot.view == view)
            return &slot;
    }
    return nullptr;
}

AiAgentDock::Slot *AiAgentDock::slotForModel(const QObject *model)
{
    if (!model)
        return nullptr;
    for (Slot &slot : m_slots) {
        if (slot.model == model)
            return &slot;
    }
    return nullptr;
}

AiAgentDock::Slot *AiAgentDock::slotForConnection(const QObject *connection)
{
    if (!connection)
        return nullptr;
    for (Slot &slot : m_slots) {
        if (slot.connection == connection)
            return &slot;
    }
    return nullptr;
}

int AiAgentDock::indexOfSession(const QString &sessionId) const
{
    for (int i = 0; i < m_slots.size(); ++i) {
        if (m_slots.at(i).sessionId == sessionId)
            return i;
    }
    return -1;
}

QString AiAgentDock::resolvedAgentName(const Slot &slot) const
{
    if (slot.model) {
        const auto &info = slot.model->agentInfo();
        if (!info.title.isEmpty())
            return info.title;
        if (!info.name.isEmpty())
            return info.name;
    }
    return slot.agentName;
}

void AiAgentDock::ensureStrip()
{
    if (m_strip)
        return;
    m_strip = new AiDockSessionStrip(m_header);
    connect(m_strip, &AiDockSessionStrip::activated, this, [this](int index) {
        switchTo(index);
        raise();
    });
    connect(m_strip, &AiDockSessionStrip::closeRequested, this, &AiAgentDock::closeSlot);
    connect(m_strip, &AiDockSessionStrip::moveRequested, this, &AiAgentDock::moveSlot);
    connect(m_strip, &QObject::destroyed, this, [this]() {
        m_strip = nullptr;
        m_tabBarForStrip = nullptr;
        scheduleStripReinstall();
    });
    m_headerLayout->addWidget(m_strip);
}

void AiAgentDock::salvageStrip()
{
    if (!m_strip)
        return;
    if (m_tabBarForStrip) {
        // Scan, never a cached index: QDockAreaLayoutInfo::updateTabBar
        // inserts/removes tabs on the host bar, so the strip's tab moves. A
        // missed detach leaves the bar to hide() + deleteLater() the strip in
        // a later removeTab, after it has moved to another bar.
        for (int i = 0; i < m_tabBarForStrip->count(); ++i) {
            if (m_tabBarForStrip->tabButton(i, QTabBar::RightSide) == m_strip) {
                m_tabBarForStrip->setTabButton(i, QTabBar::RightSide, nullptr);
                break;
            }
        }
        m_tabBarForStrip = nullptr;
    }
    m_strip->setParent(m_header);
    if (m_headerLayout->indexOf(m_strip) < 0)
        m_headerLayout->addWidget(m_strip);
}

void AiAgentDock::scheduleStripReinstall()
{
    if (QCoreApplication::closingDown())
        return;
    if (m_stripReinstallQueued)
        return;
    m_stripReinstallQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_stripReinstallQueued = false;
        reinstallStrip();
    });
}

void AiAgentDock::showLocalTabBar()
{
    if (!m_localTabBar)
        return;
    if (m_localTabBar->count() == 0)
        m_localTabBar->addTab(windowTitle());
    else
        m_localTabBar->setTabText(0, windowTitle());
    m_localTabBar->setTabData(0, QVariant::fromValue(reinterpret_cast<quintptr>(this)));
    m_localTabBar->show();
}

void AiAgentDock::hideLocalTabBar()
{
    if (!m_localTabBar)
        return;
    if (m_tabBarForStrip == m_localTabBar)
        salvageStrip();
    while (m_localTabBar->count() > 0)
        m_localTabBar->removeTab(0);
    m_localTabBar->hide();
}

void AiAgentDock::notifySiblingsRefreshStrip()
{
    // Must not run from the destructor: QObjectPrivate::deleteChildren nulls
    // sibling slots before delete, so window()->findChildren recurses into
    // nullptr and hits Q_ASSERT(parent) (qobject.cpp:2149).
    if (QCoreApplication::closingDown())
        return;
    QWidget *win = window();
    if (!win)
        return;
    const auto siblings = win->findChildren<AiAgentDock *>();
    for (auto *d : siblings) {
        if (d == this)
            continue;
        QPointer<AiAgentDock> p(d);
        QTimer::singleShot(0, d, [p]() {
            if (p)
                p->refreshSessionStripPlacement();
        });
    }
}

void AiAgentDock::reinstallStrip()
{
    if (QCoreApplication::closingDown())
        return;
    if (m_slots.isEmpty()) {
        salvageStrip();
        if (m_strip)
            m_strip->hide();
        m_header->hide();
        hideLocalTabBar();
        refreshProjectTooltip();
        return;
    }

    ensureStrip();
    if (!m_strip)
        return;
    pushStripSnapshots();
    m_header->hide();

    int tabIndex = -1;
    QTabBar *bar = hostTabBar(&tabIndex);
    if (bar && tabIndex >= 0) {
        hideLocalTabBar();
        salvageStrip();
        m_headerLayout->removeWidget(m_strip);
        bar->setTabButton(tabIndex, QTabBar::RightSide, m_strip);
        m_tabBarForStrip = bar;
        m_strip->show();
        refreshProjectTooltip();
        return;
    }

    showLocalTabBar();
    salvageStrip();
    m_headerLayout->removeWidget(m_strip);
    m_localTabBar->setTabButton(0, QTabBar::RightSide, m_strip);
    m_tabBarForStrip = m_localTabBar;
    m_strip->show();
    refreshProjectTooltip();
}

void AiAgentDock::pushStripSnapshots()
{
    refreshAgentButton();
    if (!m_strip || m_slots.isEmpty())
        return;
    QVector<AiDockSessionStrip::SlotSnapshot> snaps;
    snaps.reserve(m_slots.size());
    const bool raised = m_onScreen;
    for (int i = 0; i < m_slots.size(); ++i) {
        const Slot &slot = m_slots.at(i);
        AiDockSessionStrip::SlotSnapshot snap;
        snap.agent = resolvedAgentName(slot);
        snap.busy = slotWorking(slot);
        // The open session of a dock on screen shows its own prompt: no dot.
        snap.waiting = slotWaiting(slot) && !(raised && i == m_current);
        snap.busySinceMs = slot.busySinceMs;
        snap.activity = slot.hasActivity;
        snap.lastUserPreview = lastUserPreviewFor(slot.model);
        snaps.append(std::move(snap));
    }
    m_strip->setSlots(m_slots.size(), m_current, snaps);
}

void AiAgentDock::refreshAgentButton()
{
    if (!m_titleBar || m_slots.isEmpty())
        return;
    // The open session's agent, by its configured name (adapters all report
    // "Claude Code", which cannot tell two accounts apart).
    const Slot &open = m_slots.at(qBound(0, m_current, int(m_slots.size()) - 1));
    const AcpAgentDefinition def = slotAgent(open);
    AiDockTitleBar::AgentButton button;
    button.name = def.name;
    button.icon = acpAgentIcon(def, palette());
    button.switchingTo = open.switchingTo;
    QString tip = tr("Agent: %1").arg(def.name);
    if (open.model) {
        const AcpProtocol::AcpAgentInfo &info = open.model->agentInfo();
        const QString adapter = info.title.isEmpty() ? info.name : info.title;
        if (!adapter.isEmpty())
            tip += QLatin1Char('\n') + (info.version.isEmpty() ? adapter : adapter + QLatin1Char(' ') + info.version);
    }
    tip += QLatin1Char('\n') + tr("Click to continue this session with another agent");
    button.tooltip = tip;
    m_titleBar->setAgentButton(button);
}

QTabBar *AiAgentDock::hostTabBar(int *tabIndex) const
{
    if (tabIndex)
        *tabIndex = -1;
    QWidget *win = window();
    if (!win)
        return nullptr;
    const auto bars = win->findChildren<QTabBar *>();
    for (QTabBar *bar : bars) {
        // Skip the singleton stand-in; it lives inside this dock. Prefer the
        // QMainWindow / QDockWidgetGroupWindow bar when Qt actually shows one.
        if (bar == m_localTabBar || isAncestorOf(bar))
            continue;
        // Qt keeps a tabbed-area QTabBar at count==1 with 0 size
        // (updateTabBar returns false when count<=1). That bar still has
        // tabData pointing here — treating it as host hides the local South
        // stand-in and parks the strip on an invisible tab.
        if (!bar->isVisible() || bar->count() <= 1)
            continue;
        for (int i = 0; i < bar->count(); ++i) {
            const QVariant data = bar->tabData(i);
            if (!data.isValid())
                continue;
            auto *widget = reinterpret_cast<QWidget *>(qvariant_cast<quintptr>(data));
            if (widget == this) {
                if (tabIndex)
                    *tabIndex = i;
                return bar;
            }
        }
    }
    return nullptr;
}

void AiAgentDock::markSessionUnattended(const QString &sessionId)
{
    if (Slot *slot = slotById(sessionId); slot && slot->view)
        slot->view->setScheduledUnattended(true);
}

bool AiAgentDock::attachGoalAgent(GoalAgent *goal, const QString &sessionId)
{
    if (!goal)
        return false;
    Slot *slot = sessionId.isEmpty() ? currentSlot() : slotById(sessionId);
    if (!slot)
        return false;
    if (slot->goal && slot->goal->status() == GoalAgent::Active)
        return false;

    if (slot->goal) {
        slot->goal->deleteLater();
        slot->goal = nullptr;
    }

    slot->goal = goal;
    goal->setParent(this);

    if (m_agentManager) {
        goal->setSessionRestarter([mgr = m_agentManager](const QString &oldId) {
            GoalAgent::RestartedSession out;
            out.sessionId = mgr->restartSession(oldId);
            if (out.sessionId.isEmpty())
                return out;
            out.connection = mgr->connectionFor(out.sessionId);
            out.model = mgr->modelFor(out.sessionId);
            return out;
        });
    }

    // View survives rebind(); QPointer drops if the slot is destroyed.
    QPointer<AcpSessionView> view = slot->view;
    goal->setTargetPromptDecorator([view](const QString &text) {
        if (!view)
            return text;
        return view->applyNewWorktreeInstruction(text);
    });
    goal->setTargetHoldProbe([view]() { return view && view->holdsGoalEvaluation(); });
    QPointer<GoalAgent> goalPtr = goal;
    connect(goal, &GoalAgent::goalFollowUpQueued, this, [view, goalPtr](const QString &text) {
        if (view) {
            view->enqueueFollowUp(text, true);
            return;
        }
        if (goalPtr)
            goalPtr->noteDeferredGoalDropped();
    });

    const QString sid = slot->sessionId;
    connect(slot->goal, &GoalAgent::debugLogEntry, this, [this](const QString &entry) {
        m_goalDebugLog.append(entry);
        emit goalDebugLogAppended(entry);
    });
    connect(slot->goal, &GoalAgent::statusChanged, this, [this, sid](GoalAgent::Status s) {
        Slot *target = slotById(sid);
        if (!target)
            return;
        switch (s) {
        case GoalAgent::Active:
            if (target->view && target->goal) {
                target->view->setGoalActive(
                    target->goal->currentCriterionIndex() + 1,
                    target->goal->criteria().size(),
                    0, target->goal->maxIterations());
            }
            if (target->model)
                target->model->appendSystemMessage(tr("⟡ Goal started"));
            break;
        case GoalAgent::Achieved:
            if (target->view)
                target->view->setGoalTerminal(tr("Goal achieved"));
            if (target->model) {
                target->model->appendSystemMessage(
                    tr("✓ Goal achieved: %1").arg(
                        target->goal ? target->goal->lastActionText() : QString()),
                    QLatin1String(kAcpMarkerGoalAchieved));
            }
            break;
        case GoalAgent::Cancelled: {
            const QString handback = target->goal ? target->goal->lastActionText() : QString();
            const bool unmet = GoalActionParser::isUnmetComplete(handback);
            if (target->view)
                target->view->setGoalTerminal(tr("Goal stopped"));
            if (target->model) {
                target->model->appendSystemMessage(unmet
                    ? tr("⊘ Goal stopped, criterion unmet: %1").arg(handback)
                    : tr("⊘ Goal cancelled"));
            }
            break;
        }
        case GoalAgent::Failed:
            if (target->view)
                target->view->setGoalTerminal(tr("Goal failed"));
            if (target->model) {
                const QString reason = target->goal ? target->goal->lastActionText() : QString();
                target->model->appendSystemMessage(reason.isEmpty()
                    ? tr("✗ Goal failed")
                    : tr("✗ Goal failed: %1").arg(reason));
            }
            break;
        default:
            break;
        }
        syncSlotBusy(*target);
    });
    connect(slot->goal, &GoalAgent::iterationChanged, this, [this, sid](int critIdx, int iter) {
        Slot *target = slotById(sid);
        if (!target || !target->model)
            return;
        target->model->appendSystemMessage(tr("⟡ Goal: criterion %1, iteration %2/%3")
            .arg(critIdx + 1).arg(iter).arg(target->goal ? target->goal->maxIterations() : 0));
        if (target->view && target->goal) {
            target->view->setGoalActive(
                critIdx + 1,
                target->goal->criteria().size(),
                iter, target->goal->maxIterations());
        }
    });
    connect(slot->goal, &GoalAgent::criterionAdvanced, this, [this, sid](int newIdx) {
        Slot *target = slotById(sid);
        if (!target || !target->model)
            return;
        target->model->appendSystemMessage(tr("⟡ Goal: advancing to criterion %1/%2")
            .arg(newIdx + 1).arg(target->goal ? target->goal->criteria().size() : 0));
        if (target->view && target->goal) {
            target->view->setGoalActive(
                newIdx + 1,
                target->goal->criteria().size(),
                0, target->goal->maxIterations());
        }
    });
    return true;
}

void AiAgentDock::sendWithGoal()
{
    Slot *slot = slotForView(qobject_cast<AcpSessionView *>(sender()));
    if (!slot)
        slot = currentSlot();
    if (!slot)
        return;

    const QString id = slot->sessionId;
    if (slot->goal && slot->goal->status() == GoalAgent::Active) {
        QMessageBox::information(this, tr("Send with Goal"),
                                 tr("A goal is already active on this session. "
                                    "Stop the current goal before starting a new one."));
        return;
    }

    SendWithGoalDialog dlg(m_registry, m_appSettings, this);
    const bool remote = slot->connection && slot->connection->executionContext()
        && slot->connection->executionContext()->isRemote();
    if (ProjectGoalPresets::projectScopeAvailable(m_workingDirectory, remote))
        dlg.setProjectRoot(m_workingDirectory);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const auto res = dlg.goalResult();
    if (res.successCriteriaList.isEmpty())
        return;
    if (const QString error = startGoal(id, res); !error.isEmpty())
        QMessageBox::information(this, tr("Send with Goal"), error);
}

QString AiAgentDock::startGoal(const QString &id, const SendWithGoalResult &res)
{
    Slot *slot = slotById(id);
    if (!slot)
        return tr("The session is gone.");
    if (slot->goal && slot->goal->status() == GoalAgent::Active)
        return tr("A goal is already active on this session. Stop the current goal before starting a new one.");
    if (res.successCriteriaList.isEmpty())
        return tr("The goal has no success criteria.");

    // Peek first so Attach leaves the composer intact (no stacked send, no
    // dropped follow-up text/images while a turn is in flight).
    QString composerText;
    QVector<QPair<QByteArray, QString>> composerImages;
    if (slot->view) {
        composerText = slot->view->peekInputText();
        composerImages = slot->view->peekInputImages();
    }
    const bool hasComposer = !composerText.isEmpty() || !composerImages.isEmpty();
    const bool processing = slot->model && slot->model->isProcessing();
    const bool hasHistory = slot->model && !slot->model->isEmpty();
    const auto action = GoalAgent::launchAction(hasComposer, hasHistory, processing);
    if (action == GoalAgent::LaunchAction::NeedComposer)
        return tr("Type a message before sending with a goal.");

    const bool attach = action == GoalAgent::LaunchAction::Attach;
    if (res.useNativeGoal) {
        const QStringList goalCommands = GoalAgent::nativeGoalCommands(res.successCriteriaList);
        if (goalCommands.isEmpty())
            return tr("The goal has no success criteria.");
        if (!attach && slot->view) {
            composerText = slot->view->takeInputText();
            composerImages = slot->view->takeInputImages();
        }
        Slot *live = slotById(id);
        if (!live || !live->connection || !live->model) {
            if (!attach) {
                live = slotById(id);
                if (live && live->view)
                    live->view->insertTextToInput(composerText);
            }
            return tr("The session has no agent connection.");
        }
        live->nativeAutoCompact = res.autoCompact;
        if (!attach) {
            QList<QPair<QByteArray, QString>> imageList;
            imageList.reserve(composerImages.size());
            for (const auto &p : composerImages)
                imageList.append(p);
            if (live->view)
                live->view->noteUserSend();
            live->model->appendUserMessage(composerText, composerImages);
            const QString wireText = live->view
                                         ? live->view->applyNewWorktreeInstruction(composerText)
                                         : composerText;
            live->connection->sendPrompt(wireText, imageList);
        }
        // Re-read processing each command: SendNow opens a turn, so the next
        // /goal must queue instead of stopping that turn.
        for (const QString &goalCommand : goalCommands) {
            if (GoalAgent::mustQueueGoalFollowUp(live->model->isProcessing(),
                                                 live->model->agentAdvertisesGoalCommand(),
                                                 goalCommand)) {
                if (live->view)
                    live->view->enqueueFollowUp(goalCommand, false);
                continue;
            }
            live->model->appendUserMessage(goalCommand, {});
            const QString wireGoal = live->view
                                         ? live->view->applyNewWorktreeInstruction(goalCommand)
                                         : goalCommand;
            live->connection->sendSidePrompt(wireGoal);
        }
        emit inputFocused();
        return QString();
    }

    if (!attach && slot->view) {
        composerText = slot->view->takeInputText();
        composerImages = slot->view->takeInputImages();
    }

    auto *goal = new GoalAgent(m_agentManager, m_appSettings, this);
    goal->setTargetSession(slot->connection, slot->model);
    attachGoalAgent(goal, id);

    GoalAgent::StartRequest req;
    req.targetSessionId = id;
    req.successCriteriaList = res.successCriteriaList;
    req.agentId = res.agentId;
    req.maxIterations = res.maxIterations;
    req.promptTemplateId = res.promptTemplateId;
    req.autoCompact = res.autoCompact;
    req.prefixGoal = res.prefixGoal;
    req.originalUserMessage = attach ? QString() : composerText;
    req.attachToExistingConversation = attach;

    Slot *attached = slotById(id);
    if (!attached || !attached->goal || !attached->goal->start(req)) {
        attached = slotById(id);
        if (attached && attached->goal) {
            attached->goal->deleteLater();
            attached->goal = nullptr;
        }
        if (!attach) {
            attached = slotById(id);
            if (attached && attached->view)
                attached->view->insertTextToInput(composerText);
        }
        return tr("Failed to start goal. Check that the target session "
                  "is connected and the goal-agent is available.");
    }

    if (attach) {
        emit inputFocused();
        return QString();
    }

    QList<QPair<QByteArray, QString>> imageList;
    imageList.reserve(composerImages.size());
    for (const auto &p : composerImages)
        imageList.append(p);
    if (attached->view)
        attached->view->noteUserSend();
    attached->model->appendUserMessage(composerText, composerImages);
    const QString wireText = attached->view
                                 ? attached->view->applyNewWorktreeInstruction(composerText)
                                 : composerText;
    attached->connection->sendPrompt(wireText, imageList);
    if (res.prefixGoal && attached->goal)
        attached->goal->sendFirstCriterionGoal();
    emit inputFocused();
    return QString();
}
