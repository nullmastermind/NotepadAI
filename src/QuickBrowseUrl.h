/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QString>

// Turns Quick Browse user input into a navigable URL string.
// File paths become file:// URLs; bare hosts get an https:// prefix.
QString normalizeQuickBrowseInput(const QString &input);

// True for .html / .htm suffixes (case-insensitive). Directories and other
// extensions return false.
bool isHtmlFilePath(const QString &path);
