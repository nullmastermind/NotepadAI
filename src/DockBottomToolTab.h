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

#ifndef DOCKBOTTOMTOOLTAB_H
#define DOCKBOTTOMTOOLTAB_H

namespace ads {
class CDockManager;
class CDockWidget;
class CDockAreaWidget;
}

// Place `widget` as a bottom tool tab (terminal, etc.) relative to `editorArea`
// inside the same ADS manager. Returns the tool's dock area.
//
// A Center insertion against `editorArea` tabifies into the editor strip —
// the miss this helper exists to prevent. Bottom splits a new area under the
// editor so side QDockWidgets (tree, ACP chat) stay full height.
//
// When `existingToolArea` is non-null, Center-insert into THAT area so a second
// terminal tabifies with the first (ui-dna: same dock kind does not stack).
ads::CDockAreaWidget *addDockWidgetAsBottomTool(
    ads::CDockManager *manager,
    ads::CDockWidget *widget,
    ads::CDockAreaWidget *editorArea,
    ads::CDockAreaWidget *existingToolArea = nullptr);

// Content tabs (editors, preview/browser/mini-app) — excludes nn_toolTab.
// Empty-area / lastTabClosed uses this so a remaining terminal does not
// suppress spawning "New X".
int nonToolTabCount(ads::CDockManager *manager);

ads::CDockAreaWidget *findToolDockArea(ads::CDockManager *manager);

// Place a content tab: Center into editorArea if present; otherwise Top of
// toolArea (respawn after the last editor closed with a terminal still open).
ads::CDockAreaWidget *addDockWidgetAsContent(
    ads::CDockManager *manager,
    ads::CDockWidget *widget,
    ads::CDockAreaWidget *editorArea,
    ads::CDockAreaWidget *toolArea);

#endif
