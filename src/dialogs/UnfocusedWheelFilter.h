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


#ifndef UNFOCUSEDWHEELFILTER_H
#define UNFOCUSEDWHEELFILTER_H

#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QWidget>


class UnfocusedWheelFilter : public QObject
{
public:
    using QObject::QObject;

    static void installOnInputs(QWidget *root)
    {
        auto *filter = new UnfocusedWheelFilter(root);
        const auto widgets = root->findChildren<QWidget *>();
        for (QWidget *w : widgets)
            installOn(w, filter);
        installOn(root, filter);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel)
            return QObject::eventFilter(watched, event);

        auto *w = qobject_cast<QWidget *>(watched);
        if (!w || w->hasFocus())
            return QObject::eventFilter(watched, event);

        event->ignore();

        QWidget *p = w->parentWidget();
        while (p) {
            if (auto *sa = qobject_cast<QAbstractScrollArea *>(p)) {
                QCoreApplication::sendEvent(sa->viewport(), event);
                return true;
            }
            p = p->parentWidget();
        }
        return true;
    }

private:
    static void installOn(QWidget *w, UnfocusedWheelFilter *filter)
    {
        if (!w)
            return;
        if (qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractSpinBox *>(w)) {
            w->setFocusPolicy(Qt::StrongFocus);
            w->installEventFilter(filter);
        }
    }
};

#endif
