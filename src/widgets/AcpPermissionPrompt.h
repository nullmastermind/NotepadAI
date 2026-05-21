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

#ifndef ACP_PERMISSION_PROMPT_H
#define ACP_PERMISSION_PROMPT_H

#include <QFrame>
#include <QString>

#include "AcpProtocol.h"

class AcpPermissionPrompt : public QFrame
{
    Q_OBJECT

public:
    explicit AcpPermissionPrompt(const AcpProtocol::AcpPermissionRequest &request,
                                 QWidget *parent = nullptr);

    QString requestId() const { return m_requestId; }

signals:
    void choiceMade(const QString &requestId,
                    const QString &outcome,
                    const QString &optionId);

private:
    QString m_requestId;
};

#endif // ACP_PERMISSION_PROMPT_H
