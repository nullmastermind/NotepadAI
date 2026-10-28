/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlApiDialog.h"

#include "ControlActivityLog.h"
#include "ControlApi.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace control {

ControlApiDialog::ControlApiDialog(ControlApi *api, QWidget *parent)
    : QDialog(parent)
    , m_api(api)
    , m_status(new QLabel(this))
    , m_enable(new QCheckBox(tr("Enable Control API"), this))
    , m_port(new QSpinBox(this))
    , m_lan(new QCheckBox(tr("Allow connections from other machines (0.0.0.0)"), this))
    , m_lanAddress(new QComboBox(this))
    , m_lanWarning(new QLabel(this))
    , m_token(new QLineEdit(this))
    , m_showToken(new QToolButton(this))
    , m_copyToken(new QPushButton(tr("Copy"), this))
    , m_regenerate(new QPushButton(tr("Regenerate"), this))
    , m_prompt(new QPlainTextEdit(this))
    , m_copyPrompt(new QPushButton(tr("Copy prompt"), this))
    , m_activity(new QListView(this))
    , m_clearActivity(new QPushButton(tr("Clear"), this))
{
    setObjectName(QStringLiteral("ControlApiDialog"));
    setWindowTitle(tr("Connect External Agent"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(640, 640);

    m_status->setObjectName(QStringLiteral("labelControlApiStatus"));
    m_enable->setObjectName(QStringLiteral("checkControlApiEnabled"));
    m_port->setObjectName(QStringLiteral("spinControlApiPort"));
    m_lan->setObjectName(QStringLiteral("checkControlApiAllowLan"));
    m_lanAddress->setObjectName(QStringLiteral("comboControlApiLanAddress"));
    m_lanWarning->setObjectName(QStringLiteral("labelControlApiLanWarning"));
    m_token->setObjectName(QStringLiteral("editControlApiToken"));
    m_showToken->setObjectName(QStringLiteral("buttonShowControlApiToken"));
    m_copyToken->setObjectName(QStringLiteral("buttonCopyControlApiToken"));
    m_regenerate->setObjectName(QStringLiteral("buttonRegenerateControlApiToken"));
    m_prompt->setObjectName(QStringLiteral("textControlApiPrompt"));
    m_copyPrompt->setObjectName(QStringLiteral("buttonCopyControlApiPrompt"));
    m_activity->setObjectName(QStringLiteral("listControlApiActivity"));
    m_clearActivity->setObjectName(QStringLiteral("buttonClearControlApiActivity"));

    auto *intro = new QLabel(tr("Let an AI agent running outside NotepadAI give it commands through the Control API. "
                                "Whatever a command changes comes to the front and flashes, the status bar "
                                "names the step, and every step is listed below."),
                             this);
    intro->setObjectName(QStringLiteral("labelControlApiIntro"));
    intro->setWordWrap(true);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_port->setRange(1024, 65535);
    m_port->setToolTip(tr("Another NotepadAI window already using it takes the next free port"));
    m_lanWarning->setWordWrap(true);
    m_lanWarning->setText(tr("Traffic, including the token, is plain HTTP. Anyone on the network who sees the "
                             "token can run commands on this machine. Prefer an SSH tunnel to 127.0.0.1."));
    m_lanWarning->setStyleSheet(QStringLiteral("QLabel { background: #fff3cd; color: #664d03; border: 1px solid "
                                               "#ffeeba; border-radius: 4px; padding: 4px; }"));

    m_token->setReadOnly(true);
    m_token->setEchoMode(QLineEdit::Password);
    m_showToken->setText(tr("Show"));
    m_showToken->setCheckable(true);
    m_showToken->setAutoRaise(true);

    m_prompt->setReadOnly(true);
    m_prompt->setMinimumHeight(150);
    m_prompt->setToolTip(tr("The token is hidden here; Copy prompt copies it in full"));

    m_activity->setModel(m_api->activityLog());
    m_activity->setUniformItemSizes(true);
    m_activity->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto *form = new QFormLayout;
    form->setSpacing(6);
    auto *lanRow = new QHBoxLayout;
    lanRow->setSpacing(4);
    lanRow->addWidget(m_lan);
    lanRow->addWidget(m_lanAddress, 1);
    form->addRow(tr("Port"), m_port);
    form->addRow(QString(), m_lanWarning);
    auto *tokenRow = new QHBoxLayout;
    tokenRow->setSpacing(4);
    tokenRow->addWidget(m_token, 1);
    tokenRow->addWidget(m_showToken);
    tokenRow->addWidget(m_copyToken);
    tokenRow->addWidget(m_regenerate);
    form->addRow(tr("Token"), tokenRow);

    auto *promptLabel = new QLabel(tr("Give this prompt to the external agent:"), this);
    promptLabel->setObjectName(QStringLiteral("labelControlApiPrompt"));
    auto *activityLabel = new QLabel(tr("Activity"), this);
    activityLabel->setObjectName(QStringLiteral("labelControlApiActivity"));
    auto *activityRow = new QHBoxLayout;
    activityRow->addWidget(activityLabel, 1);
    activityRow->addWidget(m_clearActivity);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->setObjectName(QStringLiteral("buttonBoxControlApi"));
    buttons->button(QDialogButtonBox::Close)->setObjectName(QStringLiteral("buttonCloseControlApi"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    layout->addWidget(intro);
    layout->addWidget(m_status);
    layout->addWidget(m_enable);
    layout->addLayout(lanRow);
    layout->addLayout(form);
    layout->addWidget(promptLabel);
    layout->addWidget(m_prompt, 1);
    layout->addWidget(m_copyPrompt, 0, Qt::AlignLeft);
    layout->addLayout(activityRow);
    layout->addWidget(m_activity, 1);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(m_enable, &QCheckBox::toggled, m_api, &ControlApi::setEnabled);
    connect(m_port, &QSpinBox::editingFinished, this, [this] { m_api->setConfiguredPort(quint16(m_port->value())); });
    connect(m_lan, &QCheckBox::toggled, m_api, &ControlApi::setAllowLan);
    connect(m_lanAddress, &QComboBox::activated, this,
            [this](int index) { m_api->setLanAddress(m_lanAddress->itemText(index)); });
    connect(m_showToken, &QToolButton::toggled, this, [this](bool show) {
        m_token->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        m_showToken->setText(show ? tr("Hide") : tr("Show"));
    });
    connect(m_copyToken, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(m_api->token()); });
    connect(m_regenerate, &QPushButton::clicked, m_api, &ControlApi::regenerateToken);
    connect(m_copyPrompt, &QPushButton::clicked, this, &ControlApiDialog::copyPrompt);
    connect(m_clearActivity, &QPushButton::clicked, m_api->activityLog(), &ControlActivityLog::clear);
    connect(m_api, &ControlApi::stateChanged, this, &ControlApiDialog::refresh);
    connect(m_api->activityLog(), &QAbstractItemModel::rowsInserted, m_activity, [this] { m_activity->scrollToBottom(); });

    refresh();
    m_activity->scrollToBottom();
}

QString ControlApiDialog::maskedPrompt()
{
    QString prompt = m_api->connectionPrompt();
    const QString token = m_api->token();
    if (!token.isEmpty())
        prompt.replace(token, QString(token.size(), QChar(0x2022)));
    return prompt;
}

void ControlApiDialog::copyPrompt()
{
    if (!m_api->isEnabled())
        m_api->setEnabled(true);
    QGuiApplication::clipboard()->setText(m_api->connectionPrompt());
    m_copyPrompt->setText(tr("Copied"));
}

void ControlApiDialog::refresh()
{
    const bool enabled = m_api->isEnabled();
    const bool listening = m_api->isListening();
    {
        const QSignalBlocker b1(m_enable);
        const QSignalBlocker b2(m_lan);
        const QSignalBlocker b3(m_port);
        const QSignalBlocker b4(m_lanAddress);
        m_enable->setChecked(enabled);
        m_lan->setChecked(m_api->allowLan());
        m_port->setValue(m_api->configuredPort());
        m_lanAddress->clear();
        m_lanAddress->addItems(ControlApi::lanAddresses());
        m_lanAddress->setCurrentText(m_api->lanAddress());
    }
    m_lanAddress->setVisible(m_api->allowLan());
    m_lanAddress->setEnabled(m_lanAddress->count() > 0);
    m_lanWarning->setVisible(m_api->allowLan());

    QString status;
    QString style;
    if (!enabled) {
        status = tr("Off. No port is open.");
    } else if (listening) {
        status = tr("Listening on %1").arg(m_api->baseUrl());
        if (m_api->port() != m_api->configuredPort())
            status += tr(" (port %1 was taken)").arg(m_api->configuredPort());
    } else {
        status = tr("Could not start: %1. Choose another port.").arg(m_api->lastError());
        style = QStringLiteral("QLabel { background: #f8d7da; color: #58151c; border: 1px solid #f5c6cb; "
                               "border-radius: 4px; padding: 4px; }");
    }
    if (m_api->tokenIsSessionOnly())
        status += tr("\nThe system keychain is unavailable: the token lasts until NotepadAI closes.");
    m_status->setText(status);
    m_status->setStyleSheet(style);

    m_token->setText(m_api->token());
    m_prompt->setPlainText(maskedPrompt());
    m_copyPrompt->setText(tr("Copy prompt"));
}

} // namespace control
