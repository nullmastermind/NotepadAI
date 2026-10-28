/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_PICKER_H
#define CONTROL_PICKER_H

#include <QColor>
#include <QColorDialog>
#include <QFileDialog>
#include <QFont>
#include <QFontDialog>
#include <QString>
#include <QStringList>

// The one seam every picker goes through (docs/adr/0006: direct commands).
// When the Control API runs an action or clicks a button with a value, the
// next picker that code opens returns that value instead of showing itself;
// everything after it (the action's own code, its confirmations) runs as on
// a user's pick. Never call QFileDialog::get*, QFontDialog::getFont or
// QColorDialog::getColor directly: ControlPickerSeam (ctest) fails on it.
namespace control::picker {

// Armed by the Control API for one command; one picker consumes it.
void arm(const QStringList &values);
// Returns whether a picker took the value since arm(), and disarms.
bool disarm();
bool isArmed();

QString getExistingDirectory(QWidget *parent = nullptr, const QString &caption = QString(),
                             const QString &dir = QString(),
                             QFileDialog::Options options = QFileDialog::ShowDirsOnly);
QString getOpenFileName(QWidget *parent = nullptr, const QString &caption = QString(), const QString &dir = QString(),
                        const QString &filter = QString(), QString *selectedFilter = nullptr,
                        QFileDialog::Options options = QFileDialog::Options());
QStringList getOpenFileNames(QWidget *parent = nullptr, const QString &caption = QString(),
                             const QString &dir = QString(), const QString &filter = QString(),
                             QString *selectedFilter = nullptr, QFileDialog::Options options = QFileDialog::Options());
// An armed value for an existing file still asks "Replace it?" unless the
// options say not to, as the native dialog would.
QString getSaveFileName(QWidget *parent = nullptr, const QString &caption = QString(), const QString &dir = QString(),
                        const QString &filter = QString(), QString *selectedFilter = nullptr,
                        QFileDialog::Options options = QFileDialog::Options());
// Value: QFont::toString() or "Family, 11".
QFont getFont(bool *ok, const QFont &initial, QWidget *parent = nullptr, const QString &title = QString(),
              QFontDialog::FontDialogOptions options = QFontDialog::FontDialogOptions());
// Value: "#rrggbb", "#aarrggbb" or an SVG colour name.
QColor getColor(const QColor &initial = Qt::white, QWidget *parent = nullptr, const QString &title = QString(),
                QColorDialog::ColorDialogOptions options = QColorDialog::ColorDialogOptions());

} // namespace control::picker

#endif // CONTROL_PICKER_H
