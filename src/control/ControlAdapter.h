/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_ADAPTER_H
#define CONTROL_ADAPTER_H

#include <QJsonObject>
#include <QList>
#include <QRect>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

class QWidget;

namespace control {

struct ReadOptions
{
    qint64 from = 0;     // first row / line / char, by widget
    qint64 count = -1;   // -1: the widget's default window
};

// Exposes a widget whose clickable parts are not widgets of their own (item
// rows, tabs, editor text, terminal screen, chat cards) to the Control API.
// Parts are addressed as "<node id>@<key>". Every operation still goes
// through what a user does: a click at the part, keys, the widget's own
// user-facing slots.
class ControlAdapter
{
public:
    struct Part
    {
        QString key;
        QString role;
        QString label;
        bool enabled = true;
        bool selected = false;
        bool current = false;
        QJsonObject extra;
    };

    virtual ~ControlAdapter() = default;

    virtual QString name() const = 0;
    virtual bool matches(const QWidget *widget) const = 0;
    // Role in the tree ("list", "editor", ...); empty: the generic role.
    virtual QString role(const QWidget *) const { return {}; }
    // Whether the tree walks into the widget's child widgets.
    virtual bool walksChildren(const QWidget *) const { return false; }
    // Whether a user can still do `op` while the widget is hidden (a tab that
    // is not the current one is hidden, yet its tab is right there).
    virtual bool reachableWhileHidden(const QWidget *, const QString & /*op*/) const { return false; }
    // Value shown on the tree node (current item, caret line...).
    virtual QJsonObject summary(QWidget *) const { return {}; }
    virtual QList<Part> parts(QWidget *, int /*limit*/) const { return {}; }
    // false: the generic read applies.
    virtual bool read(QWidget *, const QString & /*part*/, const ReadOptions &, QJsonObject & /*out*/) const
    {
        return false;
    }
    // Brings the part on screen the way a user would (scrolling to it).
    virtual void reveal(QWidget *, const QString & /*part*/) const {}
    // The part's rectangle in eventTarget() coordinates; empty: not found.
    virtual QRect partRect(QWidget *, const QString & /*part*/) const { return {}; }
    // Receives mouse events for parts (an item view's viewport).
    virtual QWidget *eventTarget(QWidget *widget) const { return widget; }
    // An operation the adapter does itself; nullopt: the generic one.
    // Returns "" on success or an error.
    virtual std::optional<QString> perform(QWidget *, const QString & /*part*/, const QString & /*op*/,
                                           const QJsonObject & /*args*/) const
    {
        return std::nullopt;
    }
};

// Adapters, most specific first. Registered once at startup.
class ControlAdapters
{
public:
    static ControlAdapters &instance();
    void add(std::unique_ptr<ControlAdapter> adapter); // takes precedence over earlier ones
    const ControlAdapter *find(const QWidget *widget) const;

private:
    ControlAdapters();
    std::vector<std::unique_ptr<ControlAdapter>> m_adapters;
};

} // namespace control

#endif // CONTROL_ADAPTER_H
