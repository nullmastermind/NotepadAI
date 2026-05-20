/*
 * This file is part of Notepad Next.
 * Copyright 2024 Justin Dailey
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

#include <QString>


// Pure picker: returns the qrc path for the active stylesheet. Extracted from
// MainWindow::applyStyleSheet so the selection logic is testable without
// constructing a MainWindow (which transitively needs SingleApplication etc.).
inline QString chooseStylesheetResource(bool isDark)
{
    return isDark
        ? QStringLiteral(":/stylesheets/npp_dark.css")
        : QStringLiteral(":/stylesheets/npp.css");
}
