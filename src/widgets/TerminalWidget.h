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

#ifndef TERMINALWIDGET_H
#define TERMINALWIDGET_H

#include "ScrollbackBuffer.h"
#include "TerminalCellSource.h"
#include "TerminalColorScheme.h"
#include "TerminalContrast.h"
#include "TerminalGlyphCache.h"
#include "TerminalRowLayout.h"
#include "TerminalSearch.h"
#include "TerminalSearchSession.h"
#include "TerminalTaskLauncher.h"

#include <vterm.h>

#include <QAbstractScrollArea>
#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QPoint>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QFutureWatcher>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

class QMenu;
class QPainter;
class TerminalScrollBar;
class TerminalSearchBar;

class  IPtyProcess;
namespace remote { class ExecutionContext; }

enum class TerminalCursorShape : uint8_t { Block, Bar, Underline };
// Terminal: blink only when the program asks for it (DECSCUSR).
enum class TerminalCursorBlink : uint8_t { Terminal, On, Off };

// Everything the user can tune about how a terminal looks and behaves; built
// from settings by TerminalManager and applied live.
struct TerminalAppearance
{
    TerminalColorScheme scheme = TerminalColorScheme::darkScheme();
    QFont font;
    qreal lineHeight = 1.2;
    int minimumContrast = 45;
    TerminalCursorShape cursorShape = TerminalCursorShape::Block;
    TerminalCursorBlink cursorBlink = TerminalCursorBlink::Terminal;
    int scrollbackLines = 10000;
    bool copyOnSelect = false;
    bool bellSound = false;
};

class TerminalWidget : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    // `ctx` selects the PTY backend: null/local context → the existing local
    // PtyQt backend (hot path unchanged); a remote context → an SSH-backed PTY.
    // The ONLY behavioral change for local is one virtual call at construction.
    bool start(const QString &shell, const QString &cwd, const QStringList &env = {},
               remote::ExecutionContext *ctx = nullptr);
    void injectOutput(const QByteArray &data);
    void setColorScheme(const TerminalColorScheme &scheme);
    void setTerminalFont(const QFont &font);
    // Row height as a multiple of the font height (1.0 = tight).
    void setLineHeight(qreal factor);
    // Commit ceiling for a local shell's process tree, applied by start().
    // 0 = uncapped. Ignored for remote PTYs.
    void setChildMemoryLimitGb(int gb);
    void setAppearance(const TerminalAppearance &appearance);
    // Shell arguments for start() (see IPtyProcess::setArguments); empty =
    // interactive shell.
    void setLaunchArguments(const QStringList &arguments) { m_launchArguments = arguments; }
    // Working directory last reported by the shell (OSC 7); empty if never.
    QString reportedCwd() const { return m_reportedCwd; }
    // Drops the scrollback (the live screen is the shell's to clear).
    void clearScrollback();
    // Shows the find bar (Ctrl+Shift+F).
    void openSearch();
    void setScrollbackLines(int lines);
    const QFont &terminalFont() const { return m_font; }
    // How dropped paths are quoted (the shell this terminal runs).
    void setShellKind(TerminalShellKind kind) { m_shellKind = kind; }
    // Directories relative paths in the output resolve against, after the
    // shell's own OSC 7 cwd. Local path links are off for remote shells.
    void setLinkBaseDirs(const QStringList &dirs) { m_linkBaseDirs = dirs; }
    void setLocalPathLinks(bool enabled) { m_localPathLinks = enabled; }
    void selectAll();
    // Types `paths` as shell-quoted words (drag and drop of files or tabs).
    void pasteDroppedPaths(const QStringList &paths);

    bool isProcessRunning() const;
    qint64 ptyPid() const;

    void killProcess();
    void writeToPty(const QByteArray &data);

    QString title() const { return m_title; }
    bool hasExited() const { return m_exited; }
    bool hasSelection() const { return m_hasSelection; }
    QString selectedText() const;
    // Rows [from, from + count) of the scrollback followed by the live screen,
    // trailing blanks trimmed (the Control API's read). `total`: every row;
    // `history`: scrollback rows, so the live screen starts there.
    QStringList textRows(int from, int count, int *total = nullptr, int *history = nullptr) const;
    // Cursor cell on the live screen (x = column, y = row).
    QPoint cursorCell() const { return m_cursor; }
    int screenRows() const { return m_rows; }

signals:
    void titleChanged(const QString &title);
    void processExited(int exitCode);
    void spawnFailed(const QString &message);
    void firstOutputReceived();
    // Context menu hooks: after the Find/Clear section (owner adds Send to AI)
    // and at the end (owner adds Rename / New Terminal / Close).
    void contextMenuAboutToShow(QMenu *menu);
    void contextMenuTailAboutToShow(QMenu *menu);
    // BEL arrived; the owner marks the tab (the sound is optional).
    void bellRung();
    // Ctrl+wheel / Ctrl+=/-: change the shared terminal font size by `steps`;
    // 0 resets it.
    void zoomRequested(int steps);
    // A wrapped task reported its exit code (TerminalTaskLauncher OSC).
    void taskExited(int exitCode);
    // The shell reported a new working directory (OSC 7).
    void cwdChanged(const QString &cwd);
    // Ctrl+click on a file path (line/column 0 when absent).
    void openFileRequested(const QString &path, int line, int column);
    // Ctrl+click on a directory: show it in the workspace tree.
    void revealPathRequested(const QString &path);
    // Ctrl+Shift+L or the context menu's Clear.
    void clearRequested();
    // Ctrl+Shift+A or the context menu's Send to AI.
    void sendToAiRequested();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void onPtyReadyRead();
    void onPtyFinished(int exitCode);
    void processPendingInput();
    void flushFrame();
    void applyPendingResize();
    void toggleCursorBlink();
    void runSearch();
    void launchHistoryScan();
    void onHistoryScanned();

private:
    // The rows as TerminalSearchSession reads them.
    class SearchSource;
    void searchChanged();
    // Geometry. Cell sizes are whole device pixels so every row and column
    // edge lands on a pixel boundary (no seams, no glyph jitter).
    void recomputeMetrics();
    QSize gridSizeFor(const QSize &viewportSize) const;
    qreal originY() const;
    qreal columnX(int col) const { return m_padX + col * m_cellW; }
    qreal rowTop(int viewportRow) const { return originY() + viewportRow * m_cellH; }
    QRectF cursorRect() const;

    void requestResizeToViewport();
    QPoint pixelToCell(const QPoint &p) const;
    QPoint pixelToAbsoluteCell(const QPoint &p) const;
    int viewportToAbsoluteRow(int viewportRow) const;
    int absoluteToViewportRow(int absoluteRow) const;
    QString selectionText() const;
    void copySelection();
    void pasteClipboard();
    bool isMouseReportingActive(Qt::KeyboardModifiers mods) const;
    VTermModifier qtModsToVterm(Qt::KeyboardModifiers mods) const;
    void selectWord(const QPoint &cellPos);
    void selectLine(int absoluteRow);
    void clearSelection();
    bool terminalCtrl(Qt::KeyboardModifiers mods) const;
    bool appCommand(Qt::KeyboardModifiers mods) const;
    bool handleTerminalShortcut(QKeyEvent *event);
    void pasteText(const QString &text);
    qint64 lineIdForAbsoluteRow(int absoluteRow) const { return m_linesDropped + absoluteRow; }
    int absoluteRowForLineId(qint64 id) const { return static_cast<int>(id - m_linesDropped); }
    void noteLinesDropped(int count);
    void shiftSelectionForEviction(int evicted);
    QStringList pathCandidates(const QString &written) const;
    void openUrlTarget(const QString &target);
    QString textBetween(qint64 startLine, int startCol, qint64 endLine, int endCol) const;

    // Links (Ctrl+hover / Ctrl+click).
    struct HoverLink {
        bool valid = false;
        bool url = false;
        qint64 startLine = 0;   // absolute line ids, inclusive
        int startCol = 0;
        qint64 endLine = 0;
        int endCol = 0;         // exclusive on endLine
        QString target;         // URL or resolved path
        int line = 0;
        int column = 0;
    };
    struct Osc8Span {
        qint64 startLine;
        int startCol;
        qint64 endLine;
        int endCol;
        QString uri;
        QString text;       // what the span covered; differs once overwritten
        bool dead = false;
        // Output reached its rows since `text` was last compared.
        bool stale = false;
    };
    // The text is compared again only when output touched the span's rows
    // (markOsc8Stale), never per paint.
    bool osc8SpanAlive(Osc8Span &span) const;
    void markOsc8Stale(int firstLiveRow, int endLiveRow);
    // The live OSC 8 hyperlink at a cell, if any.
    const Osc8Span *osc8SpanAt(qint64 lineId, int col);
    void updateHoverLink(const QPoint &pixel, Qt::KeyboardModifiers mods);
    void setHoverLink(const HoverLink &link);
    void clearHoverLink();
    bool openHoverLink();
    bool logicalLineAt(int absoluteRow, QString *text, std::vector<qint64> *lineOf,
                       std::vector<int> *colOf) const;
    void markLineIdsDirty(qint64 from, qint64 to);

    // Search.
    void ensureSearchBar();
    void positionSearchBar();
    void closeSearch();
    void stepSearch(int delta);
    void revealMatch(const TerminalSearchMatch &match);
    void searchNewOutput();
    void syncSearchMarks();
    int collectHighlights(qint64 lineId);
    void scrollToBottom();
    void setScrollOffset(int offset);
    void applyCursorStyleToState();
    bool cursorBlinkEnabled() const;
    void updateBlinkTimer();
    void restartBlink();

    // Damage is tracked per viewport row and flushed at most once per frame.
    void markLiveRowsDirty(int startRow, int endRow);
    void markViewportRowDirty(int viewportRow);
    void markAllDirty();
    void scheduleFrame();
    void syncScrollbar();

    bool fetchViewportRow(int viewportRow, VTermScreenCell *out);
    void paintRow(QPainter &p, QPainter &devicePainter, const TerminalGlyphCache::Frame &frame, int viewportRow);
    // The window's own pixels for this paint, when writing them directly is
    // provably safe (see paintEvent); else false and m_frame is used.
    bool directPaintTarget(QPainter &p, const QRegion &region, const QSize &deviceSize, QImage *target) const;
    void paintDecorations(QPainter &p, qreal y);
    void paintCursor(QPainter &p);
    void paintPreedit(QPainter &p);

    static int vtermDamage(VTermRect rect, void *user);
    static int vtermMoveRect(VTermRect dest, VTermRect src, void *user);
    static int vtermMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int vtermSetTermProp(VTermProp prop, VTermValue *val, void *user);
    static int vtermBell(void *user);
    static int vtermResize(int rows, int cols, void *user);
    static int vtermSbPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int vtermSbPopLine(int cols, VTermScreenCell *cells, void *user);
    static int vtermSbClear(void *user);
    static void vtermOutput(const char *s, size_t len, void *user);
    static int vtermOsc(int command, VTermStringFragment frag, void *user);
    void handleOsc(int command, const QByteArray &payload);

    VTerm *m_vt = nullptr;
    VTermScreen *m_screen = nullptr;
    IPtyProcess *m_pty = nullptr;

    TerminalColorScheme m_scheme;
    TerminalPalette m_palette;
    QFont m_font;
    TerminalGlyphCache m_glyphs;

    qreal m_lineHeight = 1.2;
    qreal m_padX = 8;
    qreal m_padY = 4;
    qreal m_dpr = 1;
    qreal m_cellW = 8;
    qreal m_cellH = 16;
    qreal m_baseline = 12;
    qreal m_lineWidth = 1;
    qreal m_underlineY = 14;
    qreal m_strikeY = 8;
    int m_frameIntervalMs = 16;
    int m_childMemoryLimitGb = 0;
    QStringList m_launchArguments;
    QString m_reportedCwd;
    TerminalShellKind m_shellKind = TerminalShellKind::Other;
    QStringList m_linkBaseDirs;
    bool m_localPathLinks = true;
    // Every line that ever left the scrollback; absolute line ids
    // (m_linesDropped + history/live index) stay stable across eviction.
    qint64 m_linesDropped = 0;
    // OSC payloads arrive in fragments; collected until the final one.
    QByteArray m_oscBuffer;

    TerminalContrastCache m_contrast;
    TerminalScrollBar *m_scrollBar = nullptr;
    qreal m_wheelAccum = 0;
    bool m_copyOnSelect = false;
    bool m_bellSound = false;

    // Cursor: the user's style is pushed into libvterm as its default, so
    // DECSCUSR and save/restore work on top of it.
    TerminalCursorShape m_cursorShapeSetting = TerminalCursorShape::Block;
    TerminalCursorBlink m_cursorBlinkSetting = TerminalCursorBlink::Terminal;
    int m_cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    bool m_programBlink = false;
    bool m_blinkPhaseOn = true;
    QTimer *m_blinkTimer = nullptr;

    int m_cols = 80;
    int m_rows = 24;
    QPoint m_cursor{0, 0};
    bool m_cursorVisible = true;
    bool m_hasFocus = false;
    bool m_altScreen = false;
    QString m_title;
    QString m_errorMessage;
    QString m_preedit;

    bool m_selecting = false;
    QPoint m_selStart;
    QPoint m_selEnd;
    bool m_hasSelection = false;
    // A drag only becomes a selection past a 2px move.
    QPoint m_pressPixel;
    bool m_pressArmed = false;
    QElapsedTimer m_lastDoubleClick;
    QPoint m_lastDoubleClickPixel;

    HoverLink m_hover;
    QPoint m_hoverPixel;
    QPoint m_hoverProbePixel;
    HoverLink m_hoverPendingLink;
    QElapsedTimer m_hoverThrottle;
    QString m_hoverPendingKey;
    QFutureWatcher<QString> *m_pathWatcher = nullptr;
    struct PathProbe {
        QString resolved;   // empty: no such file
        qint64 checkedMs = 0;
    };
    QHash<QString, PathProbe> m_pathCache;
    QElapsedTimer m_clock;
    std::deque<Osc8Span> m_osc8Spans;
    bool m_osc8Open = false;
    qint64 m_osc8StartLine = 0;
    int m_osc8StartCol = 0;
    QString m_osc8Uri;

    TerminalSearchBar *m_searchBar = nullptr;
    TerminalSearchSession m_search;
    QTimer *m_searchDebounce = nullptr;
    QTimer *m_searchFollow = nullptr;
    QFutureWatcher<TerminalSearchSession::HistoryResult> *m_searchWatcher = nullptr;
    bool m_searchRevealPending = false;
    std::vector<TerminalRowLayout::Highlight> m_rowHighlights;
    int m_rowActiveFrom = -1;
    int m_rowActiveTo = -1;
    int m_zoomAccum = 0;

    ScrollbackBuffer m_scrollback{10000};
    int m_scrollOffset = 0;   // lines scrolled up from the live bottom

    int m_mouseMode = 0;
    bool m_focusReporting = false;
    bool m_exited = false;
    int m_exitCode = 0;
    bool m_firstOutputEmitted = false;

    QByteArray m_pendingInput;
    qsizetype m_pendingOffset = 0;
    QTimer *m_batchTimer = nullptr;

    std::vector<uint8_t> m_dirtyRows;
    bool m_allDirty = true;
    bool m_scrollbarDirty = false;
    bool m_cursorMoved = false;
    bool m_inResize = false;
    QTimer *m_frameTimer = nullptr;
    QElapsedTimer m_sincePaint;
    QTimer *m_resizeTimer = nullptr;

    std::vector<VTermScreenCell> m_rowCells;
    TerminalRowLayout m_layout;
    // Paint target when the window's pixels cannot be written directly: rows
    // render here, then one blit. Allocated on first need; freed (with the
    // glyph tiles) while the terminal is hidden.
    QImage m_frame;
    std::vector<QRgb> m_rowBg;
    // OSC 8 spans that reach the rows being painted (indexes into
    // m_osc8Spans), gathered once per paint.
    std::vector<std::size_t> m_paintOsc8;
    std::vector<uint8_t> m_touchedRow;
    std::vector<uint8_t> m_touchedBelow;
    std::vector<TerminalGlyphCache::Placement> m_rowPlan;
    std::vector<uint8_t> m_rowCopied;   // cells an opaque tile covers
    std::vector<uint8_t> m_rowFromAbove;   // cells the row above drew into
};

#endif
