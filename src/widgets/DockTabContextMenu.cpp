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

#include "DockTabContextMenu.h"

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QMenu>
#include <QPoint>
#include <QPointer>
#include <QTabBar>
#include <QVariant>

namespace {

bool isClosable(const QDockWidget *dock)
{
    return dock != nullptr && dock->features().testFlag(QDockWidget::DockWidgetClosable);
}

using GuardedDocks = QList<QPointer<QDockWidget>>;

GuardedDocks guard(const QList<QDockWidget *> &docks)
{
    GuardedDocks guarded;
    guarded.reserve(docks.size());
    for (QDockWidget *dock : docks)
        guarded.append(QPointer<QDockWidget>(dock));
    return guarded;
}

// Docks are guarded rather than raw because closing one can delete others:
// WA_DeleteOnClose docks self-destruct, and an owner (TerminalManager,
// MainWindow) may reap siblings in response.
void closeAll(const GuardedDocks &targets)
{
    for (const auto &dock : targets) {
        if (!dock.isNull())
            dock->close();
    }
}

struct Entry
{
    DockTabContextMenu::Scope scope;
    const char *text;
};

// Wording overlaps the editor tab menu (MainWindow.ui) on Close / Left / Right
// / All. "Except This One" replaces "Except Active Document" — these tabs are
// docks, not documents.
constexpr Entry kEntries[] = {
    {DockTabContextMenu::Scope::Close, QT_TR_NOOP("Close")},
    {DockTabContextMenu::Scope::CloseOthers, QT_TR_NOOP("Close All Except This One")},
    {DockTabContextMenu::Scope::CloseToLeft, QT_TR_NOOP("Close All to the Left")},
    {DockTabContextMenu::Scope::CloseToRight, QT_TR_NOOP("Close All to the Right")},
    {DockTabContextMenu::Scope::CloseAll, QT_TR_NOOP("Close All")},
};

} // namespace

namespace DockTabContextMenu {

QList<QDockWidget *> tabOrder(QTabBar *tabBar)
{
    if (tabBar == nullptr || tabBar->count() == 0)
        return {};

    // QDockAreaLayoutInfo::updateTabBar() stores each dock's QWidget* in the
    // tab's data as a quintptr. That is the only mapping we trust: it holds for
    // both the QMainWindow tab bar and the floating QDockWidgetGroupWindow tab
    // bar, and it is empty on every other QTabBar in the process (Files/Git,
    // Find/Replace, …). All-or-nothing — a partial map would shift positions
    // that Close-to-Left/Right read.
    QList<QDockWidget *> order;
    order.reserve(tabBar->count());
    for (int i = 0; i < tabBar->count(); ++i) {
        const QVariant data = tabBar->tabData(i);
        if (!data.isValid())
            return {};
        auto *widget = reinterpret_cast<QWidget *>(qvariant_cast<quintptr>(data));
        auto *dock = qobject_cast<QDockWidget *>(widget);
        if (dock == nullptr)
            return {};
        order.append(dock);
    }
    return order;
}

QList<QDockWidget *> closeTargets(const QList<QDockWidget *> &order, int index, Scope scope)
{
    if (index < 0 || index >= order.size())
        return {};

    int from = 0;
    int to = order.size() - 1; // inclusive
    switch (scope) {
    case Scope::Close:
        from = to = index;
        break;
    case Scope::CloseToLeft:
        to = index - 1;
        break;
    case Scope::CloseToRight:
        from = index + 1;
        break;
    case Scope::CloseOthers:
    case Scope::CloseAll:
        break; // whole range; CloseOthers skips `index` below
    }

    QList<QDockWidget *> targets;
    if (to < from)
        return targets;
    targets.reserve(to - from + 1);
    for (int i = from; i <= to; ++i) {
        if (scope == Scope::CloseOthers && i == index)
            continue;
        QDockWidget *dock = order.at(i);
        if (isClosable(dock))
            targets.append(dock);
    }
    return targets;
}

void populateMenu(QMenu *menu, const QList<QDockWidget *> &order, int index)
{
    if (menu == nullptr)
        return;

    for (const Entry &entry : kEntries) {
        const QList<QDockWidget *> targets = closeTargets(order, index, entry.scope);
        QAction *action = menu->addAction(
            QCoreApplication::translate("DockTabContextMenu", entry.text));
        action->setEnabled(!targets.isEmpty());
        if (targets.isEmpty())
            continue;
        // menu.exec() spins an event loop, so a dock can die between building
        // the menu and triggering the action.
        QObject::connect(action, &QAction::triggered, menu, [guarded = guard(targets)]() {
            closeAll(guarded);
        });
    }
}

bool showMenu(QTabBar *tabBar, const QPoint &pos)
{
    const QList<QDockWidget *> order = tabOrder(tabBar);
    if (order.isEmpty())
        return false;

    const int index = tabBar->tabAt(pos);
    if (index < 0 || index >= order.size())
        return true; // dock tab bar, miss — swallow, no menu

    order.at(index)->raise();

    QMenu menu(tabBar);
    populateMenu(&menu, order, index);
    menu.exec(tabBar->mapToGlobal(pos));
    return true;
}

} // namespace DockTabContextMenu
