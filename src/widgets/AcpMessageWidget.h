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

#include <QFont>
#include <QFrame>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QVector>

#include "AcpProtocol.h"

class QTextBrowser;
class MarkdownView;
class QTimer;
class QToolButton;
class QLabel;
class QVBoxLayout;
class QShowEvent;
class QUrl;

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
    // King-gold frame without the Goal badge — live judge streaming.
    void setGoalTint(bool on);
    bool isFromGoalAgent() const { return m_fromGoalAgent; }

    // Apply the chat (Default Font) typeface explicitly. Required because this
    // bubble and its inner QTextBrowser both carry a stylesheet, and styled
    // widgets do NOT inherit a parent's setFont() — Qt re-resolves their font
    // from the application default. So the transcript host's font never reaches
    // the bubble body; we must push it down per-widget (QTextDocument default
    // font + user QLabels) here.
    void setChatFont(const QFont &font);

    // Assistant role, a turn's final report: a row of "Copy response" / "Copy
    // conversation" buttons at the bottom of the bubble, always visible.
    void setResponseActions(bool enabled);

    // User role: show a clickable `summary` line plus one `overview` line per
    // answer instead of the content (set after setContent); clicking toggles
    // the full text. Used for grill_me answer prompts.
    void setCollapsedSummary(const QString &summary, const QStringList &overview = {});

    bool isCollapsed() const { return m_collapsed; }
    QString role() const { return m_role; }
    QString plainText() const { return m_text; }

    // User bubbles hug their text (ChatGPT-style): the transcript layout
    // right-aligns the widget, so this width — widest line, capped to the
    // transcript — is what it gets. Other roles keep the layout default.
    QSize sizeHint() const override;

signals:
    // "Copy conversation" clicked: the view owns the timeline, so it builds the
    // transcript through this message and puts it on the clipboard.
    void copyConversationRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    // QStackedWidget (session tabs) hides inactive pages without a size change,
    // so resizeEvent does not re-run on tab switch. Re-fit on show so a thought
    // that streamed in the background keeps its body height instead of staying
    // pinned to the header.
    void showEvent(QShowEvent *event) override;
    // Bubble height is derived from QTextDocument::size() under the current
    // font metrics; when the parent's font changes (Default Font preference)
    // we need to re-fit, because Qt does not auto-relayout content widgets on
    // font inheritance alone.
    void changeEvent(QEvent *event) override;
    // Watches the assistant browser's viewport to reveal/position the per-code-
    // block copy button on hover and hide it on leave.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // The width sizeHint() reports for a user bubble: its widest line, capped
    // to the transcript.
    int userBubbleWidth() const;
    // Widest unwrapped line / image of a user bubble (text shaping: cached by
    // its layout until a child changes).
    int measureUserContent() const;
    void rerender();
    void refitBrowserHeight();
    void scheduleRefit();
    void applyCollapsed(bool collapsed);
    void applyGoalSetFrame();
    void scheduleRerender();
    void flushRerender();
    void ensureResponseActions();
    void retintResponseActionIcons();

    QString m_role;
    QString m_text;
    bool m_collapsed = false;
    bool m_fromGoalAgent = false;
    bool m_refitScheduled = false; // coalesced deferred height pass already queued

    // Chat (Default Font) typeface, pushed in via setChatFont(). Held so that
    // content created/re-rendered after the initial setFont() (streamed chunks,
    // lazily-built user text blocks) is stamped with the same font. Default-
    // constructed until the first setChatFont() call.
    QFont m_chatFont;
    bool m_chatFontSet = false;

    QTextBrowser *m_browser = nullptr;     // assistant + non-thought rendered widgets
    MarkdownView *m_markdown = nullptr;    // == m_browser for the assistant role
    QToolButton *m_thoughtHeader = nullptr; // thought role
    QToolButton *m_summaryHeader = nullptr; // user role, collapsed summary
    QLabel *m_summaryOverview = nullptr;    // one line per answer while collapsed
    QString m_summary;
    QLabel *m_goalBadge = nullptr;         // Goal badge on result rows
    QVBoxLayout *m_layout = nullptr; // user role: UserBubbleLayout

    // Final-report action row (assistant role), built on first enable.
    QWidget *m_responseActions = nullptr;
    QToolButton *m_copyResponseBtn = nullptr;
    QToolButton *m_copyConversationBtn = nullptr;
    bool m_responseActionsEnabled = false;

    // Debounce timer for assistant markdown re-renders. setMarkdown on a long
    // table-bearing payload is O(N) per call; without debouncing, every
    // streamed chunk re-parses the whole document and the UI thread stalls.
    QTimer *m_rerenderTimer = nullptr;

    // Plain-text roles (thought, system): m_text.left(m_plainShown) is what the
    // document holds. While m_text only grows (appendChunk), a re-render inserts
    // just the new tail, so a streaming tick costs O(chunk), not O(length): a
    // full rebuild per 80 ms tick starved the UI thread on long thoughts.
    // m_plainRebuild forces setPlainText (first render, setText/setContent).
    qsizetype m_plainShown = 0;
    bool m_plainRebuild = true;

    // Thought role: body capped at kThoughtMaxLines with its own scrollbar;
    // m_thoughtFollow keeps it at the bottom while the reader is there.
    bool m_thoughtCapped = false;
    bool m_thoughtFollow = true;
    bool m_streamingDone = false;

    // User role: image thumbnails kept alongside their original pixmap so we
    // can rescale to bubble width on resize without quality loss.
    struct UserImage { QLabel *label; QPixmap original; };
    QVector<UserImage> m_userImages;
    QVector<QWidget *> m_userBlocks; // text labels + image labels, in block order

    void clearUserBlocks();
    void rescaleUserImages();
};

#endif // ACP_MESSAGE_WIDGET_H
