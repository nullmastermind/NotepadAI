/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "MiniAppDefinition.h"
#include "MiniAppRegistry.h"

#include <QDialog>
#include <QList>

class QComboBox;
class QDialogButtonBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;
class QToolButton;

class EditMiniAppsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EditMiniAppsDialog(MiniAppRegistry *registry,
                                const QString &workspacePath,
                                QWidget *parent = nullptr);

private slots:
    void onCurrentRowChanged(int row);
    void onAddClicked();
    void onRemoveClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onBrowseCwdClicked();
    void onScopeChanged(int index);
    void onRandomPortClicked();
    void validateFields();

private:
    void commitCurrentApp();
    void loadApp(int row);
    void updateButtonStates();
    void loadScope(int scopeIndex);
    void saveCurrentScope();

    MiniAppRegistry *m_registry;
    QString m_workspacePath;
    int m_currentRow = -1;
    int m_currentScope = 0; // 0=Global, 1=Workspace
    QList<MiniAppDefinition> m_apps;

    QComboBox *m_scopeCombo = nullptr;
    QListWidget *m_listWidget = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_upBtn = nullptr;
    QPushButton *m_downBtn = nullptr;

    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QLineEdit *m_commandEdit = nullptr;
    QLineEdit *m_cwdEdit = nullptr;
    QPushButton *m_browseCwdBtn = nullptr;
    QPlainTextEdit *m_envEdit = nullptr;
    QLabel *m_envWarningLabel = nullptr;
    QTimer *m_validateTimer = nullptr;

    // Advanced section
    QGroupBox *m_advancedGroup = nullptr;
    QLineEdit *m_healthUrlEdit = nullptr;
    QSpinBox *m_timeoutSpin = nullptr;

    // Debug section
    QGroupBox *m_debugGroup = nullptr;
    QSpinBox *m_debugPortSpin = nullptr;
    QPushButton *m_randomPortBtn = nullptr;
    QLabel *m_portWarningLabel = nullptr;

    // Proxy section
    QGroupBox *m_proxyGroup = nullptr;
    QComboBox *m_proxyTypeCombo = nullptr;
    QLineEdit *m_proxyHostEdit = nullptr;
    QSpinBox *m_proxyPortSpin = nullptr;
    QLineEdit *m_proxyBypassEdit = nullptr;
    QLabel *m_proxyWarningLabel = nullptr;

    QLabel *m_urlWarningLabel = nullptr;
    QDialogButtonBox *m_buttonBox = nullptr;
};

