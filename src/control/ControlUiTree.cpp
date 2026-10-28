/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlUiTree.h"

#include "ControlAudit.h"
#include "ControlEffects.h"
#include "ControlIds.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QGroupBox>
#include <QHash>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTabBar>
#include <QTabWidget>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QToolButton>

// Exported by QtGui for QTest: runs the shortcut map for a key press.
Q_GUI_EXPORT bool qt_sendShortcutOverrideEvent(QObject *o, ulong timestamp, int k, Qt::KeyboardModifiers mods,
                                               const QString &text = QString(), bool autorep = false,
                                               ushort count = 1);

namespace control::ui {

namespace {

constexpr int kValueChars = 200;

QString plain(const QString &text)
{
    if (Qt::mightBeRichText(text))
        return QTextDocumentFragment::fromHtml(text).toPlainText();
    return text;
}

QString withoutMnemonic(QString text)
{
    text.replace(QLatin1String("&&"), QStringLiteral("\x01"));
    text.remove(QLatin1Char('&'));
    text.replace(QStringLiteral("\x01"), QLatin1String("&"));
    return text;
}

bool isPassword(const QLineEdit *e)
{
    return e->echoMode() != QLineEdit::Normal;
}

QString elide(const QString &s, int max = kValueChars)
{
    return s.size() <= max ? s : s.left(max) + QChar(0x2026);
}

// Ids for a walk: roots from one table, positional numbering cached per
// (anchor, class), so a whole tree costs O(widgets), not O(widgets²).
struct IdContext
{
    QHash<const QWidget *, QString> roots;
    QHash<QPair<const QWidget *, QString>, QList<QWidget *>> positional;

    IdContext()
    {
        for (const auto &entry : ids::rootTable())
            roots.insert(entry.first, entry.second);
    }

    QString childId(const QWidget *w, const QString &anchorId, const QWidget *anchor, bool *stable)
    {
        if (const auto it = roots.constFind(w); it != roots.constEnd()) {
            *stable = true;
            return it.value();
        }
        if (ids::isSegment(w)) {
            *stable = true;
            return anchorId + QLatin1Char('/') + ids::segmentOf(w, nullptr); // derived names too
        }
        *stable = false;
        const QString cls = QString::fromLatin1(w->metaObject()->className());
        const QPair<const QWidget *, QString> key(anchor, cls);
        auto it = positional.find(key);
        if (it == positional.end())
            it = positional.insert(key, ids::positionalSiblings(anchor, cls));
        const qsizetype n = it->indexOf(const_cast<QWidget *>(w));
        return anchorId + QLatin1Char('/') + cls + QLatin1Char('#') + QString::number(qMax<qsizetype>(0, n));
    }
};

bool isLeafControl(const QWidget *w)
{
    return qobject_cast<const QAbstractButton *>(w) || qobject_cast<const QLineEdit *>(w)
           || qobject_cast<const QComboBox *>(w) || qobject_cast<const QAbstractSpinBox *>(w)
           || qobject_cast<const QAbstractSlider *>(w) || qobject_cast<const QLabel *>(w)
           || qobject_cast<const QTextEdit *>(w) || qobject_cast<const QPlainTextEdit *>(w)
           || qobject_cast<const QProgressBar *>(w) || qobject_cast<const QTabBar *>(w);
}

QJsonObject valueOf(QWidget *w)
{
    QJsonObject o;
    if (auto *e = qobject_cast<QLineEdit *>(w)) {
        if (isPassword(e))
            o.insert(QStringLiteral("password"), true);
        else
            o.insert(QStringLiteral("value"), elide(e->text()));
        if (e->isReadOnly())
            o.insert(QStringLiteral("readOnly"), true);
    } else if (auto *b = qobject_cast<QAbstractButton *>(w)) {
        if (b->isCheckable())
            o.insert(QStringLiteral("checked"), b->isChecked());
    } else if (auto *c = qobject_cast<QComboBox *>(w)) {
        o.insert(QStringLiteral("value"), c->currentText());
        o.insert(QStringLiteral("items"), c->count());
    } else if (auto *s = qobject_cast<QAbstractSpinBox *>(w)) {
        o.insert(QStringLiteral("value"), s->text());
    } else if (auto *sl = qobject_cast<QAbstractSlider *>(w)) {
        o.insert(QStringLiteral("value"), sl->value());
    } else if (auto *p = qobject_cast<QProgressBar *>(w)) {
        o.insert(QStringLiteral("value"), p->value());
    } else if (auto *t = qobject_cast<QTextEdit *>(w)) {
        o.insert(QStringLiteral("length"), t->document()->characterCount() - 1);
        if (t->isReadOnly())
            o.insert(QStringLiteral("readOnly"), true);
    } else if (auto *pt = qobject_cast<QPlainTextEdit *>(w)) {
        o.insert(QStringLiteral("length"), pt->document()->characterCount() - 1);
        if (pt->isReadOnly())
            o.insert(QStringLiteral("readOnly"), true);
    } else if (auto *g = qobject_cast<QGroupBox *>(w); g && g->isCheckable()) {
        o.insert(QStringLiteral("checked"), g->isChecked());
    }
    return o;
}

// A dock area's tabs and its own buttons: every tab is listed as its dock.
bool isUnlistedChrome(const QWidget *w)
{
    // QMainWindow's own bar over tabbed panels too: each tab is its panel.
    return w->inherits("ads::CDockAreaTabBar") || w->inherits("ads::CTitleBarButton")
           || w->inherits("ads::CDockWidgetTab")
           || (qobject_cast<const QTabBar *>(w) && qobject_cast<const QMainWindow *>(w->parentWidget()));
}

// The same, or the API's own chrome, anywhere up to the window (a search
// sees every widget, not a pruned walk).
bool isInsideUnlistedChrome(const QWidget *w)
{
    for (; w; w = w->parentWidget()) {
        if (ids::isIgnored(w) || isUnlistedChrome(w))
            return true;
        if (w->isWindow())
            break;
    }
    return false;
}

struct Walk
{
    TreeOptions options;
    IdContext ids;
    int count = 0;
    bool truncated = false;
};

void emitChildren(QWidget *parent, QJsonArray &into, int depth, const QString &anchorId, const QWidget *anchor,
                  Walk &walk);

void emitNode(QWidget *w, QJsonArray &into, int depth, const QString &anchorId, const QWidget *anchor, Walk &walk)
{
    if (ids::isIgnored(w))
        return;
    if (w->isWindow() && depth > 0)
        return; // other windows are roots of their own
    if (isUnlistedChrome(w))
        return;

    const ControlAdapter *adapter = ControlAdapters::instance().find(w);
    // A tab behind the current one is hidden but one click away: listed, not walked.
    bool hiddenTab = false;
    const bool shown = w->isVisible() && !ids::isBehindTab(w);
    if (!walk.options.includeHidden && !shown) {
        if (!adapter || !adapter->reachableWhileHidden(w, QStringLiteral("select")))
            return;
        hiddenTab = true;
    }
    const bool root = ids::isRoot(w);
    const bool label = qobject_cast<QLabel *>(w) && !static_cast<QLabel *>(w)->text().isEmpty();
    const bool include = root || adapter || isInteractiveWidget(w) || label
                         || qobject_cast<QGroupBox *>(w) || qobject_cast<QTabWidget *>(w)
                         || (ids::isSegment(w) && !isLeafControl(w));

    bool stable = true;
    const QString id = walk.ids.childId(w, anchorId, anchor, &stable);
    const bool newAnchor = root || ids::isSegment(w);
    const QString nextAnchorId = newAnchor ? id : anchorId;
    const QWidget *nextAnchor = newAnchor ? w : anchor;

    if (!include) {
        emitChildren(w, into, depth, nextAnchorId, nextAnchor, walk);
        return;
    }
    if (walk.count >= walk.options.maxNodes) {
        walk.truncated = true;
        return;
    }
    ++walk.count;

    QJsonObject n{{QStringLiteral("id"), id}, {QStringLiteral("role"), roleOf(w)}};
    const QString text = labelOf(w);
    if (!text.isEmpty())
        n.insert(QStringLiteral("label"), elide(text));
    if (!stable)
        n.insert(QStringLiteral("stable"), false);
    if (!w->isEnabled())
        n.insert(QStringLiteral("enabled"), false);
    if (!shown)
        n.insert(QStringLiteral("visible"), false);
    if (w->hasFocus())
        n.insert(QStringLiteral("focus"), true);
    const QJsonObject value = valueOf(w);
    for (auto it = value.constBegin(); it != value.constEnd(); ++it)
        n.insert(it.key(), it.value());
    if (adapter) {
        const QJsonObject summary = adapter->summary(w);
        for (auto it = summary.constBegin(); it != summary.constEnd(); ++it)
            n.insert(it.key(), it.value());
    }

    if (depth >= walk.options.depth || hiddenTab) {
        if (!isLeafControl(w) || adapter)
            n.insert(QStringLiteral("more"), true);
        into.append(n);
        return;
    }
    if (adapter) {
        const QList<ControlAdapter::Part> parts = adapter->parts(w, walk.options.maxParts + 1);
        QJsonArray partsJson;
        for (qsizetype i = 0; i < parts.size() && i < walk.options.maxParts; ++i) {
            const auto &p = parts.at(i);
            QJsonObject pj = p.extra;
            pj.insert(QStringLiteral("id"), id + QLatin1Char('@') + p.key);
            pj.insert(QStringLiteral("role"), p.role);
            if (!p.label.isEmpty())
                pj.insert(QStringLiteral("label"), elide(p.label));
            if (!p.enabled)
                pj.insert(QStringLiteral("enabled"), false);
            if (p.selected)
                pj.insert(QStringLiteral("selected"), true);
            if (p.current)
                pj.insert(QStringLiteral("current"), true);
            partsJson.append(pj);
        }
        if (!partsJson.isEmpty())
            n.insert(QStringLiteral("parts"), partsJson);
        if (parts.size() > walk.options.maxParts)
            n.insert(QStringLiteral("moreParts"), true);
    }
    if (!isLeafControl(w) && (!adapter || adapter->walksChildren(w))) {
        QJsonArray children;
        emitChildren(w, children, depth + 1, nextAnchorId, nextAnchor, walk);
        if (!children.isEmpty())
            n.insert(QStringLiteral("children"), children);
    }
    into.append(n);
}

void emitChildren(QWidget *parent, QJsonArray &into, int depth, const QString &anchorId, const QWidget *anchor,
                  Walk &walk)
{
    const QObjectList &children = parent->children();
    for (QObject *o : children) {
        if (!o->isWidgetType())
            continue;
        emitNode(static_cast<QWidget *>(o), into, depth, anchorId, anchor, walk);
        if (walk.truncated)
            return;
    }
}

// The widget a mouse at `pos` (in `w`) lands on, and the position there.
QWidget *hitWidget(QWidget *w, QPoint &pos)
{
    QWidget *hit = w->childAt(pos);
    if (!hit)
        return w;
    pos = hit->mapFrom(w, pos);
    return hit;
}

void sendMouse(QWidget *target, QEvent::Type type, const QPoint &local, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    const QPointF global = target->mapToGlobal(QPointF(local));
    const QPointF scene = target->window()->mapFromGlobal(global);
    QMouseEvent ev(type, QPointF(local), scene, global, button, buttons, QGuiApplication::keyboardModifiers());
    QApplication::sendEvent(target, &ev);
}

void focusForInput(QWidget *w)
{
    QWidget *target = w->focusProxy() ? w->focusProxy() : w;
    if (target->focusPolicy() != Qt::NoFocus && !target->hasFocus())
        target->setFocus(Qt::OtherFocusReason);
}

QWidget *textReceiver(QWidget *w)
{
    if (auto *combo = qobject_cast<QComboBox *>(w); combo && combo->lineEdit())
        return combo->lineEdit();
    if (qobject_cast<QAbstractSpinBox *>(w)) {
        if (auto *edit = w->findChild<QLineEdit *>())
            return edit;
    }
    if (w->focusProxy())
        return w->focusProxy();
    return w;
}

QString keyText(int key, Qt::KeyboardModifiers mods)
{
    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return {};
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter: return QStringLiteral("\r");
    case Qt::Key_Tab: return QStringLiteral("\t");
    case Qt::Key_Backspace: return QStringLiteral("\b");
    case Qt::Key_Escape: return QStringLiteral("\x1b");
    case Qt::Key_Space: return QStringLiteral(" ");
    default: break;
    }
    if (key >= 0x20 && key < 0x7f) {
        QChar c(key);
        return (mods & Qt::ShiftModifier) ? QString(c) : QString(c.toLower());
    }
    return {};
}

// A window shortcut whose window is not the active one (the agent works in
// the background): Qt's shortcut map ignores it, so find the action the
// shortcut belongs to, as Qt would.
bool triggerShortcutAction(QWidget *from, const QKeySequence &seq)
{
    QWidget *window = from->window();
    const QList<QAction *> actions = window->findChildren<QAction *>();
    for (QAction *a : actions) {
        if (!a->isEnabled() || a->shortcutContext() == Qt::WidgetShortcut)
            continue;
        const QList<QKeySequence> shortcuts = a->shortcuts();
        for (const QKeySequence &s : shortcuts) {
            if (s == seq) {
                a->trigger();
                return true;
            }
        }
    }
    return false;
}

void sendKey(QWidget *recv, int key, Qt::KeyboardModifiers mods, const QString &text, bool *consumed)
{
    if (qt_sendShortcutOverrideEvent(recv, 0, key, mods, text)) {
        *consumed = true;
        return;
    }
    const QPointer<QWidget> guard(recv);
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(recv, &press);
    *consumed = press.isAccepted();
    if (!guard)
        return; // the key closed its receiver
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QApplication::sendEvent(recv, &release);
}

QString unsupported(const QString &op, const QWidget *w)
{
    return QStringLiteral("%1 does not apply to a %2").arg(op, roleOf(w));
}

} // namespace

Target resolve(const QString &id)
{
    Target t;
    const qsizetype at = id.indexOf(QLatin1Char('@'));
    const QString nodeId = at < 0 ? id : id.left(at);
    if (at >= 0)
        t.part = id.mid(at + 1);
    t.widget = ids::resolveNode(nodeId, &t.error);
    if (t.widget && !t.part.isEmpty() && !ControlAdapters::instance().find(t.widget)) {
        t.error = QStringLiteral("'%1' has no parts").arg(nodeId);
        t.widget = nullptr;
    }
    return t;
}

QString roleOf(const QWidget *w)
{
    if (const ControlAdapter *a = ControlAdapters::instance().find(w)) {
        const QString r = a->role(w);
        if (!r.isEmpty())
            return r;
    }
    if (ids::isRoot(w))
        return rootKind(w);
    if (qobject_cast<const QCheckBox *>(w))
        return QStringLiteral("checkbox");
    if (qobject_cast<const QRadioButton *>(w))
        return QStringLiteral("radio");
    if (qobject_cast<const QAbstractButton *>(w))
        return QStringLiteral("button");
    if (qobject_cast<const QLineEdit *>(w))
        return QStringLiteral("input");
    if (qobject_cast<const QTextEdit *>(w) || qobject_cast<const QPlainTextEdit *>(w))
        return QStringLiteral("textarea");
    if (qobject_cast<const QComboBox *>(w))
        return QStringLiteral("combo");
    if (qobject_cast<const QAbstractSpinBox *>(w))
        return QStringLiteral("spin");
    if (qobject_cast<const QAbstractSlider *>(w))
        return QStringLiteral("slider");
    if (qobject_cast<const QLabel *>(w))
        return QStringLiteral("label");
    if (qobject_cast<const QProgressBar *>(w))
        return QStringLiteral("progress");
    if (qobject_cast<const QGroupBox *>(w))
        return QStringLiteral("group");
    if (qobject_cast<const QTabWidget *>(w))
        return QStringLiteral("tabwidget");
    if (qobject_cast<const QDockWidget *>(w))
        return QStringLiteral("dock");
    return QStringLiteral("panel");
}

namespace {

// The QLabel naming a field (setBuddy, QFormLayout rows): "Line height".
QString buddyText(const QWidget *w)
{
    const QWidget *parent = w->parentWidget();
    if (!parent)
        return {};
    for (const QObject *o : parent->children()) {
        if (const auto *l = qobject_cast<const QLabel *>(o); l && l->buddy() == w) {
            QString text = withoutMnemonic(plain(l->text())).simplified();
            if (text.endsWith(QLatin1Char(':')))
                text.chop(1);
            return text;
        }
    }
    return {};
}

// What the widget itself shows: a button's text, a group's title, a field's placeholder.
// A button's text, else the labels laid out inside it (a row button that
// shows a name and a muted detail), in order.
QString buttonText(const QAbstractButton *b)
{
    QString text = withoutMnemonic(b->text());
    if (!text.isEmpty())
        return text;
    QStringList parts;
    for (const QLabel *l : b->findChildren<QLabel *>()) {
        const QString t = plain(l->text()).simplified();
        if (!t.isEmpty())
            parts.append(t);
    }
    return parts.join(QStringLiteral(" · "));
}

QString ownText(const QWidget *w)
{
    if (const auto *b = qobject_cast<const QAbstractButton *>(w))
        return buttonText(b);
    if (const auto *l = qobject_cast<const QLabel *>(w))
        return plain(l->text()).simplified();
    if (const auto *g = qobject_cast<const QGroupBox *>(w))
        return withoutMnemonic(g->title());
    if (const auto *e = qobject_cast<const QLineEdit *>(w))
        return e->placeholderText();
    if (const auto *t = qobject_cast<const QTextEdit *>(w))
        return t->placeholderText();
    if (const auto *pt = qobject_cast<const QPlainTextEdit *>(w))
        return pt->placeholderText();
    if (const auto *d = qobject_cast<const QDockWidget *>(w))
        return withoutMnemonic(d->windowTitle());
    if (ids::isRoot(w))
        return rootTitle(w);
    return {};
}

} // namespace

QString labelOf(const QWidget *w)
{
    // A field is named by the label beside it, before its placeholder.
    const bool field = qobject_cast<const QLineEdit *>(w) || qobject_cast<const QComboBox *>(w)
                       || qobject_cast<const QAbstractSpinBox *>(w) || qobject_cast<const QAbstractSlider *>(w)
                       || qobject_cast<const QTextEdit *>(w) || qobject_cast<const QPlainTextEdit *>(w);
    QString text = field ? buddyText(w) : QString();
    if (text.isEmpty())
        text = ownText(w);
    if (text.isEmpty())
        text = w->accessibleName();
    if (text.isEmpty())
        text = plain(w->toolTip()).simplified();
    return text;
}

QJsonObject tree(QWidget *root, const TreeOptions &options)
{
    Walk walk;
    walk.options = options;
    QJsonArray roots;
    if (root) {
        // emitNode derives the node's own id from its parent's anchor.
        const QWidget *parentAnchor = ids::isRoot(root) ? root : ids::anchorOf(root);
        const QString parentAnchorId = parentAnchor ? ids::nodeId(parentAnchor).id : QString();
        emitNode(root, roots, 0, parentAnchorId, parentAnchor, walk);
    } else {
        for (const auto &entry : ids::rootTable()) {
            QWidget *w = entry.first;
            if (!w->isWindow() || !w->isVisible() || ids::isIgnored(w))
                continue;
            if (qobject_cast<QMenu *>(w) && w != QApplication::activePopupWidget())
                continue;
            emitNode(w, roots, 0, entry.second, w, walk);
        }
    }
    QJsonObject out{{QStringLiteral("ok"), true}, {QStringLiteral("nodes"), roots}};
    if (QWidget *modal = QApplication::activeModalWidget())
        out.insert(QStringLiteral("modal"), ids::nodeId(modal).id);
    if (QWidget *popup = QApplication::activePopupWidget())
        out.insert(QStringLiteral("popup"), ids::nodeId(popup).id);
    if (QWidget *focus = QApplication::focusWidget(); focus && !ids::isInsideIgnored(focus))
        out.insert(QStringLiteral("focus"), ids::nodeId(focus).id);
    if (walk.truncated)
        out.insert(QStringLiteral("truncated"), true);
    return out;
}

QJsonObject node(QWidget *w)
{
    const ids::Id id = ids::nodeId(w);
    QJsonObject n{{QStringLiteral("ok"), true},
                  {QStringLiteral("id"), id.id},
                  {QStringLiteral("role"), roleOf(w)},
                  {QStringLiteral("class"), QString::fromLatin1(w->metaObject()->className())},
                  {QStringLiteral("enabled"), w->isEnabled()},
                  {QStringLiteral("visible"), w->isVisible()},
                  {QStringLiteral("focus"), w->hasFocus()}};
    if (!id.stable)
        n.insert(QStringLiteral("stable"), false);
    const QString label = labelOf(w);
    if (!label.isEmpty())
        n.insert(QStringLiteral("label"), label);
    if (!w->toolTip().isEmpty())
        n.insert(QStringLiteral("tooltip"), plain(w->toolTip()));
    const QRect g(w->mapTo(w->window(), QPoint(0, 0)), w->size());
    n.insert(QStringLiteral("rect"), QJsonArray{g.x(), g.y(), g.width(), g.height()});
    const QJsonObject value = valueOf(w);
    for (auto it = value.constBegin(); it != value.constEnd(); ++it)
        n.insert(it.key(), it.value());
    if (const ControlAdapter *a = ControlAdapters::instance().find(w)) {
        n.insert(QStringLiteral("adapter"), a->name());
        const QJsonObject summary = a->summary(w);
        for (auto it = summary.constBegin(); it != summary.constEnd(); ++it)
            n.insert(it.key(), it.value());
    }
    return n;
}

QJsonObject read(QWidget *w, const QString &part, const ReadOptions &options)
{
    QJsonObject out;
    if (const ControlAdapter *a = ControlAdapters::instance().find(w); a && a->read(w, part, options, out)) {
        out.insert(QStringLiteral("ok"), true);
        out.insert(QStringLiteral("id"), ids::nodeId(w).id + (part.isEmpty() ? QString() : QLatin1Char('@') + part));
        return out;
    }
    if (!part.isEmpty())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("no part '%1'").arg(part)}};

    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("id"), ids::nodeId(w).id);
    out.insert(QStringLiteral("role"), roleOf(w));
    const qint64 from = qMax<qint64>(0, options.from);
    const qint64 count = options.count < 0 ? 200000 : options.count;
    auto textWindow = [&](const QString &text) {
        out.insert(QStringLiteral("length"), text.size());
        out.insert(QStringLiteral("from"), from);
        out.insert(QStringLiteral("text"), text.mid(from, count));
    };
    if (auto *e = qobject_cast<QLineEdit *>(w)) {
        if (isPassword(e))
            out.insert(QStringLiteral("password"), true);
        else
            out.insert(QStringLiteral("text"), e->text());
        out.insert(QStringLiteral("placeholder"), e->placeholderText());
    } else if (auto *t = qobject_cast<QTextEdit *>(w)) {
        textWindow(t->toPlainText());
    } else if (auto *pt = qobject_cast<QPlainTextEdit *>(w)) {
        textWindow(pt->toPlainText());
    } else if (auto *l = qobject_cast<QLabel *>(w)) {
        out.insert(QStringLiteral("text"), plain(l->text()));
    } else if (auto *b = qobject_cast<QAbstractButton *>(w)) {
        out.insert(QStringLiteral("text"), buttonText(b));
        if (b->isCheckable())
            out.insert(QStringLiteral("checked"), b->isChecked());
    } else if (auto *c = qobject_cast<QComboBox *>(w)) {
        QJsonArray items;
        for (int i = 0; i < c->count(); ++i)
            items.append(c->itemText(i));
        out.insert(QStringLiteral("value"), c->currentText());
        out.insert(QStringLiteral("index"), c->currentIndex());
        out.insert(QStringLiteral("items"), items);
    } else if (auto *s = qobject_cast<QAbstractSpinBox *>(w)) {
        out.insert(QStringLiteral("value"), s->text());
        if (auto *sb = qobject_cast<QSpinBox *>(w)) {
            out.insert(QStringLiteral("minimum"), sb->minimum());
            out.insert(QStringLiteral("maximum"), sb->maximum());
        }
    } else if (auto *sl = qobject_cast<QAbstractSlider *>(w)) {
        out.insert(QStringLiteral("value"), sl->value());
        out.insert(QStringLiteral("minimum"), sl->minimum());
        out.insert(QStringLiteral("maximum"), sl->maximum());
    } else {
        // A container (a dialog, a message box): the texts and buttons in it,
        // in reading order.
        if (auto *box = qobject_cast<QMessageBox *>(w)) {
            out.insert(QStringLiteral("text"), plain(box->text()));
            if (!box->informativeText().isEmpty())
                out.insert(QStringLiteral("informativeText"), plain(box->informativeText()));
        }
        out.insert(QStringLiteral("title"), w->windowTitle());
        QJsonArray texts;
        QJsonArray buttons;
        const QList<QWidget *> all = w->findChildren<QWidget *>();
        for (QWidget *c : all) {
            if (!c->isVisible() || isInsideUnlistedChrome(c))
                continue;
            if (auto *l = qobject_cast<QLabel *>(c); l && !l->text().isEmpty())
                texts.append(plain(l->text()));
            else if (auto *b = qobject_cast<QAbstractButton *>(c); b && !b->text().isEmpty())
                buttons.append(QJsonObject{{QStringLiteral("id"), ids::nodeId(b).id},
                                           {QStringLiteral("label"), withoutMnemonic(b->text())},
                                           {QStringLiteral("enabled"), b->isEnabled()}});
        }
        out.insert(QStringLiteral("texts"), texts);
        out.insert(QStringLiteral("buttons"), buttons);
    }
    return out;
}

QJsonArray find(const QString &text, const QString &role, int limit)
{
    QJsonArray out;
    for (const auto &entry : ids::rootTable()) {
        QWidget *root = entry.first;
        if (!root->isWindow() || !root->isVisible() || ids::isIgnored(root))
            continue;
        QList<QWidget *> widgets = root->findChildren<QWidget *>();
        widgets.prepend(root);
        for (QWidget *w : std::as_const(widgets)) {
            if (!ids::isShownToUser(w) || isInsideUnlistedChrome(w))
                continue;
            const QString r = roleOf(w);
            if (!role.isEmpty() && r != role)
                continue;
            if (r == QLatin1String("panel") && !ids::isSegment(w))
                continue;
            const QString label = labelOf(w);
            QString haystack = label;
            if (auto *e = qobject_cast<QLineEdit *>(w); e && !isPassword(e))
                haystack += QLatin1Char(' ') + e->text();
            if (!text.isEmpty() && !haystack.contains(text, Qt::CaseInsensitive))
                continue;
            if (text.isEmpty() && role.isEmpty())
                continue;
            const ids::Id id = ids::nodeId(w);
            QJsonObject o{{QStringLiteral("id"), id.id}, {QStringLiteral("role"), r}};
            if (!label.isEmpty())
                o.insert(QStringLiteral("label"), elide(label));
            if (!id.stable)
                o.insert(QStringLiteral("stable"), false);
            if (!w->isEnabled())
                o.insert(QStringLiteral("enabled"), false);
            out.append(o);
            if (out.size() >= limit)
                return out;
        }
    }
    return out;
}

QRect targetRect(const Target &t)
{
    if (!t.widget || t.part.isEmpty())
        return {};
    const ControlAdapter *a = ControlAdapters::instance().find(t.widget);
    if (!a)
        return {};
    const QRect r = a->partRect(t.widget, t.part);
    if (r.isEmpty())
        return {};
    QWidget *target = a->eventTarget(t.widget);
    return QRect(target->mapTo(t.widget, r.topLeft()), r.size());
}

QString blockedReason(const Target &t, const QString &op)
{
    if (!t.widget)
        return t.error.isEmpty() ? QStringLiteral("not found") : t.error;
    if (ids::isInsideIgnored(t.widget))
        return QStringLiteral("the Control API's own settings are for the user only");
    if (!ids::isShownToUser(t.widget)) {
        const ControlAdapter *a = ControlAdapters::instance().find(t.widget);
        if (!a || !a->reachableWhileHidden(t.widget, op))
            return QStringLiteral("it is hidden; a user could not reach it");
    }
    if (!t.widget->isEnabled())
        return QStringLiteral("it is disabled; a user could not use it");
    QWidget *modal = QApplication::activeModalWidget();
    if (modal && t.widget->window() != modal && !modal->isAncestorOf(t.widget)) {
        // A user cannot click behind a modal dialog.
        bool insideModalChain = false;
        for (QWidget *p = t.widget->window(); p; p = p->parentWidget()) {
            if (p == modal) {
                insideModalChain = true;
                break;
            }
        }
        if (!insideModalChain)
            return QStringLiteral("the modal dialog '%1' is open; answer it first").arg(ids::nodeId(modal).id);
    }
    // An open menu runs its own exec() loop: a command elsewhere could close
    // the menu's owner under it (its code after exec() then touches freed
    // memory). A user's click outside closes the menu first, and so does
    // ui::click; anything else waits until the menu is answered.
    if (QApplication::activePopupWidget() && op != QLatin1String("click")
        && t.widget->window()->windowType() != Qt::Popup)
        return QStringLiteral("a menu is open; pick from it, or close it with POST /ui/key {\"keys\": \"Escape\"}");
    return {};
}

QString click(const Target &t, Qt::MouseButton button, bool doubleClick)
{
    QWidget *w = t.widget;
    // A click outside an open popup closes it first, as a real click would.
    if (QWidget *popup = QApplication::activePopupWidget(); popup && w->window() != popup)
        popup->close();

    QWidget *recv = w;
    QPoint pos;
    if (!t.part.isEmpty()) {
        const ControlAdapter *a = ControlAdapters::instance().find(w);
        a->reveal(w, t.part);
        const QRect r = a->partRect(w, t.part);
        if (r.isEmpty())
            return QStringLiteral("'%1' is not on screen").arg(t.part);
        recv = a->eventTarget(w);
        pos = r.center();
    } else {
        if (auto *area = qobject_cast<QAbstractScrollArea *>(w))
            recv = area->viewport();
        pos = recv->rect().center();
        // A check box or radio button takes clicks only on its box and label,
        // which in a stretched layout do not reach the widget's centre.
        const bool check = qobject_cast<QCheckBox *>(w) != nullptr;
        if (check || qobject_cast<QRadioButton *>(w)) {
            auto *b = static_cast<QAbstractButton *>(w);
            QStyleOptionButton opt;
            opt.initFrom(b);
            opt.text = b->text();
            opt.icon = b->icon();
            opt.iconSize = b->iconSize();
            const QRect r = b->style()->subElementRect(check ? QStyle::SE_CheckBoxClickRect
                                                             : QStyle::SE_RadioButtonClickRect,
                                                       &opt, b);
            if (!r.isEmpty())
                pos = r.center();
        }
    }
    recv = hitWidget(recv, pos);

    if (button == Qt::LeftButton && (recv->focusPolicy() & Qt::ClickFocus))
        recv->setFocus(Qt::MouseFocusReason);
    QPointer<QWidget> guard(recv);
    sendMouse(recv, QEvent::MouseButtonPress, pos, button, button);
    if (guard)
        sendMouse(recv, QEvent::MouseButtonRelease, pos, button, Qt::NoButton);
    if (doubleClick && guard) {
        sendMouse(recv, QEvent::MouseButtonDblClick, pos, button, button);
        if (guard)
            sendMouse(recv, QEvent::MouseButtonRelease, pos, button, Qt::NoButton);
    }
    if (button == Qt::RightButton && guard) {
        QContextMenuEvent ev(QContextMenuEvent::Mouse, pos, recv->mapToGlobal(pos));
        QApplication::sendEvent(recv, &ev);
    }
    return {};
}

QString type(const Target &t, const QString &text, bool replace)
{
    if (const ControlAdapter *a = ControlAdapters::instance().find(t.widget)) {
        if (auto r = a->perform(t.widget, t.part, QStringLiteral("type"),
                                {{QStringLiteral("text"), text}, {QStringLiteral("replace"), replace}}))
            return *r;
    }
    QWidget *recv = textReceiver(t.widget);
    if (auto *e = qobject_cast<QLineEdit *>(recv); e && e->isReadOnly())
        return QStringLiteral("the field is read-only");
    if (auto *te = qobject_cast<QTextEdit *>(recv); te && te->isReadOnly())
        return QStringLiteral("the text is read-only");
    if (auto *pe = qobject_cast<QPlainTextEdit *>(recv); pe && pe->isReadOnly())
        return QStringLiteral("the text is read-only");
    focusForInput(recv);
    if (replace) {
        if (auto *e = qobject_cast<QLineEdit *>(recv))
            e->selectAll();
        else if (auto *te = qobject_cast<QTextEdit *>(recv))
            te->selectAll();
        else if (auto *pe = qobject_cast<QPlainTextEdit *>(recv))
            pe->selectAll();
        else
            keys({recv, {}, {}}, QStringLiteral("Ctrl+A"));
    }
    if (recv->testAttribute(Qt::WA_InputMethodEnabled)) {
        // One commit for the whole text, like a paste from an input method.
        QInputMethodEvent ev;
        ev.setCommitString(text);
        QApplication::sendEvent(recv, &ev);
        return {};
    }
    const QPointer<QWidget> guard(recv);
    for (const QChar c : text) {
        if (!guard)
            return QStringLiteral("it went away while typing");
        const int key = c == QLatin1Char('\n') ? int(Qt::Key_Return) : int(c.toUpper().unicode());
        bool consumed = false;
        sendKey(recv, key, Qt::NoModifier, c == QLatin1Char('\n') ? QStringLiteral("\r") : QString(c), &consumed);
    }
    return {};
}

QString keys(const Target &t, const QString &sequence)
{
    const QKeySequence seq = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    if (seq.isEmpty())
        return QStringLiteral("'%1' is not a key sequence (examples: Ctrl+S, Enter, Ctrl+K, Ctrl+C)").arg(sequence);
    QWidget *recv = t.widget;
    if (!recv) {
        recv = QApplication::activePopupWidget();
        if (!recv)
            recv = QApplication::activeModalWidget();
        if (!recv)
            recv = QApplication::focusWidget();
        if (!recv) {
            const QWidgetList tops = QApplication::topLevelWidgets();
            for (QWidget *top : tops) {
                if (qobject_cast<QMainWindow *>(top)) {
                    recv = top->focusWidget() ? top->focusWidget() : top;
                    break;
                }
            }
        }
        if (!recv)
            return QStringLiteral("no window to send keys to");
        if (ids::isInsideIgnored(recv))
            return QStringLiteral("the focus is in the Control API's own settings, which are for the user only");
    } else {
        recv = textReceiver(recv);
        focusForInput(recv);
    }
    QPointer<QWidget> guard(recv);
    for (int i = 0; i < seq.count() && guard; ++i) {
        const QKeyCombination combo = seq[i];
        const int key = int(combo.key());
        const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
        bool consumed = false;
        sendKey(recv, key, mods, keyText(key, mods), &consumed);
        if (!consumed && guard && (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
            triggerShortcutAction(recv, QKeySequence(combo));
    }
    return {};
}

QString select(const Target &t, const QJsonObject &args)
{
    QWidget *w = t.widget;
    if (const ControlAdapter *a = ControlAdapters::instance().find(w)) {
        if (auto r = a->perform(w, t.part, QStringLiteral("select"), args))
            return *r;
    }
    if (!t.part.isEmpty())
        return click(t, Qt::LeftButton, args.value(QStringLiteral("double")).toBool());
    if (auto *combo = qobject_cast<QComboBox *>(w)) {
        int index = -1;
        if (args.contains(QStringLiteral("index")))
            index = args.value(QStringLiteral("index")).toInt(-1);
        else if (args.contains(QStringLiteral("text")))
            index = combo->findText(args.value(QStringLiteral("text")).toString());
        else if (args.contains(QStringLiteral("data")))
            index = combo->findData(args.value(QStringLiteral("data")).toVariant());
        if (index < 0 || index >= combo->count())
            return QStringLiteral("no such item; read the combo for its items");
        combo->setCurrentIndex(index);
        emit combo->activated(index);
        emit combo->textActivated(combo->itemText(index));
        return {};
    }
    if (auto *tabs = qobject_cast<QTabWidget *>(w)) {
        int index = args.value(QStringLiteral("index")).toInt(-1);
        if (args.contains(QStringLiteral("text"))) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (withoutMnemonic(tabs->tabText(i)) == args.value(QStringLiteral("text")).toString())
                    index = i;
            }
        }
        if (index < 0 || index >= tabs->count())
            return QStringLiteral("no such tab");
        Target bar{tabs->tabBar(), QStringLiteral("tab:") + QString::number(index), {}};
        return click(bar, Qt::LeftButton, false);
    }
    if (qobject_cast<QAbstractButton *>(w))
        return click(t, Qt::LeftButton, false);
    return unsupported(QStringLiteral("select"), w);
}

QString set(const Target &t, const QJsonValue &value)
{
    QWidget *w = t.widget;
    if (const ControlAdapter *a = ControlAdapters::instance().find(w)) {
        if (auto r = a->perform(w, t.part, QStringLiteral("set"), {{QStringLiteral("value"), value}}))
            return *r;
    }
    if (auto *b = qobject_cast<QAbstractButton *>(w)) {
        if (!b->isCheckable())
            return QStringLiteral("not a checkbox; use click");
        if (b->isChecked() != value.toBool())
            return click(t, Qt::LeftButton, false);
        return {};
    }
    if (auto *g = qobject_cast<QGroupBox *>(w); g && g->isCheckable()) {
        if (g->isChecked() != value.toBool())
            g->setChecked(value.toBool());
        return {};
    }
    if (auto *combo = qobject_cast<QComboBox *>(w)) {
        if (combo->isEditable() && combo->findText(value.toString()) < 0)
            return type(t, value.toString(), true);
        return select(t, {{QStringLiteral("text"), value.toString()}});
    }
    if (auto *spin = qobject_cast<QAbstractSpinBox *>(w)) {
        focusForInput(spin);
        if (QLineEdit *edit = spin->findChild<QLineEdit *>()) {
            edit->selectAll();
            QInputMethodEvent ev;
            ev.setCommitString(value.toVariant().toString());
            QApplication::sendEvent(edit, &ev);
        }
        spin->interpretText();
        emit spin->editingFinished();
        return {};
    }
    if (auto *slider = qobject_cast<QAbstractSlider *>(w)) {
        slider->setValue(value.toInt());
        return {};
    }
    if (qobject_cast<QLineEdit *>(w) || qobject_cast<QTextEdit *>(w) || qobject_cast<QPlainTextEdit *>(w))
        return type(t, value.toString(), true);
    return unsupported(QStringLiteral("set"), w);
}

QString scroll(const Target &t, const QJsonObject &args)
{
    QWidget *w = t.widget;
    if (const ControlAdapter *a = ControlAdapters::instance().find(w)) {
        if (auto r = a->perform(w, t.part, QStringLiteral("scroll"), args))
            return *r;
        if (!t.part.isEmpty()) {
            a->reveal(w, t.part);
            return {};
        }
    }
    QAbstractScrollArea *area = qobject_cast<QAbstractScrollArea *>(w);
    if (!area) {
        // Scroll the area that holds it until it is in view.
        for (QWidget *p = w->parentWidget(); p; p = p->parentWidget()) {
            if (auto *sa = qobject_cast<QScrollArea *>(p)) {
                sa->ensureWidgetVisible(w);
                return {};
            }
        }
        return unsupported(QStringLiteral("scroll"), w);
    }
    QScrollBar *v = area->verticalScrollBar();
    QScrollBar *h = area->horizontalScrollBar();
    const QString to = args.value(QStringLiteral("to")).toString();
    if (to == QLatin1String("top"))
        v->setValue(v->minimum());
    else if (to == QLatin1String("bottom"))
        v->setValue(v->maximum());
    if (args.contains(QStringLiteral("pages")))
        v->setValue(v->value() + args.value(QStringLiteral("pages")).toInt() * v->pageStep());
    if (args.contains(QStringLiteral("dy")))
        v->setValue(v->value() + args.value(QStringLiteral("dy")).toInt());
    if (args.contains(QStringLiteral("dx")))
        h->setValue(h->value() + args.value(QStringLiteral("dx")).toInt());
    return {};
}

QString close(const Target &t)
{
    QWidget *w = t.widget;
    if (const ControlAdapter *a = ControlAdapters::instance().find(w)) {
        if (auto r = a->perform(w, t.part, QStringLiteral("close"), {}))
            return *r;
    }
    if (!t.part.isEmpty())
        return QStringLiteral("this part has no close action");
    if (w->isWindow()) {
        // Like the title bar's close button: the window may still refuse.
        w->close();
        return {};
    }
    return QStringLiteral("only windows, dialogs, docks and tabs close; find its close button");
}

QString focus(const Target &t)
{
    QWidget *recv = textReceiver(t.widget);
    if (recv->focusPolicy() == Qt::NoFocus)
        return QStringLiteral("it does not take keyboard focus");
    recv->setFocus(Qt::OtherFocusReason);
    return {};
}

namespace {

// What a user drags to give a pane `size` along `o`: the nearest edge that
// sets it, from the widget up. A main window's panel moves its separator
// (resizeDocks), a floating one its window edge, a splitter child its
// handle (Qt's and ADS's CDockSplitter); the neighbour gives or takes the
// difference, within the minimum sizes Qt enforces either way.
QString resizeAlong(QWidget *w, Qt::Orientation o, int size)
{
    const bool horizontal = o == Qt::Horizontal;
    for (QWidget *c = w; c && !c->isWindow(); c = c->parentWidget()) {
        if (auto *dock = qobject_cast<QDockWidget *>(c)) {
            if (dock->isFloating()) {
                dock->resize(horizontal ? size : dock->width(), horizontal ? dock->height() : size);
                return {};
            }
            if (auto *main = qobject_cast<QMainWindow *>(dock->parentWidget())) {
                main->resizeDocks({dock}, {size}, o);
                return {};
            }
        }
        // The central area has no edge of its own: the separator of a shown
        // panel beside it gives it room.
        if (auto *main = qobject_cast<QMainWindow *>(c->parentWidget()); main && main->centralWidget() == c) {
            const int delta = size - (horizontal ? c->width() : c->height());
            const Qt::DockWidgetArea sides[2] = {horizontal ? Qt::LeftDockWidgetArea : Qt::TopDockWidgetArea,
                                                 horizontal ? Qt::RightDockWidgetArea : Qt::BottomDockWidgetArea};
            const QList<QDockWidget *> docks = main->findChildren<QDockWidget *>(Qt::FindDirectChildrenOnly);
            for (Qt::DockWidgetArea side : sides) {
                for (QDockWidget *dock : docks) {
                    if (dock->isFloating() || !dock->isVisible() || ids::isBehindTab(dock)
                        || main->dockWidgetArea(dock) != side)
                        continue;
                    main->resizeDocks({dock}, {std::max(0, (horizontal ? dock->width() : dock->height()) - delta)}, o);
                    return {};
                }
            }
        }
        auto *splitter = qobject_cast<QSplitter *>(c->parentWidget());
        if (!splitter || splitter->orientation() != o)
            continue;
        QList<int> sizes = splitter->sizes();
        const int index = splitter->indexOf(c);
        if (index < 0)
            continue;
        // The shown neighbour across a handle: after it, else before it.
        int other = -1;
        for (int i = index + 1; i < sizes.size() && other < 0; ++i) {
            if (sizes[i] > 0)
                other = i;
        }
        for (int i = index - 1; i >= 0 && other < 0; --i) {
            if (sizes[i] > 0)
                other = i;
        }
        if (other < 0)
            continue; // alone in its splitter: the edge is further up
        const int delta = size - sizes[index];
        sizes[index] = size;
        sizes[other] = std::max(0, sizes[other] - delta);
        splitter->setSizes(sizes);
        return {};
    }
    return horizontal ? QStringLiteral("no edge sets its width; resize the pane around it")
                      : QStringLiteral("no edge sets its height; resize the pane around it");
}

QString resizePane(QWidget *w, const QJsonObject &args)
{
    const bool hasWidth = args.contains(QStringLiteral("width"));
    const bool hasHeight = args.contains(QStringLiteral("height"));
    if (!hasWidth && !hasHeight)
        return QStringLiteral("resize takes args {\"width\"} and/or {\"height\"} in pixels");
    if (hasWidth) {
        if (QString e = resizeAlong(w, Qt::Horizontal, std::max(0, args.value(QStringLiteral("width")).toInt()));
            !e.isEmpty())
            return e;
    }
    if (hasHeight)
        return resizeAlong(w, Qt::Vertical, std::max(0, args.value(QStringLiteral("height")).toInt()));
    return {};
}

} // namespace

QString perform(const Target &t, const QString &op, const QJsonObject &args)
{
    if (const ControlAdapter *a = ControlAdapters::instance().find(t.widget)) {
        if (auto r = a->perform(t.widget, t.part, op, args))
            return *r;
    }
    if (op == QLatin1String("resize") && t.part.isEmpty())
        return resizePane(t.widget, args);
    return unsupported(op, t.widget);
}

} // namespace control::ui
