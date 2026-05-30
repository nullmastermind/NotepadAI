/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
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


#include "DockedEditor.h"
#include "DockAreaSanitizer.h"
#include "DockAreaTabBar.h"
#include "DockAreaWidget.h"
#include "DockWidgetTab.h"
#include "DockComponentsFactory.h"
#include "DockedEditorTitleBar.h"
#include "DockAreaTitleBar.h"

#include "ScintillaNext.h"

#include <QEvent>
#include <QTimer>
#include <QUuid>


class DockedEditorComponentsFactory : public ads::CDockComponentsFactory
{
public:
    ads::CDockAreaTitleBar* createDockAreaTitleBar(ads::CDockAreaWidget* DockArea) const {
        DockedEditorTitleBar *titleBar = new DockedEditorTitleBar(DockArea);

        // Disable the built in context menu for the title bar since it has options we don't want
        titleBar->setContextMenuPolicy(Qt::NoContextMenu);

        return titleBar;
    }
};


DockedEditor::DockedEditor(QWidget *parent) : QObject(parent)
{
    ads::CDockComponentsFactory::setFactory(new DockedEditorComponentsFactory());

    ads::CDockManager::setConfigFlag(ads::CDockManager::AllTabsHaveCloseButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AlwaysShowTabs, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DragPreviewIsDynamic, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DragPreviewShowsContentPixmap, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, false);
    // When tabs title/text elide disabled and lots of tabs opened, tabs menu button will not show
    // as it only shows when tab title elided. 
    // So disable dynamic tabs menu visibility.
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaDynamicTabsMenuButtonVisibility, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::MiddleMouseButtonClosesTab, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, false);

    dockManager = new ads::CDockManager(parent);
    dockManager->setStyleSheet("");

    connect(dockManager, &ads::CDockManager::focusedDockWidgetChanged, this, [=](ads::CDockWidget* old, ads::CDockWidget* now) {
        Q_UNUSED(old)

        if (now == Q_NULLPTR) {
            currentEditor = Q_NULLPTR;
            return;
        }

        if (now->property("nn_previewTab").toBool()) {
            emit previewTabActivated(now->widget());
            return;
        }

        ScintillaNext *editor = qobject_cast<ScintillaNext *>(now->widget());
        if (editor == Q_NULLPTR) {
            return;
        }

        currentEditor = editor;
        editor->grabFocus();
        emit editorActivated(editor);
    });

    // Fire lastTabClosed() once the area is fully empty. dockWidgetRemoved is
    // emitted by CDockManager ONLY from CDockWidget::deleteDockWidget() (the
    // delete-on-close path) — never during drag/split/float — and AFTER the
    // widget and any now-empty dock area are removed, so totalTabCount() reads
    // the settled state here. Both tab kinds use DockWidgetDeleteOnClose, so
    // neither lingers as a hidden phantom in the layout count.
    connect(dockManager, &ads::CDockManager::dockWidgetRemoved, this, [this](ads::CDockWidget *) {
        if (totalTabCount() == 0)
            emit lastTabClosed();
    });

    connect(dockManager, &ads::CDockManager::dockAreaCreated, this, [=](ads::CDockAreaWidget* DockArea) {
        DockedEditorTitleBar *titleBar = qobject_cast<DockedEditorTitleBar *>(DockArea->titleBar());
        connect(titleBar, &DockedEditorTitleBar::doubleClicked, this, &DockedEditor::titleBarDoubleClicked);

        connect(DockArea->titleBar()->tabBar(), &ads::CDockAreaTabBar::tabMoved, this, [=](int from, int to) {
            Q_UNUSED(from);
            Q_UNUSED(to);

            emit editorOrderChanged();
        });

        // Pin preview editor if it was dragged to create this new area
        QTimer::singleShot(0, this, [this, DockArea]() {
            if (!m_previewEditor) return;
            auto *dw = qobject_cast<ads::CDockWidget *>(m_previewEditor->parentWidget());
            if (dw && dw->dockAreaWidget() == DockArea)
                pinPreviewEditor();
        });
    });
}


ScintillaNext *DockedEditor::getCurrentEditor() const
{
    return currentEditor.data();
}

int DockedEditor::count() const
{
    int total = 0;

    for (int i = 0; i < dockManager->dockAreaCount(); ++i) {
        for (const ads::CDockWidget *dw : dockManager->dockArea(i)->dockWidgets()) {
            if (!dw->property("nn_previewTab").toBool())
                ++total;
        }
    }

    return total;
}

int DockedEditor::totalTabCount() const
{
    // Counts every tab of every kind — editors AND nn_previewTab tabs (preview,
    // browser, mini-apps, future kinds) — so it answers "is the editor area
    // empty?" with no per-type branching: a new tab kind is counted for free.
    //
    // We iterate dockContainers() (the main dock manager container PLUS every
    // floating container), NOT just the main container's dockAreaCount(). Today
    // all tab types clear DockWidgetFloatable so none can be torn out into a
    // floating window — but counting only the main container would silently
    // undercount the instant any future tab type is made floatable, reopening
    // the spurious-"New 1" bug in a way that's painful to trace. Iterating all
    // containers makes the count correct regardless of the floatable flag.
    //
    // dockWidgetsCount() (the raw layout count) is correct here rather than
    // openDockWidgetsCount() (which filters !isClosed()): both our tab kinds set
    // DockWidgetDeleteOnClose, so a closed tab is REMOVED from the layout, never
    // left hidden-but-present. There is therefore no closed-but-undeleted
    // phantom to overcount, and this avoids a per-widget isClosed() scan. Each
    // dockWidgetsCount() is O(1) (a layout count), so this stays a cheap walk.
    int total = 0;

    for (const ads::CDockContainerWidget *container : dockManager->dockContainers()) {
        for (int i = 0; i < container->dockAreaCount(); ++i)
            total += container->dockArea(i)->dockWidgetsCount();
    }

    return total;
}

ScintillaNext *DockedEditor::initialEditor() const
{
    // Only a reusable scratch tab if it is the sole editor. count() is
    // editors-only (skips nn_previewTab), matching the historic semantics:
    // a lone "New X" alongside e.g. a browser tab is still replaceable.
    if (count() != 1)
        return nullptr;

    ScintillaNext *editor = getCurrentEditor();

    // getCurrentEditor() can be null mid-close: the cached pointer is auto-
    // nulled by QPointer when its editor is destroyed. Treat as "none".
    if (editor == nullptr)
        return nullptr;

    // Reject anything the user might care about: a temporary buffer, a real or
    // missing file, or a buffer with undo/redo history (i.e. it was edited).
    // Only a truly pristine "New X" survives and may be transparently closed.
    if (editor->isTemporary() || editor->isFile() || editor->canUndo() || editor->canRedo())
        return nullptr;

    return editor;
}

QVector<ScintillaNext *> DockedEditor::editors() const
{
    QVector<ScintillaNext *> editors;

    for (const ads::CDockAreaWidget* areaWidget : dockManager->openedDockAreas()) {
        for (const ads::CDockWidget* dockWidget : areaWidget->dockWidgets()) {
            if (dockWidget->property("nn_previewTab").toBool())
                continue;
            ScintillaNext *editor = qobject_cast<ScintillaNext *>(dockWidget->widget());
            if (editor)
                editors.append(editor);
        }
    }

    return editors;
}

void DockedEditor::switchToEditor(const ScintillaNext *editor)
{
    ads::CDockWidget *dockWidget = qobject_cast<ads::CDockWidget *>(editor->parentWidget());

    if (dockWidget == Q_NULLPTR) {
        qWarning() << "Expected editor's parent to be CDockWidget";
    }
    else {
        dockWidget->raise();
    }
}

void DockedEditor::dockWidgetCloseRequested()
{
    ads::CDockWidget *dockWidget = qobject_cast<ads::CDockWidget *>(sender());
    ScintillaNext *editor = qobject_cast<ScintillaNext *>(dockWidget->widget());

    emit editorCloseRequested(editor);
}

ads::CDockAreaWidget *DockedEditor::currentDockArea() const
{
    ads::CDockWidget *focused = dockManager->focusedDockWidget();
    ads::CDockAreaWidget *area = focused ? focused->dockAreaWidget() : latestDockArea.data();

    // Collapse a detached-but-not-yet-destroyed area to nullptr (see
    // sanitizeDockArea / DockAreaSanitizer.h for why this prevents the
    // null-container crash in CDockManager::addDockWidget).
    return sanitizeDockArea(area);
}

void DockedEditor::addEditor(ScintillaNext *editor)
{
    qInfo(Q_FUNC_INFO);

    Q_ASSERT(editor != Q_NULLPTR);

    if (currentEditor == Q_NULLPTR) {
        currentEditor = editor;
    }

    // Create the dock widget for the editor
    ads::CDockWidget *dockWidget = dockManager->createDockWidget(editor->getName());

    // Disable elide, elided file names not readable when lots of files opened
    dockWidget->tabWidget()->setElideMode(Qt::ElideNone);

    // We need a unique object name. Can't use the name or file path so use a uuid
    dockWidget->setObjectName(QUuid::createUuid().toString());

    dockWidget->setWidget(editor);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetDeleteOnClose, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::CustomCloseHandling, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetFloatable, false);

    dockWidget->tabWidget()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dockWidget->tabWidget(), &QWidget::customContextMenuRequested, this, [=](const QPoint &pos) {
        Q_UNUSED(pos)

        emit contextMenuRequestedForEditor(editor);
    });

    // Set the tooltip based on the buffer
    if (editor->isFile()) {
        dockWidget->tabWidget()->setToolTip(editor->getFilePath());
    }
    else {
        dockWidget->tabWidget()->setToolTip(editor->getName());
    }

    // Set the icon
    if (editor->readOnly()) {
        dockWidget->tabWidget()->setIcon(QIcon(":/icons/readonly.png"));
    }
    else {
        dockWidget->tabWidget()->setIcon(QIcon(editor->canSaveToDisk() ? ":/icons/unsaved.png" : ":/icons/saved.png"));
        connect(editor, &ScintillaNext::savePointChanged, dockWidget, [=](bool dirty) {
            Q_UNUSED(dirty)
            const bool actuallyDirty = editor->canSaveToDisk();
            const QString iconPath = actuallyDirty ? ":/icons/unsaved.png" : ":/icons/saved.png";
            dockWidget->tabWidget()->setIcon(QIcon(iconPath));
        });
    }

    connect(editor, &ScintillaNext::closed, dockWidget, &ads::CDockWidget::closeDockWidget);
    connect(editor, &ScintillaNext::closed, this, [=]() { emit editorClosed(editor); });
    connect(editor, &ScintillaNext::renamed, this, [=]() { editorRenamed(editor); });

    connect(dockWidget, &ads::CDockWidget::closeRequested, this, &DockedEditor::dockWidgetCloseRequested);

    latestDockArea = dockManager->addDockWidget(ads::CenterDockWidgetArea, dockWidget, currentDockArea());

    emit editorAdded(editor);
}

void DockedEditor::addPreviewEditor(ScintillaNext *editor)
{
    if (m_previewEditor && m_previewEditor != editor) {
        m_previewEditor->close();
    }

    m_previewEditor = editor;

    ads::CDockWidget *dockWidget = qobject_cast<ads::CDockWidget *>(editor->parentWidget());
    if (dockWidget) {
        applyPreviewStyle(dockWidget, true);
        dockWidget->tabWidget()->installEventFilter(this);
    }

    connect(editor, &ScintillaNext::savePointChanged, this, [this, editor](bool dirty) {
        if (dirty && m_previewEditor == editor) {
            pinPreviewEditor();
        }
    });

    emit previewEditorSet();
}

ScintillaNext *DockedEditor::previewEditor() const
{
    return m_previewEditor.data();
}

void DockedEditor::pinPreviewEditor()
{
    if (!m_previewEditor) return;

    ads::CDockWidget *dockWidget = qobject_cast<ads::CDockWidget *>(m_previewEditor->parentWidget());
    if (dockWidget) {
        applyPreviewStyle(dockWidget, false);
        dockWidget->tabWidget()->removeEventFilter(this);
    }
    m_previewEditor = nullptr;
}

void DockedEditor::applyPreviewStyle(ads::CDockWidget *dockWidget, bool preview)
{
    auto *tab = dockWidget->tabWidget();
    QFont f = tab->font();
    f.setItalic(preview);
    tab->setFont(f);
}

bool DockedEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonDblClick && m_previewEditor) {
        auto *tab = qobject_cast<ads::CDockWidgetTab *>(watched);
        if (tab) {
            ads::CDockWidget *dw = tab->dockWidget();
            ScintillaNext *editor = qobject_cast<ScintillaNext *>(dw->widget());
            if (editor == m_previewEditor) {
                pinPreviewEditor();
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void DockedEditor::editorRenamed(ScintillaNext *editor)
{
    Q_ASSERT(editor != Q_NULLPTR);

    ads::CDockWidget *dockWidget = qobject_cast<ads::CDockWidget *>(editor->parentWidget());

    dockWidget->setWindowTitle(editor->getName());

    if (editor->isFile()) {
        dockWidget->tabWidget()->setToolTip(editor->getFilePath());
    }
    else {
        dockWidget->tabWidget()->setToolTip(editor->getName());
    }
}

ads::CDockWidget *DockedEditor::addPreviewTab(QWidget *widget, const QString &title, const QIcon &icon)
{
    ads::CDockWidget *dockWidget = dockManager->createDockWidget(title);
    dockWidget->setObjectName(QUuid::createUuid().toString());
    dockWidget->setWidget(widget);
    dockWidget->setProperty("nn_previewTab", true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetDeleteOnClose, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetFloatable, false);

    dockWidget->tabWidget()->setElideMode(Qt::ElideNone);
    if (!icon.isNull())
        dockWidget->tabWidget()->setIcon(icon);

    latestDockArea = dockManager->addDockWidget(ads::CenterDockWidgetArea, dockWidget, currentDockArea());

    return dockWidget;
}

void DockedEditor::closeFocusedTab()
{
    ads::CDockWidget *focused = dockManager->focusedDockWidget();
    if (!focused) return;

    if (focused->property("nn_previewTab").toBool()) {
        focused->closeDockWidget();
    } else {
        ScintillaNext *editor = qobject_cast<ScintillaNext *>(focused->widget());
        if (editor)
            emit editorCloseRequested(editor);
    }
}

void DockedEditor::splitToRight(ScintillaNext *editor)
{
    Q_ASSERT(editor != Q_NULLPTR);

    ads::CDockWidget *newDockWidget = qobject_cast<ads::CDockWidget *>(editor->parentWidget());
    if (newDockWidget) {
        ads::CDockAreaWidget *currentArea = currentDockArea();
        if (currentArea) {
            dockManager->addDockWidget(ads::RightDockWidgetArea, newDockWidget, currentArea);
        }
    }
}

void DockedEditor::splitToBottom(ScintillaNext *editor)
{
    Q_ASSERT(editor != Q_NULLPTR);

    ads::CDockWidget *newDockWidget = qobject_cast<ads::CDockWidget *>(editor->parentWidget());
    if (newDockWidget) {
        ads::CDockAreaWidget *currentArea = currentDockArea();
        if (currentArea) {
            dockManager->addDockWidget(ads::BottomDockWidgetArea, newDockWidget, currentArea);
        }
    }
}
