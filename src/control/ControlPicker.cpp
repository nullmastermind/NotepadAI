/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlPicker.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>

#include <optional>

namespace control::picker {

namespace {

// GUI thread only (pickers are modal UI).
struct Armed
{
    QStringList values;
    bool active = false;
    bool consumed = false;
};

Armed &state()
{
    static Armed armed;
    return armed;
}

std::optional<QStringList> take()
{
    Armed &a = state();
    if (!a.active || a.consumed)
        return std::nullopt;
    a.consumed = true;
    return a.values;
}

QString absolute(const QString &path, const QString &dir)
{
    if (path.isEmpty())
        return path;
    const QFileInfo info(path);
    if (info.isAbsolute())
        return QDir::cleanPath(path);
    QString base = dir;
    if (!base.isEmpty() && QFileInfo(base).isFile())
        base = QFileInfo(base).absolutePath();
    return QDir::cleanPath(QDir(base.isEmpty() ? QDir::currentPath() : base).absoluteFilePath(path));
}

// The first filter whose patterns match the file, as the dialog would report.
void pickFilter(const QString &file, const QString &filter, QString *selectedFilter)
{
    if (!selectedFilter || filter.isEmpty())
        return;
    const QStringList filters = filter.split(QStringLiteral(";;"));
    const QString name = QFileInfo(file).fileName();
    static const QRegularExpression patterns(QStringLiteral("\\(([^)]*)\\)"));
    for (const QString &f : filters) {
        const QRegularExpressionMatch m = patterns.match(f);
        const QStringList globs = m.hasMatch() ? m.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts)
                                               : QStringList{f};
        for (const QString &g : globs) {
            const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(g),
                                        QRegularExpression::CaseInsensitiveOption);
            if (re.match(name).hasMatch()) {
                *selectedFilter = f;
                return;
            }
        }
    }
    *selectedFilter = filters.first();
}

} // namespace

void arm(const QStringList &values)
{
    Armed &a = state();
    a.values = values;
    a.active = true;
    a.consumed = false;
}

bool disarm()
{
    Armed &a = state();
    const bool consumed = a.active && a.consumed;
    a = Armed();
    return consumed;
}

bool isArmed()
{
    return state().active && !state().consumed;
}

QString getExistingDirectory(QWidget *parent, const QString &caption, const QString &dir,
                             QFileDialog::Options options)
{
    if (auto v = take())
        return v->isEmpty() ? QString() : absolute(v->first(), dir);
    return QFileDialog::getExistingDirectory(parent, caption, dir, options);
}

QString getOpenFileName(QWidget *parent, const QString &caption, const QString &dir, const QString &filter,
                        QString *selectedFilter, QFileDialog::Options options)
{
    if (auto v = take()) {
        const QString file = v->isEmpty() ? QString() : absolute(v->first(), dir);
        pickFilter(file, filter, selectedFilter);
        return file;
    }
    return QFileDialog::getOpenFileName(parent, caption, dir, filter, selectedFilter, options);
}

QStringList getOpenFileNames(QWidget *parent, const QString &caption, const QString &dir, const QString &filter,
                             QString *selectedFilter, QFileDialog::Options options)
{
    if (auto v = take()) {
        QStringList files;
        for (const QString &f : std::as_const(*v)) {
            if (!f.trimmed().isEmpty())
                files.append(absolute(f.trimmed(), dir));
        }
        if (!files.isEmpty())
            pickFilter(files.first(), filter, selectedFilter);
        return files;
    }
    return QFileDialog::getOpenFileNames(parent, caption, dir, filter, selectedFilter, options);
}

QString getSaveFileName(QWidget *parent, const QString &caption, const QString &dir, const QString &filter,
                        QString *selectedFilter, QFileDialog::Options options)
{
    if (auto v = take()) {
        QString file = v->isEmpty() ? QString() : absolute(v->first(), dir);
        if (file.isEmpty())
            return file;
        pickFilter(file, filter, selectedFilter);
        if (QFileInfo::exists(file) && !options.testFlag(QFileDialog::DontConfirmOverwrite)) {
            // The confirmation the save dialog would have shown.
            const auto answer = QMessageBox::question(
                parent, caption.isEmpty() ? QObject::tr("Save As") : caption,
                QObject::tr("%1 already exists.\nDo you want to replace it?").arg(QDir::toNativeSeparators(file)));
            if (answer != QMessageBox::Yes)
                return QString();
        }
        return file;
    }
    return QFileDialog::getSaveFileName(parent, caption, dir, filter, selectedFilter, options);
}

QFont getFont(bool *ok, const QFont &initial, QWidget *parent, const QString &title,
              QFontDialog::FontDialogOptions options)
{
    if (auto v = take()) {
        const QString spec = v->isEmpty() ? QString() : v->first().trimmed();
        QFont font = initial;
        if (spec.contains(QLatin1Char(',')) && !font.fromString(spec)) {
            const QStringList parts = spec.split(QLatin1Char(','));
            font.setFamily(parts.first().trimmed());
            if (parts.size() > 1)
                font.setPointSizeF(parts.at(1).trimmed().toDouble());
        } else if (!spec.contains(QLatin1Char(','))) {
            font.setFamily(spec);
        }
        if (ok)
            *ok = !spec.isEmpty();
        return font;
    }
    return QFontDialog::getFont(ok, initial, parent, title, options);
}

QColor getColor(const QColor &initial, QWidget *parent, const QString &title, QColorDialog::ColorDialogOptions options)
{
    if (auto v = take()) {
        const QColor c = v->isEmpty() ? QColor() : QColor::fromString(v->first().trimmed());
        return c.isValid() ? c : QColor();
    }
    return QColorDialog::getColor(initial, parent, title, options);
}

} // namespace control::picker
