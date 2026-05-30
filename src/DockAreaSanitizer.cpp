/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
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

#include "DockAreaSanitizer.h"

#include "DockAreaWidget.h"

ads::CDockAreaWidget *sanitizeDockArea(ads::CDockAreaWidget *area)
{
    // An area detached from its container (parent already nullptr, deleteLater
    // pending) reports dockContainer() == nullptr. Collapse it to nullptr so the
    // caller takes ADS's safe add-to-container fallback instead of dereferencing
    // a null container. nullptr in -> nullptr out; attached area passes through.
    if (area && !area->dockContainer())
        return nullptr;

    return area;
}
