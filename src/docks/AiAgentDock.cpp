/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#include "AcpConnection.h"
#include "AcpSessionModel.h"
#include "widgets/AcpSessionView.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <Qt>

AiAgentDock::AiAgentDock(QString sessionId,
                         QString agentName,
                         QString workingDirectory,
                         AcpSessionModel *model,
                         AcpConnection *connection,
                         AcpAgentRegistry *registry,
                         QWidget *parent)
    : QDockWidget(parent)
    , m_sessionId(std::move(sessionId))
    , m_agentName(std::move(agentName))
    , m_workingDirectory(std::move(workingDirectory))
    , m_model(model)
    , m_connection(connection)
    , m_registry(registry)
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
    }
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

AiAgentDock::~AiAgentDock() = default;

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
    if (m_model && !m_model->agentInfo().name.isEmpty()) {
        resolvedAgentName = m_model->agentInfo().name;
    }

    if (resolvedAgentName.isEmpty()) {
        setWindowTitle(tr("AI — %1").arg(workspaceName));
    } else {
        setWindowTitle(tr("AI — %1 (%2)").arg(workspaceName, resolvedAgentName));
    }
}
