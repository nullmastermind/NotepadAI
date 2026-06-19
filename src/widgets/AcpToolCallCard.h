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

#ifndef ACP_TOOL_CALL_CARD_H
#define ACP_TOOL_CALL_CARD_H

#include <QFont>
#include <QFrame>
#include <QJsonObject>
#include <QString>

#include "AcpProtocol.h"

class QLabel;
class QTextBrowser;
class QToolButton;
class QVBoxLayout;

class AcpToolCallCard : public QFrame
{
    Q_OBJECT

public:
    explicit AcpToolCallCard(const AcpProtocol::AcpToolCall &initial, QWidget *parent = nullptr);

    void apply(const AcpProtocol::AcpToolCallUpdate &update);

    QString id() const { return m_id; }
    QString status() const { return m_status; }
    int groupId() const { return m_groupId; }

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }
    bool shouldPreserveExpanded() const { return m_userToggled || m_autoExpandedForDiff; }

    // Apply the chat (Default Font) typeface explicitly. Required because this
    // card and its inner QTextBrowser both carry a stylesheet, and styled
    // widgets do NOT inherit a parent's setFont() — Qt re-resolves their font
    // from the application default. So the transcript host's font never reaches
    // the card; we push it down per-widget (title/status QLabels + the body
    // QTextBrowser document font) here, and thread its family/size into the
    // hardcoded HTML the body renders.
    void setChatFont(const QFont &font);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void refreshHeader();
    void rerenderBody();
    void refitBodyHeight();
    void scheduleRefit();
    void maybeAutoExpandForDiff();
    bool hasDiffContent() const;
    QString statusGlyph() const;
    QString computeEnrichedTitle() const;

    QString m_id;
    QString m_title;
    QString m_kind;
    QString m_status;
    int m_groupId = 0;
    QJsonArray m_content;
    QJsonObject m_rawInput;
    QJsonObject m_rawOutput;

    // Chat (Default Font) typeface pushed in via setChatFont(). Body HTML is
    // re-rendered with this font's family/size; default-constructed (and unused
    // for HTML) until the first setChatFont() call.
    QFont m_chatFont;
    bool m_chatFontSet = false;

    bool m_collapsed = false;
    bool m_userToggled = false;          // user has explicitly clicked the chevron
    bool m_autoExpandedForDiff = false;  // we've already auto-expanded once for diff content
    bool m_refitScheduled = false;       // a deferred refit is already queued

    QLabel *m_statusIcon = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_expandBtn = nullptr;
    QTextBrowser *m_body = nullptr;
    QVBoxLayout *m_outer = nullptr;
};

#endif // ACP_TOOL_CALL_CARD_H
