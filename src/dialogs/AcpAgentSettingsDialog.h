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

#ifndef ACP_AGENT_SETTINGS_DIALOG_H
#define ACP_AGENT_SETTINGS_DIALOG_H

#include <QDialog>

#include "AcpAgentDefinition.h"

class AcpAgentRegistry;

namespace Ui {
class AcpAgentSettingsDialog;
}

class AcpAgentSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AcpAgentSettingsDialog(AcpAgentRegistry *registry, QWidget *parent = nullptr);
    ~AcpAgentSettingsDialog() override;

private slots:
    void onAdd();
    void onEdit();
    void onRemove();
    void onSelectionChanged();
    void onDefaultAgentIndexChanged(int index);
    void onAutoApproveIndexChanged(int index);

private:
    void refreshAgentTable();
    void refreshDefaultAgentCombo();
    void refreshAutoApproveCombo();
    QString selectedAgentId() const;

    Ui::AcpAgentSettingsDialog *ui;
    AcpAgentRegistry *m_registry;
};

#endif // ACP_AGENT_SETTINGS_DIALOG_H
