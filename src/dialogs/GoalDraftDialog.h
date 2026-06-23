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

#ifndef GOAL_DRAFT_DIALOG_H
#define GOAL_DRAFT_DIALOG_H

#include <QDialog>
#include <QPointer>
#include <QString>

class AcpAgentManager;
class AcpAgentRegistry;
class AcpSessionModel;
class ApplicationSettings;
class GoalDraftGenerator;
class QLabel;
class QCloseEvent;
class QComboBox;
class QMenu;
class QPlainTextEdit;
class QPushButton;

namespace remote { class ExecutionContext; }

class GoalDraftDialog : public QDialog
{
    Q_OBJECT

public:
    GoalDraftDialog(AcpAgentManager *manager,
                    AcpAgentRegistry *registry,
                    ApplicationSettings *settings,
                    AcpSessionModel *targetModel,
                    QString workingDirectory,
                    remote::ExecutionContext *executionContext,
                    QWidget *parent = nullptr);
    ~GoalDraftDialog() override;

    QString generatedText() const { return m_generatedText; }

protected:
    void reject() override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onGenerateClicked();
    void onGenerated(const QString &text);
    void onError(const QString &message);

private:
    void populateAgents();
    void populatePresets();
    void populateTemplates();
    bool validate();
    void updateGenerateButton();
    void setGenerating(bool generating);
    void cancelGeneration();

    AcpAgentManager *m_manager = nullptr;
    AcpAgentRegistry *m_registry = nullptr;
    ApplicationSettings *m_settings = nullptr;
    QPointer<AcpSessionModel> m_targetModel;
    QString m_workingDirectory;
    remote::ExecutionContext *m_executionContext = nullptr;

    QComboBox *m_templateCombo = nullptr;
    QPushButton *m_loadPresetBtn = nullptr;
    QMenu *m_presetMenu = nullptr;
    QPlainTextEdit *m_criteriaEdit = nullptr;
    QComboBox *m_agentCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_generateBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    GoalDraftGenerator *m_generator = nullptr;
    QString m_generatedText;
};

#endif // GOAL_DRAFT_DIALOG_H
