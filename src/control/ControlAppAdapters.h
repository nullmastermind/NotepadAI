/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_APP_ADAPTERS_H
#define CONTROL_APP_ADAPTERS_H

namespace control {

// Registers the adapters for NotepadAI's own widgets: docked tabs (ADS),
// editors, terminals and chat sessions. Once, before the API serves.
void registerAppAdapters();

} // namespace control

#endif // CONTROL_APP_ADAPTERS_H
