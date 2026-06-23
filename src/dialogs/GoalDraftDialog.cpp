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

#include "GoalDraftDialog.h"

#include "AcpAgentDefinition.h"
#include "AcpAgentRegistry.h"
#include "AcpSessionModel.h"
#include "ApplicationSettings.h"
#include "GoalAgentSettings.h"
#include "GoalDraftGenerator.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

GoalDraftDialog::GoalDraftDialog(AcpAgentManager *manager,
                                 AcpAgentRegistry *registry,
                                 ApplicationSettings *settings,
                                 AcpSessionModel *targetModel,
                                 QString workingDirectory,
                                 remote::ExecutionContext *executionContext,
                                 QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
    , m_registry(registry)
    , m_settings(settings)
    , m_targetModel(targetModel)
    , m_workingDirectory(std::move(workingDirectory))
    , m_executionContext(executionContext)
{
    setWindowTitle(tr("Generate Prompt with Goal"));
    setMinimumWidth(480);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    auto *form = new QFormLayout;
    form->setSpacing(6);

    m_templateCombo = new QComboBox(this);
    populateTemplates();
    form->addRow(tr("Goal template:"), m_templateCombo);

    auto *presetLayout = new QHBoxLayout;
    m_loadPresetBtn = new QPushButton(tr("Load preset"), this);
    m_presetMenu = new QMenu(this);
    m_loadPresetBtn->setMenu(m_presetMenu);
    populatePresets();
    presetLayout->addWidget(m_loadPresetBtn);
    presetLayout->addStretch();
    form->addRow(QString(), presetLayout);

    m_criteriaEdit = new QPlainTextEdit(this);
    m_criteriaEdit->setMinimumHeight(120);
    m_criteriaEdit->setPlaceholderText(
        tr("Describe what the generated prompt should accomplish"));
    form->addRow(tr("Criteria:"), m_criteriaEdit);

    m_agentCombo = new QComboBox(this);
    populateAgents();
    form->addRow(tr("Goal-agent:"), m_agentCombo);

    mainLayout->addLayout(form);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text); font-size: 12px;"));
    m_statusLabel->hide();
    mainLayout->addWidget(m_statusLabel);

    auto *footerLayout = new QHBoxLayout;
    footerLayout->addStretch();
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    footerLayout->addWidget(m_cancelBtn);

    m_generateBtn = new QPushButton(tr("Generate"), this);
    m_generateBtn->setDefault(true);
    m_generateBtn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    connect(m_generateBtn, &QPushButton::clicked,
            this, &GoalDraftDialog::onGenerateClicked);
    footerLayout->addWidget(m_generateBtn);
    mainLayout->addLayout(footerLayout);

    m_generator = new GoalDraftGenerator(m_manager, m_settings, this);
    connect(m_generator, &GoalDraftGenerator::finished,
            this, &GoalDraftDialog::onGenerated);
    connect(m_generator, &GoalDraftGenerator::errorOccurred,
            this, &GoalDraftDialog::onError);

    connect(m_criteriaEdit, &QPlainTextEdit::textChanged,
            this, &GoalDraftDialog::updateGenerateButton);
    connect(m_agentCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &GoalDraftDialog::updateGenerateButton);
    connect(m_templateCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &GoalDraftDialog::updateGenerateButton);
    updateGenerateButton();
}

GoalDraftDialog::~GoalDraftDialog()
{
    cancelGeneration();
}

void GoalDraftDialog::reject()
{
    cancelGeneration();
    QDialog::reject();
}

void GoalDraftDialog::closeEvent(QCloseEvent *event)
{
    cancelGeneration();
    QDialog::closeEvent(event);
}

void GoalDraftDialog::onGenerateClicked()
{
    if (!validate())
        return;

    m_statusLabel->setText(tr("Generating prompt..."));
    m_statusLabel->show();
    setGenerating(true);

    GoalDraftGenerator::Request req;
    req.criteria = m_criteriaEdit->toPlainText().trimmed();
    req.agentId = m_agentCombo->currentData().toString();
    req.promptTemplateId = m_templateCombo->currentData().toString();
    req.workingDirectory = m_workingDirectory;
    req.targetModel = m_targetModel.data();
    req.executionContext = m_executionContext;

    if (!m_generator->start(req))
        setGenerating(false);
}

void GoalDraftDialog::onGenerated(const QString &text)
{
    m_generatedText = text;
    setGenerating(false);
    accept();
}

void GoalDraftDialog::onError(const QString &message)
{
    setGenerating(false);
    m_statusLabel->setText(message);
    m_statusLabel->show();
}

void GoalDraftDialog::populateAgents()
{
    if (!m_agentCombo || !m_registry)
        return;

    m_agentCombo->clear();

    QString preferredAgentId;
    if (m_settings) {
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            preferredAgentId = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object()).agentId;
        }
    }

    const auto agents = m_registry->agents();
    int selectedIdx = 0;
    for (int i = 0; i < agents.size(); ++i) {
        const auto &agent = agents[i];
        m_agentCombo->addItem(agent.name.isEmpty() ? agent.id : agent.name, agent.id);
        if (!preferredAgentId.isEmpty() && agent.id == preferredAgentId)
            selectedIdx = i;
    }
    if (m_agentCombo->count() > 0)
        m_agentCombo->setCurrentIndex(selectedIdx);
}

void GoalDraftDialog::populatePresets()
{
    if (!m_presetMenu)
        return;

    m_presetMenu->clear();

    GoalAgentSettings goalSettings;
    if (m_settings) {
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
        }
    }

    if (goalSettings.criteriaPresets.isEmpty()) {
        m_presetMenu->addAction(tr("No saved presets"))->setEnabled(false);
        return;
    }

    for (const auto &preset : goalSettings.criteriaPresets) {
        auto *action = m_presetMenu->addAction(
            QStringLiteral("%1 (%2)").arg(preset.name).arg(preset.criteria.size()));
        connect(action, &QAction::triggered, this, [this, criteria = preset.criteria]() {
            m_criteriaEdit->setPlainText(criteria.join(QLatin1Char('\n')));
            m_criteriaEdit->setFocus();
            updateGenerateButton();
        });
    }
}

void GoalDraftDialog::populateTemplates()
{
    if (!m_templateCombo)
        return;

    m_templateCombo->clear();

    GoalAgentSettings goalSettings;
    if (m_settings) {
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
        }
    }

    for (const auto &tpl : goalSettings.promptTemplates) {
        QString label = tpl.name;
        if (tpl.id == QLatin1String(GoalAgentSettings::kDefaultTemplateId))
            label += tr(" (default)");
        m_templateCombo->addItem(label, tpl.id);
    }
}

bool GoalDraftDialog::validate()
{
    if (m_criteriaEdit->toPlainText().trimmed().isEmpty()) {
        m_statusLabel->setText(tr("Enter criteria before generating a prompt."));
        m_statusLabel->show();
        return false;
    }
    if (m_templateCombo->currentData().toString().isEmpty()) {
        m_statusLabel->setText(tr("Select a goal template."));
        m_statusLabel->show();
        return false;
    }
    if (m_agentCombo->currentData().toString().isEmpty()) {
        m_statusLabel->setText(tr("Select a goal-agent."));
        m_statusLabel->show();
        return false;
    }

    m_statusLabel->hide();
    return true;
}

void GoalDraftDialog::updateGenerateButton()
{
    const bool valid = !m_criteriaEdit->toPlainText().trimmed().isEmpty()
        && !m_templateCombo->currentData().toString().isEmpty()
        && !m_agentCombo->currentData().toString().isEmpty();
    m_generateBtn->setEnabled(valid && (!m_generator || !m_generator->isRunning()));
}

void GoalDraftDialog::setGenerating(bool generating)
{
    m_templateCombo->setEnabled(!generating);
    m_criteriaEdit->setReadOnly(generating);
    m_agentCombo->setEnabled(!generating);
    m_generateBtn->setEnabled(!generating);
    m_cancelBtn->setEnabled(true);
    if (!generating)
        updateGenerateButton();
}

void GoalDraftDialog::cancelGeneration()
{
    if (m_generator && m_generator->isRunning())
        m_generator->cancel();
}
