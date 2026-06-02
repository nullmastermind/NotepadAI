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

#include "TransferProgressBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

TransferProgressBar::TransferProgressBar(QWidget *parent)
    : QWidget(parent)
    , m_showTimer(new QTimer(this))
    , m_hideTimer(new QTimer(this))
{
    // --- Layout ---
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(4, 2, 4, 2);
    outerLayout->setSpacing(2);

    auto *row = new QHBoxLayout();
    row->setSpacing(4);

    m_label = new QLabel(this);
    m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // Use PlaceholderText role for muted appearance — no hard-coded color.
    QPalette pal = m_label->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    m_label->setPalette(pal);
    {
        QFont f = m_label->font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        m_label->setFont(f);
    }
    row->addWidget(m_label, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    row->addWidget(m_progressBar, 0);

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setFocusPolicy(Qt::StrongFocus);
    // Small button: use the font at current size, just compact padding.
    m_cancelButton->setFixedHeight(m_cancelButton->sizeHint().height() - 4);
    connect(m_cancelButton, &QPushButton::clicked, this, &TransferProgressBar::onCancelClicked);
    row->addWidget(m_cancelButton, 0);

    outerLayout->addLayout(row);

    // Hidden separator line at top (palette-based, no hard-coded color).
    setAutoFillBackground(true);

    // Hide by default.
    setVisible(false);

    // Show timer: single-shot 200ms before showing.
    m_showTimer->setSingleShot(true);
    connect(m_showTimer, &QTimer::timeout, this, [this]() {
        if (m_active) setVisible(true);
    });

    // Hide timer: after "completed" state, auto-hide after 2 seconds.
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() {
        setVisible(false);
        m_active = false;
        m_progressBar->setValue(0);
        m_label->setText(QString());
    });
}

void TransferProgressBar::triggerShow()
{
    m_active = true;
    m_hideTimer->stop();
    // Show after 200ms delay to avoid flashing for near-instant transfers.
    if (!m_showTimer->isActive())
        m_showTimer->start(200);
}

void TransferProgressBar::updateProgress(int current, int total, qint64 bytesTransferred,
                                         qint64 totalBytes, int queuedCount)
{
    if (total > 0) {
        m_progressBar->setRange(0, total);
        m_progressBar->setValue(current);
    } else {
        // Indeterminate.
        m_progressBar->setRange(0, 0);
    }

    QString text;
    if (total > 0) {
        text = tr("%1 / %2 files").arg(current).arg(total);
    } else {
        text = tr("Transferring…");
    }

    if (totalBytes > 0) {
        const QLocale locale;
        text += tr("  %1 / %2").arg(locale.formattedDataSize(bytesTransferred),
                                     locale.formattedDataSize(totalBytes));
    } else if (bytesTransferred > 0) {
        const QLocale locale;
        text += QStringLiteral("  ") + locale.formattedDataSize(bytesTransferred);
    }

    if (queuedCount > 0) {
        text += tr("  (%1 queued)").arg(queuedCount);
    }

    m_label->setText(text);
}

void TransferProgressBar::setCurrentFile(const QString &fileName)
{
    if (!fileName.isEmpty()) {
        m_label->setText(tr("Transferring: %1").arg(fileName));
    }
}

void TransferProgressBar::setStatusLabel(const QString &text)
{
    if (!text.isEmpty()) {
        m_label->setText(text);
    }
}

void TransferProgressBar::showCompleted(int fileCount)
{
    m_showTimer->stop();
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(1);
    m_label->setText(tr("Done — %1 file(s) transferred").arg(fileCount));
    m_cancelButton->setVisible(false);
    setVisible(true);
    // Auto-hide after 2 seconds.
    m_hideTimer->start(2000);
}

void TransferProgressBar::showError(const QString &message)
{
    m_showTimer->stop();
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_label->setText(tr("Transfer error: %1").arg(message));
    m_cancelButton->setVisible(false);
    setVisible(true);
    // Auto-hide after 5 seconds so the user can read the error.
    m_hideTimer->start(5000);
}

void TransferProgressBar::hideImmediate()
{
    m_showTimer->stop();
    m_hideTimer->stop();
    m_active = false;
    m_cancelButton->setVisible(true);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_label->setText(QString());
    setVisible(false);
}

void TransferProgressBar::onCancelClicked()
{
    emit cancelRequested();
    hideImmediate();
}
