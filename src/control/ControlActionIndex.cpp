/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlActionIndex.h"

#include "ControlIds.h"

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>

namespace control {

namespace {

QList<QMenu *> menuChain(QMenu *menu)
{
    QList<QMenu *> chain;
    for (QMenu *m = menu; m && !chain.contains(m); m = ids::parentMenuOf(m))
        chain.prepend(m);
    return chain;
}

bool chainEnabled(const QList<QMenu *> &chain)
{
    for (const QMenu *m : chain) {
        if (!m->isEnabled() || !m->menuAction()->isEnabled())
            return false;
    }
    return true;
}

bool chainVisible(const QList<QMenu *> &chain)
{
    for (const QMenu *m : chain) {
        if (!m->menuAction()->isVisible())
            return false;
    }
    return true;
}

QStringList chainTitles(const QList<QMenu *> &chain)
{
    QStringList titles;
    for (const QMenu *m : chain) {
        const QString t = ControlActionIndex::plainText(m->title());
        if (!t.isEmpty())
            titles.append(t);
    }
    return titles;
}

void populate(QMenu *menu)
{
    // An open menu is already filled; rebuilding it would shuffle what the user sees.
    if (!menu->isVisible())
        emit menu->aboutToShow();
}

QMenu *rootPopup()
{
    auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
    while (menu) {
        QMenu *parent = ids::parentMenuOf(menu);
        if (!parent || !parent->isVisible())
            break;
        menu = parent;
    }
    return menu;
}

QList<QAction *> namedActions(const QString &name)
{
    QList<QAction *> found;
    for (QWidget *top : QApplication::topLevelWidgets()) {
        const QList<QAction *> actions = top->findChildren<QAction *>(name);
        for (QAction *a : actions) {
            if (!found.contains(a))
                found.append(a);
        }
    }
    return found;
}

QMenu *holdingMenu(const QAction *action)
{
    const QList<QObject *> owners = action->associatedObjects();
    for (QObject *o : owners) {
        if (auto *m = qobject_cast<QMenu *>(o)) {
            if (m != action->menu())
                return m;
        }
    }
    return nullptr;
}

} // namespace

void populateMenuChain(QMenu *menu)
{
    for (QMenu *m : menuChain(menu))
        populate(m);
}

ControlActionIndex::ControlActionIndex(QMainWindow *window)
    : m_window(window)
{
}

QString ControlActionIndex::plainText(const QString &text)
{
    QString out = text.section(QLatin1Char('\t'), 0, 0);
    QString clean;
    clean.reserve(out.size());
    for (qsizetype i = 0; i < out.size(); ++i) {
        if (out.at(i) == QLatin1Char('&')) {
            if (i + 1 < out.size() && out.at(i + 1) == QLatin1Char('&')) {
                clean += QLatin1Char('&');
                ++i;
            }
            continue;
        }
        clean += out.at(i);
    }
    return clean.trimmed();
}

QString ControlActionIndex::locationName(Location location)
{
    switch (location) {
    case Location::MenuBar: return QStringLiteral("menubar");
    case Location::ToolBar: return QStringLiteral("toolbar");
    case Location::Window: return QStringLiteral("window");
    case Location::Popup: return QStringLiteral("popup");
    }
    return {};
}

void ControlActionIndex::walkMenu(QMenu *menu, const QStringList &path, bool pathEnabled, Location location,
                                  QSet<QAction *> &seen, QList<Entry> &out) const
{
    if (location != Location::Popup)
        populateOnce(menu);
    const bool menuEnabled = pathEnabled && menu->isEnabled();
    const QList<QAction *> actions = menu->actions();
    for (QAction *a : actions) {
        if (a->isSeparator() || seen.contains(a))
            continue;
        seen.insert(a);
        const ids::Id id = ids::actionId(a, menu);
        Entry e;
        e.id = id.id;
        e.stable = id.stable;
        e.label = plainText(a->text());
        e.menuPath = path;
        e.shortcut = a->shortcut().toString(QKeySequence::PortableText);
        e.enabled = menuEnabled && a->isEnabled();
        e.visible = a->isVisible();
        e.checkable = a->isCheckable();
        e.checked = a->isChecked();
        e.location = location;
        e.action = a;
        e.menu = menu;
        QMenu *sub = a->menu();
        e.submenu = sub != nullptr;
        out.append(e);
        if (sub) {
            QStringList subPath = path;
            subPath.append(e.label);
            walkMenu(sub, subPath, e.enabled, location, seen, out);
        }
    }
}

QList<ControlActionIndex::Entry> ControlActionIndex::list(const QString &menuId) const
{
    QList<Entry> out;
    QSet<QAction *> seen;
    m_populated.clear();
    if (!m_window)
        return out;

    if (!menuId.isEmpty()) {
        QMenu *menu = resolveMenu(menuId);
        if (!menu)
            return out;
        const QList<QMenu *> chain = menuChain(menu);
        const bool inPopup = menuId == QLatin1String("popup") || menuId.startsWith(QLatin1String("popup/"));
        walkMenu(menu, chainTitles(chain), chainEnabled(chain), inPopup ? Location::Popup : Location::MenuBar,
                 seen, out);
        return out;
    }

    if (QMenu *popup = rootPopup())
        walkMenu(popup, {}, true, Location::Popup, seen, out);

    if (QMenuBar *bar = m_window->menuBar()) {
        const QList<QAction *> top = bar->actions();
        for (QAction *a : top) {
            if (QMenu *menu = a->menu()) {
                seen.insert(a);
                Entry e;
                const ids::Id id = ids::menuId(menu);
                e.id = id.id;
                e.stable = id.stable;
                e.label = plainText(menu->title());
                e.enabled = menu->isEnabled() && a->isEnabled();
                e.visible = a->isVisible();
                e.submenu = true;
                e.action = a;
                out.append(e);
                walkMenu(menu, {e.label}, e.enabled, Location::MenuBar, seen, out);
            }
        }
    }

    const QList<QToolBar *> toolbars = m_window->findChildren<QToolBar *>();
    for (QToolBar *bar : toolbars) {
        const QList<QAction *> actions = bar->actions();
        for (QAction *a : actions) {
            if (a->isSeparator() || seen.contains(a))
                continue;
            seen.insert(a);
            const ids::Id id = ids::actionId(a, nullptr);
            Entry e;
            e.id = id.id;
            e.stable = id.stable;
            e.label = plainText(a->text());
            e.menuPath = {plainText(bar->windowTitle())};
            e.shortcut = a->shortcut().toString(QKeySequence::PortableText);
            e.enabled = a->isEnabled();
            e.visible = a->isVisible() && bar->isVisible();
            e.checkable = a->isCheckable();
            e.checked = a->isChecked();
            e.location = Location::ToolBar;
            e.action = a;
            out.append(e);
        }
    }

    // Shortcut-only window actions and named actions of inner widgets
    // (context menus, dock title bars).
    const QList<QAction *> all = m_window->findChildren<QAction *>();
    for (QAction *a : all) {
        if (a->isSeparator() || seen.contains(a) || a->objectName().isEmpty()
            || a->objectName().startsWith(QLatin1String("qt_"))
            || a->objectName().startsWith(QLatin1String("_q_")) || a->menu())
            continue;
        seen.insert(a);
        Entry e;
        e.id = ids::actionId(a, nullptr).id;
        e.label = plainText(a->text());
        e.shortcut = a->shortcut().toString(QKeySequence::PortableText);
        e.enabled = a->isEnabled();
        e.visible = a->isVisible();
        e.checkable = a->isCheckable();
        e.checked = a->isChecked();
        e.location = Location::Window;
        e.action = a;
        out.append(e);
    }
    return out;
}

QMenu *ControlActionIndex::resolveMenu(const QString &menuId) const
{
    if (menuId == QLatin1String("popup"))
        return rootPopup();
    const qsizetype slash = menuId.lastIndexOf(QLatin1Char('/'));
    if (slash < 0) {
        if (!m_window)
            return nullptr;
        // Named menu: the window's own first, then any open popup tree.
        auto findNamed = [this, &menuId]() -> QMenu * {
            if (auto *m = m_window->findChild<QMenu *>(menuId))
                return m;
            const QWidgetList tops = QApplication::topLevelWidgets();
            for (QWidget *top : tops) {
                if (auto *m = top->findChild<QMenu *>(menuId))
                    return m;
                if (top->objectName() == menuId)
                    return qobject_cast<QMenu *>(top);
            }
            return nullptr;
        };
        QMenu *menu = findNamed();
        if (menu && !menu->isVisible()) {
            populateOnce(menu);
            menu = findNamed(); // an ancestor's aboutToShow may have rebuilt it
        }
        return menu;
    }
    QMenu *parent = resolveMenu(menuId.left(slash)); // filled already
    if (!parent)
        return nullptr;
    const bool popup = menuId.startsWith(QLatin1String("popup/"));
    const QList<QAction *> actions = parent->actions();
    for (QAction *a : actions) {
        QMenu *sub = a->menu();
        if (sub && ids::menuId(sub).id == menuId) {
            if (!popup)
                populateOnce(sub);
            return sub;
        }
    }
    return nullptr;
}

void ControlActionIndex::populateOnce(QMenu *menu) const
{
    // Top-down, each menu once per request: a second aboutToShow would
    // rebuild items (and delete the ones already found).
    for (QMenu *m : menuChain(menu)) {
        if (m_populated.contains(m))
            continue;
        m_populated.insert(m);
        populate(m);
    }
}

ControlActionIndex::Resolved ControlActionIndex::resolve(const QString &id) const
{
    Resolved r;
    if (id.isEmpty()) {
        r.error = QStringLiteral("empty id");
        return r;
    }

    m_populated.clear();
    QAction *action = nullptr;
    QMenu *menu = nullptr;
    if (!id.contains(QLatin1Char('/')) && id != QLatin1String("popup")) {
        const QList<QAction *> named = namedActions(id);
        if (!named.isEmpty()) {
            QPointer<QAction> found = named.first();
            // Opening its menus refreshes its enabled state, as for a user.
            if (QMenu *holder = holdingMenu(found); holder && !holder->isVisible())
                populateOnce(holder);
            if (!found)
                found = namedActions(id).value(0);
            action = found;
            menu = action ? holdingMenu(action) : nullptr;
        } else if (QMenu *m = resolveMenu(id)) {
            action = m->menuAction();
            menu = ids::parentMenuOf(m);
        }
    }
    if (!action) {
        const qsizetype slash = id.lastIndexOf(QLatin1Char('/'));
        if (slash > 0) {
            menu = resolveMenu(id.left(slash)); // filled already
            if (menu) {
                const QList<QAction *> actions = menu->actions();
                for (QAction *a : actions) {
                    if (!a->isSeparator() && ids::actionId(a, menu).id == id) {
                        action = a;
                        break;
                    }
                }
            }
        }
    }
    if (!action) {
        r.error = QStringLiteral("no action '%1'; list them with GET /actions").arg(id);
        return r;
    }

    const QList<QMenu *> chain = menu ? menuChain(menu) : QList<QMenu *>();
    r.action = action;
    r.menu = menu;
    r.inPopup = menu && menu->isVisible() && QApplication::activePopupWidget() != nullptr;
    r.enabled = action->isEnabled() && chainEnabled(chain);
    r.visible = action->isVisible() && chainVisible(chain);
    r.menuPath = chainTitles(chain);
    return r;
}

QJsonObject ControlActionIndex::toJson(const Entry &e)
{
    QJsonObject o{
        {QStringLiteral("id"), e.id},
        {QStringLiteral("label"), e.label},
        {QStringLiteral("enabled"), e.enabled},
        {QStringLiteral("location"), locationName(e.location)},
    };
    if (!e.stable)
        o.insert(QStringLiteral("stable"), false);
    if (!e.menuPath.isEmpty())
        o.insert(QStringLiteral("menu"), QJsonArray::fromStringList(e.menuPath));
    if (!e.shortcut.isEmpty())
        o.insert(QStringLiteral("shortcut"), e.shortcut);
    if (!e.visible)
        o.insert(QStringLiteral("visible"), false);
    if (e.checkable)
        o.insert(QStringLiteral("checked"), e.checked);
    if (e.submenu)
        o.insert(QStringLiteral("submenu"), true);
    return o;
}

} // namespace control
