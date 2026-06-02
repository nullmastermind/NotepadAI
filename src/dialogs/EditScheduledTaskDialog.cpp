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

#include "EditScheduledTaskDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include "AcpAgentRegistry.h"
#include "ApplicationSettings.h"
#include "CronExpression.h"
#include "GoalAgentConfigDialog.h"
#include "NotepadNextApplication.h"
#include "ai/CredentialStore.h"
#include "ai/LlmHttpClient.h"
#include "remote/SshProfile.h"

EditScheduledTaskDialog::EditScheduledTaskDialog(const ScheduledTaskDefinition &def,
                                                 AcpAgentRegistry *agentRegistry,
                                                 ApplicationSettings *settings,
                                                 const QStringList &recentWorkspaces,
                                                 QWidget *parent)
    : QDialog(parent)
    , m_agentRegistry(agentRegistry)
    , m_settings(settings)
    , m_original(def)
{
    setWindowTitle(def.id.isEmpty() ? tr("Add Scheduled Task") : tr("Edit Scheduled Task"));
    resize(520, 480);

    auto *mainLayout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    // Name
    m_nameEdit = new QLineEdit(def.name, this);
    form->addRow(tr("Name:"), m_nameEdit);

    // Cron expression + helper button
    auto *cronLayout = new QHBoxLayout();
    m_cronEdit = new QLineEdit(def.cron, this);
    m_cronEdit->setPlaceholderText(QStringLiteral("*/5 * * * *"));
    m_cronHelperBtn = new QPushButton(tr("AI"), this);
    m_cronHelperBtn->setToolTip(tr("Generate cron from natural language"));
    m_cronHelperBtn->setFixedWidth(40);
    cronLayout->addWidget(m_cronEdit);
    cronLayout->addWidget(m_cronHelperBtn);
    form->addRow(tr("Cron:"), cronLayout);

    // Cron preview
    m_cronPreviewLabel = new QLabel(this);
    m_cronPreviewLabel->setWordWrap(true);
    form->addRow(QString(), m_cronPreviewLabel);

    // Agent
    m_agentCombo = new QComboBox(this);
    const auto agents = m_agentRegistry->agents();
    for (const auto &agent : agents) {
        m_agentCombo->addItem(agent.name, agent.id);
    }
    int agentIdx = m_agentCombo->findData(def.agentId);
    if (agentIdx >= 0) m_agentCombo->setCurrentIndex(agentIdx);
    form->addRow(tr("Agent:"), m_agentCombo);

    // Workspace
    auto *wsLayout = new QHBoxLayout();
    m_workspaceCombo = new QComboBox(this);
    m_workspaceCombo->setEditable(true);
    for (const QString &ws : recentWorkspaces) {
        if (!ws.isEmpty())
            m_workspaceCombo->addItem(
                remote::isSshUri(ws) ? ws : QDir::toNativeSeparators(ws), ws);
    }
    if (!def.cwd.isEmpty()) {
        if (m_workspaceCombo->findData(def.cwd) < 0)
            m_workspaceCombo->insertItem(0,
                remote::isSshUri(def.cwd) ? def.cwd : QDir::toNativeSeparators(def.cwd),
                def.cwd);
        m_workspaceCombo->setCurrentIndex(m_workspaceCombo->findData(def.cwd));
    }
    m_browseBtn = new QPushButton(tr("..."), this);
    m_browseBtn->setFixedWidth(30);
    wsLayout->addWidget(m_workspaceCombo, 1);
    wsLayout->addWidget(m_browseBtn);
    form->addRow(tr("Workspace:"), wsLayout);

    // Prompt
    m_promptEdit = new QPlainTextEdit(def.prompt, this);
    m_promptEdit->setMinimumHeight(80);
    form->addRow(tr("Prompt:"), m_promptEdit);

    // Skip if running
    m_skipIfRunningCheck = new QCheckBox(tr("Skip if already running"), this);
    m_skipIfRunningCheck->setChecked(def.skipIfRunning);
    form->addRow(QString(), m_skipIfRunningCheck);

    // Timeout
    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(0, 1440);
    m_timeoutSpin->setSuffix(tr(" min"));
    m_timeoutSpin->setSpecialValueText(tr("No timeout"));
    m_timeoutSpin->setValue(def.timeoutMinutes);
    form->addRow(tr("Timeout:"), m_timeoutSpin);

    mainLayout->addLayout(form);

    // Goal section
    auto *goalRow = new QHBoxLayout();
    m_goalCheck = new QCheckBox(tr("Enable Goal Agent evaluation"), this);
    m_goalCheck->setChecked(def.hasGoalConfig);
    goalRow->addWidget(m_goalCheck);
    m_goalConfigBtn = new QPushButton(tr("Configure..."), this);
    m_goalConfigBtn->setEnabled(def.hasGoalConfig);
    goalRow->addWidget(m_goalConfigBtn);
    goalRow->addStretch();
    mainLayout->addLayout(goalRow);

    if (def.hasGoalConfig)
        m_goalConfigData = def.goalConfig;

    connect(m_goalCheck, &QCheckBox::toggled, m_goalConfigBtn, &QPushButton::setEnabled);
    connect(m_goalConfigBtn, &QPushButton::clicked, this, [this]() {
        GoalAgentConfigDialog dlg(m_goalConfigData, m_agentRegistry, m_settings, this);
        if (dlg.exec() == QDialog::Accepted)
            m_goalConfigData = dlg.goalConfig();
    });

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_saveBtn = new QPushButton(tr("Save"), this);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(cancelBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_saveBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_cronHelperBtn, &QPushButton::clicked, this, &EditScheduledTaskDialog::onCronHelperClicked);
    connect(m_browseBtn, &QPushButton::clicked, this, &EditScheduledTaskDialog::onBrowseWorkspace);

    // Cron preview debounce
    m_cronDebounce = new QTimer(this);
    m_cronDebounce->setSingleShot(true);
    m_cronDebounce->setInterval(300);
    connect(m_cronDebounce, &QTimer::timeout, this, &EditScheduledTaskDialog::updateCronPreview);
    connect(m_cronEdit, &QLineEdit::textChanged, this, &EditScheduledTaskDialog::onCronTextChanged);

    // Validation
    connect(m_nameEdit, &QLineEdit::textChanged, this, &EditScheduledTaskDialog::validate);
    connect(m_cronEdit, &QLineEdit::textChanged, this, &EditScheduledTaskDialog::validate);
    connect(m_promptEdit, &QPlainTextEdit::textChanged, this, &EditScheduledTaskDialog::validate);

    validate();
    updateCronPreview();
}

void EditScheduledTaskDialog::onCronTextChanged()
{
    m_cronDebounce->start();
}

void EditScheduledTaskDialog::updateCronPreview()
{
    const QString expr = m_cronEdit->text().trimmed();
    if (expr.isEmpty()) {
        m_cronPreviewLabel->clear();
        return;
    }

    auto parsed = CronExpression::parse(expr);
    if (!parsed || !parsed->isValid()) {
        m_cronPreviewLabel->setText(tr("Invalid cron expression"));
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const auto times = parsed->nextFireTimes(now, 5);
    QStringList lines;
    lines.reserve(times.size());
    for (const QDateTime &t : times) {
        lines.append(t.toString(QStringLiteral("yyyy-MM-dd hh:mm")));
    }
    m_cronPreviewLabel->setText(tr("Next: %1").arg(lines.join(QStringLiteral(", "))));
}

void EditScheduledTaskDialog::onBrowseWorkspace()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Workspace"));
    if (!dir.isEmpty()) {
        m_workspaceCombo->setCurrentText(dir);
    }
}

void EditScheduledTaskDialog::validate()
{
    const bool valid = !m_nameEdit->text().trimmed().isEmpty()
                    && CronExpression::parse(m_cronEdit->text().trimmed()).has_value()
                    && !m_promptEdit->toPlainText().trimmed().isEmpty();
    m_saveBtn->setEnabled(valid);
}

ScheduledTaskDefinition EditScheduledTaskDialog::taskResult() const
{
    ScheduledTaskDefinition def = m_original;
    if (def.id.isEmpty()) {
        def.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        def.createdAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    }
    def.name = m_nameEdit->text().trimmed();
    def.cron = m_cronEdit->text().trimmed();
    def.agentId = m_agentCombo->currentData().toString();
    def.cwd = m_workspaceCombo->currentData().toString();
    if (def.cwd.isEmpty())
        def.cwd = m_workspaceCombo->currentText().trimmed();
    def.prompt = m_promptEdit->toPlainText().trimmed();
    def.skipIfRunning = m_skipIfRunningCheck->isChecked();
    def.timeoutMinutes = m_timeoutSpin->value();

    def.hasGoalConfig = m_goalCheck->isChecked();
    if (def.hasGoalConfig) {
        def.goalConfig = m_goalConfigData;
    } else {
        def.goalConfig = ScheduledTaskGoalConfig{};
    }

    return def;
}

bool EditScheduledTaskDialog::editTask(ScheduledTaskDefinition &def,
                                       AcpAgentRegistry *agentRegistry,
                                       ApplicationSettings *settings,
                                       QWidget *parent,
                                       const QStringList &recentWorkspaces)
{
    EditScheduledTaskDialog dlg(def, agentRegistry, settings, recentWorkspaces, parent);
    if (dlg.exec() == QDialog::Accepted) {
        def = dlg.taskResult();
        return true;
    }
    return false;
}

void EditScheduledTaskDialog::onCronHelperClicked()
{
    if (!m_settings->commitMessageApiKeyConfigured()) {
        QMessageBox::warning(this, tr("API Key Required"),
            tr("No API key configured. Please configure your LLM provider in Settings."));
        return;
    }

    bool ok = false;
    const QString input = QInputDialog::getText(this, tr("Cron Helper"),
        tr("Describe the schedule in natural language:"),
        QLineEdit::Normal, QString(), &ok);

    if (!ok || input.trimmed().isEmpty()) {
        return;
    }

    // Get credentials
    auto *npApp = qobject_cast<NotepadNextApplication *>(qApp);
    ai::CredentialStore *credStore = npApp ? npApp->getCredentialStore() : nullptr;

    QString apiKey;
    if (credStore) {
        apiKey = credStore->retrieveApiKey();
    }
    if (apiKey.isEmpty()) {
        QMessageBox::warning(this, tr("API Key Error"),
            tr("Could not retrieve API key from credential store."));
        return;
    }

    // Send request
    auto *client = new ai::LlmHttpClient(this);
    ai::ILlmHttpClient::Request req;
    req.url = QUrl(m_settings->commitMessageProviderUrl());
    req.model = m_settings->commitMessageModel();
    req.apiKey = apiKey;
    req.systemPrompt = QStringLiteral("Convert the following natural language schedule description to a standard 5-field cron expression (minute hour day-of-month month day-of-week). Reply with ONLY the cron expression, nothing else.");
    req.prompt = input;
    req.maxTokens = 50;
    req.idleTimeoutSec = 30;

    m_cronHelperBtn->setEnabled(false);
    m_cronHelperBtn->setText(tr("..."));

    auto *accumulated = new QString();
    connect(client, &ai::ILlmHttpClient::tokenReceived, this, [accumulated](const QString &token) {
        *accumulated += token;
    });
    connect(client, &ai::ILlmHttpClient::streamEnded, this, [this, client, accumulated]() {
        m_cronHelperBtn->setEnabled(true);
        m_cronHelperBtn->setText(tr("AI"));
        const QString cronExpr = accumulated->trimmed();
        auto parsed = CronExpression::parse(cronExpr);
        if (parsed && parsed->isValid()) {
            m_cronEdit->setText(cronExpr);
        } else {
            QMessageBox::warning(this, tr("Invalid Response"),
                tr("The AI returned an invalid cron expression: %1").arg(cronExpr));
        }
        delete accumulated;
        client->deleteLater();
    });
    connect(client, &ai::ILlmHttpClient::errorOccurred, this, [this, client, accumulated](int, const QString &msg) {
        m_cronHelperBtn->setEnabled(true);
        m_cronHelperBtn->setText(tr("AI"));
        QMessageBox::warning(this, tr("AI Error"), msg);
        delete accumulated;
        client->deleteLater();
    });

    client->openStream(req);
}
