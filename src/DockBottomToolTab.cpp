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

#include "DockBottomToolTab.h"

#include "DockAreaWidget.h"
#include "DockContainerWidget.h"
#include "DockManager.h"
#include "DockSplitter.h"
#include "DockWidget.h"

#include <QChildEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QSplitterHandle>

namespace {

void pinToolPaneQuarter(ads::CDockSplitter *splitter);

class ResetToolQuarterFilter : public QObject
{
public:
    explicit ResetToolQuarterFilter(ads::CDockSplitter *splitter)
        : QObject(splitter)
        , m_splitter(splitter)
    {
        splitter->installEventFilter(this);
        for (int i = 1; i < splitter->count(); ++i) {
            if (QSplitterHandle *handle = splitter->handle(i))
                handle->installEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ChildAdded) {
            QObject *child = static_cast<QChildEvent *>(event)->child();
            if (qobject_cast<QSplitterHandle *>(child))
                child->installEventFilter(this);
            return false;
        }
        if (event->type() == QEvent::MouseButtonDblClick
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton
            && qobject_cast<QSplitterHandle *>(watched))
        {
            pinToolPaneQuarter(m_splitter);
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    ads::CDockSplitter *m_splitter;
};

void pinToolPaneQuarter(ads::CDockSplitter *splitter)
{
    if (!splitter || splitter->orientation() != Qt::Vertical || splitter->count() != 2)
        return;
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    const int h = splitter->height();
    if (h > 0)
        splitter->setSizes({h * 3 / 4, h / 4});

    if (!splitter->property("nn_toolQuarterFilter").toBool()) {
        new ResetToolQuarterFilter(splitter);
        splitter->setProperty("nn_toolQuarterFilter", true);
    }
}

} // namespace

ads::CDockAreaWidget *addDockWidgetAsBottomTool(
    ads::CDockManager *manager,
    ads::CDockWidget *widget,
    ads::CDockAreaWidget *editorArea,
    ads::CDockAreaWidget *existingToolArea)
{
    if (existingToolArea)
        return manager->addDockWidget(ads::CenterDockWidgetArea, widget, existingToolArea);

    ads::CDockAreaWidget *toolArea =
        manager->addDockWidget(ads::BottomDockWidgetArea, widget, editorArea);
    pinToolPaneQuarter(toolArea->parentSplitter());
    return toolArea;
}

int nonToolTabCount(ads::CDockManager *manager)
{
    int total = 0;
    for (const ads::CDockContainerWidget *container : manager->dockContainers()) {
        for (int i = 0; i < container->dockAreaCount(); ++i) {
            for (const ads::CDockWidget *dw : container->dockArea(i)->dockWidgets()) {
                if (!dw->property("nn_toolTab").toBool())
                    ++total;
            }
        }
    }
    return total;
}

ads::CDockAreaWidget *findToolDockArea(ads::CDockManager *manager)
{
    for (int i = 0; i < manager->dockAreaCount(); ++i) {
        ads::CDockAreaWidget *area = manager->dockArea(i);
        for (const ads::CDockWidget *dw : area->dockWidgets()) {
            if (dw->property("nn_toolTab").toBool())
                return area;
        }
    }
    return nullptr;
}

ads::CDockAreaWidget *addDockWidgetAsContent(
    ads::CDockManager *manager,
    ads::CDockWidget *widget,
    ads::CDockAreaWidget *editorArea,
    ads::CDockAreaWidget *toolArea)
{
    if (editorArea)
        return manager->addDockWidget(ads::CenterDockWidgetArea, widget, editorArea);

    if (toolArea) {
        ads::CDockAreaWidget *content =
            manager->addDockWidget(ads::TopDockWidgetArea, widget, toolArea);
        pinToolPaneQuarter(content->parentSplitter());
        return content;
    }

    return manager->addDockWidget(ads::CenterDockWidgetArea, widget, nullptr);
}
