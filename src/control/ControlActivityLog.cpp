/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "ControlActivityLog.h"

#include <QBrush>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>

namespace control {

ControlActivityLog::ControlActivityLog(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ControlActivityLog::append(Entry entry)
{
    if (m_entries.size() < kCapacity) {
        const int row = m_count;
        beginInsertRows(QModelIndex(), row, row);
        m_entries.append(std::move(entry));
        ++m_count;
        endInsertRows();
        emit appended(m_entries.last());
        return;
    }
    // Full: drop the oldest row, then append into its slot; the ring keeps it O(1).
    beginRemoveRows(QModelIndex(), 0, 0);
    const int slot = m_head;
    m_head = (m_head + 1) % kCapacity;
    --m_count;
    endRemoveRows();
    beginInsertRows(QModelIndex(), m_count, m_count);
    m_entries[slot] = std::move(entry);
    ++m_count;
    endInsertRows();
    emit appended(m_entries.at(slot));
}

void ControlActivityLog::clear()
{
    beginResetModel();
    m_entries.clear();
    m_head = 0;
    m_count = 0;
    endResetModel();
}

QString ControlActivityLog::displayText(const Entry &e)
{
    QString text = e.time.toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + e.op
                   + QStringLiteral("  ") + (e.label.isEmpty() ? e.target : e.label);
    if (!e.args.isEmpty())
        text += QStringLiteral(" ← ") + argsText(e.args);
    if (!e.ok)
        text += QStringLiteral("  — ") + e.detail;
    return text;
}

QString ControlActivityLog::argsText(const QJsonObject &args)
{
    if (args.size() == 1) {
        const QJsonValue v = *args.constBegin();
        if (v.isString())
            return v.toString();
    }
    return QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
}

QJsonArray ControlActivityLog::toJson(int limit) const
{
    QJsonArray out;
    const int n = size();
    for (int i = qMax(0, n - limit); i < n; ++i) {
        const Entry &e = at(i);
        QJsonObject o{{QStringLiteral("time"), e.time.toString(Qt::ISODateWithMs)},
                      {QStringLiteral("op"), e.op},
                      {QStringLiteral("target"), e.target},
                      {QStringLiteral("ok"), e.ok}};
        if (!e.label.isEmpty())
            o.insert(QStringLiteral("label"), e.label);
        if (!e.detail.isEmpty())
            o.insert(QStringLiteral("detail"), e.detail);
        if (!e.args.isEmpty())
            o.insert(QStringLiteral("args"), e.args);
        out.append(o);
    }
    return out;
}

int ControlActivityLog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : size();
}

QVariant ControlActivityLog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= size())
        return {};
    const Entry &e = at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        return displayText(e);
    case Qt::ToolTipRole:
        return e.target + (e.detail.isEmpty() ? QString() : QStringLiteral("\n") + e.detail);
    case Qt::ForegroundRole:
        if (!e.ok)
            return QBrush(QGuiApplication::palette().color(QPalette::BrightText));
        return {};
    default:
        return {};
    }
}

} // namespace control
