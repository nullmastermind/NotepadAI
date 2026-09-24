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

#include "TerminalWidget.h"

#include "TerminalCellSource.h"
#include "NotepadNextApplication.h"
#include "ApplicationSettings.h"

#include "remote/ExecutionContext.h"

#include "iptyprocess.h"
#include "ptyqt.h"

#include <vterm.h>
#include <vterm_keycodes.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QIODevice>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace {

class WidgetCellSource final : public TerminalCellSource
{
public:
    WidgetCellSource(const ScrollbackBuffer &scrollback,
                     VTermScreen *screen,
                     int liveRows,
                     int cols)
        : m_scrollback(scrollback)
        , m_screen(screen)
        , m_liveRows(liveRows)
        , m_cols(cols)
    {
    }

    int rowCount() const override { return m_scrollback.size() + m_liveRows; }
    int cols() const override { return m_cols; }

    VTermScreenCell cellAt(int row, int col) const override
    {
        VTermScreenCell cell;
        std::memset(&cell, 0, sizeof(cell));
        const int historySize = m_scrollback.size();
        if (row < historySize) {
            // Per-cell read into the same row is amortised by caching the last row.
            if (m_cachedRow != row || m_cachedLine.size() < m_cols) {
                if (m_cachedLine.size() != m_cols) {
                    m_cachedLine.resize(m_cols);
                }
                if (!m_scrollback.lineAt(row, m_cols, m_cachedLine.data())) {
                    m_cachedRow = -1;
                    return cell;
                }
                m_cachedRow = row;
            }
            if (col >= 0 && col < m_cols) {
                return m_cachedLine[col];
            }
            return cell;
        }
        if (m_screen) {
            VTermPos pos = { row - historySize, col };
            if (vterm_screen_get_cell(m_screen, pos, &cell)) {
                return cell;
            }
        }
        return cell;
    }

private:
    const ScrollbackBuffer &m_scrollback;
    VTermScreen *m_screen;
    int m_liveRows;
    int m_cols;
    mutable int m_cachedRow = -1;
    mutable QVector<VTermScreenCell> m_cachedLine;
};

} // namespace

TerminalWidget::TerminalWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_scheme(TerminalColorScheme::darkScheme())
{
    m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    viewport()->setCursor(Qt::IBeamCursor);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (QScrollBar *sb = verticalScrollBar()) {
        sb->setSingleStep(1);
        sb->setPageStep(m_rows);
        sb->setRange(0, 0);
    }

    m_batchTimer = new QTimer(this);
    m_batchTimer->setSingleShot(true);
    m_batchTimer->setInterval(0);
    connect(m_batchTimer, &QTimer::timeout, this, &TerminalWidget::processPendingInput);

    recomputeMetrics();
}

TerminalWidget::~TerminalWidget()
{
    if (m_pty) {
        m_pty->kill();
        delete m_pty;
        m_pty = nullptr;
    }
    if (m_vt) {
        vterm_free(m_vt);
        m_vt = nullptr;
    }
}

int TerminalWidget::vtermDamage(VTermRect rect, void *user)
{
    auto *w = static_cast<TerminalWidget *>(user);
    // Live-screen damage maps to viewport rows offset by visible history.
    const int historyRowsShown = w->m_altScreen ? 0 : std::min(w->m_scrollOffset, w->m_rows);
    const int top = (rect.start_row + historyRowsShown) * w->m_cellH;
    const int height = (rect.end_row - rect.start_row) * w->m_cellH;
    const int left = rect.start_col * w->m_cellW;
    const int width = (rect.end_col - rect.start_col) * w->m_cellW;
    if (width > 0 && height > 0) {
        w->m_pendingDamage += QRect(left, top, width, height);
        w->viewport()->update(w->m_pendingDamage);
    } else {
        w->viewport()->update();
    }
    return 1;
}

int TerminalWidget::vtermMoveRect(VTermRect dest, VTermRect src, void *user)
{
    Q_UNUSED(src);
    auto *w = static_cast<TerminalWidget *>(user);
    const int historyRowsShown = w->m_altScreen ? 0 : std::min(w->m_scrollOffset, w->m_rows);
    const int top = (dest.start_row + historyRowsShown) * w->m_cellH;
    const int height = (dest.end_row - dest.start_row) * w->m_cellH;
    const int left = dest.start_col * w->m_cellW;
    const int width = (dest.end_col - dest.start_col) * w->m_cellW;
    if (width > 0 && height > 0) {
        w->m_pendingDamage += QRect(left, top, width, height);
        w->viewport()->update(w->m_pendingDamage);
    } else {
        w->viewport()->update();
    }
    return 1;
}

int TerminalWidget::vtermMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user)
{
    Q_UNUSED(oldpos);
    auto *w = static_cast<TerminalWidget *>(user);
    w->m_cursor = QPoint(pos.col, pos.row);
    w->m_cursorVisible = visible != 0;
    w->viewport()->update();
    return 1;
}

int TerminalWidget::vtermSetTermProp(VTermProp prop, VTermValue *val, void *user)
{
    auto *w = static_cast<TerminalWidget *>(user);
    if (prop == VTERM_PROP_TITLE && val && val->string.str) {
        const QString s = QString::fromUtf8(val->string.str, static_cast<int>(val->string.len));
        if (val->string.initial && val->string.final) {
            w->m_title = s;
            emit w->titleChanged(w->m_title);
        } else if (val->string.initial) {
            w->m_title = s;
        } else {
            w->m_title.append(s);
            if (val->string.final) {
                emit w->titleChanged(w->m_title);
            }
        }
        return 1;
    }
    if (prop == VTERM_PROP_CURSORVISIBLE && val) {
        w->m_cursorVisible = val->boolean != 0;
        w->viewport()->update();
        return 1;
    }
    if (prop == VTERM_PROP_ALTSCREEN && val) {
        w->m_altScreen = val->boolean != 0;
        w->viewport()->update();
        return 1;
    }
    if (prop == VTERM_PROP_MOUSE && val) {
        w->m_mouseMode = val->number;
        return 1;
    }
    if (prop == VTERM_PROP_FOCUSREPORT && val) {
        w->m_focusReporting = val->boolean != 0;
        return 1;
    }
    return 0;
}

int TerminalWidget::vtermBell(void *user)
{
    Q_UNUSED(user);
    QApplication::beep();
    return 1;
}

int TerminalWidget::vtermResize(int rows, int cols, void *user)
{
    auto *w = static_cast<TerminalWidget *>(user);
    w->m_rows = rows;
    w->m_cols = cols;
    if (QScrollBar *sb = w->verticalScrollBar()) {
        sb->setPageStep(rows);
    }
    w->viewport()->update();
    return 1;
}

void TerminalWidget::vtermOutput(const char *s, size_t len, void *user)
{
    auto *w = static_cast<TerminalWidget *>(user);
    if (w->m_pty) {
        w->m_pty->write(QByteArray(s, static_cast<int>(len)));
    }
}

int TerminalWidget::vtermSbPushLine(int cols, const VTermScreenCell *cells, void *user)
{
    auto *w = static_cast<TerminalWidget *>(user);
    // Alt-screen apps (vim, htop, less) don't pollute scrollback.
    if (w->m_altScreen) {
        return 0;
    }

    const bool wasAtBottom = w->isAtBottom();
    const int evicted = w->m_scrollback.pushLine(cols, cells);

    // Eviction shifts absolute history-row indices down; keep selection anchored to content.
    if (evicted > 0 && w->m_hasSelection) {
        w->m_selStart.ry() -= evicted;
        w->m_selEnd.ry()   -= evicted;
        if (w->m_selStart.y() < 0 && w->m_selEnd.y() < 0) {
            w->m_hasSelection = false;
        } else {
            if (w->m_selStart.y() < 0) w->m_selStart.setY(0);
            if (w->m_selEnd.y()   < 0) w->m_selEnd.setY(0);
        }
    }

    // Sticky-bottom: only follow the new content if the user was already there.
    if (QScrollBar *sb = w->verticalScrollBar()) {
        const int oldMax = sb->maximum();
        const int oldValue = sb->value();
        const int newMax = w->m_scrollback.size();
        sb->setRange(0, newMax);
        sb->setPageStep(w->m_rows);
        if (wasAtBottom) {
            sb->setValue(newMax);
        } else {
            const int delta = newMax - oldMax;
            sb->setValue(std::clamp(oldValue + delta, 0, newMax));
        }
        w->m_scrollOffset = sb->maximum() - sb->value();
    }
    return 1;
}

int TerminalWidget::vtermSbPopLine(int cols, VTermScreenCell *cells, void *user)
{
    auto *w = static_cast<TerminalWidget *>(user);
    if (!w->m_scrollback.popLine(cols, cells)) {
        return 0;
    }
    if (QScrollBar *sb = w->verticalScrollBar()) {
        const int newMax = w->m_scrollback.size();
        const int newValue = std::min(sb->value(), newMax);
        sb->setRange(0, newMax);
        sb->setPageStep(w->m_rows);
        sb->setValue(newValue);
        w->m_scrollOffset = sb->maximum() - sb->value();
    }
    return 1;
}

bool TerminalWidget::isAtBottom() const
{
    if (const QScrollBar *sb = verticalScrollBar()) {
        return sb->value() == sb->maximum();
    }
    return true;
}

void TerminalWidget::updateScrollbarRange()
{
    if (QScrollBar *sb = verticalScrollBar()) {
        const bool atBottom = isAtBottom();
        const int newMax = m_scrollback.size();
        sb->setRange(0, newMax);
        sb->setPageStep(m_rows);
        if (atBottom) {
            sb->setValue(newMax);
        }
        m_scrollOffset = sb->maximum() - sb->value();
    }
}

bool TerminalWidget::start(const QString &shell, const QString &cwd, const QStringList &env,
                           remote::ExecutionContext *ctx)
{
    if (m_pty) {
        return false;
    }

    recomputeMetrics();
    if (viewport()->width() > 0 && viewport()->height() > 0) {
        m_cols = std::max(20, viewport()->width()  / std::max(1, m_cellW));
        m_rows = std::max(5,  viewport()->height() / std::max(1, m_cellH));
    }

    m_vt = vterm_new(m_rows, m_cols);
    vterm_set_utf8(m_vt, 1);

    m_screen = vterm_obtain_screen(m_vt);
    static const VTermScreenCallbacks callbacks = {
        &TerminalWidget::vtermDamage,
        &TerminalWidget::vtermMoveRect,
        &TerminalWidget::vtermMoveCursor,
        &TerminalWidget::vtermSetTermProp,
        &TerminalWidget::vtermBell,
        &TerminalWidget::vtermResize,
        &TerminalWidget::vtermSbPushLine,
        &TerminalWidget::vtermSbPopLine,
        nullptr,
    };
    vterm_screen_set_callbacks(m_screen, &callbacks, this);
    vterm_screen_reset(m_screen, 1);

    vterm_output_set_callback(m_vt, &TerminalWidget::vtermOutput, this);

    // PTY backend comes from the execution context: the local context returns
    // the IDENTICAL PtyQt object used before this change (so the read/write hot
    // path is byte-for-byte unchanged — only this one virtual call is added),
    // and a remote context returns an SSH-backed PTY. A null ctx falls back to
    // a local PtyQt directly so existing callers behave exactly as before.
    if (ctx) {
        m_pty = ctx->createPty(this);
    } else {
        m_pty = PtyQt::createPtyProcess(IPtyProcess::AutoPty);
    }
    if (!m_pty) {
        m_errorMessage = tr("Terminal: PTY backend unavailable on this platform.");
        emit spawnFailed(m_errorMessage);
        viewport()->update();
        return false;
    }

    if (!m_pty->isAvailable()) {
#ifdef Q_OS_WIN
        m_errorMessage = tr("Terminal: ConPTY requires Windows 10 build 17763 (1809) or later.");
#else
        m_errorMessage = tr("Terminal: PTY is unavailable on this platform.");
#endif
        emit spawnFailed(m_errorMessage);
        viewport()->update();
        return false;
    }

    if (!(ctx && ctx->isRemote())) {
        int limitGb = 0;
        if (auto *app = qobject_cast<NotepadNextApplication *>(qApp)) {
            if (ApplicationSettings *settings = app->getSettings()) {
                limitGb = settings->terminalChildMemoryLimitGb();
            }
        }
        if (limitGb < 0) {
            limitGb = 0;
        } else if (limitGb > 256) {
            limitGb = 256;
        }
        if (limitGb > 0) {
            m_pty->setChildCommitLimit(static_cast<quint64>(limitGb) << 30);
        }
        connect(m_pty, &IPtyProcess::memoryLimitExceeded, this, [this, limitGb]() {
            const QString text = limitGb > 0
                ? TerminalWidget::tr("Stopped: a program exceeded the %1 GB terminal memory limit.").arg(limitGb)
                : TerminalWidget::tr("Stopped: a program exceeded the terminal memory limit.");
            injectOutput(QStringLiteral("\r\n\x1b[31m%1\x1b[0m\r\n").arg(text).toUtf8());
        });
    }

    if (!m_pty->startProcess(shell, cwd, env, static_cast<qint16>(m_cols), static_cast<qint16>(m_rows))) {
        m_errorMessage = tr("Terminal: failed to launch shell '%1'.\n%2").arg(shell, m_pty->lastError());
        emit spawnFailed(m_errorMessage);
        viewport()->update();
        return false;
    }

    if (QIODevice *n = m_pty->notifier()) {
        connect(n, &QIODevice::readyRead, this, &TerminalWidget::onPtyReadyRead);
    }
    connect(m_pty, &IPtyProcess::finished, this, &TerminalWidget::onPtyFinished);

    viewport()->update();
    return true;
}

void TerminalWidget::setColorScheme(const TerminalColorScheme &scheme)
{
    m_scheme = scheme;
    viewport()->update();
}

void TerminalWidget::setTerminalFont(const QFont &font)
{
    m_font = font;
    recomputeMetrics();
    requestResizeToViewport();
    viewport()->update();
}

bool TerminalWidget::isProcessRunning() const
{
    if (m_exited) return false;
    if (!m_pty) return false;
    return m_pty->pid() > 0;
}

void TerminalWidget::killProcess()
{
    if (m_pty) {
        m_pty->kill();
    }
}

void TerminalWidget::onPtyReadyRead()
{
    if (!m_pty || !m_vt) return;
    const QByteArray data = m_pty->readAll();
    if (data.isEmpty()) return;
    if (!m_firstOutputEmitted) {
        m_firstOutputEmitted = true;
        emit firstOutputReceived();
    }
    m_pendingInput.append(data);
    if (!m_batchTimer->isActive()) {
        m_batchTimer->start();
    }
}

void TerminalWidget::processPendingInput()
{
    if (!m_vt || m_pendingInput.isEmpty()) return;

    static constexpr int kChunkSize = 16 * 1024;
    static constexpr auto kMaxTime = std::chrono::milliseconds(4);

    const auto start = std::chrono::steady_clock::now();
    int offset = 0;
    const int totalSize = static_cast<int>(m_pendingInput.size());

    while (offset < totalSize) {
        const int chunk = std::min(kChunkSize, totalSize - offset);
        vterm_input_write(m_vt, m_pendingInput.constData() + offset, static_cast<size_t>(chunk));
        offset += chunk;
        if (std::chrono::steady_clock::now() - start > kMaxTime) {
            break;
        }
    }

    vterm_screen_flush_damage(m_screen);
    m_pendingInput.remove(0, offset);
    viewport()->update();

    if (!m_pendingInput.isEmpty()) {
        m_batchTimer->start();
    }
}

void TerminalWidget::onPtyFinished(int exitCode)
{
    m_exited = true;
    m_exitCode = exitCode;
    if (!m_pendingInput.isEmpty()) {
        processPendingInput();
    }
    emit processExited(exitCode);
    viewport()->update();
}

void TerminalWidget::writeToPty(const QByteArray &data)
{
    if (m_pty) {
        m_pty->write(data);
    }
}

void TerminalWidget::injectOutput(const QByteArray &data)
{
    if (!m_vt || data.isEmpty()) return;
    m_pendingInput.append(data);
    if (!m_batchTimer->isActive()) {
        m_batchTimer->start();
    }
}

bool TerminalWidget::isMouseReportingActive(Qt::KeyboardModifiers mods) const
{
    if (m_mouseMode == VTERM_PROP_MOUSE_NONE) return false;
    if (mods & Qt::ShiftModifier) return false;
    return true;
}

VTermModifier TerminalWidget::qtModsToVterm(Qt::KeyboardModifiers mods) const
{
    VTermModifier vmod = VTERM_MOD_NONE;
    if (mods & Qt::ShiftModifier)   vmod = static_cast<VTermModifier>(vmod | VTERM_MOD_SHIFT);
    if (mods & Qt::ControlModifier) vmod = static_cast<VTermModifier>(vmod | VTERM_MOD_CTRL);
    if (mods & Qt::AltModifier)     vmod = static_cast<VTermModifier>(vmod | VTERM_MOD_ALT);
    return vmod;
}

void TerminalWidget::recomputeMetrics()
{
    QFontMetricsF fm(m_font);
    m_cellW = std::max(1, qCeil(fm.horizontalAdvance(QLatin1Char('M'))));
    m_cellH = std::max(1, qCeil(fm.height()));
    m_cellAscent = qCeil(fm.ascent());
}

void TerminalWidget::requestResizeToViewport()
{
    if (!m_vt || !m_pty) return;
    const int newCols = std::max(20, viewport()->width()  / std::max(1, m_cellW));
    const int newRows = std::max(5,  viewport()->height() / std::max(1, m_cellH));
    if (newCols == m_cols && newRows == m_rows) return;
    m_cols = newCols;
    m_rows = newRows;
    vterm_set_size(m_vt, m_rows, m_cols);
    m_pty->resize(static_cast<qint16>(m_cols), static_cast<qint16>(m_rows));
}

void TerminalWidget::paintCell(QPainter &p, int viewportRow, int col, const VTermScreenCell &cell)
{
    QColor bg = m_scheme.defaultBg;
    QColor fg = m_scheme.defaultFg;

    VTermColor cbg = cell.bg;
    VTermColor cfg = cell.fg;
    if (m_screen) {
        vterm_screen_convert_color_to_rgb(m_screen, &cbg);
        vterm_screen_convert_color_to_rgb(m_screen, &cfg);
    }

    if (VTERM_COLOR_IS_INDEXED(&cell.bg)) {
        if (cell.bg.indexed.idx < 16) bg = m_scheme.ansi[cell.bg.indexed.idx];
    } else if (VTERM_COLOR_IS_RGB(&cell.bg)) {
        bg = QColor(cbg.rgb.red, cbg.rgb.green, cbg.rgb.blue);
    } else if (VTERM_COLOR_IS_DEFAULT_BG(&cell.bg)) {
        bg = m_scheme.defaultBg;
    }

    if (VTERM_COLOR_IS_INDEXED(&cell.fg)) {
        if (cell.fg.indexed.idx < 16) fg = m_scheme.ansi[cell.fg.indexed.idx];
    } else if (VTERM_COLOR_IS_RGB(&cell.fg)) {
        fg = QColor(cfg.rgb.red, cfg.rgb.green, cfg.rgb.blue);
    } else if (VTERM_COLOR_IS_DEFAULT_FG(&cell.fg)) {
        fg = m_scheme.defaultFg;
    }

    if (cell.attrs.reverse) {
        std::swap(bg, fg);
    }

    const int x = col * m_cellW;
    const int y = viewportRow * m_cellH;
    const int cellSpan = std::max(1, static_cast<int>(cell.width));

    p.fillRect(QRect(x, y, m_cellW * cellSpan, m_cellH), bg);

    if (cell.chars[0]) {
        QString glyph;
        for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; ++i) {
            const char32_t cp = cell.chars[i];
            glyph.append(QString::fromUcs4(&cp, 1));
        }
        if (!glyph.isEmpty()) {
            p.setPen(fg);
            QFont f = m_font;
            if (cell.attrs.bold)   f.setBold(true);
            if (cell.attrs.italic) f.setItalic(true);
            if (cell.attrs.underline) f.setUnderline(true);
            p.setFont(f);
            p.drawText(QPointF(x, y + m_cellAscent), glyph);
        }
    }
}

void TerminalWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(viewport());
    p.fillRect(viewport()->rect(), m_scheme.defaultBg);

    if (!m_errorMessage.isEmpty() && (!m_pty || m_pty->pid() == 0)) {
        p.setPen(m_scheme.defaultFg);
        p.setFont(m_font);
        p.drawText(viewport()->rect().adjusted(8, 8, -8, -8), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_errorMessage);
        m_pendingDamage = QRegion();
        return;
    }

    if (!m_screen) {
        m_pendingDamage = QRegion();
        return;
    }

    p.setFont(m_font);

    // Alt-screen apps hide history; otherwise show as much history as the user has scrolled up.
    const int historyRowsShown = m_altScreen ? 0 : std::min(m_scrollOffset, m_rows);
    const int historySize      = m_scrollback.size();

    if (m_rowScratch.size() != m_cols) {
        m_rowScratch.resize(m_cols);
    }

    for (int viewportRow = 0; viewportRow < historyRowsShown; ++viewportRow) {
        const int historyIndex = historySize - m_scrollOffset + viewportRow;
        if (historyIndex < 0 || historyIndex >= historySize) {
            continue;
        }
        if (!m_scrollback.lineAt(historyIndex, m_cols, m_rowScratch.data())) {
            continue;
        }
        for (int col = 0; col < m_cols; ++col) {
            const auto &cell = m_rowScratch[col];
            if (cell.width == 0) continue;
            paintCell(p, viewportRow, col, cell);
            if (cell.width > 1) col += cell.width - 1;
        }
    }

    for (int row = historyRowsShown; row < m_rows; ++row) {
        for (int col = 0; col < m_cols; ++col) {
            VTermPos pos = { row - historyRowsShown, col };
            VTermScreenCell cell;
            if (!vterm_screen_get_cell(m_screen, pos, &cell)) {
                continue;
            }
            if (cell.width == 0) continue;
            paintCell(p, row, col, cell);
            if (cell.width > 1) col += cell.width - 1;
        }
    }

    // Selection is in absolute (col, absoluteRow); off-screen rows still contribute to copied text.
    if (m_hasSelection) {
        const QPoint a = m_selStart;
        const QPoint b = m_selEnd;
        const QPoint lo = (a.y() < b.y() || (a.y() == b.y() && a.x() <= b.x())) ? a : b;
        const QPoint hi = (lo == a) ? b : a;
        QColor sel = m_scheme.selection;
        sel.setAlpha(128);
        for (int absRow = lo.y(); absRow <= hi.y(); ++absRow) {
            const int viewportRow = absoluteToViewportRow(absRow);
            if (viewportRow < 0 || viewportRow >= m_rows) continue;
            const int startCol = (absRow == lo.y()) ? lo.x() : 0;
            const int endCol   = (absRow == hi.y()) ? hi.x() : (m_cols - 1);
            for (int col = startCol; col <= endCol; ++col) {
                const int x = col * m_cellW;
                const int y = viewportRow * m_cellH;
                p.fillRect(QRect(x, y, m_cellW, m_cellH), sel);
            }
        }
    }

    // Cursor only at live bottom; otherwise it would blink over old text.
    if (m_cursorVisible && m_scrollOffset == 0) {
        const int x = m_cursor.x() * m_cellW;
        const int y = m_cursor.y() * m_cellH;
        QRect cr(x, y, m_cellW, m_cellH);
        if (m_hasFocus) {
            p.fillRect(cr, m_scheme.cursor);
        } else {
            p.setPen(m_scheme.cursor);
            p.drawRect(cr.adjusted(0, 0, -1, -1));
        }
    }

    m_pendingDamage = QRegion();
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    requestResizeToViewport();
    updateScrollbarRange();
}

void TerminalWidget::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    if (QScrollBar *sb = verticalScrollBar()) {
        m_scrollOffset = sb->maximum() - sb->value();
    }
    m_pendingDamage = QRegion();
    viewport()->update();
}

bool TerminalWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && m_vt && m_pty) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        const int key = ke->key();
        const auto mods = ke->modifiers() & ~Qt::KeypadModifier;

        if (key == Qt::Key_Control || key == Qt::Key_Shift ||
            key == Qt::Key_Alt || key == Qt::Key_Meta) {
            return QAbstractScrollArea::event(event);
        }

        if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
            return QAbstractScrollArea::event(event);
        }

        struct ShortcutEntry { Qt::KeyboardModifiers mods; int key; };
        static constexpr ShortcutEntry appShortcuts[] = {
            {Qt::ControlModifier, Qt::Key_S},
            {Qt::ControlModifier, Qt::Key_N},
            {Qt::ControlModifier, Qt::Key_O},
            {Qt::ControlModifier, Qt::Key_W},
            {Qt::ControlModifier, Qt::Key_Q},
            {Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_S},
            {Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_W},
            {Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_T},
            {Qt::ControlModifier, Qt::Key_Tab},
            {Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Tab},
            {Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Backtab},
        };

        for (const auto &s : appShortcuts) {
            if (mods == s.mods && key == s.key) {
                return QAbstractScrollArea::event(event);
            }
        }

        ke->accept();
        return true;
    }

    // Qt routes Tab/Backtab through focusNextPrevChild() in QWidget::event(),
    // so they never reach keyPressEvent unless we intercept here.
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            keyPressEvent(ke);
            if (ke->isAccepted()) {
                return true;
            }
        }
    }
    return QAbstractScrollArea::event(event);
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_vt || !m_pty) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    const int key = event->key();
    const auto mods = event->modifiers();

    if ((mods & Qt::ControlModifier) && (mods & Qt::ShiftModifier)) {
        if (key == Qt::Key_C) {
            copySelection();
            return;
        }
        if (key == Qt::Key_V) {
            pasteClipboard();
            return;
        }
    }

    VTermModifier vmod = VTERM_MOD_NONE;
    if (mods & Qt::ShiftModifier)   vmod = static_cast<VTermModifier>(vmod | VTERM_MOD_SHIFT);
    if (mods & Qt::ControlModifier) vmod = static_cast<VTermModifier>(vmod | VTERM_MOD_CTRL);
    if (mods & Qt::AltModifier)     vmod = static_cast<VTermModifier>(vmod | VTERM_MOD_ALT);

    auto sendKey = [&](VTermKey k) {
        vterm_keyboard_key(m_vt, k, vmod);
    };

    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:    sendKey(VTERM_KEY_ENTER); return;
    case Qt::Key_Backspace: sendKey(VTERM_KEY_BACKSPACE); return;
    case Qt::Key_Tab:       sendKey(VTERM_KEY_TAB); return;
    case Qt::Key_Backtab:   sendKey(VTERM_KEY_TAB); return;
    case Qt::Key_Escape:    sendKey(VTERM_KEY_ESCAPE); return;
    case Qt::Key_Up:        sendKey(VTERM_KEY_UP); return;
    case Qt::Key_Down:      sendKey(VTERM_KEY_DOWN); return;
    case Qt::Key_Left:      sendKey(VTERM_KEY_LEFT); return;
    case Qt::Key_Right:     sendKey(VTERM_KEY_RIGHT); return;
    case Qt::Key_Insert:    sendKey(VTERM_KEY_INS); return;
    case Qt::Key_Delete:    sendKey(VTERM_KEY_DEL); return;
    case Qt::Key_Home:      sendKey(VTERM_KEY_HOME); return;
    case Qt::Key_End:       sendKey(VTERM_KEY_END); return;
    case Qt::Key_PageUp:    sendKey(VTERM_KEY_PAGEUP); return;
    case Qt::Key_PageDown:  sendKey(VTERM_KEY_PAGEDOWN); return;
    default:
        break;
    }

    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) {
        sendKey(static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 1 + (key - Qt::Key_F1)));
        return;
    }

    if ((mods & Qt::ControlModifier) && key >= Qt::Key_A && key <= Qt::Key_Z) {
        vterm_keyboard_unichar(m_vt, static_cast<uint32_t>('a' + (key - Qt::Key_A)), vmod);
        return;
    }

    const QString text = event->text();
    if (!text.isEmpty()) {
        for (uint cp : text.toUcs4()) {
            vterm_keyboard_unichar(m_vt, static_cast<uint32_t>(cp), vmod);
        }
        return;
    }

    QAbstractScrollArea::keyPressEvent(event);
}

void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
    }

    if (m_vt && isMouseReportingActive(event->modifiers())) {
        const QPoint cell = pixelToCell(event->pos());
        int button = 0;
        if (event->button() == Qt::LeftButton)   button = 1;
        else if (event->button() == Qt::MiddleButton) button = 2;
        else if (event->button() == Qt::RightButton)  button = 3;
        if (button > 0) {
            VTermModifier vmod = qtModsToVterm(event->modifiers());
            vterm_mouse_move(m_vt, cell.y(), cell.x(), vmod);
            vterm_mouse_button(m_vt, button, true, vmod);
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        m_selStart = pixelToAbsoluteCell(event->pos());
        m_selEnd = m_selStart;
        m_selecting = true;
        m_hasSelection = false;
        viewport()->update();
    } else if (event->button() == Qt::MiddleButton) {
        const QClipboard *cb = QApplication::clipboard();
        const QString sel = cb->text(QClipboard::Selection);
        if (!sel.isEmpty()) {
            writeToPty(sel.toUtf8());
        }
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_vt && isMouseReportingActive(event->modifiers())) {
        const QPoint cell = pixelToCell(event->pos());
        bool shouldReport = false;
        if (m_mouseMode == VTERM_PROP_MOUSE_MOVE) {
            shouldReport = true;
        } else if (m_mouseMode == VTERM_PROP_MOUSE_DRAG && (event->buttons() != Qt::NoButton)) {
            shouldReport = true;
        }
        if (shouldReport) {
            VTermModifier vmod = qtModsToVterm(event->modifiers());
            vterm_mouse_move(m_vt, cell.y(), cell.x(), vmod);
        }
        event->accept();
        return;
    }

    if (m_selecting) {
        m_selEnd = pixelToAbsoluteCell(event->pos());
        m_hasSelection = (m_selStart != m_selEnd);
        viewport()->update();
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_vt && isMouseReportingActive(event->modifiers())) {
        int button = 0;
        if (event->button() == Qt::LeftButton)   button = 1;
        else if (event->button() == Qt::MiddleButton) button = 2;
        else if (event->button() == Qt::RightButton)  button = 3;
        if (button > 0) {
            const QPoint cell = pixelToCell(event->pos());
            VTermModifier vmod = qtModsToVterm(event->modifiers());
            vterm_mouse_move(m_vt, cell.y(), cell.x(), vmod);
            vterm_mouse_button(m_vt, button, false, vmod);
        }
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        m_selEnd = pixelToAbsoluteCell(event->pos());
        m_hasSelection = (m_selStart != m_selEnd);
        viewport()->update();
        if (m_hasSelection) {
            QClipboard *cb = QApplication::clipboard();
            if (cb->supportsSelection()) {
                cb->setText(selectionText(), QClipboard::Selection);
            }
        }
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    m_hasFocus = true;
    if (m_focusReporting && m_pty) {
        writeToPty(QByteArray("\x1b[I", 3));
    }
    viewport()->update();
    QAbstractScrollArea::focusInEvent(event);
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    m_hasFocus = false;
    if (m_focusReporting && m_pty) {
        writeToPty(QByteArray("\x1b[O", 3));
    }
    viewport()->update();
    QAbstractScrollArea::focusOutEvent(event);
}

void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    if (m_vt && isMouseReportingActive(event->modifiers())) {
        const QPoint cell = pixelToCell(event->position().toPoint());
        VTermModifier vmod = qtModsToVterm(event->modifiers());
        const int delta = event->angleDelta().y();
        const int button = (delta > 0) ? 4 : 5;
        vterm_mouse_move(m_vt, cell.y(), cell.x(), vmod);
        vterm_mouse_button(m_vt, button, true, vmod);
        vterm_mouse_button(m_vt, button, false, vmod);
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void TerminalWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !isMouseReportingActive(event->modifiers())) {
        setFocus(Qt::MouseFocusReason);
        selectWord(pixelToAbsoluteCell(event->pos()));
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void TerminalWidget::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty()) {
        writeToPty(event->commitString().toUtf8());
    }
    event->accept();
}

QPoint TerminalWidget::pixelToCell(const QPoint &p) const
{
    int col = std::clamp(p.x() / std::max(1, m_cellW), 0, m_cols - 1);
    int row = std::clamp(p.y() / std::max(1, m_cellH), 0, m_rows - 1);
    return QPoint(col, row);
}

int TerminalWidget::viewportToAbsoluteRow(int viewportRow) const
{
    // Alt screen hides history: viewport rows map straight to live rows.
    const int historySize = m_scrollback.size();
    const int total = historySize + m_rows;
    if (total <= 0) return 0;
    int absolute;
    if (m_altScreen) {
        absolute = historySize + viewportRow;
    } else {
        absolute = historySize - m_scrollOffset + viewportRow;
    }
    return std::clamp(absolute, 0, total - 1);
}

int TerminalWidget::absoluteToViewportRow(int absoluteRow) const
{
    if (m_altScreen) {
        return absoluteRow - m_scrollback.size();
    }
    return absoluteRow - (m_scrollback.size() - m_scrollOffset);
}

QPoint TerminalWidget::pixelToAbsoluteCell(const QPoint &p) const
{
    const QPoint vp = pixelToCell(p);
    return QPoint(vp.x(), viewportToAbsoluteRow(vp.y()));
}

QString TerminalWidget::selectionText() const
{
    if (!m_screen || !m_hasSelection) return QString();
    WidgetCellSource src(m_scrollback, m_screen, m_rows, m_cols);
    return extractSelectionText(src, m_selStart, m_selEnd);
}

void TerminalWidget::copySelection()
{
    if (!m_hasSelection) return;
    QApplication::clipboard()->setText(selectionText());
}

void TerminalWidget::pasteClipboard()
{
    const QString text = QApplication::clipboard()->text();
    if (text.isEmpty() || !m_vt) return;
    vterm_keyboard_start_paste(m_vt);
    for (uint cp : text.toUcs4()) {
        vterm_keyboard_unichar(m_vt, static_cast<uint32_t>(cp), VTERM_MOD_NONE);
    }
    vterm_keyboard_end_paste(m_vt);
}

QString TerminalWidget::selectedText() const
{
    return selectionText();
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (isMouseReportingActive(QGuiApplication::keyboardModifiers())) {
        event->ignore();
        return;
    }

    QMenu menu(this);

    QAction *copyAct = menu.addAction(tr("Copy"));
    copyAct->setEnabled(m_hasSelection);
    connect(copyAct, &QAction::triggered, this, [this]() { copySelection(); });

    QAction *pasteAct = menu.addAction(tr("Paste"));
    connect(pasteAct, &QAction::triggered, this, [this]() { pasteClipboard(); });

    emit contextMenuAboutToShow(&menu);

    menu.exec(event->globalPos());
}

void TerminalWidget::selectWord(const QPoint &absCell)
{
    if (!m_screen) return;
    WidgetCellSource src(m_scrollback, m_screen, m_rows, m_cols);
    const int row = absCell.y();
    const int cols = src.cols();
    if (row < 0 || row >= src.rowCount()) return;

    static const auto isWordChar = [](uint32_t ch) {
        if (ch == 0 || ch == ' ' || ch == '\t') return false;
        static constexpr char32_t delims[] = U"!@#$%^&*(){}[]|;:'\",.<>?/`~-=+\\";
        for (char32_t d : delims) {
            if (ch == d) return false;
        }
        return true;
    };

    int startCol = absCell.x();
    int endCol = absCell.x();

    while (startCol > 0) {
        VTermScreenCell cell = src.cellAt(row, startCol - 1);
        if (!isWordChar(cell.chars[0])) break;
        --startCol;
    }
    while (endCol < cols - 1) {
        VTermScreenCell cell = src.cellAt(row, endCol + 1);
        if (!isWordChar(cell.chars[0])) break;
        ++endCol;
    }

    m_selStart = QPoint(startCol, row);
    m_selEnd = QPoint(endCol, row);
    m_hasSelection = (startCol != endCol || src.cellAt(row, startCol).chars[0] != 0);
    m_selecting = false;
    viewport()->update();

    if (m_hasSelection) {
        QClipboard *cb = QApplication::clipboard();
        if (cb->supportsSelection()) {
            cb->setText(selectionText(), QClipboard::Selection);
        }
    }
}
