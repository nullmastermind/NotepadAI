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

#include "AcpAgentManager.h"
#include "AcpConnection.h"
#include "AcpSessionModel.h"
#include "AiDockGroup.h"
#include "ApplicationSettings.h"
#include "GoalAgent.h"
#include "dialogs/GoalDraftDialog.h"
#include "dialogs/SendWithGoalDialog.h"
#include "widgets/AcpSessionView.h"
#include "widgets/AiDockSessionStrip.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
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
    addSlot(std::move(sessionId), std::move(agentName), model, connection, true);

    connect(this, &QDockWidget::topLevelChanged, this, [this](bool) {
        scheduleStripReinstall();
    });
    connect(this, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea) {
        scheduleStripReinstall();
    });
    connect(this, &QDockWidget::visibilityChanged, this, [this](bool) {
        // Hidden docks must reinstall too: tabify rebuilds the area bar and
        // the inactive project's [1][2] live on that bar, not on this widget.
        scheduleStripReinstall();
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
    if (slot && slot->goal && slot->goal->status() == GoalAgent::Active)
        slot->goal->stop();
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
}

AiAgentDock::~AiAgentDock()
{
    for (Slot &slot : m_slots)
        destroySlotGoal(slot);

    for (Slot &slot : m_slots)
        unwireSlot(slot);
}

void AiAgentDock::setActivityIndicator(bool active)
{
    if (active) {
        if (!m_slots.isEmpty()) {
            if (m_current != m_slots.size() - 1)
                m_slots.last().hasActivity = true;
            else if (!isVisible())
                m_slots[m_current].hasActivity = true;
        }
    } else if (Slot *slot = currentSlot()) {
        slot->hasActivity = false;
    }
    refreshActivityIcon();
}

bool AiAgentDock::shouldShowActivityIcon() const
{
    const bool raised = isVisible();
    for (int i = 0; i < m_slots.size(); ++i) {
        if (!m_slots.at(i).hasActivity)
            continue;
        if (!raised || i != m_current)
            return true;
    }
    return false;
}

void AiAgentDock::refreshActivityIcon()
{
    if (shouldShowActivityIcon()) {
        QPixmap px(8, 8);
        px.fill(Qt::transparent);
        QPainter painter(&px);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Highlight));
        painter.drawEllipse(0, 0, 8, 8);
        painter.end();
        setWindowIcon(QIcon(px));
    } else {
        setWindowIcon(QIcon());
    }
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
    return m_slots.isEmpty() ? QString() : m_slots.last().sessionId;
}

QString AiAgentDock::sessionIdAt(int index) const
{
    if (index < 0 || index >= m_slots.size())
        return {};
    return m_slots.at(index).sessionId;
}

void AiAgentDock::switchTo(int index)
{
    if (index < 0 || index >= m_slots.size())
        return;
    m_current = index;
    m_stack->setCurrentIndex(index);
    m_slots[index].hasActivity = false;
    pushStripSnapshots();
    refreshActivityIcon();
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
    connect(m_strip, &QObject::destroyed, this, [this]() {
        m_strip = nullptr;
        m_tabBarForStrip = nullptr;
        m_stripTabIndex = -1;
        scheduleStripReinstall();
    });
    m_headerLayout->addWidget(m_strip);
}

void AiAgentDock::salvageStrip()
{
    if (!m_strip)
        return;
    if (m_tabBarForStrip) {
        if (m_stripTabIndex >= 0 && m_stripTabIndex < m_tabBarForStrip->count()
            && m_tabBarForStrip->tabButton(m_stripTabIndex, QTabBar::RightSide) == m_strip) {
            m_tabBarForStrip->setTabButton(m_stripTabIndex, QTabBar::RightSide, nullptr);
        }
        m_tabBarForStrip = nullptr;
        m_stripTabIndex = -1;
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
        m_stripTabIndex = tabIndex;
        m_strip->show();
        refreshProjectTooltip();
        return;
    }

    showLocalTabBar();
    salvageStrip();
    m_headerLayout->removeWidget(m_strip);
    m_localTabBar->setTabButton(0, QTabBar::RightSide, m_strip);
    m_tabBarForStrip = m_localTabBar;
    m_stripTabIndex = 0;
    m_strip->show();
    refreshProjectTooltip();
}

void AiAgentDock::pushStripSnapshots()
{
    if (!m_strip || m_slots.isEmpty())
        return;
    QVector<AiDockSessionStrip::SlotSnapshot> snaps;
    snaps.reserve(m_slots.size());
    for (const Slot &slot : m_slots) {
        AiDockSessionStrip::SlotSnapshot snap;
        snap.agent = resolvedAgentName(slot);
        snap.busy = slotBusy(slot);
        snap.busySinceMs = slot.busySinceMs;
        snap.activity = slot.hasActivity;
        snap.lastUserPreview = lastUserPreviewFor(slot.model);
        snaps.append(std::move(snap));
    }
    m_strip->setSlots(m_slots.size(), m_current, snaps);
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
                target->model->appendSystemMessage(tr("✓ Goal achieved: %1").arg(
                    target->goal ? target->goal->lastActionText() : QString()));
            }
            break;
        case GoalAgent::Cancelled:
            if (target->view)
                target->view->setGoalTerminal(tr("Goal stopped"));
            if (target->model)
                target->model->appendSystemMessage(tr("⊘ Goal cancelled"));
            break;
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
    if (dlg.exec() != QDialog::Accepted)
        return;

    const auto res = dlg.goalResult();
    if (res.successCriteriaList.isEmpty())
        return;

    slot = slotById(id);
    if (!slot)
        return;

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
    if (action == GoalAgent::LaunchAction::NeedComposer) {
        QMessageBox::information(this, tr("Send with Goal"),
                                 tr("Type a message before sending with a goal."));
        return;
    }

    const bool attach = action == GoalAgent::LaunchAction::Attach;
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
    req.originalUserMessage = attach ? QString() : composerText;
    req.attachToExistingConversation = attach;

    Slot *attached = slotById(id);
    if (!attached || !attached->goal || !attached->goal->start(req)) {
        QMessageBox::warning(this, tr("Send with Goal"),
                             tr("Failed to start goal. Check that the target session "
                                "is connected and the goal-agent is available."));
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
        return;
    }

    if (attach) {
        emit inputFocused();
        return;
    }

    QList<QPair<QByteArray, QString>> imageList;
    imageList.reserve(composerImages.size());
    for (const auto &p : composerImages)
        imageList.append(p);
    attached->model->appendUserMessage(composerText, composerImages);
    const QString wireText = attached->view
                                 ? attached->view->applyNewWorktreeInstruction(composerText)
                                 : composerText;
    attached->connection->sendPrompt(wireText, imageList);
    emit inputFocused();
}
