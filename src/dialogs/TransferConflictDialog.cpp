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

#include "TransferConflictDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

TransferConflictDialog::TransferConflictDialog(const QStringList &conflictPaths, QWidget *parent)
    : QDialog(parent)
    , m_paths(conflictPaths)
{
    setWindowTitle(tr("Upload Conflicts"));
    setMinimumWidth(480);
    setModal(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);

    // Header.
    auto *headerLabel = new QLabel(
        tr("The following %n remote file(s) already exist. How would you like to handle each?",
           nullptr, conflictPaths.size()),
        this);
    headerLabel->setWordWrap(true);
    mainLayout->addWidget(headerLabel);

    // Conflict list (read-only; current item highlighted).
    m_listWidget = new QListWidget(this);
    m_listWidget->addItems(conflictPaths);
    m_listWidget->setCurrentRow(0);
    m_listWidget->setSelectionMode(QAbstractItemView::NoSelection);
    mainLayout->addWidget(m_listWidget, 1);

    // "Override current" checkbox — bypass conflict detection entirely.
    m_overrideCheck = new QCheckBox(tr("Override current — replace all without prompting"), this);
    mainLayout->addWidget(m_overrideCheck);
    connect(m_overrideCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) accept();
    });

    // Buttons row: Skip / Replace / Skip All / Replace All.
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(6);

    auto *skipBtn = new QPushButton(tr("Skip"), this);
    auto *replaceBtn = new QPushButton(tr("Replace"), this);
    auto *skipAllBtn = new QPushButton(tr("Skip All"), this);
    auto *replaceAllBtn = new QPushButton(tr("Replace All"), this);

    btnLayout->addWidget(skipBtn);
    btnLayout->addWidget(replaceBtn);
    btnLayout->addStretch(1);
    btnLayout->addWidget(skipAllBtn);
    btnLayout->addWidget(replaceAllBtn);

    mainLayout->addLayout(btnLayout);

    // Cancel button — aborts the entire upload.
    auto *cancelBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(cancelBox);
    connect(cancelBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(skipBtn, &QPushButton::clicked, this, &TransferConflictDialog::onSkipClicked);
    connect(replaceBtn, &QPushButton::clicked, this, &TransferConflictDialog::onReplaceClicked);
    connect(skipAllBtn, &QPushButton::clicked, this, &TransferConflictDialog::onSkipAllClicked);
    connect(replaceAllBtn, &QPushButton::clicked, this, &TransferConflictDialog::onReplaceAllClicked);

    // Initialize all paths to Skip (safe default).
    for (const QString &path : conflictPaths) {
        m_resolutions.insert(path, Skip);
    }
}

void TransferConflictDialog::onSkipClicked()
{
    if (m_currentIndex < m_paths.size()) {
        m_resolutions.insert(m_paths.at(m_currentIndex), Skip);
        advanceOrAccept();
    }
}

void TransferConflictDialog::onReplaceClicked()
{
    if (m_currentIndex < m_paths.size()) {
        m_resolutions.insert(m_paths.at(m_currentIndex), Replace);
        advanceOrAccept();
    }
}

void TransferConflictDialog::onSkipAllClicked()
{
    applyToRemaining(Skip);
    accept();
}

void TransferConflictDialog::onReplaceAllClicked()
{
    applyToRemaining(Replace);
    accept();
}

void TransferConflictDialog::applyToRemaining(Resolution resolution)
{
    for (int i = m_currentIndex; i < m_paths.size(); ++i) {
        m_resolutions.insert(m_paths.at(i), resolution);
    }
}

void TransferConflictDialog::advanceOrAccept()
{
    ++m_currentIndex;
    if (m_currentIndex >= m_paths.size()) {
        accept();
    } else {
        m_listWidget->setCurrentRow(m_currentIndex);
    }
}

bool TransferConflictDialog::overrideAll() const
{
    return m_overrideCheck && m_overrideCheck->isChecked();
}
