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

// Built in code (no .ui), matching the code-built dialogs in this tree
// (EditTasksDialog, SendWithGoalDialog). Chrome stays on palette roles — no
// hard-coded colors — per ui-dna. Full keyboard nav: a sensible tab order, the
// list drives the form, Enter on the form Saves, Esc closes (Close button).

#include "SshConnectionManagerDialog.h"

#include "ai/CredentialStore.h"
#include "remote/SshProfileRegistry.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

using remote::SshProfile;

SshConnectionManagerDialog::SshConnectionManagerDialog(remote::SshProfileRegistry *registry,
                                                       ai::CredentialStore *credentialStore,
                                                       QWidget *parent)
    : QDialog(parent)
    , m_registry(registry)
    , m_credentialStore(credentialStore)
{
    setWindowTitle(tr("SSH Connection Manager"));
    setModal(true);
    resize(620, 420);

    auto *root = new QVBoxLayout(this);
    auto *split = new QHBoxLayout();
    root->addLayout(split, 1);

    // --- left: profile list + New/Delete ---
    auto *leftCol = new QVBoxLayout();
    m_list = new QListWidget(this);
    m_list->setAccessibleName(tr("Saved connections"));
    leftCol->addWidget(m_list, 1);

    auto *leftBtns = new QHBoxLayout();
    auto *newBtn = new QPushButton(tr("New"), this);
    m_deleteBtn = new QPushButton(tr("Delete"), this);
    leftBtns->addWidget(newBtn);
    leftBtns->addWidget(m_deleteBtn);
    leftBtns->addStretch(1);
    leftCol->addLayout(leftBtns);
    split->addLayout(leftCol, 1);

    // --- right: edit form ---
    auto *form = new QFormLayout();
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setPlaceholderText(tr("example.com"));
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(22);
    m_userEdit = new QLineEdit(this);
    m_authCombo = new QComboBox(this);
    m_authCombo->addItem(tr("SSH agent"), int(SshProfile::AuthMethod::Agent));
    m_authCombo->addItem(tr("Password"), int(SshProfile::AuthMethod::Password));
    m_authCombo->addItem(tr("Key file"), int(SshProfile::AuthMethod::KeyFile));

    m_keyPathEdit = new QLineEdit(this);
    m_keyBrowseBtn = new QPushButton(tr("Browse…"), this);
    auto *keyRow = new QHBoxLayout();
    keyRow->addWidget(m_keyPathEdit, 1);
    keyRow->addWidget(m_keyBrowseBtn);
    auto *keyRowWidget = new QWidget(this);
    keyRowWidget->setLayout(keyRow);

    m_secretLabel = new QLabel(tr("Password"), this);
    m_secretEdit = new QLineEdit(this);
    m_secretEdit->setEchoMode(QLineEdit::Password);
    m_secretEdit->setPlaceholderText(tr("Stored in the OS keychain"));

    m_lastPathEdit = new QLineEdit(this);
    m_lastPathEdit->setPlaceholderText(tr("/home/you/project (optional)"));

    form->addRow(tr("Host"), m_hostEdit);
    form->addRow(tr("Port"), m_portSpin);
    form->addRow(tr("Username"), m_userEdit);
    form->addRow(tr("Authentication"), m_authCombo);
    form->addRow(tr("Key file"), keyRowWidget);
    form->addRow(m_secretLabel, m_secretEdit);
    form->addRow(tr("Initial remote path"), m_lastPathEdit);

    auto *rightCol = new QVBoxLayout();
    rightCol->addLayout(form, 1);

    m_emptyHint = new QLabel(tr("Select a connection, or click New to add one."), this);
    m_emptyHint->setWordWrap(true);
    m_emptyHint->setAlignment(Qt::AlignCenter);
    rightCol->addWidget(m_emptyHint);

    auto *rightBtns = new QHBoxLayout();
    m_saveBtn = new QPushButton(tr("Save"), this);
    m_connectBtn = new QPushButton(tr("Connect"), this);
    m_connectBtn->setDefault(true);
    rightBtns->addStretch(1);
    rightBtns->addWidget(m_saveBtn);
    rightBtns->addWidget(m_connectBtn);
    rightCol->addLayout(rightBtns);
    split->addLayout(rightCol, 2);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    root->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Tab order: list → host → port → user → auth → key → secret → path → save.
    setTabOrder(m_list, m_hostEdit);
    setTabOrder(m_hostEdit, m_portSpin);
    setTabOrder(m_portSpin, m_userEdit);
    setTabOrder(m_userEdit, m_authCombo);
    setTabOrder(m_authCombo, m_keyPathEdit);
    setTabOrder(m_keyPathEdit, m_keyBrowseBtn);
    setTabOrder(m_keyBrowseBtn, m_secretEdit);
    setTabOrder(m_secretEdit, m_lastPathEdit);
    setTabOrder(m_lastPathEdit, m_saveBtn);
    setTabOrder(m_saveBtn, m_connectBtn);

    connect(newBtn, &QPushButton::clicked, this, &SshConnectionManagerDialog::onNew);
    connect(m_deleteBtn, &QPushButton::clicked, this, &SshConnectionManagerDialog::onDelete);
    connect(m_saveBtn, &QPushButton::clicked, this, [this]() { saveFormToProfile(); });
    connect(m_connectBtn, &QPushButton::clicked, this, &SshConnectionManagerDialog::onConnect);
    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int) { loadSelectionIntoForm(); });
    connect(m_authCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { updateAuthRows(); });
    connect(m_keyBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, tr("Select private key"));
        if (!path.isEmpty()) {
            m_keyPathEdit->setText(path);
        }
    });

    rebuildList();
}

QString SshConnectionManagerDialog::selectedProfileId() const
{
    QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void SshConnectionManagerDialog::rebuildList(const QString &selectId)
{
    m_list->blockSignals(true);
    m_list->clear();
    if (m_registry) {
        const auto profiles = m_registry->profiles();
        for (const SshProfile &p : profiles) {
            const QString label = (p.username.isEmpty() ? p.host
                                                         : p.username + QLatin1Char('@') + p.host)
                + QStringLiteral(":") + QString::number(p.port);
            auto *item = new QListWidgetItem(label, m_list);
            item->setData(Qt::UserRole, p.id);
        }
    }
    m_list->blockSignals(false);

    int row = -1;
    if (!selectId.isEmpty()) {
        for (int i = 0; i < m_list->count(); ++i) {
            if (m_list->item(i)->data(Qt::UserRole).toString() == selectId) {
                row = i;
                break;
            }
        }
    }
    if (row < 0 && m_list->count() > 0) {
        row = 0;
    }
    m_list->setCurrentRow(row);
    if (row < 0) {
        loadSelectionIntoForm(); // clear/disable form when the list is empty
    }
}

void SshConnectionManagerDialog::setFormEnabled(bool on)
{
    m_hostEdit->setEnabled(on);
    m_portSpin->setEnabled(on);
    m_userEdit->setEnabled(on);
    m_authCombo->setEnabled(on);
    m_keyPathEdit->setEnabled(on);
    m_keyBrowseBtn->setEnabled(on);
    m_secretEdit->setEnabled(on);
    m_lastPathEdit->setEnabled(on);
    m_saveBtn->setEnabled(on);
    m_connectBtn->setEnabled(on);
    m_deleteBtn->setEnabled(on && !m_editingId.isEmpty());
    m_emptyHint->setVisible(!on);
}

void SshConnectionManagerDialog::updateAuthRows()
{
    const auto method = static_cast<SshProfile::AuthMethod>(m_authCombo->currentData().toInt());
    const bool isKey = method == SshProfile::AuthMethod::KeyFile;
    const bool isPassword = method == SshProfile::AuthMethod::Password;
    m_keyPathEdit->setVisible(isKey);
    m_keyBrowseBtn->setVisible(isKey);
    // Agent auth stores no secret.
    m_secretEdit->setVisible(isKey || isPassword);
    m_secretLabel->setVisible(isKey || isPassword);
    m_secretLabel->setText(isKey ? tr("Passphrase") : tr("Password"));
}

void SshConnectionManagerDialog::loadSelectionIntoForm()
{
    const QString id = selectedProfileId();
    m_editingId = id;
    if (id.isEmpty() || !m_registry) {
        setFormEnabled(false);
        return;
    }
    setFormEnabled(true);
    const SshProfile p = m_registry->profile(id);
    m_hostEdit->setText(p.host);
    m_portSpin->setValue(p.port);
    m_userEdit->setText(p.username);
    const int idx = m_authCombo->findData(int(p.authMethod));
    m_authCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    m_keyPathEdit->setText(p.keyPath);
    m_lastPathEdit->setText(p.lastRemotePath);
    m_secretEdit->clear(); // never surface a stored secret back into the field
    updateAuthRows();
}

remote::SshProfile SshConnectionManagerDialog::currentFormProfile() const
{
    SshProfile p;
    p.id = m_editingId;
    p.host = m_hostEdit->text().trimmed();
    p.port = m_portSpin->value();
    p.username = m_userEdit->text().trimmed();
    p.authMethod = static_cast<SshProfile::AuthMethod>(m_authCombo->currentData().toInt());
    p.keyPath = m_keyPathEdit->text().trimmed();
    p.lastRemotePath = m_lastPathEdit->text().trimmed();
    return p;
}

void SshConnectionManagerDialog::onNew()
{
    if (!m_registry) return;
    SshProfile p;
    p.id = remote::SshProfileRegistry::generateId();
    p.host = QStringLiteral("");
    p.port = 22;
    p.authMethod = SshProfile::AuthMethod::Agent;
    if (m_registry->addProfile(p)) {
        rebuildList(p.id);
        m_hostEdit->setFocus();
    }
}

void SshConnectionManagerDialog::saveFormToProfile()
{
    if (!m_registry || m_editingId.isEmpty()) {
        return;
    }
    SshProfile p = currentFormProfile();
    if (p.host.isEmpty()) {
        QMessageBox::warning(this, tr("SSH"), tr("Enter a host name."));
        m_hostEdit->setFocus();
        return;
    }
    m_registry->updateProfile(p);

    // Persist the secret to the OS keychain (never to settings). Empty clears.
    if (m_credentialStore && m_secretEdit->isVisible()) {
        const QString kind = (p.authMethod == SshProfile::AuthMethod::KeyFile)
                                 ? QStringLiteral("passphrase")
                                 : QStringLiteral("password");
        const QString secret = m_secretEdit->text();
        if (!secret.isEmpty()) {
            m_credentialStore->storeSecret(remote::sshSecretKey(p.id, kind), secret);
            m_secretEdit->clear();
        }
    }
    rebuildList(p.id);
}

void SshConnectionManagerDialog::onDelete()
{
    if (!m_registry || m_editingId.isEmpty()) {
        return;
    }
    const QString id = m_editingId;
    const auto answer = QMessageBox::question(
        this, tr("Delete connection?"),
        tr("Delete this saved SSH connection? Its stored secret is also removed."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    if (m_credentialStore) {
        m_credentialStore->clearSecret(remote::sshSecretKey(id, QStringLiteral("password")));
        m_credentialStore->clearSecret(remote::sshSecretKey(id, QStringLiteral("passphrase")));
    }
    m_registry->removeProfile(id);
    rebuildList();
}

void SshConnectionManagerDialog::onConnect()
{
    if (m_editingId.isEmpty()) {
        return;
    }
    // Persist any unsaved edits first so the connect uses current values.
    saveFormToProfile();
    if (m_editingId.isEmpty()) {
        return; // save failed (e.g. empty host)
    }
    emit connectRequested(m_editingId);
}

