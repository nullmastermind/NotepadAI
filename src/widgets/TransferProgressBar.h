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

#ifndef WIDGETS_TRANSFER_PROGRESS_BAR_H
#define WIDGETS_TRANSFER_PROGRESS_BAR_H

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;

// TransferProgressBar — An inline progress widget for SFTP transfers.
// Embedded at the bottom of FolderAsWorkspaceDock's layout. Hidden by default;
// shown after a 200ms delay (so brief transfers don't flash). Hides itself on
// completion or cancellation.
//
// All QPalette roles — no hard-coded colors (ui-dna.md requirement).
class TransferProgressBar : public QWidget
{
    Q_OBJECT

public:
    explicit TransferProgressBar(QWidget *parent = nullptr);

    // Update the progress bar state. `current` and `total` are file counts.
    // `bytesTransferred` and `totalBytes` are cumulative byte counts (0 if unknown).
    // `queuedCount` > 0 shows "(N queued)" in the label.
    // Call this repeatedly as each file completes; calling with current==total signals done.
    void updateProgress(int current, int total, qint64 bytesTransferred, qint64 totalBytes,
                        int queuedCount = 0);

    // Set the name of the current file being transferred (shown in the label).
    void setCurrentFile(const QString &fileName);

    // Set an arbitrary status label (e.g. "Scanning remote destination…").
    // Unlike setCurrentFile, this does NOT prepend "Transferring:".
    void setStatusLabel(const QString &text);

    // Show the "completed" state briefly, then auto-hide.
    void showCompleted(int fileCount);

    // Show the "error" state (e.g. connection lost mid-transfer).
    void showError(const QString &message);

    // Immediately hide the bar (used on cancel confirmation).
    void hideImmediate();

    // Programmatically trigger show (the 200ms delay is applied internally).
    void triggerShow();

signals:
    // Emitted when the user clicks the Cancel button.
    void cancelRequested();

private slots:
    void onCancelClicked();

private:
    QLabel *m_label = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QTimer *m_showTimer = nullptr;
    QTimer *m_hideTimer = nullptr;

    // True while a transfer is active (between triggerShow and hideImmediate/showCompleted).
    bool m_active = false;
};

#endif // WIDGETS_TRANSFER_PROGRESS_BAR_H
