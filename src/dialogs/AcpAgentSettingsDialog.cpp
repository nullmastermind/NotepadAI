/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#include "AcpAgentSettingsDialog.h"
#include "ui_AcpAgentSettingsDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidgetItem>
#include <QUuid>
#include <QVBoxLayout>

#include "AcpAgentRegistry.h"


namespace {

QString joinArgsForDisplay(const QStringList &args)
{
    return args.join(QLatin1Char(' '));
}

QStringList splitArgsFromInput(const QString &text)
{
    // Newline-separated, one arg per line. Empty lines are skipped.
    QStringList out;
    const QStringList rawLines = text.split(QLatin1Char('\n'));
    for (const QString &line : rawLines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            out.append(trimmed);
        }
    }
    return out;
}

QString joinArgsForEdit(const QStringList &args)
{
    return args.join(QLatin1Char('\n'));
}

// Minimal in-place editor dialog for an agent definition. Returns true on accept.
class EditAgentDialog : public QDialog
{
public:
    EditAgentDialog(QWidget *parent, AcpAgentDefinition *def)
        : QDialog(parent)
        , m_def(def)
    {
        setWindowTitle(tr("Edit Agent"));
        auto *form = new QFormLayout;

        m_nameEdit = new QLineEdit(def->name, this);
        form->addRow(tr("Name:"), m_nameEdit);

        m_commandEdit = new QLineEdit(def->command, this);
        form->addRow(tr("Command:"), m_commandEdit);

        m_argsEdit = new QPlainTextEdit(joinArgsForEdit(def->args), this);
        m_argsEdit->setPlaceholderText(tr("One argument per line"));
        form->addRow(tr("Arguments:"), m_argsEdit);

        QString envText;
        for (auto it = def->env.constBegin(); it != def->env.constEnd(); ++it) {
            envText += it.key() + QLatin1Char('=') + it.value() + QLatin1Char('\n');
        }
        m_envEdit = new QPlainTextEdit(envText, this);
        m_envEdit->setPlaceholderText(tr("KEY=VALUE, one per line"));
        form->addRow(tr("Environment:"), m_envEdit);

        m_iconEdit = new QLineEdit(def->icon, this);
        form->addRow(tr("Icon:"), m_iconEdit);

        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto *root = new QVBoxLayout(this);
        root->addLayout(form);
        root->addWidget(buttons);
    }

    void accept() override
    {
        const QString name = m_nameEdit->text().trimmed();
        const QString command = m_commandEdit->text().trimmed();
        if (name.isEmpty() || command.isEmpty()) {
            QMessageBox::warning(this, tr("Invalid agent"),
                tr("Name and Command are required."));
            return;
        }

        m_def->name = name;
        m_def->command = command;
        m_def->args = splitArgsFromInput(m_argsEdit->toPlainText());
        m_def->icon = m_iconEdit->text().trimmed();

        m_def->env.clear();
        const QStringList envLines = m_envEdit->toPlainText().split(QLatin1Char('\n'));
        for (const QString &line : envLines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            const int eq = trimmed.indexOf(QLatin1Char('='));
            if (eq <= 0) {
                continue;
            }
            const QString key = trimmed.left(eq).trimmed();
            const QString val = trimmed.mid(eq + 1);
            if (!key.isEmpty()) {
                m_def->env.insert(key, val);
            }
        }

        QDialog::accept();
    }

private:
    AcpAgentDefinition *m_def;
    QLineEdit *m_nameEdit;
    QLineEdit *m_commandEdit;
    QPlainTextEdit *m_argsEdit;
    QPlainTextEdit *m_envEdit;
    QLineEdit *m_iconEdit;
};

} // namespace


AcpAgentSettingsDialog::AcpAgentSettingsDialog(AcpAgentRegistry *registry, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::AcpAgentSettingsDialog)
    , m_registry(registry)
{
    ui->setupUi(this);

    ui->agentTable->setColumnCount(4);
    QStringList headers;
    headers << tr("Name") << tr("Command") << tr("Arguments") << tr("Built-in");
    ui->agentTable->setHorizontalHeaderLabels(headers);
    ui->agentTable->horizontalHeader()->setStretchLastSection(true);

    ui->autoApproveCombo->addItem(tr("Manual — ask before each action"),
        QStringLiteral("manual"));
    ui->autoApproveCombo->addItem(tr("Allow All — auto-approve everything"),
        QStringLiteral("allowAll"));

    connect(ui->addButton, &QPushButton::clicked, this, &AcpAgentSettingsDialog::onAdd);
    connect(ui->editButton, &QPushButton::clicked, this, &AcpAgentSettingsDialog::onEdit);
    connect(ui->removeButton, &QPushButton::clicked, this, &AcpAgentSettingsDialog::onRemove);
    connect(ui->agentTable, &QTableWidget::itemSelectionChanged,
        this, &AcpAgentSettingsDialog::onSelectionChanged);
    connect(ui->defaultAgentCombo, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &AcpAgentSettingsDialog::onDefaultAgentIndexChanged);
    connect(ui->autoApproveCombo, qOverload<int>(&QComboBox::currentIndexChanged),
        this, &AcpAgentSettingsDialog::onAutoApproveIndexChanged);

    if (m_registry) {
        connect(m_registry, &AcpAgentRegistry::changed,
            this, &AcpAgentSettingsDialog::refreshAgentTable);
        connect(m_registry, &AcpAgentRegistry::changed,
            this, &AcpAgentSettingsDialog::refreshDefaultAgentCombo);
    }

    refreshAgentTable();
    refreshDefaultAgentCombo();
    refreshAutoApproveCombo();
    onSelectionChanged();
}

AcpAgentSettingsDialog::~AcpAgentSettingsDialog()
{
    delete ui;
}

void AcpAgentSettingsDialog::refreshAgentTable()
{
    if (!m_registry) {
        return;
    }
    const QString previousSelection = selectedAgentId();

    ui->agentTable->blockSignals(true);
    ui->agentTable->setRowCount(0);
    const QList<AcpAgentDefinition> agents = m_registry->agents();
    int restoreRow = -1;
    for (int i = 0; i < agents.size(); ++i) {
        const AcpAgentDefinition &def = agents.at(i);
        ui->agentTable->insertRow(i);

        auto *nameItem = new QTableWidgetItem(def.name);
        nameItem->setData(Qt::UserRole, def.id);
        ui->agentTable->setItem(i, 0, nameItem);

        ui->agentTable->setItem(i, 1, new QTableWidgetItem(def.command));
        ui->agentTable->setItem(i, 2, new QTableWidgetItem(joinArgsForDisplay(def.args)));
        ui->agentTable->setItem(i, 3, new QTableWidgetItem(
            def.builtin ? tr("Yes") : tr("No")));

        if (def.id == previousSelection) {
            restoreRow = i;
        }
    }
    ui->agentTable->blockSignals(false);

    if (restoreRow >= 0) {
        ui->agentTable->selectRow(restoreRow);
    } else if (ui->agentTable->rowCount() > 0) {
        ui->agentTable->selectRow(0);
    }
    onSelectionChanged();
}

void AcpAgentSettingsDialog::refreshDefaultAgentCombo()
{
    if (!m_registry) {
        return;
    }
    ui->defaultAgentCombo->blockSignals(true);
    ui->defaultAgentCombo->clear();
    const QList<AcpAgentDefinition> agents = m_registry->agents();
    for (const AcpAgentDefinition &def : agents) {
        ui->defaultAgentCombo->addItem(def.name, def.id);
    }
    const QString current = m_registry->defaultAgentId();
    const int idx = ui->defaultAgentCombo->findData(current);
    if (idx >= 0) {
        ui->defaultAgentCombo->setCurrentIndex(idx);
    }
    ui->defaultAgentCombo->blockSignals(false);
}

void AcpAgentSettingsDialog::refreshAutoApproveCombo()
{
    if (!m_registry) {
        return;
    }
    ui->autoApproveCombo->blockSignals(true);
    const QString policy = m_registry->autoApprovePolicy();
    const int idx = ui->autoApproveCombo->findData(policy);
    ui->autoApproveCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    ui->autoApproveCombo->blockSignals(false);
}

QString AcpAgentSettingsDialog::selectedAgentId() const
{
    const QList<QTableWidgetItem *> selected = ui->agentTable->selectedItems();
    if (selected.isEmpty()) {
        return QString();
    }
    const int row = selected.first()->row();
    const QTableWidgetItem *nameItem = ui->agentTable->item(row, 0);
    if (!nameItem) {
        return QString();
    }
    return nameItem->data(Qt::UserRole).toString();
}

void AcpAgentSettingsDialog::onSelectionChanged()
{
    const QString id = selectedAgentId();
    bool isBuiltin = false;
    bool hasSelection = !id.isEmpty();
    if (m_registry && hasSelection) {
        isBuiltin = m_registry->agent(id).builtin;
    }
    ui->editButton->setEnabled(hasSelection && !isBuiltin);
    ui->removeButton->setEnabled(hasSelection && !isBuiltin);
}

void AcpAgentSettingsDialog::onAdd()
{
    if (!m_registry) {
        return;
    }
    AcpAgentDefinition def;
    def.id = QStringLiteral("user:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    def.builtin = false;

    EditAgentDialog edit(this, &def);
    if (edit.exec() != QDialog::Accepted) {
        return;
    }
    if (!m_registry->addAgent(def)) {
        QMessageBox::warning(this, tr("Add agent failed"),
            tr("Could not add the agent. The id may already exist."));
    }
}

void AcpAgentSettingsDialog::onEdit()
{
    if (!m_registry) {
        return;
    }
    const QString id = selectedAgentId();
    if (id.isEmpty()) {
        return;
    }
    AcpAgentDefinition def = m_registry->agent(id);
    if (def.id.isEmpty() || def.builtin) {
        return;
    }

    EditAgentDialog edit(this, &def);
    if (edit.exec() != QDialog::Accepted) {
        return;
    }
    if (!m_registry->updateAgent(def)) {
        QMessageBox::warning(this, tr("Update agent failed"),
            tr("Could not update the agent."));
    }
}

void AcpAgentSettingsDialog::onRemove()
{
    if (!m_registry) {
        return;
    }
    const QString id = selectedAgentId();
    if (id.isEmpty()) {
        return;
    }
    const AcpAgentDefinition def = m_registry->agent(id);
    if (def.builtin) {
        return;
    }
    const auto answer = QMessageBox::question(this, tr("Remove agent"),
        tr("Remove agent \"%1\"?").arg(def.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_registry->removeAgent(id);
}

void AcpAgentSettingsDialog::onDefaultAgentIndexChanged(int index)
{
    if (!m_registry || index < 0) {
        return;
    }
    const QString id = ui->defaultAgentCombo->itemData(index).toString();
    if (!id.isEmpty()) {
        m_registry->setDefaultAgentId(id);
    }
}

void AcpAgentSettingsDialog::onAutoApproveIndexChanged(int index)
{
    if (!m_registry || index < 0) {
        return;
    }
    const QString policy = ui->autoApproveCombo->itemData(index).toString();
    if (!policy.isEmpty()) {
        m_registry->setAutoApprovePolicy(policy);
    }
}
