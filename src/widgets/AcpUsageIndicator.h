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

#ifndef ACP_USAGE_INDICATOR_H
#define ACP_USAGE_INDICATOR_H

#include <QWidget>

#include <optional>

#include "AcpProtocol.h"

class QLabel;
class QProgressBar;

// Compact widget showing token usage as a label + optional progress bar.
// Hides itself in pieces when the underlying values are nullopt.
class AcpUsageIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit AcpUsageIndicator(QWidget *parent = nullptr);

    void setUsage(const std::optional<AcpProtocol::AcpUsage> &usage);

private:
    static QString formatThousands(int value);

    QLabel *m_label = nullptr;
    QProgressBar *m_bar = nullptr;
};

#endif // ACP_USAGE_INDICATOR_H
