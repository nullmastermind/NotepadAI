/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "DockTabReorder.h"

#include "DockAreaWidget.h"
#include "DockManager.h"
#include "DockWidget.h"

bool moveDockWidgetToIndex(ads::CDockWidget *dw, int toIndex)
{
    if (!dw)
        return false;

    ads::CDockAreaWidget *area = dw->dockAreaWidget();
    ads::CDockManager *manager = dw->dockManager();
    if (!area || !manager)
        return false;

    const int n = area->dockWidgetsCount();
    if (toIndex < 0 || toIndex >= n)
        return false;

    int from = -1;
    for (int i = 0; i < n; ++i) {
        if (area->dockWidget(i) == dw) {
            from = i;
            break;
        }
    }
    if (from < 0)
        return false;
    if (from == toIndex)
        return true;

    // CenterDockWidgetArea is load-bearing: any other area identifier splits
    // a new pane instead of reinserting as a tab.
    ads::CDockAreaWidget *result = manager->addDockWidget(
        ads::CenterDockWidgetArea, dw, area, toIndex);
    return result == area && area->dockWidget(toIndex) == dw;
}
