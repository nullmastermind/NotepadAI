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
    QString m_status;
    int m_groupId = 0;
    QJsonArray m_content;
    QJsonObject m_rawInput;

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
