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

#ifndef TERMINALTABTITLE_H
#define TERMINALTABTITLE_H

#include <QDir>
#include <QString>

// ADS tab label for an interactive terminal: the spawn cwd, never the PTY
// window title (Windows ConPTY reports the shell image, e.g. pwsh.exe).
inline QString terminalTabTitle(const QString &cwd)
{
    if (cwd.trimmed().isEmpty())
        return QStringLiteral("Terminal");

    QString native = QDir::toNativeSeparators(QDir::cleanPath(cwd));
#ifdef Q_OS_WIN
    // cleanPath("d:/") collapses to "d:"; keep the root slash the user expects.
    if (native.size() == 2 && native.at(1) == QLatin1Char(':'))
        return native + QLatin1Char('\\');
#endif
    return native;
}

#endif
