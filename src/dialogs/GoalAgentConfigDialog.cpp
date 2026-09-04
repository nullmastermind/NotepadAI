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

#include "GoalAgentConfigDialog.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

#include "GoalConfigWidget.h"

GoalAgentConfigDialog::GoalAgentConfigDialog(const ScheduledTaskGoalConfig &config,
                                             AcpAgentRegistry *registry,
                                             ApplicationSettings *settings,
                                             QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Goal Configuration"));
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    m_goalConfig = new GoalConfigWidget(registry, settings, this);
    layout->addWidget(m_goalConfig);

    if (!config.criteriaList.isEmpty())
        m_goalConfig->setCriteria(config.criteriaList);
    if (!config.agentId.isEmpty())
        m_goalConfig->setAgentId(config.agentId);
    if (config.maxIterations > 0)
        m_goalConfig->setMaxIterations(config.maxIterations);
    if (!config.promptTemplateId.isEmpty())
        m_goalConfig->setPromptTemplateId(config.promptTemplateId);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        m_goalConfig->persistPendingCustomApi();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

ScheduledTaskGoalConfig GoalAgentConfigDialog::goalConfig() const
{
    ScheduledTaskGoalConfig cfg;
    const auto gcr = m_goalConfig->result();
    cfg.criteriaList = gcr.criteriaList;
    cfg.agentId = gcr.agentId;
    cfg.maxIterations = gcr.maxIterations;
    cfg.promptTemplateId = gcr.promptTemplateId;
    return cfg;
}
