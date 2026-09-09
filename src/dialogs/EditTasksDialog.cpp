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

#include "EditTasksDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>

EditTasksDialog::EditTasksDialog(const QString &workspacePath,
                                 const QList<TerminalTask> &tasks,
                                 QWidget *parent)
    : QDialog(parent)
    , m_workspacePath(workspacePath)
    , m_tasks(tasks)
{
    setWindowTitle(tr("Edit Tasks"));
    resize(700, 480);

    auto *mainLayout = new QVBoxLayout(this);

    auto *splitter = new QSplitter(this);
    mainLayout->addWidget(splitter, 1);

    // Left panel: task list + buttons
    auto *leftWidget = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_listWidget = new QListWidget(leftWidget);
    m_listWidget->setMinimumWidth(160);
    leftLayout->addWidget(m_listWidget, 1);

    auto *listBtnLayout = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("+"), leftWidget);
    m_removeBtn = new QPushButton(tr("-"), leftWidget);
    m_upBtn = new QPushButton(tr("\xe2\x96\xb2"), leftWidget);
    m_downBtn = new QPushButton(tr("\xe2\x96\xbc"), leftWidget);
    m_addBtn->setFixedWidth(32);
    m_removeBtn->setFixedWidth(32);
    m_upBtn->setFixedWidth(32);
    m_downBtn->setFixedWidth(32);
    listBtnLayout->addWidget(m_addBtn);
    listBtnLayout->addWidget(m_removeBtn);
    listBtnLayout->addWidget(m_upBtn);
    listBtnLayout->addWidget(m_downBtn);
    listBtnLayout->addStretch();
    leftLayout->addLayout(listBtnLayout);

    splitter->addWidget(leftWidget);

    // Right panel: task form
    auto *rightWidget = new QWidget(splitter);
    auto *formLayout = new QVBoxLayout(rightWidget);
    formLayout->setContentsMargins(8, 0, 0, 0);

    formLayout->addWidget(new QLabel(tr("Name:"), rightWidget));
    m_nameEdit = new QLineEdit(rightWidget);
    formLayout->addWidget(m_nameEdit);

    formLayout->addWidget(new QLabel(tr("Command:"), rightWidget));
    m_commandEdit = new QLineEdit(rightWidget);
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
    m_envEdit->setPlaceholderText(tr("KEY=VALUE (one per line, # for comments)"));
    m_envEdit->setTabChangesFocus(false);
    formLayout->addWidget(m_envEdit, 1);

    m_envWarningLabel = new QLabel(rightWidget);
    m_envWarningLabel->setStyleSheet(QStringLiteral("color: #b58900;"));
    m_envWarningLabel->hide();
    formLayout->addWidget(m_envWarningLabel);

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    // Button box
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(m_buttonBox);

    // Env validation timer (debounce 300ms)
    m_envValidateTimer = new QTimer(this);
    m_envValidateTimer->setSingleShot(true);
    m_envValidateTimer->setInterval(300);
    connect(m_envValidateTimer, &QTimer::timeout, this, &EditTasksDialog::validateEnv);

    // Connections
    connect(m_listWidget, &QListWidget::currentRowChanged, this, &EditTasksDialog::onCurrentRowChanged);
    connect(m_addBtn, &QPushButton::clicked, this, &EditTasksDialog::onAddClicked);
    connect(m_removeBtn, &QPushButton::clicked, this, &EditTasksDialog::onRemoveClicked);
    connect(m_upBtn, &QPushButton::clicked, this, &EditTasksDialog::onMoveUpClicked);
    connect(m_downBtn, &QPushButton::clicked, this, &EditTasksDialog::onMoveDownClicked);
    connect(m_browseCwdBtn, &QPushButton::clicked, this, &EditTasksDialog::onBrowseCwdClicked);
    connect(m_envEdit, &QPlainTextEdit::textChanged, this, [this]() { m_envValidateTimer->start(); });
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        commitCurrentTask();
        // Validate: all tasks must have non-empty command
        for (int i = 0; i < m_tasks.size(); ++i) {
            if (m_tasks[i].command.isEmpty()) {
                m_listWidget->setCurrentRow(i);
                m_commandEdit->setFocus();
                m_commandEdit->setStyleSheet(QStringLiteral("border: 1px solid red;"));
                return;
            }
        }
        accept();
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Populate list. Empty open would leave the form enabled with
    // m_currentRow == -1, so typing looked real but OK discarded it.
    for (const TerminalTask &t : m_tasks) {
        m_listWidget->addItem(t.name.isEmpty() ? t.command : t.name);
    }
    if (!m_tasks.isEmpty()) {
        m_listWidget->setCurrentRow(0);
    } else {
        onAddClicked();
    }
    updateButtonStates();
}

QList<TerminalTask> EditTasksDialog::tasks() const
{
    return m_tasks;
}

void EditTasksDialog::onCurrentRowChanged(int row)
{
    if (m_currentRow >= 0 && m_currentRow < m_tasks.size()) {
        commitCurrentTask();
    }
    m_currentRow = row;
    loadTask(row);
    updateButtonStates();
}

void EditTasksDialog::commitCurrentTask()
{
    if (m_currentRow < 0 || m_currentRow >= m_tasks.size())
        return;

    TerminalTask &t = m_tasks[m_currentRow];
    t.name = m_nameEdit->text().trimmed();
    t.command = m_commandEdit->text().trimmed();
    t.env = m_envEdit->toPlainText();
    t.cwd = m_cwdEdit->text().trimmed();

    // Convert absolute path inside workspace to relative
    if (!t.cwd.isEmpty() && QDir::isAbsolutePath(t.cwd)) {
        const QString cleanCwd = QDir::cleanPath(t.cwd);
        const QString cleanWs = QDir::cleanPath(m_workspacePath);
        if (cleanCwd.startsWith(cleanWs + QLatin1Char('/')) || cleanCwd == cleanWs) {
            QDir wsDir(cleanWs);
            t.cwd = wsDir.relativeFilePath(cleanCwd);
            if (t.cwd == QLatin1String("."))
                t.cwd.clear();
        }
    }

    // Update list item text
    const QString display = t.name.isEmpty() ? t.command : t.name;
    if (auto *item = m_listWidget->item(m_currentRow))
        item->setText(display.isEmpty() ? tr("(new task)") : display);

    m_commandEdit->setStyleSheet(QString());
}

void EditTasksDialog::loadTask(int row)
{
    const bool valid = (row >= 0 && row < m_tasks.size());
    m_nameEdit->setEnabled(valid);
    m_commandEdit->setEnabled(valid);
    m_envEdit->setEnabled(valid);
    m_cwdEdit->setEnabled(valid);
    m_browseCwdBtn->setEnabled(valid);

    if (!valid) {
        m_nameEdit->clear();
        m_commandEdit->clear();
        m_envEdit->clear();
        m_cwdEdit->clear();
        m_envWarningLabel->hide();
        return;
    }

    const TerminalTask &t = m_tasks[row];
    m_nameEdit->setText(t.name);
    m_commandEdit->setText(t.command);
    m_envEdit->setPlainText(t.env);
    m_cwdEdit->setText(t.cwd);
    m_commandEdit->setStyleSheet(QString());
    validateEnv();
}

void EditTasksDialog::onAddClicked()
{
    commitCurrentTask();
    TerminalTask newTask;
    newTask.name = tr("New Task");
    m_tasks.append(newTask);
    m_listWidget->addItem(newTask.name);
    m_listWidget->setCurrentRow(m_tasks.size() - 1);
    m_nameEdit->selectAll();
    m_nameEdit->setFocus();
}

void EditTasksDialog::onRemoveClicked()
{
    const int row = m_listWidget->currentRow();
    if (row < 0 || row >= m_tasks.size())
        return;

    m_currentRow = -1; // prevent commitCurrentTask on row change
    m_tasks.removeAt(row);
    delete m_listWidget->takeItem(row);

    if (!m_tasks.isEmpty()) {
        m_listWidget->setCurrentRow(qMin(row, m_tasks.size() - 1));
    } else {
        loadTask(-1);
    }
    updateButtonStates();
}

void EditTasksDialog::onMoveUpClicked()
{
    moveCurrentBy(-1);
}

void EditTasksDialog::onMoveDownClicked()
{
    moveCurrentBy(1);
}

void EditTasksDialog::moveCurrentBy(int delta)
{
    const int row = m_listWidget->currentRow();
    const int dest = row + delta;
    if (row < 0 || dest < 0 || dest >= m_tasks.size())
        return;
    commitCurrentTask();
    m_tasks.swapItemsAt(row, dest);
    // takeItem/insertItem emit currentRowChanged. Commit during that window
    // writes the form into the post-swap neighbor and duplicates the moved task.
    m_currentRow = -1;
    {
        const QSignalBlocker blocker(m_listWidget);
        auto *item = m_listWidget->takeItem(row);
        m_listWidget->insertItem(dest, item);
        m_listWidget->setCurrentRow(dest);
    }
    m_currentRow = dest;
    loadTask(m_currentRow);
    updateButtonStates();
}

void EditTasksDialog::onBrowseCwdClicked()
{
    QString startDir = m_workspacePath;
    if (!m_cwdEdit->text().trimmed().isEmpty()) {
        QString current = m_cwdEdit->text().trimmed();
        if (QDir::isRelativePath(current))
            current = QDir::cleanPath(m_workspacePath + QLatin1Char('/') + current);
        if (QDir(current).exists())
            startDir = current;
    }

    const QString dir = QFileDialog::getExistingDirectory(this, tr("Select Working Directory"), startDir);
    if (dir.isEmpty())
        return;

    const QString cleanDir = QDir::cleanPath(dir);
    const QString cleanWs = QDir::cleanPath(m_workspacePath);
    if (cleanDir.startsWith(cleanWs + QLatin1Char('/')) || cleanDir == cleanWs) {
        QDir wsDir(cleanWs);
        QString rel = wsDir.relativeFilePath(cleanDir);
        if (rel == QLatin1String("."))
            rel.clear();
        m_cwdEdit->setText(rel);
    } else {
        m_cwdEdit->setText(cleanDir);
    }
}

void EditTasksDialog::validateEnv()
{
    const QString text = m_envEdit->toPlainText();
    if (text.trimmed().isEmpty()) {
        m_envWarningLabel->hide();
        return;
    }

    QList<int> badLines;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            badLines.append(i + 1);
    }

    if (badLines.isEmpty()) {
        m_envWarningLabel->hide();
    } else if (badLines.size() <= 3) {
        QStringList nums;
        for (int n : badLines) nums.append(QString::number(n));
        m_envWarningLabel->setText(
            tr("\xe2\x9a\xa0 Lines %1: invalid format (will be ignored)").arg(nums.join(QStringLiteral(", "))));
        m_envWarningLabel->show();
    } else {
        m_envWarningLabel->setText(
            tr("\xe2\x9a\xa0 %1 lines have invalid format (will be ignored)").arg(badLines.size()));
        m_envWarningLabel->show();
    }
}

void EditTasksDialog::updateButtonStates()
{
    const int row = m_listWidget->currentRow();
    const int count = m_tasks.size();
    m_removeBtn->setEnabled(row >= 0);
    m_upBtn->setEnabled(row > 0);
    m_downBtn->setEnabled(row >= 0 && row < count - 1);
}
