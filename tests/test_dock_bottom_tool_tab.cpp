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

#include <QSplitterHandle>
#include <QWidget>

#include "DockManager.h"
#include "DockAreaWidget.h"
#include "DockWidget.h"

#include "DockBottomToolTab.h"
#include "DockSplitter.h"

namespace {

void enableEqualSplit()
{
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);
}

// Shown 800x400 manager with one editor over one bottom tool. Caller owns `manager`.
ads::CDockAreaWidget *addShownEditorAndTool(ads::CDockManager *manager, QWidget *toolWidget = nullptr)
{
    enableEqualSplit();
    manager->resize(800, 400);
    manager->show();
    if (!QTest::qWaitForWindowExposed(manager))
        return nullptr;

    auto *editorDw = manager->createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    ads::CDockAreaWidget *editorArea =
        manager->addDockWidget(ads::CenterDockWidgetArea, editorDw);

    auto *toolDw = manager->createDockWidget(QStringLiteral("terminal"));
    toolDw->setWidget(toolWidget ? toolWidget : new QWidget);
    return addDockWidgetAsBottomTool(manager, toolDw, editorArea);
}

void dragToolToRatio(ads::CDockManager *manager, ads::CDockAreaWidget *toolArea, int toolShare, int parts)
{
    QList<int> sizes = manager->splitterSizes(toolArea);
    const int total = sizes.at(0) + sizes.at(1);
    manager->setSplitterSizes(toolArea, {total * (parts - toolShare) / parts, total * toolShare / parts});
}

void assertToolQuarter(ads::CDockManager *manager, ads::CDockAreaWidget *toolArea)
{
    const QList<int> sizes = manager->splitterSizes(toolArea);
    QCOMPARE(sizes.size(), 2);
    const int total = sizes.at(0) + sizes.at(1);
    QVERIFY(total > 0);
    QVERIFY2(qAbs(sizes.at(1) - total / 4) <= 8,
             qPrintable(QStringLiteral("tool pane %1 of %2, expected ~25%%")
                            .arg(sizes.at(1))
                            .arg(total)));
}

} // namespace

// Guards the VS Code-style split: a bottom tool tab (terminal) must land in its
// own dock area below the editor, never tabify into the editor area. Tabifying
// is what addDockWidget(Center, ..., editorArea) does — the exact miss that
// would leave tree/ACP full-height but put the terminal in the editor tab strip.
class TestDockBottomToolTab : public QObject
{
    Q_OBJECT

private slots:
    void firstTool_withNoExistingToolArea_splitsIntoOwnAreaBelowEditor();
    void firstTool_occupiesQuarterOfVerticalSplitter();
    void userAdjustedSplitter_isNotResetToQuarterOnResize();
    void doubleClickSplitterHandle_resetsToolToQuarter();
    void doubleClickSplitterHandle_fromTenPercent_resetsToQuarter();
    void doubleClickSplitterHandle_fromSeventyPercent_resetsToQuarter();
    void doubleClickSplitterHandle_alreadyQuarter_isIdempotent();
    void doubleClickSplitterHandle_nearCollapsed_resetsToQuarter();
    void doubleClickSplitterHandle_zeroHeight_doesNotCrash();
    void doubleClickHorizontalEditorSplit_doesNotReset();
    void pinQuarter_doesNotHonorInnerWidgetMinimumHeight();
    void editorRightSplit_staysEqual();
    void secondTool_withExistingToolArea_tabifiesInsteadOfStacking();
    void closingEditor_leavesToolTabInPlace();
    void newEditorAfterClosingAll_doesNotTabifyWithTool();
    void closingLastEditor_withToolRemaining_nonToolCountHitsZero();
    void respawnedEditor_splitsAboveRemainingTool();
    void twoEditorPanes_closingOne_doesNotSpawn();
    void twoToolAreas_respawnDoesNotSwallowTools();
};

void TestDockBottomToolTab::firstTool_withNoExistingToolArea_splitsIntoOwnAreaBelowEditor()
{
    ads::CDockManager manager;

    auto *editorDw = manager.createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    ads::CDockAreaWidget *editorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editorDw);
    QVERIFY(editorArea != nullptr);

    auto *toolDw = manager.createDockWidget(QStringLiteral("terminal"));
    toolDw->setWidget(new QWidget);
    ads::CDockAreaWidget *toolArea =
        addDockWidgetAsBottomTool(&manager, toolDw, editorArea);

    QVERIFY(toolArea != nullptr);
    QVERIFY2(toolArea != editorArea,
             "tool tab tabified with the editor instead of splitting below it");
}

void TestDockBottomToolTab::firstTool_occupiesQuarterOfVerticalSplitter()
{
    // Match DockedEditor: EqualSplitOnInsertion is what currently yields the
    // 50/50 terminal. The helper must override that to 75/25 without changing
    // the global flag (editor split-to-right/bottom still wants equal split).
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);

    ads::CDockManager manager;
    manager.resize(800, 400);
    manager.show();
    QVERIFY(QTest::qWaitForWindowExposed(&manager));

    auto *editorDw = manager.createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    ads::CDockAreaWidget *editorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editorDw);

    auto *toolDw = manager.createDockWidget(QStringLiteral("terminal"));
    toolDw->setWidget(new QWidget);
    ads::CDockAreaWidget *toolArea =
        addDockWidgetAsBottomTool(&manager, toolDw, editorArea);

    const QList<int> sizes = manager.splitterSizes(toolArea);
    QCOMPARE(sizes.size(), 2);
    const int total = sizes.at(0) + sizes.at(1);
    QVERIFY(total > 0);
    // Bottom appends, so the tool is the last pane. 25% ± splitter-handle slack.
    QVERIFY2(qAbs(sizes.at(1) - total / 4) <= 8,
             qPrintable(QStringLiteral("tool pane %1 of %2, expected ~25%%")
                            .arg(sizes.at(1))
                            .arg(total)));
}

void TestDockBottomToolTab::userAdjustedSplitter_isNotResetToQuarterOnResize()
{
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);

    ads::CDockManager manager;
    manager.resize(800, 400);
    manager.show();
    QVERIFY(QTest::qWaitForWindowExposed(&manager));

    auto *editorDw = manager.createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    ads::CDockAreaWidget *editorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editorDw);

    auto *toolDw = manager.createDockWidget(QStringLiteral("terminal"));
    toolDw->setWidget(new QWidget);
    ads::CDockAreaWidget *toolArea =
        addDockWidgetAsBottomTool(&manager, toolDw, editorArea);

    QList<int> sizes = manager.splitterSizes(toolArea);
    QCOMPARE(sizes.size(), 2);
    const int total0 = sizes.at(0) + sizes.at(1);
    manager.setSplitterSizes(toolArea, {total0 / 2, total0 / 2});

    manager.resize(800, 800);
    QVERIFY(QTest::qWaitForWindowExposed(&manager));

    sizes = manager.splitterSizes(toolArea);
    const int total = sizes.at(0) + sizes.at(1);
    QVERIFY(total > 0);
    QVERIFY2(qAbs(sizes.at(1) - total / 4) > 30,
             qPrintable(QStringLiteral("snapped back to 25%%: tool %1 of %2")
                            .arg(sizes.at(1))
                            .arg(total)));
}

void TestDockBottomToolTab::doubleClickSplitterHandle_resetsToolToQuarter()
{
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);

    ads::CDockManager manager;
    manager.resize(800, 400);
    manager.show();
    QVERIFY(QTest::qWaitForWindowExposed(&manager));

    auto *editorDw = manager.createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    ads::CDockAreaWidget *editorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editorDw);

    auto *toolDw = manager.createDockWidget(QStringLiteral("terminal"));
    toolDw->setWidget(new QWidget);
    ads::CDockAreaWidget *toolArea =
        addDockWidgetAsBottomTool(&manager, toolDw, editorArea);

    QList<int> sizes = manager.splitterSizes(toolArea);
    QCOMPARE(sizes.size(), 2);
    const int total0 = sizes.at(0) + sizes.at(1);
    manager.setSplitterSizes(toolArea, {total0 / 2, total0 / 2});
    sizes = manager.splitterSizes(toolArea);
    QVERIFY(qAbs(sizes.at(1) - total0 / 2) <= 8);

    ads::CDockSplitter *splitter = toolArea->parentSplitter();
    QVERIFY(splitter);
    QSplitterHandle *handle = splitter->handle(1);
    QVERIFY(handle);
    QTest::mouseDClick(handle, Qt::LeftButton);

    sizes = manager.splitterSizes(toolArea);
    const int total = sizes.at(0) + sizes.at(1);
    QVERIFY(total > 0);
    QVERIFY2(qAbs(sizes.at(1) - total / 4) <= 8,
             qPrintable(QStringLiteral("dblclick left tool at %1 of %2, expected ~25%%")
                            .arg(sizes.at(1))
                            .arg(total)));
}

void TestDockBottomToolTab::doubleClickSplitterHandle_fromTenPercent_resetsToQuarter()
{
    ads::CDockManager manager;
    ads::CDockAreaWidget *toolArea = addShownEditorAndTool(&manager);
    QVERIFY(toolArea);
    dragToolToRatio(&manager, toolArea, 1, 10);
    QSplitterHandle *handle = toolArea->parentSplitter()->handle(1);
    QVERIFY(handle);
    QTest::mouseDClick(handle, Qt::LeftButton);
    assertToolQuarter(&manager, toolArea);
}

void TestDockBottomToolTab::doubleClickSplitterHandle_fromSeventyPercent_resetsToQuarter()
{
    ads::CDockManager manager;
    ads::CDockAreaWidget *toolArea = addShownEditorAndTool(&manager);
    QVERIFY(toolArea);
    dragToolToRatio(&manager, toolArea, 7, 10);
    QSplitterHandle *handle = toolArea->parentSplitter()->handle(1);
    QVERIFY(handle);
    QTest::mouseDClick(handle, Qt::LeftButton);
    assertToolQuarter(&manager, toolArea);
}

void TestDockBottomToolTab::doubleClickSplitterHandle_alreadyQuarter_isIdempotent()
{
    ads::CDockManager manager;
    ads::CDockAreaWidget *toolArea = addShownEditorAndTool(&manager);
    QVERIFY(toolArea);
    assertToolQuarter(&manager, toolArea);
    QSplitterHandle *handle = toolArea->parentSplitter()->handle(1);
    QVERIFY(handle);
    QTest::mouseDClick(handle, Qt::LeftButton);
    assertToolQuarter(&manager, toolArea);
}

void TestDockBottomToolTab::doubleClickSplitterHandle_nearCollapsed_resetsToQuarter()
{
    ads::CDockManager manager;
    ads::CDockAreaWidget *toolArea = addShownEditorAndTool(&manager);
    QVERIFY(toolArea);
    QList<int> sizes = manager.splitterSizes(toolArea);
    const int total0 = sizes.at(0) + sizes.at(1);
    manager.setSplitterSizes(toolArea, {total0 - 1, 1});
    QSplitterHandle *handle = toolArea->parentSplitter()->handle(1);
    QVERIFY(handle);
    QTest::mouseDClick(handle, Qt::LeftButton);
    assertToolQuarter(&manager, toolArea);
}

void TestDockBottomToolTab::doubleClickSplitterHandle_zeroHeight_doesNotCrash()
{
    ads::CDockManager manager;
    ads::CDockAreaWidget *toolArea = addShownEditorAndTool(&manager);
    QVERIFY(toolArea);
    ads::CDockSplitter *splitter = toolArea->parentSplitter();
    QVERIFY(splitter);
    splitter->resize(splitter->width(), 0);
    QCOMPARE(splitter->height(), 0);
    QSplitterHandle *handle = splitter->handle(1);
    QVERIFY(handle);
    QTest::mouseDClick(handle, Qt::LeftButton);
    QVERIFY(toolArea->dockWidgetsCount() >= 1);
}

void TestDockBottomToolTab::doubleClickHorizontalEditorSplit_doesNotReset()
{
    enableEqualSplit();
    ads::CDockManager manager;
    manager.resize(800, 400);
    manager.show();
    QVERIFY(QTest::qWaitForWindowExposed(&manager));

    auto *leftDw = manager.createDockWidget(QStringLiteral("ed-left"));
    leftDw->setWidget(new QWidget);
    ads::CDockAreaWidget *leftArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, leftDw);

    auto *rightDw = manager.createDockWidget(QStringLiteral("ed-right"));
    rightDw->setWidget(new QWidget);
    ads::CDockAreaWidget *rightArea =
        manager.addDockWidget(ads::RightDockWidgetArea, rightDw, leftArea);

    ads::CDockSplitter *splitter = rightArea->parentSplitter();
    QVERIFY(splitter);
    QCOMPARE(splitter->orientation(), Qt::Horizontal);
    const QList<int> before = manager.splitterSizes(rightArea);
    QSplitterHandle *handle = splitter->handle(1);
    QVERIFY(handle);
    QTest::mouseDClick(handle, Qt::LeftButton);
    QCOMPARE(manager.splitterSizes(rightArea), before);
}

void TestDockBottomToolTab::pinQuarter_doesNotHonorInnerWidgetMinimumHeight()
{
    // Splitter children are CDockAreaWidgets; an inner widget's minimumHeight
    // does not clamp setSizes. 25% is applied even when it is below that min.
    auto *fat = new QWidget;
    fat->setMinimumHeight(300);
    ads::CDockManager manager;
    ads::CDockAreaWidget *toolArea = addShownEditorAndTool(&manager, fat);
    QVERIFY(toolArea);
    QList<int> sizes = manager.splitterSizes(toolArea);
    const int total0 = sizes.at(0) + sizes.at(1);
    manager.setSplitterSizes(toolArea, {total0 / 2, total0 / 2});
    QSplitterHandle *handle = toolArea->parentSplitter()->handle(1);
    QVERIFY(handle);
    QTest::mouseDClick(handle, Qt::LeftButton);
    assertToolQuarter(&manager, toolArea);
}

void TestDockBottomToolTab::editorRightSplit_staysEqual()
{
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);

    ads::CDockManager manager;
    manager.resize(800, 400);
    manager.show();
    QVERIFY(QTest::qWaitForWindowExposed(&manager));

    auto *leftDw = manager.createDockWidget(QStringLiteral("ed-left"));
    leftDw->setWidget(new QWidget);
    ads::CDockAreaWidget *leftArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, leftDw);

    auto *rightDw = manager.createDockWidget(QStringLiteral("ed-right"));
    rightDw->setWidget(new QWidget);
    ads::CDockAreaWidget *rightArea =
        manager.addDockWidget(ads::RightDockWidgetArea, rightDw, leftArea);

    const QList<int> sizes = manager.splitterSizes(rightArea);
    QCOMPARE(sizes.size(), 2);
    QVERIFY2(qAbs(sizes.at(0) - sizes.at(1)) <= 8,
             qPrintable(QStringLiteral("editor split %1/%2, expected equal")
                            .arg(sizes.at(0))
                            .arg(sizes.at(1))));
}

void TestDockBottomToolTab::secondTool_withExistingToolArea_tabifiesInsteadOfStacking()
{
    ads::CDockManager manager;

    auto *editorDw = manager.createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    ads::CDockAreaWidget *editorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editorDw);

    auto *firstDw = manager.createDockWidget(QStringLiteral("term-1"));
    firstDw->setWidget(new QWidget);
    ads::CDockAreaWidget *toolArea =
        addDockWidgetAsBottomTool(&manager, firstDw, editorArea);

    auto *secondDw = manager.createDockWidget(QStringLiteral("term-2"));
    secondDw->setWidget(new QWidget);
    ads::CDockAreaWidget *again =
        addDockWidgetAsBottomTool(&manager, secondDw, editorArea, toolArea);

    QCOMPARE(again, toolArea);
    QCOMPARE(toolArea->dockWidgetsCount(), 2);
}

void TestDockBottomToolTab::closingEditor_leavesToolTabInPlace()
{
    ads::CDockManager manager;

    auto *editorDw = manager.createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    editorDw->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    ads::CDockAreaWidget *editorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editorDw);

    auto *toolDw = manager.createDockWidget(QStringLiteral("terminal"));
    toolDw->setWidget(new QWidget);
    addDockWidgetAsBottomTool(&manager, toolDw, editorArea);

    editorDw->closeDockWidget();

    QVERIFY2(toolDw->dockAreaWidget() != nullptr,
             "closing the last editor removed the terminal with it");
    QCOMPARE(toolDw->dockAreaWidget()->dockWidgetsCount(), 1);
}

void TestDockBottomToolTab::newEditorAfterClosingAll_doesNotTabifyWithTool()
{
    ads::CDockManager manager;

    auto *editorDw = manager.createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    editorDw->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    ads::CDockAreaWidget *editorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editorDw);

    auto *toolDw = manager.createDockWidget(QStringLiteral("terminal"));
    toolDw->setWidget(new QWidget);
    ads::CDockAreaWidget *toolArea =
        addDockWidgetAsBottomTool(&manager, toolDw, editorArea);

    editorDw->closeDockWidget();
    QVERIFY(toolDw->dockAreaWidget() == toolArea);

    auto *editor2 = manager.createDockWidget(QStringLiteral("editor-2"));
    editor2->setWidget(new QWidget);
    ads::CDockAreaWidget *newEditorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editor2, nullptr);

    QVERIFY2(newEditorArea != toolArea,
             "new editor tabified with the terminal after the last editor closed");
}

void TestDockBottomToolTab::closingLastEditor_withToolRemaining_nonToolCountHitsZero()
{
    ads::CDockManager manager;

    auto *editorDw = manager.createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    editorDw->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    ads::CDockAreaWidget *editorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editorDw);

    auto *toolDw = manager.createDockWidget(QStringLiteral("terminal"));
    toolDw->setWidget(new QWidget);
    toolDw->setProperty("nn_toolTab", true);
    addDockWidgetAsBottomTool(&manager, toolDw, editorArea);

    QCOMPARE(nonToolTabCount(&manager), 1);
    editorDw->closeDockWidget();
    QCOMPARE(nonToolTabCount(&manager), 0);
}

void TestDockBottomToolTab::respawnedEditor_splitsAboveRemainingTool()
{
    ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);

    ads::CDockManager manager;
    manager.resize(800, 400);
    manager.show();
    QVERIFY(QTest::qWaitForWindowExposed(&manager));

    auto *editorDw = manager.createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    editorDw->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    ads::CDockAreaWidget *editorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editorDw);

    auto *toolDw = manager.createDockWidget(QStringLiteral("terminal"));
    toolDw->setWidget(new QWidget);
    toolDw->setProperty("nn_toolTab", true);
    ads::CDockAreaWidget *toolArea =
        addDockWidgetAsBottomTool(&manager, toolDw, editorArea);

    editorDw->closeDockWidget();

    auto *editor2 = manager.createDockWidget(QStringLiteral("New 1"));
    editor2->setWidget(new QWidget);
    ads::CDockAreaWidget *newEditorArea =
        addDockWidgetAsContent(&manager, editor2, nullptr, toolArea);

    QVERIFY(newEditorArea != nullptr);
    QVERIFY2(newEditorArea != toolArea,
             "respawned editor tabified with the terminal");
    ads::CDockSplitter *splitter = newEditorArea->parentSplitter();
    QVERIFY(splitter);
    QCOMPARE(splitter->orientation(), Qt::Vertical);
    QCOMPARE(splitter->indexOf(newEditorArea), 0);
}

void TestDockBottomToolTab::twoEditorPanes_closingOne_doesNotSpawn()
{
    ads::CDockManager manager;

    auto *ed1 = manager.createDockWidget(QStringLiteral("ed1"));
    ed1->setWidget(new QWidget);
    ed1->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    ads::CDockAreaWidget *area1 =
        manager.addDockWidget(ads::CenterDockWidgetArea, ed1);

    auto *ed2 = manager.createDockWidget(QStringLiteral("ed2"));
    ed2->setWidget(new QWidget);
    ed2->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    manager.addDockWidget(ads::RightDockWidgetArea, ed2, area1);

    auto *toolDw = manager.createDockWidget(QStringLiteral("terminal"));
    toolDw->setWidget(new QWidget);
    toolDw->setProperty("nn_toolTab", true);
    addDockWidgetAsBottomTool(&manager, toolDw, area1);

    QCOMPARE(nonToolTabCount(&manager), 2);
    ed1->closeDockWidget();
    QCOMPARE(nonToolTabCount(&manager), 1);
    QVERIFY(toolDw->dockAreaWidget() != nullptr);

    ed2->closeDockWidget();
    QCOMPARE(nonToolTabCount(&manager), 0);
    QVERIFY(toolDw->dockAreaWidget() != nullptr);
}

void TestDockBottomToolTab::twoToolAreas_respawnDoesNotSwallowTools()
{
    ads::CDockManager manager;

    auto *editorDw = manager.createDockWidget(QStringLiteral("editor"));
    editorDw->setWidget(new QWidget);
    editorDw->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    ads::CDockAreaWidget *editorArea =
        manager.addDockWidget(ads::CenterDockWidgetArea, editorDw);

    auto *tool1 = manager.createDockWidget(QStringLiteral("term-1"));
    tool1->setWidget(new QWidget);
    tool1->setProperty("nn_toolTab", true);
    ads::CDockAreaWidget *toolArea =
        addDockWidgetAsBottomTool(&manager, tool1, editorArea);

    auto *tool2 = manager.createDockWidget(QStringLiteral("term-2"));
    tool2->setWidget(new QWidget);
    tool2->setProperty("nn_toolTab", true);
    ads::CDockAreaWidget *toolArea2 =
        manager.addDockWidget(ads::RightDockWidgetArea, tool2, toolArea);

    editorDw->closeDockWidget();
    QCOMPARE(nonToolTabCount(&manager), 0);

    auto *editor2 = manager.createDockWidget(QStringLiteral("New 1"));
    editor2->setWidget(new QWidget);
    ads::CDockAreaWidget *newEditorArea =
        addDockWidgetAsContent(&manager, editor2, nullptr, findToolDockArea(&manager));

    QVERIFY(tool1->dockAreaWidget() != nullptr);
    QVERIFY(tool2->dockAreaWidget() != nullptr);
    QVERIFY(newEditorArea != tool1->dockAreaWidget());
    QVERIFY(newEditorArea != toolArea2);
}

QTEST_MAIN(TestDockBottomToolTab)
#include "test_dock_bottom_tool_tab.moc"
