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


#ifndef DOCKEDEDITOR_H
#define DOCKEDEDITOR_H

#include <QObject>
#include <QPointer>
#include <QIcon>

#include "DockManager.h"
#include "ScintillaNext.h"

class DockedEditor : public QObject
{
    Q_OBJECT

private:
    ads::CDockManager* dockManager = Q_NULLPTR;
    // QPointer so we read nullptr instead of a dangling pointer if ADS destroys
    // the area (e.g. last editor closed). currentDockArea() then falls back to
    // "no current area" and addDockWidget creates a fresh CenterDockWidgetArea.
    QPointer<ads::CDockAreaWidget> latestDockArea;
    // Same reasoning for the cached editor: it can outlive its ScintillaNext
    // when the tab is closed and the widget is deleteLater()'d. getCurrentEditor()
    // returns nullptr in that case; callers must null-check.
    QPointer<ScintillaNext> currentEditor;

public:
    explicit DockedEditor(QWidget *parent);

    ScintillaNext *getCurrentEditor() const;
    ads::CDockAreaWidget *currentDockArea() const;

    QVector<ScintillaNext *> editors() const;

    void switchToEditor(const ScintillaNext *editor);

    // Number of real editors only (excludes preview/browser/mini-app tabs).
    int count() const;

    // Number of tabs of EVERY kind currently in the center dock area —
    // editors plus all `nn_previewTab` tabs (preview, browser, mini-apps, and
    // any future tab type routed through the same dock manager). Use this, not
    // count(), to decide whether the editor area is truly empty (e.g. whether
    // to spawn a fresh "New X" buffer). New tab kinds are counted automatically
    // as long as they are added through addEditor()/addPreviewTab().
    int totalTabCount() const;

    // The single reusable "initial" editor — an unedited, pristine "New X"
    // scratch buffer that is currently the sole editor — or nullptr if there
    // isn't one. "Pristine" means: a New-type buffer (not a file, not missing,
    // not temporary) with nothing to undo/redo, i.e. the user never touched it.
    //
    // This is THE shared definition of "is there a throwaway scratch tab I can
    // transparently replace?". Every surface that opens a new piece of content
    // (newFile, file open, file preview, web/browser tabs, future tab kinds)
    // must snapshot this BEFORE adding its own tab, then close the result AFTER,
    // so the scratch "New X" is swapped out without ever emptying the area mid-
    // flight (which would otherwise fire lastTabClosed and respawn). Centralised
    // here — on DockedEditor, which every tab-spawning subsystem already holds —
    // so MainWindow-less callers (e.g. MiniAppManager's web tabs) share it
    // instead of reinventing the pristine test. MainWindow::getInitialEditor()
    // delegates here.
    ScintillaNext *initialEditor() const;

    ScintillaNext *previewEditor() const;
    void pinPreviewEditor();

    ads::CDockWidget *addPreviewTab(QWidget *widget, const QString &title, const QIcon &icon);
    void closeFocusedTab();

    void splitToRight(ScintillaNext *editor);
    void splitToBottom(ScintillaNext *editor);

public slots:
    void addEditor(ScintillaNext *editor);
    void addPreviewEditor(ScintillaNext *editor);

private slots:
    void dockWidgetCloseRequested();
    void editorRenamed(ScintillaNext *editor);

private:
    QPointer<ScintillaNext> m_previewEditor;
    void applyPreviewStyle(ads::CDockWidget *dockWidget, bool preview);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void editorAdded(ScintillaNext *editor);
    void editorCloseRequested(ScintillaNext *editor);
    void editorClosed(ScintillaNext *editor);
    void editorActivated(ScintillaNext *editor);
    void editorOrderChanged();
    void previewTabActivated(QWidget *widget);
    void previewEditorSet();

    // Emitted after the LAST tab of ANY kind (editor or nn_previewTab) is
    // removed from the dock manager, i.e. totalTabCount() just hit 0. The one
    // type-agnostic signal callers use to react to "the editor area is now
    // empty" — a new tab kind needs no new signal.
    void lastTabClosed();

    void contextMenuRequestedForEditor(ScintillaNext *editor);
    void titleBarDoubleClicked();
};

#endif // DOCKEDEDITOR_H
