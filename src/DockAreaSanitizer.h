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

#ifndef DOCKAREASANITIZER_H
#define DOCKAREASANITIZER_H

namespace ads { class CDockAreaWidget; }

// Pure predicate guarding the "feed a dock area to ADS addDockWidget" hot path.
//
// ADS's CDockContainerWidget::removeDockArea() does setParent(nullptr) and THEN
// deleteLater(), so for one event-loop turn a removed dock area is alive (a
// QPointer to it stays non-null) yet orphaned: CDockAreaWidget::dockContainer()
// walks parentWidget() and returns nullptr. Passing such an area as the 3rd arg
// of CDockManager::addDockWidget() makes Container = area->dockContainer() null,
// and the following topLevelDockArea() dereferences that null container — the
// crash recorded in crash_report.txt (Preview reopened while its area was being
// torn down).
//
// sanitizeDockArea() collapses a detached area to nullptr so callers route into
// ADS's add-to-container fallback (Container = the always-valid dock manager),
// which creates a fresh area for the tab. A null input stays null; a still-
// attached area passes through unchanged. Pure and side-effect-free so it is
// unit-testable in isolation (see tests/test_docked_editor_current_area.cpp),
// mirroring the TerminalCwdResolver split.
ads::CDockAreaWidget *sanitizeDockArea(ads::CDockAreaWidget *area);

#endif // DOCKAREASANITIZER_H
