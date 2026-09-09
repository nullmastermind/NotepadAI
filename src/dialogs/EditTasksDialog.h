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

#pragma once

#include "TerminalTaskRegistry.h"

#include <QDialog>
#include <QList>

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimer;

class EditTasksDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EditTasksDialog(const QString &workspacePath,
                             const QList<TerminalTask> &tasks,
                             QWidget *parent = nullptr);

    QList<TerminalTask> tasks() const;

private slots:
    void onCurrentRowChanged(int row);
    void onAddClicked();
    void onRemoveClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onBrowseCwdClicked();
    void validateEnv();

private:
    void commitCurrentTask();
    void loadTask(int row);
    void updateButtonStates();
    void moveCurrentBy(int delta);

    QString m_workspacePath;
    QList<TerminalTask> m_tasks;
    int m_currentRow = -1;

    QListWidget *m_listWidget = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;

    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_commandEdit = nullptr;
    QPlainTextEdit *m_envEdit = nullptr;
    QLineEdit *m_cwdEdit = nullptr;
    QPushButton *m_browseCwdBtn = nullptr;
    QLabel *m_envWarningLabel = nullptr;
    QTimer *m_envValidateTimer = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
};
