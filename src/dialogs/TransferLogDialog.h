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

#ifndef DIALOGS_TRANSFER_LOG_DIALOG_H
#define DIALOGS_TRANSFER_LOG_DIALOG_H

#include <QDialog>

class QCloseEvent;
class QPlainTextEdit;
class QPushButton;
class TransferProgressBar;

class TransferLogDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TransferLogDialog(QWidget *parent = nullptr);

    TransferProgressBar *progressBar() const { return m_progressBar; }

    void setLocalDestination(const QString &dirPath);
    void setTransferActive(bool active) { m_transferActive = active; }
    void appendStatus(const QString &path, bool ok, const QString &error);
    void appendInfo(const QString &message);
    void setFinished();

signals:
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    TransferProgressBar *m_progressBar = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_showInExplorerButton = nullptr;
    QPushButton *m_closeButton = nullptr;
    QString m_localDestDir;
    bool m_transferActive = false;
    int m_okCount = 0;
    int m_failCount = 0;
};

#endif // DIALOGS_TRANSFER_LOG_DIALOG_H
