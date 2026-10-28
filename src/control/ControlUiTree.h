/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_UI_TREE_H
#define CONTROL_UI_TREE_H

#include "ControlAdapter.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QRect>
#include <QString>

class QWidget;

// The widget side of the Control API: the tree of what is on screen, reading
// a node, and operating one the way a mouse or keyboard would. Ids are
// "<node id>[@<part>]" (docs/adr/0006).
namespace control::ui {

struct TreeOptions
{
    int depth = 8;          // emitted levels below each root
    int maxNodes = 4000;    // whole response
    int maxParts = 50;      // adapter parts per node
    bool includeHidden = false;
};

struct Target
{
    QWidget *widget = nullptr;
    QString part; // after '@'
    QString error;
};

// "dock:x/foo@row:a" -> widget + part.
Target resolve(const QString &id);

QString roleOf(const QWidget *widget);
// What a user reads on it: button text, label, title, placeholder.
QString labelOf(const QWidget *widget);

// Every visible root (or `root` alone) as nested nodes.
QJsonObject tree(QWidget *root, const TreeOptions &options);
// One node in detail.
QJsonObject node(QWidget *widget);
// Its content: text, value, items, rows; adapters may answer for a part.
QJsonObject read(QWidget *widget, const QString &part, const ReadOptions &options);
// Visible nodes whose label or text contains `text` (case-insensitive) and
// whose role is `role` (if given).
QJsonArray find(const QString &text, const QString &role, int limit);

// Where a target is in its widget (for the highlight); empty: whole widget.
QRect targetRect(const Target &target);
// Can a user do `op` on it? "" or the reason (hidden, disabled, behind a modal).
QString blockedReason(const Target &target, const QString &op = {});

// Operations. Each returns "" on success or an error, and acts only through
// user input (mouse and key events, the user-facing slots of the widget).
QString click(const Target &target, Qt::MouseButton button, bool doubleClick);
QString type(const Target &target, const QString &text, bool replace);
QString keys(const Target &target, const QString &sequence);
QString select(const Target &target, const QJsonObject &args);
QString set(const Target &target, const QJsonValue &value);
QString scroll(const Target &target, const QJsonObject &args);
QString close(const Target &target);
QString focus(const Target &target);
QString perform(const Target &target, const QString &op, const QJsonObject &args);

} // namespace control::ui

#endif // CONTROL_UI_TREE_H
