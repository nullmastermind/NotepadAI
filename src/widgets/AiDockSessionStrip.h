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

#ifndef AI_DOCK_SESSION_STRIP_H
#define AI_DOCK_SESSION_STRIP_H

#include <QString>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QToolButton;

// Clickable 1 2 3 session buttons for a project-grouped AI dock.
// The strip is a pure view: it never queries ACP. The owner pushes a
// snapshot and reacts to activated/closeRequested.
class AiDockSessionStrip : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kSpacingPx = 4;

    struct SlotSnapshot
    {
        QString agent;
        bool busy = false;
        qint64 busySinceMs = 0; // 0 = idle; monotonic ms, italic busy-non-current
        bool activity = false;
        QString lastUserPreview; // truncated last human prompt; empty = no tooltip
    };

    explicit AiDockSessionStrip(QWidget *parent = nullptr);

    void setSlots(int count, int current, const QVector<SlotSnapshot> &snapshots);

    int slotCount() const { return m_count; }
    int currentIndex() const { return m_current; }
    QString slotTooltip(int index) const { return tooltipFor(index); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activated(int index);
    void closeRequested(int index);

protected:
    void changeEvent(QEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildButtons();
    void restyleButtons();
    void syncIntrinsicSize();
    void notifyHostTabBar();
    QString tooltipFor(int index) const;
    int indexOfButton(const QObject *obj) const;

    QHBoxLayout *m_layout = nullptr;
    QVector<QToolButton *> m_buttons;
    QVector<SlotSnapshot> m_snapshots;
    int m_count = 0;
    int m_current = 0;
};

#endif // AI_DOCK_SESSION_STRIP_H
