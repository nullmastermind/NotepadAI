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

#ifndef DIALOGS_TRANSFER_CONFLICT_DIALOG_H
#define DIALOGS_TRANSFER_CONFLICT_DIALOG_H

#include <QDialog>
#include <QHash>
#include <QStringList>

class QCheckBox;
class QListWidget;

// TransferConflictDialog — Modal dialog that presents conflicting upload paths
// and asks the user whether to Skip or Replace each one.
//
// The dialog populates a list of paths that already exist on the remote side.
// The user can:
//   - Skip: don't overwrite the remote file (preserves it)
//   - Replace: overwrite the remote file
//   - Skip All: apply Skip to all remaining conflicts
//   - Replace All: apply Replace to all remaining conflicts
//   - "Override current" checkbox: bypass conflict detection entirely for this transfer
//
// Call result() after exec() to get the per-path decisions, or
// overrideAll() to check if the user chose to bypass detection entirely.
class TransferConflictDialog : public QDialog
{
    Q_OBJECT

public:
    enum Resolution { Skip, Replace };

    explicit TransferConflictDialog(const QStringList &conflictPaths, QWidget *parent = nullptr);

    // Returns the per-path resolutions for all paths shown in the dialog.
    // Only meaningful if overrideAll() returns false.
    QHash<QString, Resolution> result() const { return m_resolutions; }

    // True if the user checked "Override current" — bypass conflict checking.
    bool overrideAll() const;

private slots:
    void onSkipClicked();
    void onReplaceClicked();
    void onSkipAllClicked();
    void onReplaceAllClicked();

private:
    void applyToRemaining(Resolution resolution);
    void advanceOrAccept();

    QStringList m_paths;
    QHash<QString, Resolution> m_resolutions;
    int m_currentIndex = 0;

    QListWidget *m_listWidget = nullptr;
    QCheckBox *m_overrideCheck = nullptr;
};

#endif // DIALOGS_TRANSFER_CONFLICT_DIALOG_H
