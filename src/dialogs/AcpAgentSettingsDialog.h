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

#ifndef ACP_AGENT_SETTINGS_DIALOG_H
#define ACP_AGENT_SETTINGS_DIALOG_H

#include <QDialog>

#include "AcpAgentDefinition.h"

class AcpAgentRegistry;
class ApplicationSettings;
class QComboBox;
class QGroupBox;
class QPlainTextEdit;
class QSpinBox;

namespace Ui {
class AcpAgentSettingsDialog;
}

class AcpAgentSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AcpAgentSettingsDialog(AcpAgentRegistry *registry,
                                   ApplicationSettings *appSettings = nullptr,
                                   QWidget *parent = nullptr);
    ~AcpAgentSettingsDialog() override;

private slots:
    void onAdd();
    void onEdit();
    void onRemove();
    void onSelectionChanged();
    void onDefaultAgentIndexChanged(int index);
    void onAutoApproveIndexChanged(int index);
    void onGoalAgentChanged(int index);
    void onGoalMaxIterChanged(int value);
    void onGoalTemplateChanged(int index);
    void onGoalTemplateContentChanged();
    void onGoalTemplateNew();
    void onGoalTemplateDelete();

private:
    void refreshAgentTable();
    void refreshDefaultAgentCombo();
    void refreshAutoApproveCombo();
    void buildGoalSection();
    void loadGoalSettings();
    void saveGoalSettings();
    void refreshGoalTemplateCombo();
    QString selectedAgentId() const;

    Ui::AcpAgentSettingsDialog *ui;
    AcpAgentRegistry *m_registry;
    ApplicationSettings *m_appSettings = nullptr;

    // Goal section widgets
    QGroupBox *m_goalGroup = nullptr;
    QComboBox *m_goalAgentCombo = nullptr;
    QSpinBox *m_goalMaxIterSpin = nullptr;
    QComboBox *m_goalTemplateCombo = nullptr;
    QPlainTextEdit *m_goalTemplateEdit = nullptr;
    bool m_goalLoading = false;
};

#endif // ACP_AGENT_SETTINGS_DIALOG_H
