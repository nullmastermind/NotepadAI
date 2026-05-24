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

#ifndef ACP_MESSAGE_WIDGET_H
#define ACP_MESSAGE_WIDGET_H

#include <QFrame>
#include <QPixmap>
#include <QString>
#include <QVector>

#include "AcpProtocol.h"

class QTextBrowser;
class QTimer;
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
    Q_PROPERTY(bool goalAgent READ isFromGoalAgent)

public:
    AcpMessageWidget(QString role, QWidget *parent = nullptr);

    // Append a streamed chunk to the message body and re-render.
    void appendChunk(const QString &chunk);

    // Replace the body text wholesale and re-render. Used when the model
    // rewrites a streaming message in-place (e.g. compaction transition where
    // "Compacting..." becomes "Compacting completed." rather than appending).
    void setText(const QString &fullText);

    // Replace the body with the joined text of `content` blocks. Used when
    // hydrating from history. Images are inserted as `[image]` placeholders.
    void setContent(const QVector<AcpProtocol::AcpContentBlock> &content);

    // Mark a streaming thought as finished. Collapses the frame for the
    // "thought" role; no-op for other roles.
    void markStreamingDone();

    void setFromGoalAgent(bool goal);
    bool isFromGoalAgent() const { return m_fromGoalAgent; }

    bool isCollapsed() const { return m_collapsed; }
    QString role() const { return m_role; }
    QString plainText() const { return m_text; }

protected:
    void resizeEvent(QResizeEvent *event) override;
    // Bubble height is derived from QTextDocument::size() under the current
    // font metrics; when the parent's font changes (Default Font preference)
    // we need to re-fit, because Qt does not auto-relayout content widgets on
    // font inheritance alone.
    void changeEvent(QEvent *event) override;

private:
    void rerender();
    void refitBrowserHeight();
    void applyCollapsed(bool collapsed);
    void scheduleRerender();
    void flushRerender();

    QString m_role;
    QString m_text;
    bool m_collapsed = false;
    bool m_fromGoalAgent = false;

    QTextBrowser *m_browser = nullptr;     // assistant + non-thought rendered widgets
    QToolButton *m_thoughtHeader = nullptr; // thought role
    QVBoxLayout *m_layout = nullptr;
    // Debounce timer for assistant markdown re-renders. setMarkdown on a long
    // table-bearing payload is O(N) per call; without debouncing, every
    // streamed chunk re-parses the whole document and the UI thread stalls.
    QTimer *m_rerenderTimer = nullptr;

    // User role: image thumbnails kept alongside their original pixmap so we
    // can rescale to bubble width on resize without quality loss.
    struct UserImage { QLabel *label; QPixmap original; };
    QVector<UserImage> m_userImages;
    QVector<QWidget *> m_userBlocks; // text labels + image labels, in block order

    void clearUserBlocks();
    void rescaleUserImages();
};

#endif // ACP_MESSAGE_WIDGET_H
