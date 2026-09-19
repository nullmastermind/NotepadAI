/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace ads { class CDockWidget; }

// Move `dw` to `toIndex` inside its current dock area using the public ADS
// API (addDockWidget Center + index). That path removes and reinserts the
// tab and the content widget together, so they cannot desync the way a
// tab-bar-only shuffle plus a private reorderDockWidget slot can.
//
// Returns false without moving if the widget, area, or index is invalid, or
// if the widget is not at `toIndex` afterwards. from == to is success.
bool moveDockWidgetToIndex(ads::CDockWidget *dw, int toIndex);
