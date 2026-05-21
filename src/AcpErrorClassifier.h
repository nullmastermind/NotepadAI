/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#ifndef ACP_ERROR_CLASSIFIER_H
#define ACP_ERROR_CLASSIFIER_H

#include <QString>
#include <QStringList>

#include <cstdint>

namespace AcpErrorClassifier {

enum class AcpErrorKind : std::uint8_t {
    AuthRequired,
    SpawnFailed,
    InitFailed,
};

// Classify a raw agent stderr / process error message into one of the
// three buckets per design.md D7. Case-insensitive matching.
AcpErrorKind classify(const QString &message);

// Derive the "<tool> login" hint to show next to the friendly error
// banner. Lowercased combined match across command + args per D7.
QString loginHint(const QString &command, const QStringList &args);

// Map an error kind + raw text + login hint to a user-visible string.
QString friendlyMessage(AcpErrorKind kind, const QString &raw, const QString &loginHint);

} // namespace AcpErrorClassifier

#endif // ACP_ERROR_CLASSIFIER_H
