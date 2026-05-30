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

#include <QtTest>

#include "DockManager.h"
#include "DockAreaWidget.h"

#include "DockAreaSanitizer.h"

// Guards the decision in DockedEditor::currentDockArea(): a dock area detached
// from its container (parent already nullptr, deleteLater pending) must collapse
// to nullptr so it is never fed as the 3rd arg of CDockManager::addDockWidget(),
// where a null container would be dereferenced. See crash_report.txt and
// DockAreaSanitizer.h for the full chain.
//
// CDockAreaWidget::dockContainer() is internal::findParent<CDockContainerWidget*>
// walking parentWidget(), so the detached state is reproduced exactly by a
// parentless area, and the attached state by parenting one under a CDockManager
// (which IS-A CDockContainerWidget). No private ADS state or real tear-down
// timing is needed — the predicate is pure.
class TestDockedEditorCurrentArea : public QObject
{
    Q_OBJECT

private slots:
    void nullInput_returnsNull();
    void attachedArea_passesThrough();
    void detachedArea_collapsesToNull();
    void detachTransition_flipsToNull();
};

void TestDockedEditorCurrentArea::nullInput_returnsNull()
{
    QVERIFY(sanitizeDockArea(nullptr) == nullptr);
}

void TestDockedEditorCurrentArea::attachedArea_passesThrough()
{
    ads::CDockManager manager; // a CDockContainerWidget

    // Parent is the container, so dockContainer() resolves to &manager.
    // Manager arg is null only to skip the dockAreaCreated emit — irrelevant
    // to the predicate, which keys solely on the parent chain.
    auto *area = new ads::CDockAreaWidget(nullptr, &manager);

    QVERIFY(area->dockContainer() == &manager);
    QCOMPARE(sanitizeDockArea(area), area);

    delete area;
}

void TestDockedEditorCurrentArea::detachedArea_collapsesToNull()
{
    // No parent -> dockContainer() == nullptr: the exact state an area is in
    // between removeDockArea()'s setParent(nullptr) and its deleteLater().
    auto *area = new ads::CDockAreaWidget(nullptr, nullptr);

    QVERIFY(area->dockContainer() == nullptr);
    QVERIFY(sanitizeDockArea(area) == nullptr);

    delete area;
}

void TestDockedEditorCurrentArea::detachTransition_flipsToNull()
{
    ads::CDockManager manager;
    auto *area = new ads::CDockAreaWidget(nullptr, &manager);

    // Attached: the predicate hands the area straight back.
    QVERIFY(area->dockContainer() == &manager);
    QCOMPARE(sanitizeDockArea(area), area);

    // Mimic removeDockArea()'s detach. The same pointer is now orphaned, so the
    // predicate must flip to nullptr instead of letting the stale area through.
    area->setParent(nullptr);

    QVERIFY(area->dockContainer() == nullptr);
    QVERIFY(sanitizeDockArea(area) == nullptr);

    delete area; // top-level and unowned now; manager won't reap it
}

QTEST_MAIN(TestDockedEditorCurrentArea)
#include "test_docked_editor_current_area.moc"
