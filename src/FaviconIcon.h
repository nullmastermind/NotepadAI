/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QIcon>

// Turns raw favicon image bytes (PNG/JPEG/ICO) into a tab icon.
// Empty or unloadable data returns a null QIcon.
QIcon faviconIconFromData(const QByteArray &data);
