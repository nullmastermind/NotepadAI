/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#ifndef ACP_MESSAGE_WIDGET_H
#define ACP_MESSAGE_WIDGET_H

#include <QFrame>
#include <QString>
#include <QVector>

#include "AcpProtocol.h"

class QTextBrowser;
class QToolButton;
class QLabel;
class QVBoxLayout;

// One transcript row representing either a user/assistant/thought/system
// message. Assistant messages render markdown via QTextDocument::setMarkdown;
// user messages render as plain text in a styled frame; thought messages
// render as collapsible italic plain text.
class AcpMessageWidget : public QFrame
{
    Q_OBJECT

public:
    AcpMessageWidget(QString role, QWidget *parent = nullptr);

    // Append a streamed chunk to the message body and re-render.
    void appendChunk(const QString &chunk);

    // Replace the body with the joined text of `content` blocks. Used when
    // hydrating from history. Images are inserted as `[image]` placeholders.
    void setContent(const QVector<AcpProtocol::AcpContentBlock> &content);

    // Mark a streaming thought as finished. Collapses the frame for the
    // "thought" role; no-op for other roles.
    void markStreamingDone();

    bool isCollapsed() const { return m_collapsed; }
    QString role() const { return m_role; }
    QString plainText() const { return m_text; }

private:
    void rerender();
    void applyCollapsed(bool collapsed);

    QString m_role;
    QString m_text;
    bool m_collapsed = false;

    QTextBrowser *m_browser = nullptr;     // assistant + non-thought rendered widgets
    QLabel *m_userLabel = nullptr;         // user role
    QToolButton *m_thoughtHeader = nullptr; // thought role
    QVBoxLayout *m_layout = nullptr;
};

#endif // ACP_MESSAGE_WIDGET_H
