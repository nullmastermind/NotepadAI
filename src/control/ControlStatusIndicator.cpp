/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlStatusIndicator.h"

#include "ControlApi.h"
#include "ControlIds.h"

#include <QHBoxLayout>
#include <QToolButton>

namespace control {

ControlStatusIndicator::ControlStatusIndicator(ControlApi *api, QWidget *parent)
    : QWidget(parent)
    , m_api(api)
    , m_label(new QToolButton(this))
    , m_stop(new QToolButton(this))
{
    setObjectName(QStringLiteral("controlApiIndicator"));
    // An agent must not be able to press Stop or open the settings for itself.
    ids::setIgnored(this);
    m_label->setObjectName(QStringLiteral("controlApiIndicatorButton"));
    m_stop->setObjectName(QStringLiteral("controlApiStopButton"));
    m_label->setAutoRaise(true);
    m_stop->setAutoRaise(true);
    m_stop->setText(tr("Stop"));
    m_stop->setToolTip(tr("Turn the Control API off and close every external agent connection"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(m_label);
    layout->addWidget(m_stop);

    m_actingTimer.setSingleShot(true);
    m_actingTimer.setInterval(kActingMs);
    connect(&m_actingTimer, &QTimer::timeout, this, [this] { setActing(false); });

    connect(m_label, &QToolButton::clicked, this, &ControlStatusIndicator::openRequested);
    connect(m_stop, &QToolButton::clicked, m_api, [this] { m_api->setEnabled(false); });
    connect(m_api, &ControlApi::stateChanged, this, &ControlStatusIndicator::refresh);
    connect(m_api, &ControlApi::commandRan, this, &ControlStatusIndicator::markActing);
    m_label->setText(tr("Agent API"));
    refresh();
}

void ControlStatusIndicator::markActing()
{
    m_actingTimer.start();
    if (!m_acting)
        setActing(true);
}

void ControlStatusIndicator::setActing(bool acting)
{
    m_acting = acting;
    m_label->setText(acting ? tr("Agent acting") : tr("Agent API"));
    // Static fill while an agent is acting; nothing animates.
    m_label->setStyleSheet(acting
        ? QStringLiteral("QToolButton { background: palette(highlight); color: palette(highlighted-text);"
                         " border-radius: 4px; padding: 0 4px; }")
        : QString());
}

void ControlStatusIndicator::refresh()
{
    const bool on = m_api->isEnabled();
    setVisible(on);
    if (!on)
        return;
    m_label->setToolTip(m_api->isListening()
        ? tr("Control API listening on %1. Click to see what external agents did.").arg(m_api->baseUrl())
        : tr("Control API could not start: %1").arg(m_api->lastError()));
}

} // namespace control
