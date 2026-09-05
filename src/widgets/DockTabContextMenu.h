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

#pragma once

#include <QList>

class QDockWidget;
class QMenu;
class QPoint;
class QTabBar;

// Right-click menu for Qt's internal dock tab bar — the strip shown when docks
// are tabified (workspace, terminal, task, AI, git...). The editor area is ADS,
// not QDockWidget, and keeps its own menu (MainWindow::tabBarRightClicked).
//
// tabOrder() and closeTargets() are split out from the menu so the "which tabs
// does this scope close" decision is testable without a shown window.
namespace DockTabContextMenu {

enum class Scope {
    Close,
    CloseOthers,
    CloseToLeft,
    CloseToRight,
    CloseAll,
};

// The docks behind `tabBar`, in tab order (which is drag-reorderable and so is
// NOT the order docks were added). Empty when `tabBar` is not a dock tab bar —
// the app-wide filter sees every QTabBar in the process, including the
// workspace dock's own Files/Git strip, and those must not resolve to docks.
QList<QDockWidget *> tabOrder(QTabBar *tabBar);

// The docks `scope` closes when the tab at `index` was clicked. Non-closable
// docks are dropped, so an empty result is what greys the menu item out.
QList<QDockWidget *> closeTargets(const QList<QDockWidget *> &order, int index, Scope scope);

// Five actions in editor-tab order (Close / Except This / Left / Right / All).
// Empty scopes are disabled, never hidden. Connections capture QPointer so a
// dock dying while the menu is up cannot dangle.
void populateMenu(QMenu *menu, const QList<QDockWidget *> &order, int index);

// Right-click at `pos` (tab-bar coordinates). Returns false only when `tabBar`
// is not a dock tab bar, so the caller can let that event through. A click on
// the empty strip of a real dock tab bar returns true without popping a menu —
// swallowing it keeps QMainWindow from showing the toggle-docks popup.
bool showMenu(QTabBar *tabBar, const QPoint &pos);

} // namespace DockTabContextMenu
