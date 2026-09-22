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
#include <QString>
#include <QVector>

#include "AcpProtocol.h"

class QTextBrowser;
class QTimer;
class QToolButton;
class QLabel;
class QVBoxLayout;
class QShowEvent;

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

    // Apply the chat (Default Font) typeface explicitly. Required because this
    // bubble and its inner QTextBrowser both carry a stylesheet, and styled
    // widgets do NOT inherit a parent's setFont() — Qt re-resolves their font
    // from the application default. So the transcript host's font never reaches
    // the bubble body; we must push it down per-widget (QTextDocument default
    // font + user QLabels) here.
    void setChatFont(const QFont &font);

    bool isCollapsed() const { return m_collapsed; }
    QString role() const { return m_role; }
    QString plainText() const { return m_text; }

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
    void rerender();
    void refitBrowserHeight();
    void scheduleRefit();
    void applyCollapsed(bool collapsed);
    void applyGoalSetFrame();
    void scheduleRerender();
    void flushRerender();

    // Assistant code-block affordances. Code (fenced `<pre>`) blocks get a
    // distinct inset surface; a single reusable hover button copies the block
    // under the cursor. Built lazily on first assistant render.
    void ensureCopyButton();
    void rebuildCopyIcon();
    void scanCodeRegions();       // map fenced-code doc ranges + raw text
    void updateCopyButtonForPos(const QPoint &viewportPos);
    QString codeStyleSheet() const; // pre/code CSS with chat font + palette
    // Post-process the assistant document's code fragments: rewrite the
    // monospace family Qt bakes during setMarkdown to the chat family, loosen
    // line spacing inside code blocks, and add top/bottom block margins around
    // each fenced region so it separates from adjacent prose. Runs on the
    // document model (reliable) where QTextDocument's CSS subset is not.
    void styleCodeInDocument();
    // One fenced code region: document character range + its raw text.
    struct CodeRegion { int start; int end; QString text; };

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
    QToolButton *m_thoughtHeader = nullptr; // thought role
    QVBoxLayout *m_layout = nullptr;

    // Hover copy button for fenced code blocks (assistant role only). One
    // reusable button parented to the browser viewport, repositioned to the
    // top-right of the code region under the cursor. m_codeRegions is rebuilt
    // on every assistant render; m_hoverCodeIndex tracks which region the
    // button currently serves (-1 = hidden).
    QToolButton *m_copyCodeBtn = nullptr;
    QVector<CodeRegion> m_codeRegions;
    int m_hoverCodeIndex = -1;
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
