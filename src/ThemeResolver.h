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

#include "ApplicationSettings.h"

#include <Qt>


// Pure resolver: maps the user's Theme choice against the current system color
// scheme. Lives in its own header so it can be unit-tested without standing up
// SingleApplication / NotepadNextApplication.
inline bool resolveThemeIsDark(ApplicationSettings::ThemeEnum theme, Qt::ColorScheme systemScheme)
{
    switch (theme) {
    case ApplicationSettings::Light:
        return false;
    case ApplicationSettings::Dark:
        return true;
    case ApplicationSettings::System:
    default:
        return systemScheme == Qt::ColorScheme::Dark;
    }
}
