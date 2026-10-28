/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_IDS_H
#define CONTROL_IDS_H

#include <QList>
#include <QString>
#include <QVariant>
#include <QWidget>

class QAction;
class QMenu;

// Stable Control API ids (docs/adr/0006). Ids come from object names and data
// keys, never from displayed text; a fallback to text or position is marked
// unstable.
namespace control::ids {

inline constexpr const char *kKeyProperty = "nnControlKey";
inline constexpr const char *kRootProperty = "nnControlRoot";
inline constexpr const char *kIgnoreProperty = "nnControlIgnore";
// Name of the adapter that exposes a custom-painted widget's parts.
inline constexpr const char *kAdapterProperty = "nnControlAdapter";

// The Control API's own chrome (highlight overlay, its settings dialog and
// status-bar indicator): never listed, never counted, never operated, so an
// agent cannot turn on LAN access or replace the token.
inline void setIgnored(QWidget *widget)
{
    if (widget)
        widget->setProperty(kIgnoreProperty, true);
}

inline bool isIgnored(const QWidget *widget)
{
    return widget->property(kIgnoreProperty).toBool();
}

inline bool isInsideIgnored(const QWidget *widget)
{
    for (const QWidget *w = widget; w; w = w->parentWidget()) {
        if (isIgnored(w))
            return true;
    }
    return false;
}

// Data key of a dynamic menu item or list entry ("agent:claude-code",
// "task:build"). Cheap: one dynamic property.
inline void setKey(QObject *object, const QString &key)
{
    if (object)
        object->setProperty(kKeyProperty, key);
}

inline QString key(const QObject *object)
{
    return object ? object->property(kKeyProperty).toString() : QString();
}

// Marks a widget as an id root (its subtree is addressed from it, wherever it
// is reparented). ads::CDockWidget is a root without this ("dock:<name>").
inline void setRoot(QWidget *widget, const QString &rootId)
{
    if (widget)
        widget->setProperty(kRootProperty, rootId);
}

// "Open AI Agent in &Folder…" -> "open-ai-agent-in-folder".
QString slug(const QString &text);

struct Id
{
    QString id;
    bool stable = true;
};

// --- Actions ---------------------------------------------------------------

// The menu this menu is a submenu of, if any.
QMenu *parentMenuOf(const QMenu *menu);
// A menu: its objectName, else <parent menu id>/<key or slug of its title>.
Id menuId(const QMenu *menu);
// An action inside `menu` (nullptr: a toolbar or window action). A submenu's
// action takes the submenu's id.
Id actionId(const QAction *action, const QMenu *menu);

// --- Widgets ---------------------------------------------------------------

Id nodeId(const QWidget *widget);
// nullptr and `error` when nothing matches.
QWidget *resolveNode(const QString &id, QString *error = nullptr);
// Every root (windows, docks, marked widgets) with its id, main window first.
// One pass over the widget tree; build it once per request.
QList<QPair<QWidget *, QString>> rootTable();
QList<QWidget *> rootWidgets();
// A window's id needs the top-level windows only (repeats are numbered);
// a dock's or marked root's id needs nothing but the root.
QString rootIdOf(const QWidget *root);
// '/', '@', '#' and '%' are id syntax: escaped inside a segment.
QString escapeSegment(const QString &segment);
// Named and not a Qt/ADS internal: its name is a segment of ids below it.
bool isSegment(const QWidget *widget);
// Unnamed widgets of `className` whose anchor is `anchor`, in findChildren
// order (their <Class>#<n> numbering). Cache per request when walking.
QList<QWidget *> positionalSiblings(const QWidget *anchor, const QString &className);
// Segment of a widget below its root: its name, or <Class>#<n> (unstable).
QString segmentOf(const QWidget *widget, bool *stable = nullptr);
// The nearest ancestor that is a root or a named segment.
const QWidget *anchorOf(const QWidget *widget);
bool isRoot(const QWidget *widget);
// True for widgets whose names never form a segment (Qt and ADS internals).
bool isTransparentContainer(const QWidget *widget);

} // namespace control::ids

#endif // CONTROL_IDS_H
