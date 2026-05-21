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

#ifndef TERMINALWIDGET_H
#define TERMINALWIDGET_H

#include "ScrollbackBuffer.h"
#include "TerminalCellSource.h"
#include "TerminalColorScheme.h"

#include <vterm.h>

#include <QAbstractScrollArea>
#include <QFont>
#include <QPoint>
#include <QRegion>
#include <QString>
#include <QVector>

class QPainter;

class  IPtyProcess;

class TerminalWidget : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    bool start(const QString &shell, const QString &cwd);
    void setColorScheme(const TerminalColorScheme &scheme);
    void setTerminalFont(const QFont &font);

    bool isProcessRunning() const;
    void killProcess();

    QString title() const { return m_title; }

signals:
    void titleChanged(const QString &title);
    void processExited(int exitCode);
    void spawnFailed(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private slots:
    void onPtyReadyRead();

private:
    void writeToPty(const QByteArray &data);
    void recomputeMetrics();
    void requestResizeToViewport();
    QPoint pixelToCell(const QPoint &p) const;
    QPoint pixelToAbsoluteCell(const QPoint &p) const;
    int viewportToAbsoluteRow(int viewportRow) const;
    int absoluteToViewportRow(int absoluteRow) const;
    QString selectionText() const;
    void copySelection();
    void pasteClipboard();

    void paintCell(QPainter &p, int viewportRow, int col, const VTermScreenCell &cell);
    void updateScrollbarRange();
    bool isAtBottom() const;

    static int vtermDamage(VTermRect rect, void *user);
    static int vtermMoveRect(VTermRect dest, VTermRect src, void *user);
    static int vtermMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int vtermSetTermProp(VTermProp prop, VTermValue *val, void *user);
    static int vtermBell(void *user);
    static int vtermResize(int rows, int cols, void *user);
    static int vtermSbPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int vtermSbPopLine(int cols, VTermScreenCell *cells, void *user);
    static void vtermOutput(const char *s, size_t len, void *user);

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    IPtyProcess *m_pty = nullptr;

    TerminalColorScheme m_scheme;
    QFont m_font;
    int m_cellW = 8;
    int m_cellH = 16;
    int m_cellAscent = 12;
    int m_cols = 80;
    int m_rows = 24;
    QPoint m_cursor{0, 0};
    bool m_cursorVisible = true;
    bool m_hasFocus = false;
    bool m_altScreen = false;
    QString m_title;
    QString m_errorMessage;

    bool m_selecting = false;
    QPoint m_selStart;
    QPoint m_selEnd;
    bool m_hasSelection = false;

    ScrollbackBuffer m_scrollback{5000};
    int m_scrollOffset = 0;

    QRegion m_pendingDamage;
    QVector<VTermScreenCell> m_rowScratch;
};

#endif
