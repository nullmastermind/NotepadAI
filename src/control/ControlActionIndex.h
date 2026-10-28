/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_ACTION_INDEX_H
#define CONTROL_ACTION_INDEX_H

#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>

class QAction;
class QMainWindow;
class QMenu;

namespace control {

// Every QAction a user can reach in a main window: the menu bar (menus
// filled in aboutToShow are filled first, exactly as opening them does),
// toolbars, window shortcuts, named actions of inner widgets, and the open
// popup menu. Built per request; nothing is cached.
class ControlActionIndex
{
public:
    enum class Location { MenuBar, ToolBar, Window, Popup };

    struct Entry
    {
        QString id;
        bool stable = true;
        QString label;          // text without mnemonics
        QStringList menuPath;   // menu titles leading to it
        QString shortcut;
        bool enabled = true;    // the action and every menu on its path
        bool visible = true;
        bool checkable = false;
        bool checked = false;
        bool submenu = false;
        Location location = Location::MenuBar;
        QPointer<QAction> action;
        QPointer<QMenu> menu;   // the menu holding it, if any
    };

    struct Resolved
    {
        QPointer<QAction> action;
        QPointer<QMenu> menu; // holding menu, if any
        bool enabled = false;
        bool visible = false;
        bool inPopup = false;
        QStringList menuPath;
        QString error;
    };

    explicit ControlActionIndex(QMainWindow *window);

    // `menuId` empty: everything; else the actions of that menu and its submenus.
    QList<Entry> list(const QString &menuId = {}) const;
    Resolved resolve(const QString &id) const;

    static QJsonObject toJson(const Entry &entry);
    static QString locationName(Location location);
    // Text without '&' mnemonics or a trailing tab-separated shortcut.
    static QString plainText(const QString &text);

private:
    QMenu *resolveMenu(const QString &menuId) const;
    void populateOnce(QMenu *menu) const;
    void walkMenu(QMenu *menu, const QStringList &path, bool pathEnabled, Location location,
                  QSet<QAction *> &seen, QList<Entry> &out) const;

    QPointer<QMainWindow> m_window;
    mutable QSet<const QMenu *> m_populated; // per list()/resolve() call
};

// Emits aboutToShow on the menu and the menus above it, top-down, so menus
// built on demand hold their items; menus already on screen are left alone.
void populateMenuChain(QMenu *menu);

} // namespace control

#endif // CONTROL_ACTION_INDEX_H
