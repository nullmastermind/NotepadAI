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

#ifndef SSHCONNECTDIALOG_H
#define SSHCONNECTDIALOG_H

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QPlainTextEdit;
class QTimer;

namespace remote { class RemoteExecutionContext; class SshConnection; }

// Staged connect flow for one RemoteExecutionContext: Connecting →
// Authenticating → Ready, with a per-stage Cancel, a host-key Accept/Reject
// prompt showing the SHA256 fingerprint, and an error state with Retry.
// Accepts the dialog on Ready; rejects on cancel / reject / unrecoverable
// error. Palette-driven chrome, keyboard nav, status text never carried by
// color alone (paired with a label + button) per ui-dna.
class SshConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SshConnectDialog(remote::RemoteExecutionContext *context,
                              QWidget *parent = nullptr);
    ~SshConnectDialog() override;

private:
    enum class Page : quint8 { Progress, HostKey, Error };

    void wireConnection();
    void showPage(Page page);
    void setStage(const QString &text);
    void appendLog(const QString &message);
    void onHostKey(const QString &fingerprint, const QByteArray &key);
    void onConnected();
    void onConnectionLost(const QString &reason);
    void onAuthFailed(const QString &reason);
    void onCancel();
    void onRetry();
    void onOverallTimeout();

    remote::RemoteExecutionContext *m_context;
    remote::SshConnection *m_connection = nullptr;

    QStackedWidget *m_stack = nullptr;

    // Progress page
    QLabel *m_stageLabel = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_logOutput = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    // Host-key page
    QLabel *m_hostKeyText = nullptr;
    QPlainTextEdit *m_fingerprint = nullptr;
    QPushButton *m_acceptBtn = nullptr;
    QPushButton *m_rejectBtn = nullptr;

    // Error page
    QLabel *m_errorText = nullptr;
    QPushButton *m_retryBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;

    QTimer *m_overallTimer = nullptr;
    bool m_hostKeyHandled = false;
};

#endif // SSHCONNECTDIALOG_H
