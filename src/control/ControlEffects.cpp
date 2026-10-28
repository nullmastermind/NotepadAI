/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlEffects.h"

#include "ControlIds.h"

#include <QApplication>
#include <QDialog>
#include <QDockWidget>
#include <QJsonArray>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>

#include <memory>

namespace control {

namespace {

// How long a command that closed its dialog waits for the code after that
// dialog's exec() to finish.
constexpr qint64 kAfterModalMs = 2000;

} // namespace

QString rootKind(const QWidget *root)
{
    if (root->inherits("ads::CDockWidget"))
        return QStringLiteral("dock");
    if (qobject_cast<const QMenu *>(root))
        return QStringLiteral("menu");
    if (qobject_cast<const QDialog *>(root))
        return QStringLiteral("dialog");
    if (root->isWindow())
        return QStringLiteral("window");
    // A marked root says what it is: "chat:<session>" is a chat.
    const QString marked = root->property(ids::kRootProperty).toString();
    if (const qsizetype colon = marked.indexOf(QLatin1Char(':')); colon > 0)
        return marked.left(colon);
    return QStringLiteral("panel");
}

QString rootTitle(const QWidget *root)
{
    if (const auto *menu = qobject_cast<const QMenu *>(root))
        return menu->title();
    // The title as shown: Qt draws "[*]" as "*" while modified, else not at all.
    for (const QWidget *w = root; w; w = w->parentWidget()) {
        QString title = w->windowTitle();
        if (!title.isEmpty()) {
            title.replace(QLatin1String("[*]"), w->isWindowModified() ? QStringLiteral("*") : QString());
            return title;
        }
        // An untitled marked root (a chat) takes its dock's title.
        if (w->isWindow() || qobject_cast<const QDockWidget *>(w) || w->inherits("ads::CDockWidget"))
            break;
    }
    return {};
}

ControlSnapshot ControlSnapshot::take(const Presence &presence)
{
    ControlSnapshot s;
    for (const auto &entry : ids::rootTable()) {
        QWidget *w = entry.first;
        const bool present = presence ? presence(w) : w->isVisible();
        if (!present)
            continue;
        s.m_entries.append({w, entry.second, rootKind(w), rootTitle(w)});
        // A main window's panels (the workspace tree) are its direct children.
        if (qobject_cast<const QMainWindow *>(w)) {
            for (QDockWidget *dock : w->findChildren<QDockWidget *>(Qt::FindDirectChildrenOnly)) {
                if (dock->isVisible())
                    s.m_entries.append({dock, ids::nodeId(dock).id, QStringLiteral("panel"), dock->windowTitle()});
            }
        }
    }
    return s;
}

QJsonObject ControlSnapshot::diff(const ControlSnapshot &after) const
{
    auto contains = [](const QList<Entry> &list, const QWidget *w) {
        for (const Entry &e : list) {
            if (e.widget == w)
                return true;
        }
        return false;
    };
    auto toJson = [](const Entry &e) {
        return QJsonObject{{QStringLiteral("id"), e.id}, {QStringLiteral("kind"), e.kind},
                           {QStringLiteral("title"), e.title}};
    };
    QJsonArray opened;
    QJsonArray closed;
    for (const Entry &e : after.m_entries) {
        if (!contains(m_entries, e.widget))
            opened.append(toJson(e));
    }
    for (const Entry &e : m_entries) {
        if (!e.widget || !contains(after.m_entries, e.widget))
            closed.append(toJson(e));
    }

    QJsonObject out{{QStringLiteral("opened"), opened}, {QStringLiteral("closed"), closed}};
    if (QWidget *focus = QApplication::focusWidget(); focus && !ids::isInsideIgnored(focus))
        out.insert(QStringLiteral("focus"), ids::nodeId(focus).id);
    if (QWidget *modal = QApplication::activeModalWidget())
        out.insert(QStringLiteral("modal"), ids::nodeId(modal).id);
    if (QWidget *popup = QApplication::activePopupWidget())
        out.insert(QStringLiteral("popup"), ids::nodeId(popup).id);
    return out;
}

void runUntilSettled(QObject *context, std::function<void()> act, std::function<void(bool)> done)
{
    auto answered = std::make_shared<bool>(false);
    auto finish = [answered, done = std::move(done)](bool finished) {
        if (*answered)
            return;
        *answered = true;
        done(finished);
    };
    QTimer::singleShot(0, context, [context, act = std::move(act), finish]() mutable {
        // Fires only inside act()'s nested event loops (5 ms apart, only while
        // act() runs): a dialog or menu that act() opened and that is now
        // waiting for input settles the command.
        QWidget *const modalBefore = QApplication::activeModalWidget();
        QWidget *const popupBefore = QApplication::activePopupWidget();
        auto *probe = new QTimer(context);
        probe->setInterval(5);
        QObject::connect(probe, &QTimer::timeout, probe, [probe, finish, modalBefore, popupBefore]() mutable {
            QWidget *modal = QApplication::activeModalWidget();
            QWidget *popup = QApplication::activePopupWidget();
            if ((modal && modal != modalBefore) || (popup && popup != popupBefore)) {
                probe->stop();
                finish(false);
            }
        });
        probe->start();
        const QPointer<QTimer> guard(probe);
        const QPointer<QWidget> closing(modalBefore);
        const int level = QThread::currentThread()->loopLevel();
        act();
        delete guard.data();
        // act() closed the dialog it ran inside (its OK button): what the
        // dialog was for (open the tab, save the settings) runs after that
        // dialog's exec() returns, below this event loop. Answer once the loop
        // is gone, so effects show it; a new dialog or menu still settles it.
        QWidget *const modalAfter = QApplication::activeModalWidget();
        const bool closedItsDialog = modalBefore && (!closing || closing != modalAfter);
        if (!closedItsDialog) {
            finish(true);
            return;
        }
        auto *after = new QTimer(context);
        after->setInterval(5);
        QElapsedTimer waited;
        waited.start();
        QObject::connect(after, &QTimer::timeout, after, [after, finish, level, waited, modalAfter]() mutable {
            QWidget *modal = QApplication::activeModalWidget();
            QWidget *popup = QApplication::activePopupWidget();
            const bool interrupted = (modal && modal != modalAfter) || popup;
            if (interrupted || QThread::currentThread()->loopLevel() < level || waited.elapsed() > kAfterModalMs) {
                after->deleteLater();
                after->stop();
                finish(!interrupted);
            }
        });
        after->start();
    });
}

} // namespace control
