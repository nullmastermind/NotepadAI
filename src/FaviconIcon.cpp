/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "FaviconIcon.h"

#include <QPixmap>

QIcon faviconIconFromData(const QByteArray &data)
{
    if (data.isEmpty())
        return {};
    QPixmap pm;
    if (!pm.loadFromData(data) || pm.isNull())
        return {};
    return QIcon(pm);
}
