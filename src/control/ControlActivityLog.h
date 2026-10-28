/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef CONTROL_ACTIVITY_LOG_H
#define CONTROL_ACTIVITY_LOG_H

#include <QAbstractListModel>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace control {

// What external agents did, newest last; a fixed ring so a chatty agent
// costs O(1) per entry and bounded memory.
class ControlActivityLog : public QAbstractListModel
{
    Q_OBJECT

public:
    static constexpr int kCapacity = 512;

    struct Entry
    {
        QDateTime time;
        QString op;     // "trigger", "click", "type", ...
        QString target; // id
        QString label;  // what the user sees ("AI ▸ AI Agents…")
        bool ok = true;
        QString detail;   // error or extra
        QJsonObject args; // the request minus its id: value, text, op + args, ...
    };

    explicit ControlActivityLog(QObject *parent = nullptr);

    void append(Entry entry);
    void clear();
    int size() const { return m_count; }
    const Entry &at(int i) const { return m_entries.at((m_head + i) % m_entries.size()); }
    // Newest `limit` entries, oldest first.
    QJsonArray toJson(int limit = kCapacity) const;
    static QString displayText(const Entry &entry);
    // The arguments as one short line: a lone text/value/keys as itself, else compact JSON.
    static QString argsText(const QJsonObject &args);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

signals:
    void appended(const control::ControlActivityLog::Entry &entry);

private:
    QVector<Entry> m_entries;
    int m_head = 0;  // index of the oldest entry once full
    int m_count = 0; // rows the model reports
};

} // namespace control

#endif // CONTROL_ACTIVITY_LOG_H
