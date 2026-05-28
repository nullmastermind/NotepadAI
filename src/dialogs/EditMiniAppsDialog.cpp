/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "EditMiniAppsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QSplitter>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

EditMiniAppsDialog::EditMiniAppsDialog(MiniAppRegistry *registry,
                                       const QString &workspacePath,
                                       QWidget *parent)
    : QDialog(parent)
    , m_registry(registry)
    , m_workspacePath(workspacePath)
{
    setWindowTitle(tr("Edit Miniapps"));
    resize(750, 520);

    auto *mainLayout = new QVBoxLayout(this);

    // Scope selector (hidden if no workspace)
    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->addItem(tr("Global"));
    if (!workspacePath.isEmpty()) {
        QDir dir(workspacePath);
        m_scopeCombo->addItem(tr("Workspace: %1").arg(dir.dirName()));
    } else {
        m_scopeCombo->hide();
    }
    mainLayout->addWidget(m_scopeCombo);

    auto *splitter = new QSplitter(this);
    mainLayout->addWidget(splitter, 1);

    // Left panel: app list + buttons
    auto *leftWidget = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_listWidget = new QListWidget(leftWidget);
    m_listWidget->setMinimumWidth(160);
    leftLayout->addWidget(m_listWidget, 1);

    auto *btnLayout = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("+"), leftWidget);
    m_removeBtn = new QPushButton(tr("-"), leftWidget);
    m_upBtn = new QPushButton(tr("\xe2\x86\x91"), leftWidget);
    m_downBtn = new QPushButton(tr("\xe2\x86\x93"), leftWidget);
    for (auto *btn : {m_addBtn, m_removeBtn, m_upBtn, m_downBtn})
        btn->setFixedWidth(30);
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_upBtn);
    btnLayout->addWidget(m_downBtn);
    leftLayout->addLayout(btnLayout);

    // Right panel: form fields
    auto *rightWidget = new QWidget(splitter);
    auto *formLayout = new QVBoxLayout(rightWidget);
    formLayout->setContentsMargins(8, 0, 0, 0);

    formLayout->addWidget(new QLabel(tr("Name:"), rightWidget));
    m_nameEdit = new QLineEdit(rightWidget);
    m_nameEdit->setPlaceholderText(tr("Display name (required)"));
    formLayout->addWidget(m_nameEdit);

    formLayout->addWidget(new QLabel(tr("URL:"), rightWidget));
    m_urlEdit = new QLineEdit(rightWidget);
    m_urlEdit->setPlaceholderText(tr("http://localhost:3000"));
    formLayout->addWidget(m_urlEdit);
    m_urlWarningLabel = new QLabel(rightWidget);
    m_urlWarningLabel->setStyleSheet(QStringLiteral("color: red; font-size: 10px;"));
    m_urlWarningLabel->hide();
    formLayout->addWidget(m_urlWarningLabel);

    formLayout->addWidget(new QLabel(tr("Command:"), rightWidget));
    m_commandEdit = new QLineEdit(rightWidget);
    m_commandEdit->setPlaceholderText(tr("e.g. npm run storybook (optional)"));
    formLayout->addWidget(m_commandEdit);

    formLayout->addWidget(new QLabel(tr("Working Directory:"), rightWidget));
    auto *cwdLayout = new QHBoxLayout;
    m_cwdEdit = new QLineEdit(rightWidget);
    m_cwdEdit->setPlaceholderText(tr("(workspace root)"));
    m_browseCwdBtn = new QPushButton(tr("Browse..."), rightWidget);
    cwdLayout->addWidget(m_cwdEdit, 1);
    cwdLayout->addWidget(m_browseCwdBtn);
    formLayout->addLayout(cwdLayout);

    formLayout->addWidget(new QLabel(tr("Environment:"), rightWidget));
    m_envEdit = new QPlainTextEdit(rightWidget);
    m_envEdit->setPlaceholderText(tr("KEY=VALUE (one per line)"));
    m_envEdit->setMaximumHeight(80);
    m_envEdit->setTabChangesFocus(false);
    formLayout->addWidget(m_envEdit);
    m_envWarningLabel = new QLabel(rightWidget);
    m_envWarningLabel->setStyleSheet(QStringLiteral("color: orange; font-size: 10px;"));
    m_envWarningLabel->hide();
    formLayout->addWidget(m_envWarningLabel);

    // Advanced section (collapsible)
    m_advancedGroup = new QGroupBox(tr("Advanced"), rightWidget);
    m_advancedGroup->setCheckable(true);
    m_advancedGroup->setChecked(false);
    auto *advLayout = new QVBoxLayout(m_advancedGroup);
    advLayout->addWidget(new QLabel(tr("Health Check URL:"), m_advancedGroup));
    m_healthUrlEdit = new QLineEdit(m_advancedGroup);
    m_healthUrlEdit->setPlaceholderText(tr("(defaults to main URL)"));
    advLayout->addWidget(m_healthUrlEdit);
    advLayout->addWidget(new QLabel(tr("Startup Timeout (seconds):"), m_advancedGroup));
    m_timeoutSpin = new QSpinBox(m_advancedGroup);
    m_timeoutSpin->setRange(5, 300);
    m_timeoutSpin->setSingleStep(5);
    m_timeoutSpin->setValue(30);
    advLayout->addWidget(m_timeoutSpin);
    formLayout->addWidget(m_advancedGroup);

    // Debug section (collapsible)
    m_debugGroup = new QGroupBox(tr("Debug"), rightWidget);
    m_debugGroup->setCheckable(true);
    m_debugGroup->setChecked(false);
    auto *debugLayout = new QVBoxLayout(m_debugGroup);
    debugLayout->addWidget(new QLabel(tr("CDP Debug Port:"), m_debugGroup));
    auto *portLayout = new QHBoxLayout;
    m_debugPortSpin = new QSpinBox(m_debugGroup);
    m_debugPortSpin->setRange(0, 65535);
    m_debugPortSpin->setSpecialValueText(tr("Disabled"));
    m_debugPortSpin->setToolTip(tr("0 = disabled. Set a port to enable Chrome DevTools Protocol debugging."));
    portLayout->addWidget(m_debugPortSpin, 1);
    m_randomPortBtn = new QPushButton(tr("Random"), m_debugGroup);
    m_randomPortBtn->setToolTip(tr("Find an available port in range 9222-9322"));
    portLayout->addWidget(m_randomPortBtn);
    debugLayout->addLayout(portLayout);
    m_portWarningLabel = new QLabel(m_debugGroup);
    m_portWarningLabel->setStyleSheet(QStringLiteral("color: orange; font-size: 10px;"));
    m_portWarningLabel->hide();
    debugLayout->addWidget(m_portWarningLabel);
    formLayout->addWidget(m_debugGroup);

    // Proxy section (collapsible)
    m_proxyGroup = new QGroupBox(tr("Proxy"), rightWidget);
    m_proxyGroup->setCheckable(true);
    m_proxyGroup->setChecked(false);
    auto *proxyLayout = new QFormLayout(m_proxyGroup);
    m_proxyTypeCombo = new QComboBox(m_proxyGroup);
    m_proxyTypeCombo->addItem(QStringLiteral("HTTP"), 1);
    m_proxyTypeCombo->addItem(QStringLiteral("HTTPS"), 2);
    m_proxyTypeCombo->addItem(QStringLiteral("SOCKS4"), 3);
    m_proxyTypeCombo->addItem(QStringLiteral("SOCKS5"), 4);
    proxyLayout->addRow(tr("Type:"), m_proxyTypeCombo);
    m_proxyHostEdit = new QLineEdit(m_proxyGroup);
    m_proxyHostEdit->setPlaceholderText(QStringLiteral("proxy.example.com"));
    proxyLayout->addRow(tr("Host:"), m_proxyHostEdit);
    m_proxyPortSpin = new QSpinBox(m_proxyGroup);
    m_proxyPortSpin->setRange(0, 65535);
    m_proxyPortSpin->setSpecialValueText(tr("Default"));
    proxyLayout->addRow(tr("Port:"), m_proxyPortSpin);
    m_proxyBypassEdit = new QLineEdit(m_proxyGroup);
    m_proxyBypassEdit->setPlaceholderText(QStringLiteral("localhost;127.0.0.1;[::1];<local>"));
    proxyLayout->addRow(tr("Bypass list:"), m_proxyBypassEdit);
    m_proxyWarningLabel = new QLabel(m_proxyGroup);
    m_proxyWarningLabel->setStyleSheet(QStringLiteral("color: orange; font-size: 10px;"));
    m_proxyWarningLabel->hide();
    proxyLayout->addRow(QString(), m_proxyWarningLabel);
    formLayout->addWidget(m_proxyGroup);

    formLayout->addStretch();

    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    // Button box
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(m_buttonBox);

    // Validation timer
    m_validateTimer = new QTimer(this);
    m_validateTimer->setSingleShot(true);
    m_validateTimer->setInterval(300);
    connect(m_validateTimer, &QTimer::timeout, this, &EditMiniAppsDialog::validateFields);

    // Connections
    connect(m_listWidget, &QListWidget::currentRowChanged, this, &EditMiniAppsDialog::onCurrentRowChanged);
    connect(m_addBtn, &QPushButton::clicked, this, &EditMiniAppsDialog::onAddClicked);
    connect(m_removeBtn, &QPushButton::clicked, this, &EditMiniAppsDialog::onRemoveClicked);
    connect(m_upBtn, &QPushButton::clicked, this, &EditMiniAppsDialog::onMoveUpClicked);
    connect(m_downBtn, &QPushButton::clicked, this, &EditMiniAppsDialog::onMoveDownClicked);
    connect(m_browseCwdBtn, &QPushButton::clicked, this, &EditMiniAppsDialog::onBrowseCwdClicked);
    connect(m_randomPortBtn, &QPushButton::clicked, this, &EditMiniAppsDialog::onRandomPortClicked);
    connect(m_scopeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EditMiniAppsDialog::onScopeChanged);
    connect(m_urlEdit, &QLineEdit::textChanged, this, [this]() { m_validateTimer->start(); });
    connect(m_envEdit, &QPlainTextEdit::textChanged, this, [this]() { m_validateTimer->start(); });
    connect(m_debugPortSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this]() { m_validateTimer->start(); });
    connect(m_proxyGroup, &QGroupBox::toggled, this, [this]() { m_validateTimer->start(); });
    connect(m_proxyHostEdit, &QLineEdit::textChanged, this, [this]() { m_validateTimer->start(); });

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        commitCurrentApp();
        // Validate: all apps must have name and valid URL
        for (int i = 0; i < m_apps.size(); ++i) {
            if (m_apps[i].name.isEmpty() || m_apps[i].url.isEmpty()) {
                m_listWidget->setCurrentRow(i);
                m_nameEdit->setFocus();
                return;
            }
            QUrl u(m_apps[i].url);
            if (!u.isValid() || (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https"))) {
                m_listWidget->setCurrentRow(i);
                m_urlEdit->setFocus();
                return;
            }
        }
        saveCurrentScope();
        accept();
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Load initial scope
    loadScope(0);
}

void EditMiniAppsDialog::onScopeChanged(int index)
{
    commitCurrentApp();
    saveCurrentScope();
    loadScope(index);
}

void EditMiniAppsDialog::loadScope(int scopeIndex)
{
    m_currentScope = scopeIndex;
    m_currentRow = -1;
    m_listWidget->clear();

    if (scopeIndex == 0) {
        m_apps = m_registry->globalApps();
    } else {
        m_apps = m_registry->workspaceApps(m_workspacePath);
    }

    for (const MiniAppDefinition &def : m_apps) {
        m_listWidget->addItem(def.name.isEmpty() ? def.url : def.name);
    }
    if (!m_apps.isEmpty())
        m_listWidget->setCurrentRow(0);
    else
        loadApp(-1);
    updateButtonStates();
}

void EditMiniAppsDialog::saveCurrentScope()
{
    if (m_currentScope == 0) {
        m_registry->setGlobalApps(m_apps);
    } else {
        m_registry->setWorkspaceApps(m_workspacePath, m_apps);
    }
}

void EditMiniAppsDialog::onCurrentRowChanged(int row)
{
    if (m_currentRow >= 0 && m_currentRow < m_apps.size())
        commitCurrentApp();
    m_currentRow = row;
    loadApp(row);
    updateButtonStates();
}

void EditMiniAppsDialog::commitCurrentApp()
{
    if (m_currentRow < 0 || m_currentRow >= m_apps.size())
        return;

    MiniAppDefinition &def = m_apps[m_currentRow];
    def.name = m_nameEdit->text().trimmed();
    def.url = m_urlEdit->text().trimmed();
    def.command = m_commandEdit->text().trimmed();
    def.cwd = m_cwdEdit->text().trimmed();
    def.env = m_envEdit->toPlainText();
    def.healthCheckUrl = m_healthUrlEdit->text().trimmed();
    def.healthTimeoutMs = m_timeoutSpin->value() * 1000;
    def.debugPort = m_debugGroup->isChecked() ? m_debugPortSpin->value() : 0;
    def.proxyType = m_proxyGroup->isChecked() ? m_proxyTypeCombo->currentData().toInt() : 0;
    def.proxyHost = m_proxyHostEdit->text().trimmed();
    def.proxyPort = m_proxyPortSpin->value();
    def.proxyBypassList = m_proxyBypassEdit->text().trimmed();

    // Ensure ID
    if (def.id.isEmpty())
        def.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // Update list item text
    if (m_currentRow < m_listWidget->count()) {
        m_listWidget->item(m_currentRow)->setText(
            def.name.isEmpty() ? def.url : def.name);
    }
}

void EditMiniAppsDialog::loadApp(int row)
{
    const bool valid = (row >= 0 && row < m_apps.size());
    m_nameEdit->setEnabled(valid);
    m_urlEdit->setEnabled(valid);
    m_commandEdit->setEnabled(valid);
    m_cwdEdit->setEnabled(valid);
    m_browseCwdBtn->setEnabled(valid);
    m_envEdit->setEnabled(valid);
    m_healthUrlEdit->setEnabled(valid);
    m_timeoutSpin->setEnabled(valid);
    m_debugPortSpin->setEnabled(valid);
    m_randomPortBtn->setEnabled(valid);

    if (!valid) {
        m_nameEdit->clear();
        m_urlEdit->clear();
        m_commandEdit->clear();
        m_cwdEdit->clear();
        m_envEdit->clear();
        m_healthUrlEdit->clear();
        m_timeoutSpin->setValue(30);
        m_debugPortSpin->setValue(0);
        m_proxyGroup->setChecked(false);
        m_proxyTypeCombo->setCurrentIndex(0);
        m_proxyHostEdit->clear();
        m_proxyPortSpin->setValue(0);
        m_proxyBypassEdit->clear();
        m_proxyWarningLabel->hide();
        m_urlWarningLabel->hide();
        m_envWarningLabel->hide();
        m_portWarningLabel->hide();
        return;
    }

    const MiniAppDefinition &def = m_apps[row];
    m_nameEdit->setText(def.name);
    m_urlEdit->setText(def.url);
    m_commandEdit->setText(def.command);
    m_cwdEdit->setText(def.cwd);
    m_envEdit->setPlainText(def.env);
    m_healthUrlEdit->setText(def.healthCheckUrl);
    m_timeoutSpin->setValue(def.healthTimeoutMs / 1000);
    m_debugPortSpin->setValue(def.debugPort);
    m_debugGroup->setChecked(def.debugPort > 0);
    m_proxyGroup->setChecked(def.proxyType > 0);
    {
        int idx = m_proxyTypeCombo->findData(def.proxyType > 0 ? def.proxyType : 1);
        m_proxyTypeCombo->setCurrentIndex(idx == -1 ? 0 : idx);
    }
    m_proxyHostEdit->setText(def.proxyHost);
    m_proxyPortSpin->setValue(def.proxyPort);
    m_proxyBypassEdit->setText(def.proxyBypassList);
    m_urlWarningLabel->hide();
    m_portWarningLabel->hide();
    validateFields();
}

void EditMiniAppsDialog::onAddClicked()
{
    commitCurrentApp();
    MiniAppDefinition def;
    def.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    def.name = tr("New App");
    m_apps.append(def);
    m_listWidget->addItem(def.name);
    m_listWidget->setCurrentRow(m_apps.size() - 1);
    m_nameEdit->selectAll();
    m_nameEdit->setFocus();
}

void EditMiniAppsDialog::onRemoveClicked()
{
    const int row = m_listWidget->currentRow();
    if (row < 0 || row >= m_apps.size()) return;

    m_currentRow = -1;
    m_apps.removeAt(row);
    delete m_listWidget->takeItem(row);

    if (!m_apps.isEmpty())
        m_listWidget->setCurrentRow(qMin(row, m_apps.size() - 1));
    else
        loadApp(-1);
    updateButtonStates();
}

void EditMiniAppsDialog::onMoveUpClicked()
{
    const int row = m_listWidget->currentRow();
    if (row <= 0) return;
    commitCurrentApp();
    m_apps.swapItemsAt(row, row - 1);
    m_currentRow = row - 1;
    QSignalBlocker blocker(m_listWidget);
    auto *item = m_listWidget->takeItem(row);
    m_listWidget->insertItem(row - 1, item);
    m_listWidget->setCurrentRow(row - 1);
    blocker.unblock();
    loadApp(m_currentRow);
    updateButtonStates();
}

void EditMiniAppsDialog::onMoveDownClicked()
{
    const int row = m_listWidget->currentRow();
    if (row < 0 || row >= m_apps.size() - 1) return;
    commitCurrentApp();
    m_apps.swapItemsAt(row, row + 1);
    m_currentRow = row + 1;
    QSignalBlocker blocker(m_listWidget);
    auto *item = m_listWidget->takeItem(row);
    m_listWidget->insertItem(row + 1, item);
    m_listWidget->setCurrentRow(row + 1);
    blocker.unblock();
    loadApp(m_currentRow);
    updateButtonStates();
}

void EditMiniAppsDialog::onBrowseCwdClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Working Directory"), m_cwdEdit->text());
    if (!dir.isEmpty())
        m_cwdEdit->setText(dir);
}

void EditMiniAppsDialog::validateFields()
{
    // URL validation
    const QString url = m_urlEdit->text().trimmed();
    if (!url.isEmpty()) {
        QUrl u(url);
        if (!u.isValid() || (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https"))) {
            m_urlWarningLabel->setText(tr("URL must start with http:// or https://"));
            m_urlWarningLabel->show();
        } else {
            m_urlWarningLabel->hide();
        }
    } else {
        m_urlWarningLabel->hide();
    }

    // Debug port conflict validation
    const int port = m_debugPortSpin->value();
    if (port > 0 && m_currentRow >= 0) {
        bool conflict = false;
        for (int i = 0; i < m_apps.size(); ++i) {
            if (i == m_currentRow) continue;
            if (m_apps[i].debugPort == port) {
                m_portWarningLabel->setText(tr("Port %1 is already used by \"%2\"")
                    .arg(port).arg(m_apps[i].name));
                m_portWarningLabel->show();
                conflict = true;
                break;
            }
        }
        if (!conflict)
            m_portWarningLabel->hide();
    } else {
        m_portWarningLabel->hide();
    }

    // Proxy validation
    if (m_proxyGroup->isChecked() && m_proxyHostEdit->text().trimmed().isEmpty()) {
        m_proxyWarningLabel->setText(tr("Proxy host is empty — proxy will not be applied"));
        m_proxyWarningLabel->show();
    } else {
        m_proxyWarningLabel->hide();
    }

    // Env validation (same as EditTasksDialog)
    const QString envText = m_envEdit->toPlainText();
    if (envText.trimmed().isEmpty()) {
        m_envWarningLabel->hide();
        return;
    }
    QList<int> badLines;
    const QStringList lines = envText.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.indexOf(QLatin1Char('=')) <= 0)
            badLines.append(i + 1);
    }
    if (badLines.isEmpty()) {
        m_envWarningLabel->hide();
    } else {
        QStringList nums;
        for (int n : badLines) nums.append(QString::number(n));
        m_envWarningLabel->setText(tr("Lines %1: invalid KEY=VALUE format").arg(nums.join(QStringLiteral(", "))));
        m_envWarningLabel->show();
    }
}

void EditMiniAppsDialog::updateButtonStates()
{
    const int row = m_listWidget->currentRow();
    const int count = m_apps.size();
    m_removeBtn->setEnabled(row >= 0);
    m_upBtn->setEnabled(row > 0);
    m_downBtn->setEnabled(row >= 0 && row < count - 1);
}

void EditMiniAppsDialog::onRandomPortClicked()
{
    // Collect ports already assigned to other apps in the list
    QSet<int> usedPorts;
    for (int i = 0; i < m_apps.size(); ++i) {
        if (i == m_currentRow) continue;
        if (m_apps[i].debugPort > 0)
            usedPorts.insert(m_apps[i].debugPort);
    }

    // Scan 9222-9322 for an available port
    for (int port = 9222; port <= 9322; ++port) {
        if (usedPorts.contains(port))
            continue;

        // Bind-test: check if port is actually free on the system
        QTcpSocket sock;
        if (sock.bind(QHostAddress::LocalHost, static_cast<quint16>(port))) {
            sock.close();
            m_debugPortSpin->setValue(port);
            return;
        }
    }

    // All ports exhausted
    QMessageBox::warning(this, tr("No Available Port"),
        tr("No available port in range 9222-9322. Please enter a port manually."));
}
