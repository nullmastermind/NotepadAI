/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_API_DIALOG_H
#define CONTROL_API_DIALOG_H

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace control {

class ControlApi;

// AI ▸ Connect External Agent…: the switch, port, LAN option, token, the
// connection prompt and the activity log. Modeless, one instance.
class ControlApiDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ControlApiDialog(ControlApi *api, QWidget *parent = nullptr);

private:
    void refresh();
    void copyPrompt();
    QString maskedPrompt();

    ControlApi *m_api;
    QLabel *m_status;
    QCheckBox *m_enable;
    QSpinBox *m_port;
    QCheckBox *m_lan;
    QComboBox *m_lanAddress;
    QLabel *m_lanWarning;
    QLineEdit *m_token;
    QToolButton *m_showToken;
    QPushButton *m_copyToken;
    QPushButton *m_regenerate;
    QPlainTextEdit *m_prompt;
    QPushButton *m_copyPrompt;
    QListView *m_activity;
    QPushButton *m_clearActivity;
};

} // namespace control

#endif // CONTROL_API_DIALOG_H
