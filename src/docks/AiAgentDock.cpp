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
#include "ApplicationSettings.h"
#include "GoalAgent.h"
#include "dialogs/GoalDraftDialog.h"
#include "dialogs/SendWithGoalDialog.h"
#include "widgets/AcpSessionView.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <Qt>

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
    , m_sessionId(std::move(sessionId))
    , m_agentName(std::move(agentName))
    , m_workingDirectory(std::move(workingDirectory))
    , m_model(model)
    , m_connection(connection)
    , m_registry(registry)
    , m_agentManager(agentManager)
    , m_appSettings(appSettings)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    setObjectName(QStringLiteral("AiAgentDock_%1").arg(m_sessionId));
    // Spec ("Default dock area"): dock is unrestricted — user may move it to
    // any side. defaultArea() is only consulted on first attach.
    setAllowedAreas(Qt::AllDockWidgetAreas);

    auto *view = new AcpSessionView(m_model, m_connection, m_registry, this);
    m_view = view;
    setWidget(view);

    refreshTitle();

    if (m_model) {
        connect(m_model, &AcpSessionModel::metadataChanged,
                this, &AiAgentDock::onMetadataChanged);
    }
    wireConnectionSignals();
}

void AiAgentDock::wireConnectionSignals()
{
    if (m_connection) {
        connect(m_connection, &AcpConnection::agentExited,
                this, &AiAgentDock::onAgentExited);
    }
    if (m_view) {
        connect(m_view, &AcpSessionView::retryRequested,
                this, &AiAgentDock::onRetryFromView,
                Qt::UniqueConnection);
        connect(m_view, &AcpSessionView::restartSessionRequested,
                this, &AiAgentDock::onRestartFromView,
                Qt::UniqueConnection);
        connect(m_view, &AcpSessionView::sendWithGoalRequested,
                this, &AiAgentDock::sendWithGoal,
                Qt::UniqueConnection);
        connect(m_view, &AcpSessionView::generatePromptWithGoalRequested,
                this, &AiAgentDock::generatePromptWithGoal,
                Qt::UniqueConnection);
        // NOTE: both of these MUST target a member-function slot, not a lambda.
        // Qt::UniqueConnection silently refuses functor/lambda slots
        // (QObject::connectImpl returns an empty Connection for them), so a
        // lambda here would never actually be wired up and the goal would keep
        // running. Route the goal-row Stop button and the composer Cancel
        // button into the same member slot.
        connect(m_view, &AcpSessionView::goalStopRequested,
                this, &AiAgentDock::stopGoalAgentIfActive,
                Qt::UniqueConnection);
        connect(m_view, &AcpSessionView::cancelRequested,
                this, &AiAgentDock::stopGoalAgentIfActive,
                Qt::UniqueConnection);
        connect(m_view, &AcpSessionView::inputFocused,
                this, &AiAgentDock::inputFocused,
                Qt::UniqueConnection);
    }
}

void AiAgentDock::stopGoalAgentIfActive()
{
    if (m_goalAgent && m_goalAgent->status() == GoalAgent::Active) {
        m_goalAgent->stop();
    }
}

void AiAgentDock::generatePromptWithGoal()
{
    if (!m_view)
        return;

    GoalDraftDialog dlg(m_agentManager,
                        m_registry,
                        m_appSettings,
                        m_model,
                        m_connection ? m_connection->workingDirectory() : m_workingDirectory,
                        m_connection ? m_connection->executionContext() : nullptr,
                        this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString generated = dlg.generatedText().trimmed();
    if (!generated.isEmpty())
        m_view->insertTextToInput(generated);
}

void AiAgentDock::onAgentExited(int exitCode, QProcess::ExitStatus status)
{
    Q_UNUSED(status);
    m_agentExited = true;
    if (m_view) {
        m_view->setBanner(
            tr("Agent exited (code %1). Click Restart to start a new session.").arg(exitCode),
            AcpSessionView::BannerKind::Error);
    }
}

void AiAgentDock::onRetryFromView()
{
    if (m_agentExited) {
        // Defer to the manager — it allocates a new id and rebinds us.
        emit restartRequested(m_sessionId);
    }
    // Non-exit retries (auth flow) are still consumed by retryRequested but
    // not handled here yet — auth retry would be wired in by future work.
}

void AiAgentDock::onRestartFromView()
{
    if (m_restartDialogShowing) return;

    // Mirror closeEvent's safety net: a restart is destructive to the running
    // turn, so confirm if the agent is mid-prompt. The manager tears down the
    // old connection during rebind, which would cancel the in-flight call.
    if (m_model && m_model->isProcessing()) {
        m_restartDialogShowing = true;
        const QMessageBox::StandardButton choice = QMessageBox::question(
            this,
            tr("AI Agent"),
            tr("A prompt is still running. Restart the session anyway?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        m_restartDialogShowing = false;
        if (choice != QMessageBox::Yes) {
            return;
        }
        if (m_connection) {
            m_connection->cancelPrompt();
        }
    }
    emit restartRequested(m_sessionId);
}

void AiAgentDock::rebind(AcpConnection *connection,
                         AcpSessionModel *model,
                         QString newSessionId,
                         QString newAgentName)
{
    // Disconnect from the old connection — the manager has already (or will)
    // tear it down. Disconnecting here defends against late queued signals.
    if (m_connection) {
        disconnect(m_connection, nullptr, this, nullptr);
    }
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }

    m_sessionId = std::move(newSessionId);
    m_agentName = std::move(newAgentName);
    m_connection = connection;
    m_model = model;
    m_agentExited = false;

    setObjectName(QStringLiteral("AiAgentDock_%1").arg(m_sessionId));

    if (m_view) {
        m_view->rebind(model, connection);
    }

    if (m_model) {
        connect(m_model, &AcpSessionModel::metadataChanged,
                this, &AiAgentDock::onMetadataChanged);
    }
    wireConnectionSignals();
    refreshTitle();
}

AiAgentDock::~AiAgentDock()
{
    // GoalAgent is a direct child of this dock, but the view (m_view) is a
    // grandchild (reparented into QDockWidget's internal container by
    // setWidget()). QWidget::~QWidget::deleteChildren iterates forward, so the
    // container (and thus the view) is destroyed BEFORE GoalAgent. If GoalAgent
    // is active, its destructor emits statusChanged which our lambda delivers
    // to the already-freed view → use-after-free. Destroy it here first.
    delete m_goalAgent;
    m_goalAgent = nullptr;

    // Disconnect non-owning pointers so no late signals arrive during the
    // remainder of the destructor chain.
    if (m_connection) {
        disconnect(m_connection, nullptr, this, nullptr);
    }
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }
}

void AiAgentDock::setActivityIndicator(bool active)
{
    if (m_hasActivity == active) {
        return;
    }
    m_hasActivity = active;
    if (active) {
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
    if (m_model && m_model->isProcessing())
        return true;
    if (m_goalAgent && m_goalAgent->status() == GoalAgent::Active)
        return true;
    return false;
}

QSize AiAgentDock::sizeHint() const
{
    return QSize(600, 400);
}

void AiAgentDock::closeEvent(QCloseEvent *event)
{
    if (m_model && m_model->isProcessing()) {
        if (!confirmCloseWhileRunning()) {
            event->ignore();
            return;
        }
        if (m_connection) {
            m_connection->cancelPrompt();
        }
    }
    QDockWidget::closeEvent(event);
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
    refreshTitle();
}

void AiAgentDock::refreshTitle()
{
    const QString cleaned = QDir::cleanPath(m_workingDirectory);
    QString workspaceName = QFileInfo(cleaned).fileName();
    if (workspaceName.isEmpty()) {
        workspaceName = m_workingDirectory;
    }

    QString resolvedAgentName = m_agentName;
    if (m_model) {
        const auto &info = m_model->agentInfo();
        if (!info.title.isEmpty())
            resolvedAgentName = info.title;
        else if (!info.name.isEmpty())
            resolvedAgentName = info.name;
    }

    if (resolvedAgentName.isEmpty()) {
        setWindowTitle(tr("AI — %1").arg(workspaceName));
    } else {
        setWindowTitle(tr("AI — %1 (%2)").arg(workspaceName, resolvedAgentName));
    }
}

void AiAgentDock::insertTextToInput(const QString &text)
{
    if (m_view) {
        m_view->insertTextToInput(text);
    }
}

bool AiAgentDock::attachGoalAgent(GoalAgent *goal)
{
    if (!goal) return false;
    if (m_goalAgent && m_goalAgent->status() == GoalAgent::Active)
        return false;

    if (m_goalAgent) {
        m_goalAgent->deleteLater();
        m_goalAgent = nullptr;
    }

    m_goalAgent = goal;
    goal->setParent(this);

    connect(m_goalAgent, &GoalAgent::debugLogEntry, this, [this](const QString &entry) {
        m_goalDebugLog.append(entry);
        emit goalDebugLogAppended(entry);
    });
    connect(m_goalAgent, &GoalAgent::statusChanged, this, [this](GoalAgent::Status s) {
        // The view's goal-status update drives both the on-screen status row
        // AND the input's busy-placeholder clock (m_goalRunning), so it MUST
        // run on every transition independent of m_model — otherwise a goal
        // that reaches a terminal status while the model is detached (mid
        // rebind) would leave the placeholder stuck at "Agent is working…".
        // Only the appendSystemMessage transcript notes are gated on m_model.
        switch (s) {
        case GoalAgent::Active:
            if (m_view && m_goalAgent) {
                m_view->setGoalActive(
                    m_goalAgent->currentCriterionIndex() + 1,
                    m_goalAgent->criteria().size(),
                    0, m_goalAgent->maxIterations());
            }
            if (m_model) m_model->appendSystemMessage(tr("⟡ Goal started"));
            break;
        case GoalAgent::Achieved:
            if (m_view) m_view->setGoalTerminal(tr("Goal achieved"));
            if (m_model) m_model->appendSystemMessage(tr("✓ Goal achieved: %1").arg(
                m_goalAgent ? m_goalAgent->lastActionText() : QString()));
            break;
        case GoalAgent::Cancelled:
            if (m_view) m_view->setGoalTerminal(tr("Goal stopped"));
            if (m_model) m_model->appendSystemMessage(tr("⊘ Goal cancelled"));
            break;
        case GoalAgent::Failed:
            if (m_view) m_view->setGoalTerminal(tr("Goal failed"));
            if (m_model) m_model->appendSystemMessage(tr("✗ Goal failed"));
            break;
        default:
            break;
        }
    });
    connect(m_goalAgent, &GoalAgent::iterationChanged, this, [this](int critIdx, int iter) {
        if (!m_model) return;
        m_model->appendSystemMessage(tr("⟡ Goal: criterion %1, iteration %2/%3")
            .arg(critIdx + 1).arg(iter).arg(m_goalAgent ? m_goalAgent->maxIterations() : 0));
        if (m_view && m_goalAgent) {
            m_view->setGoalActive(
                critIdx + 1,
                m_goalAgent->criteria().size(),
                iter, m_goalAgent->maxIterations());
        }
    });
    connect(m_goalAgent, &GoalAgent::criterionAdvanced, this, [this](int newIdx) {
        if (!m_model) return;
        m_model->appendSystemMessage(tr("⟡ Goal: advancing to criterion %1/%2")
            .arg(newIdx + 1).arg(m_goalAgent ? m_goalAgent->criteria().size() : 0));
        if (m_view && m_goalAgent) {
            m_view->setGoalActive(
                newIdx + 1,
                m_goalAgent->criteria().size(),
                0, m_goalAgent->maxIterations());
        }
    });
    return true;
}

void AiAgentDock::sendWithGoal()
{
    if (m_goalAgent && m_goalAgent->status() == GoalAgent::Active) {
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

    // Capture composer text BEFORE starting the goal — if empty, abort early
    // without spawning the judge process.
    QString composerText;
    QVector<QPair<QByteArray, QString>> composerImages;
    if (m_view) {
        composerText = m_view->takeInputText();
        composerImages = m_view->takeInputImages();
    }
    if (composerText.isEmpty() && composerImages.isEmpty()) {
        QMessageBox::information(this, tr("Send with Goal"),
                                 tr("Type a message before sending with a goal."));
        return;
    }

    auto *goal = new GoalAgent(m_agentManager, m_appSettings, this);
    goal->setTargetSession(m_connection, m_model);
    attachGoalAgent(goal);

    GoalAgent::StartRequest req;
    req.targetSessionId = m_sessionId;
    req.successCriteriaList = res.successCriteriaList;
    req.agentId = res.agentId;
    req.maxIterations = res.maxIterations;
    req.promptTemplateId = res.promptTemplateId;
    req.originalUserMessage = composerText;

    if (!m_goalAgent->start(req)) {
        QMessageBox::warning(this, tr("Send with Goal"),
                             tr("Failed to start goal. Check that the target session "
                                "is connected and the goal-agent is available."));
        m_goalAgent->deleteLater();
        m_goalAgent = nullptr;
        // Restore the text we took from the composer.
        if (m_view) {
            m_view->insertTextToInput(composerText);
        }
        return;
    }

    // Goal started — now send the composer text to the target session.
    // The goal agent's promptEnded subscription is already wired, so it will
    // catch the response.
    QList<QPair<QByteArray, QString>> imageList;
    imageList.reserve(composerImages.size());
    for (const auto &p : composerImages) imageList.append(p);
    m_model->appendUserMessage(composerText, composerImages);
    m_connection->sendPrompt(composerText, imageList);
    emit inputFocused();
}
