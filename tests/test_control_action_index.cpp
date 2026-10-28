/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtTest>
#include <QDialog>
#include <QJsonArray>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>

#include "control/ControlActionIndex.h"
#include "control/ControlEffects.h"
#include "control/ControlIds.h"

using control::ControlActionIndex;

namespace {

// A main window shaped like NotepadAI's: named static actions, a submenu the
// parent menu fills in aboutToShow, an unnamed action, a toolbar.
class FakeWindow : public QMainWindow
{
public:
    FakeWindow()
    {
        setObjectName(QStringLiteral("MainWindow"));
        QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
        file->setObjectName(QStringLiteral("menuFile"));
        open = file->addAction(QStringLiteral("&Open…"));
        open->setObjectName(QStringLiteral("actionOpen"));
        open->setShortcut(QKeySequence(QStringLiteral("Ctrl+O")));
        file->addSeparator();
        disabled = file->addAction(QStringLiteral("Disabled"));
        disabled->setObjectName(QStringLiteral("actionDisabled"));
        disabled->setEnabled(false);
        unnamed = file->addAction(QStringLiteral("&Reload From Disk…"));
        modal = file->addAction(QStringLiteral("Settings"));
        modal->setObjectName(QStringLiteral("actionSettings"));
        connect(modal, &QAction::triggered, this, [this] {
            QDialog dlg(this);
            dlg.setObjectName(QStringLiteral("SettingsDialog"));
            dlg.setWindowTitle(QStringLiteral("Settings"));
            dialogOpen = true;
            dlg.exec();
            dialogOpen = false;
        });

        QMenu *ai = menuBar()->addMenu(QStringLiteral("AI"));
        ai->setObjectName(QStringLiteral("menuAi"));
        agents = ai->addMenu(QStringLiteral("Agents"));
        agents->setObjectName(QStringLiteral("menuAgents"));
        connect(ai, &QMenu::aboutToShow, this, [this] {
            ++aiShown;
            agents->clear();
            agents->setEnabled(agentsEnabled);
            for (const QString &id : {QStringLiteral("a"), QStringLiteral("b")}) {
                QAction *act = agents->addAction(agentLabel + QStringLiteral(" ") + id);
                control::ids::setKey(act, QStringLiteral("agent:") + id);
                connect(act, &QAction::triggered, this, [this, id] { picked = id; });
            }
        });

        QToolBar *bar = addToolBar(QStringLiteral("Main"));
        bar->addAction(open); // shared with the menu: listed once
        toolOnly = bar->addAction(QStringLiteral("Tool"));
        toolOnly->setObjectName(QStringLiteral("actionToolOnly"));
    }

    QAction *open = nullptr;
    QAction *disabled = nullptr;
    QAction *unnamed = nullptr;
    QAction *modal = nullptr;
    QAction *toolOnly = nullptr;
    QMenu *agents = nullptr;
    QString agentLabel = QStringLiteral("Agent");
    QString picked;
    int aiShown = 0;
    bool agentsEnabled = true;
    bool dialogOpen = false;
};

const ControlActionIndex::Entry *find(const QList<ControlActionIndex::Entry> &list, const QString &id)
{
    for (const auto &e : list) {
        if (e.id == id)
            return &e;
    }
    return nullptr;
}

} // namespace

class TestControlActionIndex : public QObject
{
    Q_OBJECT

private slots:
    void listsMenusToolbarsAndDynamicItems();
    void idsDoNotDependOnText();
    void unnamedActionsFallBackToTextAndAreUnstable();
    void resolveFillsTheMenuFirst();
    void disabledMenusDisableTheirItems();
    void modalDialogSettlesTheCommandWhileOpen();
    void plainActionSettlesAfterItRan();
    void openPopupIsListed();
};

void TestControlActionIndex::listsMenusToolbarsAndDynamicItems()
{
    FakeWindow w;
    const ControlActionIndex index(&w);
    const auto list = index.list();

    const auto *open = find(list, QStringLiteral("actionOpen"));
    QVERIFY(open);
    QCOMPARE(open->label, QStringLiteral("Open…"));
    QCOMPARE(open->menuPath, QStringList{QStringLiteral("File")});
    QCOMPARE(open->shortcut, QStringLiteral("Ctrl+O"));
    QCOMPARE(open->location, ControlActionIndex::Location::MenuBar);

    int openCount = 0;
    for (const auto &e : list)
        openCount += e.id == QLatin1String("actionOpen") ? 1 : 0;
    QCOMPARE(openCount, 1);

    const auto *agentB = find(list, QStringLiteral("menuAgents/agent:b"));
    QVERIFY(agentB);
    QVERIFY(agentB->stable);
    QCOMPARE(agentB->menuPath, (QStringList{QStringLiteral("AI"), QStringLiteral("Agents")}));
    QCOMPARE(w.aiShown, 1);

    const auto *tool = find(list, QStringLiteral("actionToolOnly"));
    QVERIFY(tool);
    QCOMPARE(tool->location, ControlActionIndex::Location::ToolBar);

    QVERIFY(find(list, QStringLiteral("menuAi")));
    QVERIFY(find(list, QStringLiteral("menuAgents")));

    const QJsonObject json = ControlActionIndex::toJson(*agentB);
    QCOMPARE(json.value(QStringLiteral("id")).toString(), QStringLiteral("menuAgents/agent:b"));
    QCOMPARE(json.value(QStringLiteral("menu")).toArray().size(), 2);
}

void TestControlActionIndex::idsDoNotDependOnText()
{
    FakeWindow w;
    const ControlActionIndex index(&w);
    w.agentLabel = QStringLiteral("Tác tử"); // a translated label
    const auto list = index.list();
    QVERIFY(find(list, QStringLiteral("menuAgents/agent:a")));
    QCOMPARE(find(list, QStringLiteral("menuAgents/agent:a"))->label, QStringLiteral("Tác tử a"));
}

void TestControlActionIndex::unnamedActionsFallBackToTextAndAreUnstable()
{
    FakeWindow w;
    const ControlActionIndex index(&w);
    const auto list = index.list();
    const auto *e = find(list, QStringLiteral("menuFile/reload-from-disk"));
    QVERIFY(e);
    QVERIFY(!e->stable);
    QCOMPARE(index.resolve(QStringLiteral("menuFile/reload-from-disk")).action.data(), w.unnamed);
}

void TestControlActionIndex::resolveFillsTheMenuFirst()
{
    FakeWindow w;
    const ControlActionIndex index(&w);
    QCOMPARE(w.agents->actions().size(), 0); // never opened yet
    const auto r = index.resolve(QStringLiteral("menuAgents/agent:b"));
    QVERIFY2(r.action, qPrintable(r.error));
    QVERIFY(r.enabled);
    QCOMPARE(r.menuPath, (QStringList{QStringLiteral("AI"), QStringLiteral("Agents")}));
    r.action->trigger();
    QCOMPARE(w.picked, QStringLiteral("b"));

    QVERIFY(!index.resolve(QStringLiteral("menuAgents/agent:zzz")).action);
    QVERIFY(!index.resolve(QStringLiteral("menuAgents/agent:zzz")).error.isEmpty());
}

void TestControlActionIndex::disabledMenusDisableTheirItems()
{
    FakeWindow w;
    const ControlActionIndex index(&w);
    QVERIFY(!index.resolve(QStringLiteral("actionDisabled")).enabled);
    QVERIFY(index.resolve(QStringLiteral("actionOpen")).enabled);

    w.agentsEnabled = false;
    const auto r = index.resolve(QStringLiteral("menuAgents/agent:a"));
    QVERIFY(r.action);
    QVERIFY(!r.enabled);
}

void TestControlActionIndex::modalDialogSettlesTheCommandWhileOpen()
{
    FakeWindow w;
    w.show();
    const ControlActionIndex index(&w);
    const auto r = index.resolve(QStringLiteral("actionSettings"));
    QVERIFY(r.action);

    const control::ControlSnapshot before = control::ControlSnapshot::take();
    bool called = false;
    bool finished = true;
    QJsonObject effects;
    control::runUntilSettled(&w, [a = r.action] { a->trigger(); }, [&](bool f) {
        called = true;
        finished = f;
        effects = before.diff(control::ControlSnapshot::take());
        // Still inside the dialog's exec(): close it so the test can go on.
        QVERIFY(w.dialogOpen);
        if (QWidget *modal = QApplication::activeModalWidget())
            modal->close();
    });
    QTRY_VERIFY(called);
    QVERIFY(!finished);
    const QJsonArray opened = effects.value(QStringLiteral("opened")).toArray();
    QCOMPARE(opened.size(), 1);
    QCOMPARE(opened.at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("SettingsDialog"));
    QCOMPARE(opened.at(0).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("dialog"));
    QCOMPARE(effects.value(QStringLiteral("modal")).toString(), QStringLiteral("SettingsDialog"));
    QTRY_VERIFY(!w.dialogOpen);
}

void TestControlActionIndex::plainActionSettlesAfterItRan()
{
    FakeWindow w;
    const ControlActionIndex index(&w);
    const auto r = index.resolve(QStringLiteral("menuAgents/agent:a"));
    bool called = false;
    bool finished = false;
    control::runUntilSettled(&w, [a = r.action] { a->trigger(); }, [&](bool f) {
        called = true;
        finished = f;
    });
    QVERIFY(!called); // never synchronous
    QTRY_VERIFY(called);
    QVERIFY(finished);
    QCOMPARE(w.picked, QStringLiteral("a"));
}

void TestControlActionIndex::openPopupIsListed()
{
    FakeWindow w;
    w.show();
    QMenu popup(&w);
    QAction *copy = popup.addAction(QStringLiteral("Copy &Path"));
    popup.popup(w.mapToGlobal(QPoint(10, 10)));
    QTRY_VERIFY(popup.isVisible());

    const ControlActionIndex index(&w);
    const auto list = index.list(QStringLiteral("popup"));
    const auto *e = find(list, QStringLiteral("popup/copy-path"));
    QVERIFY(e);
    QCOMPARE(e->location, ControlActionIndex::Location::Popup);
    const auto r = index.resolve(QStringLiteral("popup/copy-path"));
    QCOMPARE(r.action.data(), copy);
    QVERIFY(r.inPopup);
    popup.close();
}

QTEST_MAIN(TestControlActionIndex)
#include "test_control_action_index.moc"
