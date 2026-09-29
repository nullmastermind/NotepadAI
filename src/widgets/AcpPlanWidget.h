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

#ifndef ACP_PLAN_WIDGET_H
#define ACP_PLAN_WIDGET_H

#include <QFrame>
#include <QList>
#include <QPointer>

#include "AcpProtocol.h"

class QLabel;
class QPushButton;
class QScrollBar;
class QTimer;
class QVBoxLayout;
class QWheelEvent;

class AcpPlanWidget : public QFrame
{
    Q_OBJECT

public:
    explicit AcpPlanWidget(QWidget *parent = nullptr);

    void setEntries(const QList<AcpProtocol::AcpPlanEntry> &entries);
    void setAgentIdle(bool idle);

signals:
    void resumeRequested(const QString &prompt);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void clearRows();
    void updateBadge();
    void updateResumeButton();
    void tickSpin();
    void updateListHeight();
    void scrollActiveIntoView();
    void applyListScroll(int startRow);
    void scrollByWheel(QWheelEvent *event);
    int windowHeight(int startRow) const;
    int indexToReveal(const QList<AcpProtocol::AcpPlanEntry> &next) const;

    QVBoxLayout *m_layout = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QWidget *m_listViewport = nullptr;
    QWidget *m_listHost = nullptr;
    QScrollBar *m_vbar = nullptr;
    QLabel *m_badge = nullptr;
    QPushButton *m_resumeBtn = nullptr;
    QTimer *m_spinTimer = nullptr;
    QPointer<QWidget> m_activeRow;
    QList<QWidget *> m_spinGlyphs;
    QList<AcpProtocol::AcpPlanEntry> m_entries;
    int m_spinAngle = 0;
    bool m_agentIdle = false;
    bool m_inRelayout = false;
    QList<int> m_rowHeights;
};

#endif // ACP_PLAN_WIDGET_H
