/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "Pm2ProcessEditDialog.h"
#include "control/ControlPicker.h"

#include "Pm2Protocol.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

struct ShellChoice
{
    const char *value;
    const char *label;
};

#ifdef Q_OS_WIN
constexpr ShellChoice kShells[] = {
    {"", QT_TRANSLATE_NOOP("Pm2ProcessEditDialog", "Default (cmd.exe)")},
    {"powershell", "Windows PowerShell"},
    {"pwsh", "PowerShell 7 (pwsh)"},
    {"bash", "bash"},
};
#else
constexpr ShellChoice kShells[] = {
    {"", QT_TRANSLATE_NOOP("Pm2ProcessEditDialog", "Default (/bin/sh)")},
    {"bash", "bash"},
    {"pwsh", "PowerShell (pwsh)"},
};
#endif

QPlainTextEdit *codeEdit(QWidget *parent, int lines)
{
    auto *edit = new QPlainTextEdit(parent);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit->setTabChangesFocus(true);
    const int lineHeight = edit->fontMetrics().lineSpacing();
    edit->setMinimumHeight(lineHeight * lines + 2 * edit->frameWidth() + 8);
    return edit;
}

} // namespace

Pm2ProcessEditDialog::Pm2ProcessEditDialog(const std::optional<Pm2Definition> &original,
                                           const QString &defaultCwd, const QStringList &takenNames,
                                           QWidget *parent)
    : QDialog(parent)
    , m_original(original)
    , m_takenNames(takenNames)
{
    setWindowTitle(original ? tr("Edit PM2 Process") : tr("New PM2 Process"));
    resize(560, 0);

    if (m_original)
        m_takenNames.removeAll(m_original->name);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *form = new QFormLayout;
    form->setSpacing(6);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("api-server"));
    form->addRow(tr("Name:"), m_name);

    m_command = codeEdit(this, 3);
    m_command->setPlaceholderText(tr("npm run dev"));
    m_command->setToolTip(tr("Runs through the shell below; NotepadAI writes an entrypoint script that pm2 keeps running."));
    form->addRow(tr("Command:"), m_command);

    m_shell = new QComboBox(this);
    for (const ShellChoice &s : kShells)
        m_shell->addItem(tr(s.label), QString::fromLatin1(s.value));
    form->addRow(tr("Shell:"), m_shell);

    auto *cwdRow = new QHBoxLayout;
    cwdRow->setSpacing(4);
    m_cwd = new QLineEdit(this);
    auto *browse = new QPushButton(tr("Browse…"), this);
    cwdRow->addWidget(m_cwd, 1);
    cwdRow->addWidget(browse);
    m_cwd->setToolTip(tr("The command runs in this folder; pm2's Watch option watches it too."));
    browse->setToolTip(tr("Choose the folder the command runs in"));
    form->addRow(tr("Working directory:"), cwdRow);

    m_env = codeEdit(this, 4);
    m_env->setPlaceholderText(tr("KEY=value, one per line (# for comments)"));
    form->addRow(tr("Environment:"), m_env);
    layout->addLayout(form);

    m_advancedToggle = new QToolButton(this);
    m_advancedToggle->setText(tr("Advanced"));
    m_advancedToggle->setCheckable(true);
    m_advancedToggle->setAutoRaise(true);
    m_advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advancedToggle->setArrowType(Qt::RightArrow);
    layout->addWidget(m_advancedToggle, 0, Qt::AlignLeft);

    m_advanced = new QWidget(this);
    auto *advanced = new QFormLayout(m_advanced);
    advanced->setContentsMargins(0, 0, 0, 0);
    advanced->setSpacing(6);
    m_autorestart = new QCheckBox(tr("Restart when it exits"), m_advanced);
    m_watch = new QCheckBox(tr("Restart when files in the directory change"), m_advanced);
    m_watch->setToolTip(tr("node_modules and .git are ignored."));
    m_maxMemory = new QLineEdit(m_advanced);
    m_maxMemory->setPlaceholderText(tr("e.g. 512M (empty: no limit)"));
    m_restartDelay = new QSpinBox(m_advanced);
    m_restartDelay->setRange(0, 3'600'000);
    m_restartDelay->setSingleStep(500);
    m_restartDelay->setSuffix(tr(" ms"));
    advanced->addRow(QString(), m_autorestart);
    advanced->addRow(QString(), m_watch);
    advanced->addRow(tr("Max memory:"), m_maxMemory);
    advanced->addRow(tr("Restart delay:"), m_restartDelay);
    m_advanced->hide();
    layout->addWidget(m_advanced);

    m_problem = new QLabel(this);
    m_problem->setWordWrap(true);
    m_recreateNote = new QLabel(tr("Saving deletes the process and starts it again, so its restart count goes back to 0."), this);
    m_recreateNote->setWordWrap(true);
    m_recreateNote->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_problem);
    layout->addWidget(m_recreateNote);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(original ? tr("Save") : tr("Start"));
    layout->addWidget(m_buttons);

    // Fill.
    const Pm2Definition def = original.value_or(Pm2Definition{});
    m_name->setText(def.name);
    m_command->setPlainText(def.command);
    m_shell->setCurrentIndex(qMax(0, m_shell->findData(def.shell)));
    m_cwd->setText(QDir::toNativeSeparators(original ? def.cwd : defaultCwd));
    m_env->setPlainText(def.envText);
    m_autorestart->setChecked(def.options.autorestart);
    m_watch->setChecked(def.options.watch);
    m_maxMemory->setText(def.options.maxMemoryRestart);
    m_restartDelay->setValue(def.options.restartDelayMs);
    if (original && !(def.options == Pm2Options{}))
        m_advancedToggle->setChecked(true);

    connect(m_advancedToggle, &QToolButton::toggled, this, [this](bool on) {
        m_advancedToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        // Grow or shrink by the section's height only: adjustSize() would
        // also snap the width back to the minimum.
        const int before = height();
        const int section = m_advanced->sizeHint().height() + this->layout()->spacing();
        m_advanced->setVisible(on);
        this->layout()->activate();
        resize(width(), qMax(minimumSizeHint().height(), before + (on ? section : -section)));
    });
    m_advanced->setVisible(m_advancedToggle->isChecked());
    m_advancedToggle->setArrowType(m_advancedToggle->isChecked() ? Qt::DownArrow : Qt::RightArrow);

    connect(browse, &QPushButton::clicked, this, &Pm2ProcessEditDialog::browseCwd);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (QLineEdit *e : {m_name, m_cwd, m_maxMemory})
        connect(e, &QLineEdit::textChanged, this, &Pm2ProcessEditDialog::updateState);
    for (QPlainTextEdit *e : {m_command, m_env})
        connect(e, &QPlainTextEdit::textChanged, this, &Pm2ProcessEditDialog::updateState);
    for (QCheckBox *c : {m_autorestart, m_watch})
        connect(c, &QCheckBox::toggled, this, &Pm2ProcessEditDialog::updateState);
    connect(m_shell, &QComboBox::currentIndexChanged, this, &Pm2ProcessEditDialog::updateState);
    connect(m_restartDelay, &QSpinBox::valueChanged, this, &Pm2ProcessEditDialog::updateState);

    updateState();
    (original ? static_cast<QWidget *>(m_command) : m_name)->setFocus();
}

Pm2Definition Pm2ProcessEditDialog::definition() const
{
    Pm2Definition def;
    def.name = m_name->text().trimmed();
    def.command = m_command->toPlainText().trimmed();
    def.shell = m_shell->currentData().toString();
    def.cwd = QDir::toNativeSeparators(QDir::cleanPath(m_cwd->text().trimmed()));
    def.envText = m_env->toPlainText();
    def.options.autorestart = m_autorestart->isChecked();
    def.options.watch = m_watch->isChecked();
    def.options.maxMemoryRestart = m_maxMemory->text().trimmed().toUpper();
    def.options.restartDelayMs = m_restartDelay->value();
    return def;
}

bool Pm2ProcessEditDialog::needsRecreate() const
{
    if (!m_original)
        return false;
    const Pm2Definition def = definition();
    return def.name != m_original->name || QDir::cleanPath(def.cwd) != QDir::cleanPath(m_original->cwd)
        || !(def.options == m_original->options);
}

void Pm2ProcessEditDialog::updateState()
{
    const Pm2Definition def = definition();
    static const QRegularExpression memory(QStringLiteral("^\\d+[KMG]?$"));

    QString problem = Pm2Protocol::validateName(def.name, m_takenNames);
    if (problem.isEmpty() && def.command.isEmpty())
        problem = tr("Enter the command to run.");
    if (problem.isEmpty() && (m_cwd->text().trimmed().isEmpty() || !QFileInfo(def.cwd).isDir()))
        problem = tr("Choose an existing working directory.");
    if (problem.isEmpty()) {
        // A line the .env parser would skip silently.
        int lineNo = 0;
        for (const QStringView raw : QStringView(def.envText).split(QLatin1Char('\n'))) {
            ++lineNo;
            const QStringView line = raw.trimmed();
            if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')) && line.indexOf(QLatin1Char('=')) <= 0) {
                problem = tr("Environment line %1 is not KEY=value.").arg(lineNo);
                break;
            }
        }
    }
    if (problem.isEmpty() && !def.options.maxMemoryRestart.isEmpty()
        && !memory.match(def.options.maxMemoryRestart).hasMatch())
        problem = tr("Write the memory limit as a number with K, M or G, e.g. 512M.");

    // Untouched New form: no complaint yet, just a disabled button.
    const bool pristine = !m_original && m_name->text().isEmpty() && m_command->toPlainText().isEmpty();
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty() && !pristine);
    m_recreateNote->setVisible(needsRecreate());
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

void Pm2ProcessEditDialog::browseCwd()
{
    const QString dir = control::picker::getExistingDirectory(this, tr("Working Directory"), m_cwd->text());
    if (!dir.isEmpty())
        m_cwd->setText(QDir::toNativeSeparators(dir));
}
