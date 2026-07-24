/*
 * This file is part of NotepadAI.
 * Copyright 2026 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <functional>
#include <utility>

// Coalesces all ways an embedded tab can become interactive (ADS tab raise,
// QWidget Show/FocusIn, native child click) into one focus restore per event-loop
// turn. QObject-context queued calls and QPointer guards make host destruction
// before delivery safe.
class EmbeddedWindowFocusRouter final : public QObject
{
public:
    explicit EmbeddedWindowFocusRouter(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    void registerHost(QWidget *host, std::function<void()> restore)
    {
        if (!host)
            return;
        m_entries.insert(host, {host, std::move(restore)});
        connect(host, &QObject::destroyed, this, [this, host]() {
            m_pending.remove(host);
            m_entries.remove(host);
        });
    }

    void unregisterHost(QWidget *host)
    {
        m_pending.remove(host);
        m_entries.remove(host);
    }

    void request(QWidget *widget)
    {
        auto it = m_entries.find(widget);
        if (it == m_entries.end() || !it->host || m_pending.contains(widget))
            return;
        m_pending.insert(widget);
        const QPointer<QWidget> guard = it->host;
        QMetaObject::invokeMethod(this, [this, widget, guard]() {
            m_pending.remove(widget);
            auto current = m_entries.find(widget);
            if (!guard || current == m_entries.end() || current->host != guard)
                return;
            current->restore();
        }, Qt::QueuedConnection);
    }

private:
    struct Entry {
        QPointer<QWidget> host;
        std::function<void()> restore;
    };

    QHash<QWidget *, Entry> m_entries;
    QSet<QWidget *> m_pending;
};
