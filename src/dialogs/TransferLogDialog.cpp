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

#include "TransferLogDialog.h"

#include "../widgets/TransferProgressBar.h"

#include <QCloseEvent>
#include <QDesktopServices>
#include <QFont>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

TransferLogDialog::TransferLogDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Transfer Log"));
    setMinimumSize(520, 320);
    resize(600, 400);
    setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    m_progressBar = new TransferProgressBar(this);
    m_progressBar->setVisible(false);
    layout->addWidget(m_progressBar, 0);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setUndoRedoEnabled(false);
    m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = m_log->font();
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_log->setFont(mono);
    layout->addWidget(m_log, 1);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(6);

    m_showInExplorerButton = new QPushButton(tr("Show in Explorer"), this);
    m_showInExplorerButton->setFocusPolicy(Qt::StrongFocus);
    m_showInExplorerButton->setEnabled(false);
    connect(m_showInExplorerButton, &QPushButton::clicked, this, [this]() {
        if (!m_localDestDir.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_localDestDir));
    });
    buttonRow->addWidget(m_showInExplorerButton);

    buttonRow->addStretch(1);

    m_closeButton = new QPushButton(tr("Close"), this);
    m_closeButton->setFocusPolicy(Qt::StrongFocus);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::close);
    buttonRow->addWidget(m_closeButton);

    layout->addLayout(buttonRow);
}

void TransferLogDialog::setLocalDestination(const QString &dirPath)
{
    m_localDestDir = dirPath;
    m_showInExplorerButton->setEnabled(!dirPath.isEmpty());
}

void TransferLogDialog::appendStatus(const QString &path, bool ok, const QString &error)
{
    QString line = path;
    if (ok) {
        line += QStringLiteral("  -- ok");
        ++m_okCount;
    } else {
        line += QStringLiteral("  -- FAILED");
        if (!error.isEmpty())
            line += QStringLiteral(": ") + error;
        ++m_failCount;
    }
    m_log->appendPlainText(line);
}

void TransferLogDialog::appendInfo(const QString &message)
{
    m_log->appendPlainText(message);
}

void TransferLogDialog::setFinished()
{
    m_transferActive = false;
    m_log->appendPlainText(QString());
    m_log->appendPlainText(
        tr("Done. %1 ok, %2 failed.").arg(m_okCount).arg(m_failCount));
    setWindowTitle(tr("Transfer Log (complete)"));
}

void TransferLogDialog::closeEvent(QCloseEvent *event)
{
    if (m_transferActive)
        emit cancelRequested();
    QDialog::closeEvent(event);
}
