/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef PM2_PROCESS_EDIT_DIALOG_H
#define PM2_PROCESS_EDIT_DIALOG_H

#include <QDialog>
#include <QStringList>

#include <optional>

#include "Pm2Entrypoint.h"

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QToolButton;
class QWidget;

// New / Edit form for a PM2 process NotepadAI creates. `original` empty =
// New. `takenNames` are every live pm2 name (the original's own name is
// allowed back).
class Pm2ProcessEditDialog : public QDialog
{
    Q_OBJECT

public:
    Pm2ProcessEditDialog(const std::optional<Pm2Definition> &original, const QString &defaultCwd,
                         const QStringList &takenNames, QWidget *parent = nullptr);

    Pm2Definition definition() const;
    // Saving needs pm2 to delete and start the process again (name, directory
    // or a pm2 option changed); its restart count goes back to 0.
    bool needsRecreate() const;

    // What is wrong with `def` (the form's own check), empty when it can be saved.
    static QString problemWith(const Pm2Definition &def, const QStringList &takenNames);
    static bool needsRecreate(const Pm2Definition &original, const Pm2Definition &def);

private:
    void updateState();
    void browseCwd();

    std::optional<Pm2Definition> m_original;
    QStringList m_takenNames;

    QLineEdit *m_name = nullptr;
    QPlainTextEdit *m_command = nullptr;
    QComboBox *m_shell = nullptr;
    QLineEdit *m_cwd = nullptr;
    QPlainTextEdit *m_env = nullptr;
    QToolButton *m_advancedToggle = nullptr;
    QWidget *m_advanced = nullptr;
    QCheckBox *m_autorestart = nullptr;
    QCheckBox *m_watch = nullptr;
    QLineEdit *m_maxMemory = nullptr;
    QSpinBox *m_restartDelay = nullptr;
    QLabel *m_problem = nullptr;
    QLabel *m_recreateNote = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif // PM2_PROCESS_EDIT_DIALOG_H
