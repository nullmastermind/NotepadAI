/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_STATUS_INDICATOR_H
#define CONTROL_STATUS_INDICATOR_H

#include <QTimer>
#include <QWidget>

class QToolButton;

namespace control {

class ControlApi;

// Status bar chip shown while the Control API is on: "Agent API", or for a few
// seconds after each agent command what it did ("Agent: Open a file", static
// Highlight fill), and Stop.
class ControlStatusIndicator : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kActingMs = 3000;

    explicit ControlStatusIndicator(ControlApi *api, QWidget *parent = nullptr);

signals:
    void openRequested();

private:
    // Server state changed: visibility and tooltip.
    void refresh();
    // A command ran: names it and restarts the timer; restyles only when the fill turns on.
    void markActing(const QString &label);
    void setActing(bool acting);

    ControlApi *m_api;
    QToolButton *m_label;
    QToolButton *m_stop;
    QTimer m_actingTimer;
    bool m_acting = false;
    QString m_actingLabel;
};

} // namespace control

#endif // CONTROL_STATUS_INDICATOR_H
