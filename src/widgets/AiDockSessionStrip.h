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

#include <QColor>
#include <QPoint>
#include <QString>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QHideEvent;
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
        qint64 busySinceMs = 0; // 0 = idle; monotonic ms
        bool activity = false;
        // The agent is blocked on the user (question form or permission
        // request): the number wears a red dot until it is answered.
        bool waiting = false;
        QString lastUserPreview; // truncated last human prompt; empty = no tooltip
        QString name;            // the session's name (Rename…); empty = none

        bool operator==(const SlotSnapshot &) const = default;
    };

    // The one red used for "needs you" (documented in ui-dna.md): readable on
    // light and dark. Shared with the project tab's dock icon.
    static QColor attentionColor();

    explicit AiDockSessionStrip(QWidget *parent = nullptr);

    void setSlots(int count, int current, const QVector<SlotSnapshot> &snapshots);

    int slotCount() const { return m_count; }
    int currentIndex() const { return m_current; }
    QString slotTooltip(int index) const { return tooltipFor(index); }
    // The session is working: its number sits on a static Highlight fill.
    bool busyAt(int index) const;
    bool waitingAt(int index) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activated(int index);
    void closeRequested(int index);
    // A drag released over another number: move session `from` to position `to`.
    void moveRequested(int from, int to);
    // Right-click on session `index` (the owner shows the menu: Rename…).
    void contextMenuRequested(int index, const QPoint &globalPos);

protected:
    void changeEvent(QEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void rebuildButtons();
    void restyleButtons();
    void syncIntrinsicSize();
    void notifyHostTabBar();
    void detachFromHostTabBar();
    QString tooltipFor(int index) const;
    int indexOfButton(const QObject *obj) const;
    // Drag-to-reorder: the dragged button slides through the layout while the
    // cursor moves; the owner is only asked to move the session on release.
    bool handleDrag(int index, QEvent *event);
    int dropPositionAt(int stripX) const;
    void moveDraggedButton(int position);
    void endDrag();

    QHBoxLayout *m_layout = nullptr;
    QVector<QToolButton *> m_buttons;
    QVector<SlotSnapshot> m_snapshots;
    int m_count = 0;
    int m_current = 0;

    int m_pressIndex = -1;
    QPoint m_pressGlobalPos;
    bool m_dragging = false;
    int m_dragPosition = -1; // where the dragged button sits in the layout now
};

#endif // AI_DOCK_SESSION_STRIP_H
