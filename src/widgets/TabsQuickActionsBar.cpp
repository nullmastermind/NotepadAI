/*
 * This file is part of Notepad Next.
 * Copyright 2026 Justin Dailey
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


#include <QApplication>
#include <QToolButton>
#include <QStyle>
#include <QMenu>
#include <QEvent>
#include <QPainter>
#include <QPixmap>

#include <map>

#include "TabsQuickActionsBar.h"

namespace
{
    constexpr QLatin1StringView IconPlusPath(":/icons/plus.svg");
    constexpr QLatin1StringView IconListPath(":/icons/list_with_icons.svg");
    constexpr QLatin1StringView IconCrossPath(":/icons/cross.svg");

    // The three svgs we ship use stroke="currentColor". Qt's svg icon engine
    // resolves that to opaque black, which is invisible on dark backgrounds.
    // Re-render the icon as a set of pixmaps at common toolbutton sizes and
    // tint each one with `color` via SourceIn so the alpha (the strokes) is
    // preserved while the rgb becomes the palette text color.
    QIcon tintedIcon(const QString &svgPath, const QColor &color)
    {
        QIcon source(svgPath);
        if (source.isNull()) return source;

        QIcon dst;
        // Cover the sizes Qt asks for in practice: small icon (16 on most
        // styles, sometimes 22/24), plus a few larger so HiDPI scaling has
        // sharp source pixmaps.
        const QList<int> sizes{16, 20, 22, 24, 32, 48, 64};
        for (int sz : sizes) {
            QPixmap pm = source.pixmap(sz, sz);
            if (pm.isNull()) continue;
            QPainter p(&pm);
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(pm.rect(), color);
            p.end();
            dst.addPixmap(pm);
        }
        return dst;
    }
}

TabsQuickActionsBar::TabsQuickActionsBar(const Buttons &visibileButtons, QWidget *parent)
    : QToolBar(parent)
{
    createNewTabAction = addAction(QIcon(), "");
    createNewTabAction->setObjectName(QStringLiteral("actionQuickNewTab"));
    createNewTabAction->setToolTip(tr("Create a new file"));

    showTabsMenuAction = addAction(QIcon(), "");
    showTabsMenuAction->setObjectName(QStringLiteral("actionQuickTabsMenu"));
    showTabsMenuAction->setToolTip(tr("Show opened files list"));

    const auto tabsMenu = new QMenu(this);
    tabsMenu->setObjectName(QStringLiteral("menuQuickTabs"));
    showTabsMenuAction->setMenu(tabsMenu);

    closeCurrentTabAction = addAction(QIcon(), "");
    closeCurrentTabAction->setObjectName(QStringLiteral("actionQuickCloseTab"));
    closeCurrentTabAction->setToolTip(tr("Close the current file"));

    rebuildIcons();

    const auto iconSize = qApp->style()->pixelMetric(QStyle::PM_SmallIconSize);
    setIconSize({ iconSize, iconSize });
    setStyleSheet(
        "QToolBar { padding: 0px; margin: 0px; }"
        "QToolButton::menu-indicator { image: none; }"
    );

    // Trick, cause addWidget will lose some style things
    const auto toolButton = qobject_cast<QToolButton*>(widgetForAction(showTabsMenuAction));
    if (toolButton) toolButton->setPopupMode(QToolButton::InstantPopup);

    connect(createNewTabAction, &QAction::triggered, this, &TabsQuickActionsBar::createNewTabClicked);
    connect(tabsMenu, &QMenu::aboutToShow, this, [this, tabsMenu]() { emit tabsMenuAboutToShow(tabsMenu); });
    connect(closeCurrentTabAction, &QAction::triggered, this, &TabsQuickActionsBar::closeCurrentTabClicked);

    setVisibileButtons(visibileButtons);
}

void TabsQuickActionsBar::rebuildIcons()
{
    const QColor color = palette().color(QPalette::WindowText);
    createNewTabAction->setIcon(tintedIcon(IconPlusPath, color));
    showTabsMenuAction->setIcon(tintedIcon(IconListPath, color));
    closeCurrentTabAction->setIcon(tintedIcon(IconCrossPath, color));
}

void TabsQuickActionsBar::changeEvent(QEvent *event)
{
    QToolBar::changeEvent(event);

    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::ApplicationPaletteChange:
        rebuildIcons();
        break;
    default:
        break;
    }
}

void TabsQuickActionsBar::setVisibileButtons(const Buttons &buttons)
{
    if (visibileButtons == buttons)
        return;

    visibileButtons = buttons;

    const std::map<QAction*, Button> mapping {
        { createNewTabAction,    CreateNewTab    },
        { showTabsMenuAction,    ShowTabsMenu    },
        { closeCurrentTabAction, CloseCurrentTab }
    };

    for (const auto &pair : mapping)
        pair.first->setVisible(buttons.testFlag(pair.second));

    emit visibileButtonsChanged(buttons);
}

void TabsQuickActionsBar::setVisibileButton(Button button, bool on)
{
    const auto &currentOptions = visibileButtons;
    setVisibileButtons(
        on ? (currentOptions |  button)
           : (currentOptions & ~button)
    );
}
