/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlAudit.h"

#include "ControlActionIndex.h"
#include "ControlIds.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QComboBox>
#include <QHash>
#include <QHeaderView>
#include <QLineEdit>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTabBar>
#include <QTextEdit>
#include <QTextStream>

#include <cstdio>

namespace control {

namespace {

// Inside another control (combo, spin box, item view, line edit), the
// docking chrome (driven through the dock adapter, not by id) or the API's
// own chrome.
bool isPartOfAnotherControl(const QWidget *w)
{
    if (qstrncmp(w->metaObject()->className(), "ads::", 5) == 0)
        return true;
    // QMainWindow's own bar over tabbed panels: driven through the panels.
    if (qobject_cast<const QTabBar *>(w) && qobject_cast<const QMainWindow *>(w->parentWidget()))
        return true;
    for (const QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
        if (ids::isIgnored(p))
            return true;
        if (qobject_cast<const QComboBox *>(p) || qobject_cast<const QAbstractSpinBox *>(p)
            || qobject_cast<const QAbstractItemView *>(p) || qobject_cast<const QLineEdit *>(p))
            return true;
        if (p->inherits("ads::CDockAreaTitleBar"))
            return true;
        // A combo's popup list is a window of its own, owned by the combo.
        if (p->inherits("QComboBoxPrivateContainer"))
            return true;
        if (p->isWindow())
            break;
    }
    return false;
}

QString describe(const QWidget *w)
{
    QString text;
    if (const auto *b = qobject_cast<const QAbstractButton *>(w))
        text = b->text();
    if (text.isEmpty())
        text = w->toolTip();
    if (text.isEmpty())
        text = w->accessibleName();
    return QStringLiteral("%1 \"%2\"").arg(QString::fromLatin1(w->metaObject()->className()), text.left(60));
}

} // namespace

bool isInteractiveWidget(const QWidget *w)
{
    if (ids::isIgnored(w) || w->objectName().startsWith(QLatin1String("qt_")))
        return false;
    if (qobject_cast<const QScrollBar *>(w) || qobject_cast<const QHeaderView *>(w))
        return false;
    const bool interactive = qobject_cast<const QAbstractButton *>(w) || qobject_cast<const QLineEdit *>(w)
                             || qobject_cast<const QTextEdit *>(w) || qobject_cast<const QPlainTextEdit *>(w)
                             || qobject_cast<const QComboBox *>(w) || qobject_cast<const QAbstractSpinBox *>(w)
                             || qobject_cast<const QAbstractSlider *>(w)
                             || qobject_cast<const QAbstractItemView *>(w) || qobject_cast<const QTabBar *>(w)
                             || !w->property(ids::kAdapterProperty).toString().isEmpty();
    return interactive && !isPartOfAnotherControl(w);
}

QStringList auditProblems(QMainWindow *window)
{
    QStringList problems;
    if (!window)
        return problems;

    // Actions.
    const ControlActionIndex index(window);
    const auto entries = index.list();
    QHash<QString, const QAction *> actionIds;
    for (const auto &e : entries) {
        if (!e.stable) {
            problems.append(QStringLiteral("action  %1  (%2): no object name or data key")
                                .arg(e.id, (e.menuPath + QStringList{e.label}).join(QStringLiteral(" > "))));
        }
        const auto it = actionIds.constFind(e.id);
        if (it != actionIds.constEnd() && it.value() != e.action.data())
            problems.append(QStringLiteral("action  %1: id used by two actions").arg(e.id));
        actionIds.insert(e.id, e.action.data());
    }
    // Shortcut-only actions without a name cannot be addressed at all.
    const QList<QAction *> all = window->findChildren<QAction *>();
    for (const QAction *a : all) {
        if (!a->shortcut().isEmpty() && a->objectName().isEmpty() && a->associatedObjects().isEmpty()) {
            problems.append(QStringLiteral("action  \"%1\" (%2): shortcut action without an object name")
                                .arg(ControlActionIndex::plainText(a->text()),
                                     a->shortcut().toString(QKeySequence::PortableText)));
        }
    }

    // Widgets.
    QHash<QString, const QWidget *> nodeIds;
    const QList<QWidget *> widgets = window->findChildren<QWidget *>();
    for (const QWidget *w : widgets) {
        if (!isInteractiveWidget(w))
            continue;
        const ids::Id id = ids::nodeId(w);
        if (!id.stable)
            problems.append(QStringLiteral("widget  %1  %2: no object name").arg(id.id, describe(w)));
        const auto it = nodeIds.constFind(id.id);
        if (it != nodeIds.constEnd() && it.value() != w)
            problems.append(QStringLiteral("widget  %1  %2: id used by two widgets").arg(id.id, describe(w)));
        nodeIds.insert(id.id, w);
    }
    return problems;
}

int runAudit(QMainWindow *window)
{
    const QStringList problems = auditProblems(window);
    QTextStream out(stdout);
    for (const QString &p : problems)
        out << p << '\n';
    out << "control-api-audit: " << problems.size() << " problem(s)\n";
    out.flush();
    return int(problems.size());
}

} // namespace control
