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

#ifndef ACP_PROMPT_QUEUE_H
#define ACP_PROMPT_QUEUE_H

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>

#include <cstdint>
#include <optional>

class AcpPromptQueue
{
public:
    enum class SendKind : std::uint8_t {
        Ignore,
        SendNow,
        Enqueue,
        SidePrompt,
    };

    struct Item {
        QString text;
        QVector<QPair<QByteArray, QString>> images;
        // Flushed as a goal-agent message so the badge and judge handshake match
        // a side-prompt that was deferred onto this queue.
        bool fromGoalAgent = false;
    };

    static SendKind classifySend(bool processing, bool hasContent, bool nativeGoalSlash);

    void enqueue(Item item);
    std::optional<Item> dequeue();
    bool removeAt(int index);
    std::optional<Item> takeNextIfIdle(bool processing);
    int size() const;
    const QVector<Item> &items() const { return m_items; }

private:
    QVector<Item> m_items;
};

#endif // ACP_PROMPT_QUEUE_H
