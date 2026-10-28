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
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

#include "control/ControlIds.h"
#include "control/ControlPicker.h"
#include "control/ControlUiTree.h"

using namespace control;

namespace {

// A dialog with one of each control a NotepadAI dialog uses.
class Form : public QDialog
{
public:
    Form()
    {
        setObjectName(QStringLiteral("TestDialog"));
        setWindowTitle(QStringLiteral("Test"));
        auto *layout = new QVBoxLayout(this);
        auto *label = new QLabel(QStringLiteral("Project <b>name</b>"), this);
        name = new QLineEdit(this);
        name->setObjectName(QStringLiteral("editName"));
        name->setPlaceholderText(QStringLiteral("Name"));
        secret = new QLineEdit(this);
        secret->setObjectName(QStringLiteral("editSecret"));
        secret->setEchoMode(QLineEdit::Password);
        secret->setText(QStringLiteral("hunter2"));
        flag = new QCheckBox(QStringLiteral("&Flag"), this);
        flag->setObjectName(QStringLiteral("checkFlag"));
        kind = new QComboBox(this);
        kind->setObjectName(QStringLiteral("comboKind"));
        kind->addItems({QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")});
        count = new QSpinBox(this);
        count->setObjectName(QStringLiteral("spinCount"));
        count->setRange(0, 100);
        list = new QListWidget(this);
        list->setObjectName(QStringLiteral("listItems"));
        list->addItems({QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gamma")});
        tabs = new QTabWidget(this);
        tabs->setObjectName(QStringLiteral("tabs"));
        tabs->addTab(new QWidget, QStringLiteral("General"));
        tabs->addTab(new QWidget, QStringLiteral("Advanced"));
        ok = new QPushButton(QStringLiteral("OK"), this);
        ok->setObjectName(QStringLiteral("buttonOk"));
        cancel = new QPushButton(QStringLiteral("Cancel"), this); // unnamed on purpose
        for (QWidget *w : std::initializer_list<QWidget *>{label, name, secret, flag, kind, count, list, tabs, ok, cancel})
            layout->addWidget(w);
        connect(ok, &QPushButton::clicked, this, [this] { ++okClicks; });
        connect(kind, &QComboBox::activated, this, [this](int i) { activated = i; });
    }

    QLineEdit *name, *secret;
    QCheckBox *flag;
    QComboBox *kind;
    QSpinBox *count;
    QListWidget *list;
    QTabWidget *tabs;
    QPushButton *ok, *cancel;
    int okClicks = 0;
    int activated = -1;
};

QJsonObject findNode(const QJsonArray &nodes, const QString &id)
{
    for (const QJsonValue &v : nodes) {
        const QJsonObject n = v.toObject();
        if (n.value(QStringLiteral("id")).toString() == id)
            return n;
        const QJsonObject inChildren = findNode(n.value(QStringLiteral("children")).toArray(), id);
        if (!inChildren.isEmpty())
            return inChildren;
    }
    return {};
}

ui::Target target(const QString &id)
{
    const ui::Target t = ui::resolve(id);
    if (!t.widget)
        qWarning() << "unresolved" << id << t.error;
    return t;
}

} // namespace

class TestControlUiTree : public QObject
{
    Q_OBJECT

private slots:
    void idsComeFromNamesAndRoundTrip();
    void unnamedWidgetsGetPositionalIds();
    void buttonsTakeTheirIdFromWhatTheyStandFor();
    void markedRootsKeepTheirIdsWhenReparented();
    void treeListsControlsWithRolesAndValues();
    void passwordsAreNeverRead();
    void clickPressesTheButton();
    void typeAndReplaceText();
    void keysRunWindowShortcutsInTheBackground();
    void selectComboAndTab();
    void setCheckboxAndSpin();
    void listRowsArePartsYouCanClick();
    void modalDialogBlocksTheWindowBehind();
    void readContainerListsTextsAndButtons();
    void pickerReturnsTheArmedValueOnce();
};

void TestControlUiTree::idsComeFromNamesAndRoundTrip()
{
    Form f;
    f.show();
    const ids::Id id = ids::nodeId(f.name);
    QCOMPARE(id.id, QStringLiteral("TestDialog/editName"));
    QVERIFY(id.stable);
    QCOMPARE(ui::resolve(id.id).widget, f.name);
    QCOMPARE(ids::nodeId(&f).id, QStringLiteral("TestDialog"));
    QVERIFY(!ui::resolve(QStringLiteral("TestDialog/nope")).widget);
}

void TestControlUiTree::unnamedWidgetsGetPositionalIds()
{
    Form f;
    f.show();
    const ids::Id id = ids::nodeId(f.cancel);
    QCOMPARE(id.id, QStringLiteral("TestDialog/QPushButton#0"));
    QVERIFY(!id.stable);
    QCOMPARE(ui::resolve(id.id).widget, f.cancel);
}

void TestControlUiTree::buttonsTakeTheirIdFromWhatTheyStandFor()
{
    // A message box's standard buttons are named by their role.
    QMessageBox box(QMessageBox::Question, QStringLiteral("Save?"), QStringLiteral("Save changes?"),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setObjectName(QStringLiteral("ConfirmSave"));
    box.show();
    QAbstractButton *discard = box.button(QMessageBox::Discard);
    const ids::Id id = ids::nodeId(discard);
    QCOMPARE(id.id, QStringLiteral("ConfirmSave/button:discard"));
    QVERIFY(id.stable);
    QCOMPARE(ui::resolve(id.id).widget, discard);

    // A toolbar button is its action's button.
    QMainWindow window;
    window.setObjectName(QStringLiteral("MainWindow"));
    QToolBar *bar = window.addToolBar(QStringLiteral("Main"));
    bar->setObjectName(QStringLiteral("mainToolBar"));
    QAction *save = bar->addAction(QStringLiteral("Save"));
    save->setObjectName(QStringLiteral("actionSave"));
    window.show();
    QWidget *button = bar->widgetForAction(save);
    QCOMPARE(ids::nodeId(button).id, QStringLiteral("MainWindow/mainToolBar/button:actionSave"));
    QVERIFY(ids::nodeId(button).stable);
    QCOMPARE(ui::resolve(QStringLiteral("MainWindow/mainToolBar/button:actionSave")).widget, button);
}

void TestControlUiTree::markedRootsKeepTheirIdsWhenReparented()
{
    QMainWindow a;
    a.setObjectName(QStringLiteral("MainWindow"));
    QWidget holderA(&a);
    auto *dock = new QWidget(&holderA);
    ids::setRoot(dock, QStringLiteral("dock:files"));
    auto *button = new QPushButton(QStringLiteral("Go"), dock);
    button->setObjectName(QStringLiteral("buttonGo"));
    QCOMPARE(ids::nodeId(button).id, QStringLiteral("dock:files/buttonGo"));

    QDialog floating; // a floating container
    dock->setParent(&floating);
    QCOMPARE(ids::nodeId(button).id, QStringLiteral("dock:files/buttonGo"));
    QCOMPARE(ui::resolve(QStringLiteral("dock:files/buttonGo")).widget, button);
}

void TestControlUiTree::treeListsControlsWithRolesAndValues()
{
    Form f;
    f.show();
    f.name->setText(QStringLiteral("demo"));
    const QJsonObject tree = ui::tree(&f, {});
    const QJsonArray nodes = tree.value(QStringLiteral("nodes")).toArray();
    QCOMPARE(nodes.size(), 1);
    QCOMPARE(nodes.at(0).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("dialog"));

    const QJsonObject name = findNode(nodes, QStringLiteral("TestDialog/editName"));
    QCOMPARE(name.value(QStringLiteral("role")).toString(), QStringLiteral("input"));
    QCOMPARE(name.value(QStringLiteral("value")).toString(), QStringLiteral("demo"));
    QCOMPARE(name.value(QStringLiteral("label")).toString(), QStringLiteral("Name"));

    const QJsonObject flag = findNode(nodes, QStringLiteral("TestDialog/checkFlag"));
    QCOMPARE(flag.value(QStringLiteral("role")).toString(), QStringLiteral("checkbox"));
    QCOMPARE(flag.value(QStringLiteral("label")).toString(), QStringLiteral("Flag"));
    QCOMPARE(flag.value(QStringLiteral("checked")).toBool(), false);

    const QJsonObject list = findNode(nodes, QStringLiteral("TestDialog/listItems"));
    QCOMPARE(list.value(QStringLiteral("role")).toString(), QStringLiteral("list"));
    QCOMPARE(list.value(QStringLiteral("parts")).toArray().size(), 3);
    QCOMPARE(list.value(QStringLiteral("parts")).toArray().at(1).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("TestDialog/listItems@row:Beta"));

    const QJsonObject cancel = findNode(nodes, QStringLiteral("TestDialog/QPushButton#0"));
    QCOMPARE(cancel.value(QStringLiteral("stable")).toBool(true), false);

    // The rich-text label reads as plain text.
    bool sawLabel = false;
    std::function<void(const QJsonArray &)> scan = [&](const QJsonArray &a) {
        for (const QJsonValue &v : a) {
            const QJsonObject n = v.toObject();
            if (n.value(QStringLiteral("label")).toString() == QLatin1String("Project name"))
                sawLabel = true;
            scan(n.value(QStringLiteral("children")).toArray());
        }
    };
    scan(nodes);
    QVERIFY(sawLabel);
}

void TestControlUiTree::passwordsAreNeverRead()
{
    Form f;
    f.show();
    const QJsonObject node = findNode(ui::tree(&f, {}).value(QStringLiteral("nodes")).toArray(),
                                      QStringLiteral("TestDialog/editSecret"));
    QVERIFY(!node.contains(QStringLiteral("value")));
    QVERIFY(node.value(QStringLiteral("password")).toBool());
    const QJsonObject read = ui::read(f.secret, {}, {});
    QVERIFY(!read.contains(QStringLiteral("text")));
    QVERIFY(!QJsonDocument(read).toJson().contains("hunter2"));
}

void TestControlUiTree::clickPressesTheButton()
{
    Form f;
    f.show();
    QCOMPARE(ui::click(target(QStringLiteral("TestDialog/buttonOk")), Qt::LeftButton, false), QString());
    QCOMPARE(f.okClicks, 1);
}

void TestControlUiTree::typeAndReplaceText()
{
    Form f;
    f.show();
    QCOMPARE(ui::type(target(QStringLiteral("TestDialog/editName")), QStringLiteral("hello"), false), QString());
    QCOMPARE(f.name->text(), QStringLiteral("hello"));
    QCOMPARE(ui::type(target(QStringLiteral("TestDialog/editName")), QStringLiteral(" world"), false), QString());
    QCOMPARE(f.name->text(), QStringLiteral("hello world"));
    QCOMPARE(ui::type(target(QStringLiteral("TestDialog/editName")), QStringLiteral("bye"), true), QString());
    QCOMPARE(f.name->text(), QStringLiteral("bye"));

    f.name->setReadOnly(true);
    QVERIFY(!ui::type(target(QStringLiteral("TestDialog/editName")), QStringLiteral("x"), false).isEmpty());
}

void TestControlUiTree::keysRunWindowShortcutsInTheBackground()
{
    QMainWindow w;
    w.setObjectName(QStringLiteral("MainWindow"));
    auto *edit = new QLineEdit(&w);
    edit->setObjectName(QStringLiteral("editor"));
    w.setCentralWidget(edit);
    auto *action = new QAction(QStringLiteral("Do"), &w);
    action->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    w.addAction(action);
    QSignalSpy spy(action, &QAction::triggered);
    w.show();

    QCOMPARE(ui::keys(target(QStringLiteral("MainWindow/editor")), QStringLiteral("Ctrl+K")), QString());
    QCOMPARE(spy.size(), 1);

    QCOMPARE(ui::keys(target(QStringLiteral("MainWindow/editor")), QStringLiteral("A")), QString());
    QCOMPARE(edit->text(), QStringLiteral("a"));
    QVERIFY(!ui::keys(target(QStringLiteral("MainWindow/editor")), QStringLiteral("")).isEmpty());
}

void TestControlUiTree::selectComboAndTab()
{
    Form f;
    f.show();
    QCOMPARE(ui::select(target(QStringLiteral("TestDialog/comboKind")), {{QStringLiteral("text"), QStringLiteral("gamma")}}),
             QString());
    QCOMPARE(f.kind->currentIndex(), 2);
    QCOMPARE(f.activated, 2);
    QVERIFY(!ui::select(target(QStringLiteral("TestDialog/comboKind")), {{QStringLiteral("text"), QStringLiteral("zzz")}})
                 .isEmpty());

    QCOMPARE(ui::select(target(QStringLiteral("TestDialog/tabs")), {{QStringLiteral("text"), QStringLiteral("Advanced")}}),
             QString());
    QCOMPARE(f.tabs->currentIndex(), 1);
}

void TestControlUiTree::setCheckboxAndSpin()
{
    Form f;
    f.show();
    QSignalSpy toggled(f.flag, &QCheckBox::toggled);
    QCOMPARE(ui::set(target(QStringLiteral("TestDialog/checkFlag")), true), QString());
    QVERIFY(f.flag->isChecked());
    QCOMPARE(toggled.size(), 1);
    QCOMPARE(ui::set(target(QStringLiteral("TestDialog/checkFlag")), true), QString()); // already on
    QCOMPARE(toggled.size(), 1);

    QSignalSpy finished(f.count, &QSpinBox::editingFinished);
    QCOMPARE(ui::set(target(QStringLiteral("TestDialog/spinCount")), 42), QString());
    QCOMPARE(f.count->value(), 42);
    QCOMPARE(finished.size(), 1);
}

void TestControlUiTree::listRowsArePartsYouCanClick()
{
    Form f;
    f.show();
    QCOMPARE(ui::click(target(QStringLiteral("TestDialog/listItems@row:Beta")), Qt::LeftButton, false), QString());
    QCOMPARE(f.list->currentItem()->text(), QStringLiteral("Beta"));

    const QJsonObject rows = ui::read(f.list, {}, {});
    QCOMPARE(rows.value(QStringLiteral("rows")).toArray().size(), 3);
    QVERIFY(rows.value(QStringLiteral("rows")).toArray().at(1).toObject().value(QStringLiteral("current")).toBool());
    QVERIFY(!ui::resolve(QStringLiteral("TestDialog/editName@row:x")).widget); // no parts on a line edit
}

void TestControlUiTree::modalDialogBlocksTheWindowBehind()
{
    Form f;
    f.show();
    QMessageBox box(QMessageBox::Question, QStringLiteral("Q"), QStringLiteral("Sure?"),
                    QMessageBox::Yes | QMessageBox::No);
    box.setObjectName(QStringLiteral("ConfirmBox"));
    box.setWindowModality(Qt::ApplicationModal);
    box.show();
    QTRY_VERIFY(QApplication::activeModalWidget() == &box);
    const QString reason = ui::blockedReason(target(QStringLiteral("TestDialog/buttonOk")));
    QVERIFY2(reason.contains(QLatin1String("ConfirmBox")), qPrintable(reason));

    const QJsonObject read = ui::read(&box, {}, {});
    QCOMPARE(read.value(QStringLiteral("text")).toString(), QStringLiteral("Sure?"));
    QVERIFY(read.value(QStringLiteral("buttons")).toArray().size() >= 2);
    box.close();
}

void TestControlUiTree::readContainerListsTextsAndButtons()
{
    Form f;
    f.show();
    const QJsonObject read = ui::read(&f, {}, {});
    QVERIFY(read.value(QStringLiteral("texts")).toArray().contains(QStringLiteral("Project name")));
    bool sawOk = false;
    for (const QJsonValue &b : read.value(QStringLiteral("buttons")).toArray())
        sawOk = sawOk || b.toObject().value(QStringLiteral("id")).toString() == QLatin1String("TestDialog/buttonOk");
    QVERIFY(sawOk);
}

void TestControlUiTree::pickerReturnsTheArmedValueOnce()
{
    QTemporaryDir dir;
    picker::arm({dir.path()});
    QVERIFY(picker::isArmed());
    QCOMPARE(picker::getExistingDirectory(nullptr, QString(), QString()), QDir::cleanPath(dir.path()));
    QVERIFY(!picker::isArmed());
    QVERIFY(picker::disarm()); // it was used

    picker::arm({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    QString filter;
    const QStringList files = picker::getOpenFileNames(nullptr, {}, dir.path(), QStringLiteral("Text (*.txt);;All (*)"),
                                                       &filter);
    QCOMPARE(files, (QStringList{QDir(dir.path()).filePath(QStringLiteral("a.txt")),
                                 QDir(dir.path()).filePath(QStringLiteral("b.txt"))}));
    QCOMPARE(filter, QStringLiteral("Text (*.txt)"));
    QVERIFY(picker::disarm());

    picker::arm({QStringLiteral("unused")});
    QVERIFY(!picker::disarm()); // no picker took it

    picker::arm({QStringLiteral("#336699")});
    QCOMPARE(picker::getColor(), QColor(0x33, 0x66, 0x99));
    picker::disarm();
}

QTEST_MAIN(TestControlUiTree)
#include "test_control_ui_tree.moc"
