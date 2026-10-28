/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlIds.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QMetaEnum>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>

#include <algorithm>

namespace control::ids {

namespace {

QString escapeSegmentImpl(const QString &s)
{
    if (!s.contains(QLatin1Char('%')) && !s.contains(QLatin1Char('/')) && !s.contains(QLatin1Char('@'))
        && !s.contains(QLatin1Char('#')))
        return s;
    QString out;
    out.reserve(s.size() + 8);
    for (QChar c : s) {
        switch (c.unicode()) {
        case '%': out += QLatin1String("%25"); break;
        case '/': out += QLatin1String("%2F"); break;
        case '@': out += QLatin1String("%40"); break;
        case '#': out += QLatin1String("%23"); break;
        default: out += c;
        }
    }
    return out;
}

QString unescapeSegment(const QString &s)
{
    if (!s.contains(QLatin1Char('%')))
        return s;
    QString out = s;
    out.replace(QLatin1String("%2F"), QLatin1String("/"), Qt::CaseInsensitive);
    out.replace(QLatin1String("%40"), QLatin1String("@"));
    out.replace(QLatin1String("%23"), QLatin1String("#"));
    out.replace(QLatin1String("%25"), QLatin1String("%"));
    return out;
}

bool isNamed(const QObject *o)
{
    const QString &n = o->objectName();
    return !n.isEmpty() && !n.startsWith(QLatin1String("qt_"));
}

// The name a widget's segment uses: its object name, else one derived from
// what it stands for (a tool button showing a named action is that action's
// button), so toolbars need no hand-named buttons.
constexpr QLatin1StringView kTabWidgetTabBar("qt_tabwidget_tabbar");

QString widgetName(const QWidget *w)
{
    // A data key wins: widgets that share a style name (a form's options) are
    // told apart by it.
    if (QString k = key(w); !k.isEmpty())
        return k;
    if (isNamed(w))
        return w->objectName();
    // A QTabWidget's own tab bar.
    if (w->objectName() == kTabWidgetTabBar)
        return QStringLiteral("tabBar");
    if (w->objectName().isEmpty()) {
        if (const auto *button = qobject_cast<const QToolButton *>(w)) {
            if (const QAction *a = button->defaultAction(); a && isNamed(a))
                return QStringLiteral("button:") + a->objectName();
        }
        // A dialog's standard button is its role: button:ok, button:cancel, button:yes...
        if (auto *box = qobject_cast<QDialogButtonBox *>(w->parentWidget())) {
            if (auto *b = qobject_cast<QAbstractButton *>(const_cast<QWidget *>(w))) {
                const QDialogButtonBox::StandardButton sb = box->standardButton(b);
                if (sb != QDialogButtonBox::NoButton) {
                    if (const char *k = QMetaEnum::fromType<QDialogButtonBox::StandardButtons>().valueToKey(sb))
                        return QStringLiteral("button:") + QString::fromLatin1(k).toLower();
                }
            }
        }
    }
    return {};
}

const QWidget *rootOf(const QWidget *w)
{
    while (w && !isRoot(w))
        w = w->parentWidget();
    return w;
}


QString className(const QWidget *w)
{
    return QString::fromLatin1(w->metaObject()->className());
}

// Unnamed widgets of `cls` under `anchor` in preorder, the way findChildren walks.
QList<QWidget *> positional(const QWidget *anchor, const QString &cls)
{
    QList<QWidget *> out;
    const QList<QWidget *> all = anchor->findChildren<QWidget *>();
    for (QWidget *c : all) {
        if (!isSegment(c) && !isRoot(c) && !isIgnored(c) && className(c) == cls && anchorOf(c) == anchor)
            out.append(c);
    }
    return out;
}

// ADS names docks "{uuid}": curl reads {} in a URL as a glob, so ids drop them
// (the braced form still resolves).
QString withoutBraces(const QString &name)
{
    if (name.size() >= 2 && name.front() == QLatin1Char('{') && name.back() == QLatin1Char('}'))
        return name.mid(1, name.size() - 2);
    return name;
}

QString baseRootId(const QWidget *root)
{
    const QString explicitId = root->property(kRootProperty).toString();
    if (!explicitId.isEmpty())
        return explicitId;
    if (root->inherits("ads::CDockWidget"))
        return QStringLiteral("dock:") + escapeSegment(withoutBraces(root->objectName()));
    return escapeSegment(isNamed(root) ? root->objectName() : className(root));
}

} // namespace

QString escapeSegment(const QString &s)
{
    return escapeSegmentImpl(s);
}

bool isSegment(const QWidget *w)
{
    return !widgetName(w).isEmpty() && !isTransparentContainer(w);
}

QList<QWidget *> positionalSiblings(const QWidget *anchor, const QString &className)
{
    return positional(anchor, className);
}

bool isRoot(const QWidget *w)
{
    return w->isWindow() || !w->property(kRootProperty).toString().isEmpty()
           || w->inherits("ads::CDockWidget");
}

const QWidget *anchorOf(const QWidget *w)
{
    for (const QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
        if (isRoot(p) || isSegment(p))
            return p;
    }
    return nullptr;
}

QString segmentOf(const QWidget *widget, bool *stable)
{
    if (isSegment(widget)) {
        if (stable)
            *stable = true;
        return escapeSegment(widgetName(widget));
    }
    if (stable)
        *stable = false;
    const QWidget *anchor = anchorOf(widget);
    const QString cls = className(widget);
    const qsizetype n = anchor ? positional(anchor, cls).indexOf(const_cast<QWidget *>(widget)) : 0;
    return cls + QLatin1Char('#') + QString::number(qMax<qsizetype>(0, n));
}

QMenu *parentMenuOf(const QMenu *menu)
{
    const QList<QObject *> owners = menu->menuAction()->associatedObjects();
    for (QObject *o : owners) {
        if (auto *parentMenu = qobject_cast<QMenu *>(o)) {
            if (parentMenu != menu)
                return parentMenu;
        }
    }
    return nullptr;
}

QString slug(const QString &text)
{
    QString out;
    out.reserve(text.size());
    bool dash = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('&')) {
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('&')) {
                ++i;
                if (!out.isEmpty())
                    dash = true;
            }
            continue; // mnemonic marker
        }
        if (c.isLetterOrNumber()) {
            if (dash && !out.isEmpty())
                out += QLatin1Char('-');
            dash = false;
            out += c.toLower();
        } else if (!out.isEmpty()) {
            dash = true;
        }
    }
    return out;
}

Id menuId(const QMenu *menu)
{
    if (!menu)
        return {};
    if (isNamed(menu))
        return {escapeSegment(menu->objectName()), true};
    const QString explicitKey = key(menu).isEmpty() ? key(menu->menuAction()) : key(menu);
    const QString own = escapeSegment(explicitKey.isEmpty() ? slug(menu->title()) : explicitKey);
    const bool ownStable = !explicitKey.isEmpty();
    if (const QMenu *parentMenu = parentMenuOf(menu)) {
        const Id parent = menuId(parentMenu);
        return {parent.id + QLatin1Char('/') + own, parent.stable && ownStable};
    }
    // An unnamed menu nothing owns is a context menu opened on its own.
    if (own.isEmpty())
        return {QStringLiteral("popup"), false};
    return {own, ownStable};
}

Id actionId(const QAction *action, const QMenu *menu)
{
    if (!action)
        return {};
    if (QMenu *sub = action->menu())
        return menuId(sub);
    if (isNamed(action))
        return {escapeSegment(action->objectName()), true};
    QString explicitKey = key(action);
    // A named dock's own show/hide action is that dock's toggle.
    if (explicitKey.isEmpty()) {
        if (const auto *dock = qobject_cast<const QDockWidget *>(action->parent());
            dock && isNamed(dock) && dock->toggleViewAction() == action)
            explicitKey = QStringLiteral("toggle:") + dock->objectName();
    }
    const QString own = escapeSegment(explicitKey.isEmpty() ? slug(action->text()) : explicitKey);
    if (!menu)
        return {own, !explicitKey.isEmpty()};
    const Id parent = menuId(menu);
    return {parent.id + QLatin1Char('/') + own, parent.stable && !explicitKey.isEmpty()};
}

bool isTransparentContainer(const QWidget *widget)
{
    const QString &name = widget->objectName();
    if (name.startsWith(QLatin1String("qt_")) && name != kTabWidgetTabBar)
        return true;
    // Plain Qt widgets ADS names for itself, and its title bar (the app subclasses it).
    if (name == QLatin1String("dockWidgetScrollArea") || name == QLatin1String("tabsContainerWidget")
        || widget->inherits("ads::CDockAreaTitleBar"))
        return true;
    const char *cls = widget->metaObject()->className();
    return qstrncmp(cls, "ads::", 5) == 0 && !widget->inherits("ads::CDockWidget");
}

namespace {

// Windows by base id, numbering repeats ("QDialog", "QDialog#1"): the main
// window first, then by id, then by address. Only windows repeat; a dock's
// name and a marked root's id are unique by construction, so they are never
// numbered and finding one's id needs no walk over every widget.
QList<QPair<QWidget *, QString>> windowTable()
{
    const QList<QWidget *> tops = QApplication::topLevelWidgets();
    QList<QPair<QWidget *, QString>> table;
    table.reserve(tops.size());
    for (QWidget *w : tops)
        table.append({w, baseRootId(w)});
    std::stable_sort(table.begin(), table.end(), [](const auto &a, const auto &b) {
        const bool am = qobject_cast<const QMainWindow *>(a.first) != nullptr;
        const bool bm = qobject_cast<const QMainWindow *>(b.first) != nullptr;
        if (am != bm)
            return am;
        if (a.second != b.second)
            return a.second < b.second;
        return std::less<const QWidget *>()(a.first, b.first);
    });
    // Equal base ids are adjacent now: number the repeats.
    for (qsizetype i = 1, run = 0; i < table.size(); ++i) {
        const QString base = table.at(i).second.section(QLatin1Char('#'), 0, 0);
        if (base == table.at(i - 1).second.section(QLatin1Char('#'), 0, 0)) {
            ++run;
            table[i].second = base + QLatin1Char('#') + QString::number(run);
        } else {
            run = 0;
        }
    }
    return table;
}

} // namespace

QList<QPair<QWidget *, QString>> rootTable()
{
    QList<QPair<QWidget *, QString>> table = windowTable();
    const qsizetype windows = table.size();
    for (qsizetype i = 0; i < windows; ++i) {
        for (QWidget *c : table.at(i).first->findChildren<QWidget *>()) {
            if (!c->isWindow() && isRoot(c))
                table.append({c, baseRootId(c)});
        }
    }
    return table;
}

QList<QWidget *> rootWidgets()
{
    QList<QWidget *> out;
    for (const auto &entry : rootTable())
        out.append(entry.first);
    return out;
}

QString rootIdOf(const QWidget *root)
{
    if (!root->isWindow())
        return baseRootId(root);
    for (const auto &entry : windowTable()) {
        if (entry.first == root)
            return entry.second;
    }
    return baseRootId(root);
}

Id nodeId(const QWidget *widget)
{
    if (!widget)
        return {};
    const QWidget *root = rootOf(widget);
    if (!root)
        return {};
    Id result{rootIdOf(root), isNamed(root) || !root->property(kRootProperty).toString().isEmpty()};
    if (widget == root)
        return result;

    QStringList segments;
    bool selfStable = true;
    segments.append(segmentOf(widget, &selfStable));
    result.stable = result.stable && selfStable;
    for (const QWidget *p = widget->parentWidget(); p && p != root; p = p->parentWidget()) {
        if (isSegment(p))
            segments.prepend(escapeSegment(widgetName(p)));
    }
    result.id += QLatin1Char('/') + segments.join(QLatin1Char('/'));
    return result;
}

QWidget *resolveNode(const QString &id, QString *error)
{
    auto fail = [error](const QString &message) -> QWidget * {
        if (error)
            *error = message;
        return nullptr;
    };
    const QStringList parts = id.split(QLatin1Char('/'));
    if (parts.isEmpty() || parts.first().isEmpty())
        return fail(QStringLiteral("empty id"));

    QString rootId = parts.first();
    if (rootId.startsWith(QLatin1String("dock:{")) && rootId.endsWith(QLatin1Char('}')))
        rootId = QStringLiteral("dock:") + withoutBraces(rootId.mid(5));
    QWidget *current = nullptr;
    for (const auto &entry : rootTable()) {
        if (entry.second == rootId) {
            current = entry.first;
            break;
        }
    }
    if (!current)
        return fail(QStringLiteral("no window or dock '%1'").arg(parts.first()));

    for (qsizetype i = 1; i < parts.size(); ++i) {
        const QString &seg = parts.at(i);
        const qsizetype hash = seg.indexOf(QLatin1Char('#'));
        QWidget *next = nullptr;
        if (hash > 0) {
            bool ok = false;
            const int n = seg.mid(hash + 1).toInt(&ok);
            const QList<QWidget *> candidates = positional(current, seg.left(hash));
            if (ok && n >= 0 && n < candidates.size())
                next = candidates.at(n);
        } else {
            const QString name = unescapeSegment(seg);
            // Segments are widget names (object name, data key or a derived name).
            const QList<QWidget *> all = current->findChildren<QWidget *>();
            for (QWidget *c : all) {
                if (widgetName(c) == name && !isTransparentContainer(c) && anchorOf(c) == current) {
                    next = c;
                    break;
                }
            }
        }
        if (!next)
            return fail(QStringLiteral("'%1' not found under '%2'")
                            .arg(seg, parts.mid(0, i).join(QLatin1Char('/'))));
        current = next;
    }
    return current;
}

} // namespace control::ids
