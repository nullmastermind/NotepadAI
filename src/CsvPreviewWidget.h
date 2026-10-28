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

#ifndef CSVPREVIEWWIDGET_H
#define CSVPREVIEWWIDGET_H

#include "PreviewContentWidget.h"

#include <QPalette>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <cstdint>
#include <memory>

class NotepadNextApplication;
class CsvDocument;
class CsvTableModel;

class QTableView;
class QComboBox;
class QToolButton;
class QCheckBox;
class QLabel;
class QLineEdit;
class QFrame;
class QPushButton;
class QScrollBar;
class QTimer;
class QProgressBar;
class QUndoStack;
class QFileSystemWatcher;
template <typename T> class QFutureWatcher;

// CsvPreviewWidget renders a delimited file as a spreadsheet-style grid backed by
// a memory-mapped, sparse-indexed CsvDocument and a windowed CsvTableModel. It is
// the file-path load route of PreviewTabManager (typeId "csv"/"tsv"): instead of
// the QByteArray-decode-to-QString path it owns its own mmap + 2 GB cap.
//
// Coordinate model: selection/copy/search/edit all operate in ABSOLUTE data-row
// coordinates read straight from the model (window-independent). The view is a
// pure projection of the current 256K-row window.
class CsvPreviewWidget : public PreviewContentWidget
{
    Q_OBJECT

public:
    explicit CsvPreviewWidget(NotepadNextApplication *app, QWidget *parent = nullptr);
    ~CsvPreviewWidget() override;

    QString typeId() const override { return QStringLiteral("csv"); }
    QString displayName() const override { return m_displayName; }

    // PreviewContentWidget contract. CSV uses the file-path route, so setContent/
    // refresh are no-ops (the manager calls loadFromFile instead).
    void setContent(const QString &text, const QString &basePath) override { Q_UNUSED(text); Q_UNUSED(basePath); }
    void refresh(const QString &text) override { Q_UNUSED(text); }
    void applyTheme(const QPalette &palette, bool isDark) override;

    // File-path load route (wantsFilePath registration). Enforces the 2 GB cap,
    // shows the >500 MB notice, kicks off indexing on a worker, swaps in the
    // model on completion.
    void loadFromFile(const QString &path) override;

    // --- Control API (docs/adr/0007): what the grid's menus and toolbar do ---
    // Rows are VIEW rows (the displayed order under a sort/filter, else data
    // rows); columns are 0-based. Edits return why they could not run, empty
    // when they did.
    CsvTableModel *tableModel() const { return m_model; }   // null until indexed
    bool isIndexing() const { return m_indexing; }
    QString filePath() const { return m_path; }
    QChar delimiter() const;
    QString encodingName() const;
    bool isModified() const;
    // Re-checks the "open dirty in an editor" guard, as an edit commit does.
    bool refreshReadOnly() { return conflictActive(true); }
    quint64 viewRows() const { return viewRowCount(); }
    int sortColumn() const { return m_pendingViewSortColumn; }
    Qt::SortOrder sortOrder() const { return m_pendingViewSortOrder; }
    QStringList filters() const { return currentFilters(); }
    bool sortFilterAvailable() const;
    bool viewRecomputing() const;
    // A cell as the grid shows it.
    QString cellText(quint64 viewRow, int col) const;
    QString setCell(quint64 viewRow, int col, const QString &value);
    QString insertRows(quint64 at, int count);
    QString deleteRows(QList<quint64> rows);
    QString insertColumns(int at, int count);
    QString deleteColumns(QList<int> cols);
    QString renameColumn(int col, const QString &name);
    QString clearCells(const QList<QPair<quint64, int>> &cells);
    // Header click cycle's states: ascending, descending, or cleared.
    QString sortBy(int col, bool active, Qt::SortOrder order);
    // A filter field's text (empty clears it); recomputes at once.
    QString setFilter(int col, const QString &text);
    void clearFilters();
    // The Delimiter combo; re-indexes (indexed() fires when done).
    QString setDelimiterChar(QChar delimiter);
    // The "First row as header" box.
    void setHeaderRow(bool on);
    // Ctrl+S. Empty when written (a re-index follows; indexed() fires).
    QString save();

signals:
    // Toggles the tab's dirty indicator (overlay non-empty).
    void dirtyChanged(bool dirty);
    // An index pass finished (load, delimiter change, reload, after a save).
    void indexed();
    // A sort/filter recompute that ran on a worker was applied.
    void viewRecomputed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    // --- UI construction ---
    void buildUi();
    void buildToolbar();
    void buildFindBar();
    void buildBanner();

    // --- Indexing lifecycle ---
    void startIndexing();          // launch buildIndex on a worker
    void onIndexingFinished();     // swap model in, configure the view
    void reindex();                // re-run after a delimiter change / reload

    // Cancel + JOIN every worker that reads the document's mmap (copy, find, view)
    // and bump their staleness signals. MUST be called before any path that
    // unmaps/reopens m_doc (open/unmap/remap) so a background reader can never
    // dereference a freed mapping (use-after-unmap). Does NOT touch m_indexWatcher
    // (its own cancel/join is handled by startIndexing, which calls this first).
    void cancelAndJoinDocumentReaders();

    // --- Windowing / scrollbar ---
    void configureView();
    void fitColumnsToContent();    // size each column to header + sampled cell content (O(sample×cols))
    void syncAbsoluteScrollRange();
    void onAbsoluteScroll(int absTop);
    void onViewScrolled(int viewValue);
    void rebaseTo(quint64 desiredBase, bool centerScroll = false);
    // Map absolute anchor/caret onto the view. `repaint` defaults true: the
    // blocked clear()+select() inside leaves no incremental repaint, so callers
    // that are NOT followed by a model reset must force one. Callers that re-base
    // / reset the model (rebaseTo, header-toggle) pass false — endResetModel()
    // already schedules the viewport repaint, so a second is redundant on those
    // (incl. the per-tick absolute-scrollbar drag-across-window path).
    void projectSelection(bool repaint = true);

    // --- Selection (view-row coordinates) ---
    void setAnchorCaret(quint64 row, int col);
    void extendCaret(quint64 row, int col);
    void selectAll();
    void selectColumn(int col);
    void selectRow(int dataRow);   // whole-row select (row-header click)
    void clampCaretVisible();      // re-base if the caret left the window
    void boundingBox(quint64 &minRow, quint64 &maxRow, int &minCol, int &maxCol) const;
    // Number of selectable rows in CURRENT (view) coordinates: the displayed row
    // count under an active sort/filter view, else the full data-row count. The
    // identity when no view is active (rowCount()==dataRowCount()), so every
    // navigation/selection clamp reduces to today's behavior on the no-view path.
    quint64 viewRowCount() const;

    // --- Deletion / clearing (push QUndoCommands onto m_undoStack) ---
    void clearSelectedCells();     // Delete / Backspace
    void deleteSelectedRows();     // row-header menu
    void deleteSelectedColumns();  // column-header menu

    // --- Copy ---
    enum class CopyFormat : std::uint8_t { Tsv, Csv, Markdown };
    QString serializeSelection(CopyFormat fmt) const; // small selections (sync)
    void copySelection(CopyFormat fmt);
    void startWorkerCopy(CopyFormat fmt, quint64 minRow, quint64 maxRow, int minCol, int maxCol);
    qint64 estimateCopyBytes(quint64 minRow, quint64 maxRow, int minCol, int maxCol) const;

    // --- Edit / save ---
    void onCellEdited();           // overlay changed
    QString saveToDisk();          // Ctrl+S; why it did not save, empty when it did
    void changeDelimiter();        // the Delimiter combo's current item
    bool conflictActive(bool refresh); // dirty-editor guard; updates the banner

    // --- Structural edits (insert blank row/column) ---
    // dataRow/col are ABSOLUTE data-grid coordinates. Each rebases the window if
    // the target is off-screen, mutates the model (begin/endInsert*, no reset),
    // shifts the stored selection to track the same logical cells, then reprojects
    // with repaint=true. Undo is out of scope (no QUndoStack in this layer).
    void insertRowAt(quint64 dataRow);
    void insertColumnAt(int col);
    void renameColumnHeader(int col);   // edit the header-row text of a column (header mode)
    void showRowHeaderMenu(const QPoint &pos);
    void showColumnHeaderMenu(const QPoint &pos);

    // --- Sort / filter view layer ---
    // Header single-click: Ctrl ⇒ column-select (relocated), else advance the
    // tri-state sort cycle (none→asc→desc→cleared) for `col`. Gated on
    // canSortFilter() (hidden + one-time notice above the window size).
    void onHeaderClicked(int col);
    void applySort(int col, bool active, Qt::SortOrder order); // set/clear header indicator + recompute
    // Filter strip (one QLineEdit per column in a viewport top-margin band).
    void buildFilterStrip();        // (re)create the line edits for the current column count
    QLineEdit *createFilterField(); // factory: one fully-wired + styled filter field (shared by build & splice)
    void onFilterColumnsInserted(int first, int last); // splice blank fields in at [first,last] (keeps text↔column aligned)
    void onFilterColumnsRemoved(int first, int last);  // delete the QLineEdits over [first,last] + erase the slots
    void rebuildFilterStripIfCountChanged();           // guarded modelReset handler: O(1) early-out when count unchanged
    void layoutFilterStrip();       // align each field to header section pos/size
    void updateFilterStripGeometry(); // reserve/clear the viewport top margin + place the strip
    void refreshFilterFieldStyle(QLineEdit *field); // active (non-empty) indicator: border + glyph
    void clearAllFilters();         // empty every field, drop indicators, recompute (keep sort)
    QStringList currentFilters() const; // per-column filter texts (size == columnCount)
    bool anyFilterActive() const;   // any non-empty filter field
    void resetSortFilterState();    // clear order + sort indicator + filter texts + rebuild/hide strip
    void updateSortFilterAvailability(); // enable/disable affordances on canSortFilter()

    // --- Combined recompute (sort + all filters → one view order) ---
    void recomputeView();           // sync for small candidate sets, worker above
    void onViewRecomputeFinished();  // apply a non-stale worker result to the model

    // --- External watcher ---
    void installWatcher();
    void onFileChangedExternally();

    // --- Find ---
    void openFindBar();
    void closeFindBar();
    void onFindTextChanged();
    void findNext(bool forward);
    void findAllAsync();
    bool searchFrom(quint64 startRow, int startCol, bool forward,
                    const QString &needle, quint64 &hitRow, int &hitCol) const;
    void focusHit(quint64 row, int col);

    // --- Wrap / resize ---
    void setWordWrapEnabled(bool on);
    void resizeVisibleRows();      // visible-range-only row resize (wrap on)

    // --- Theming helpers ---
    void applyFonts();
    void applyPaletteToChrome();   // push m_palette onto the view/find-bar/banner
    void retintIcons();
    void updateEncodingLabel();
    void showNotice(const QString &text);   // transient, non-blocking
    void setBannerVisible(bool visible, const QString &text = QString());

    NotepadNextApplication *m_app = nullptr;

    // Core
    std::unique_ptr<CsvDocument> m_doc;
    CsvTableModel *m_model = nullptr;
    QString m_path;
    QString m_displayName;

    // Worker
    QFutureWatcher<bool> *m_indexWatcher = nullptr;
    std::atomic<bool> m_cancelIndex{false};
    bool m_indexing = false;

    // Copy worker
    QFutureWatcher<QString> *m_copyWatcher = nullptr;
    std::atomic<bool> m_cancelCopy{false};

    // Find worker (count)
    QFutureWatcher<qint64> *m_findWatcher = nullptr;
    std::atomic<bool> m_cancelFind{false};
    std::atomic<int> m_findGeneration{0};

    // Sort/filter recompute worker. PER-LAUNCH CANCEL TOKEN: each run owns its own
    // std::atomic<bool> held by a shared_ptr; the worker lambda captures the token
    // BY VALUE (so it outlives a reassignment of m_viewCancel). Superseding sets the
    // OLD token true and mints a FRESH false token for the new run, so a new worker
    // can never observe a stale `true` and abort itself, and a superseded worker
    // stays cancelled (its token is never reset to false). A single shared atomic
    // could not give both guarantees under overlap. The generation counter is the
    // staleness signal for the finished() slot (every teardown bumps it); the slot
    // drops any result whose launch generation is no longer live.
    QFutureWatcher<QVector<quint32>> *m_viewWatcher = nullptr;
    std::shared_ptr<std::atomic<bool>> m_viewCancel;   // per-launch token (null until first run)
    std::atomic<int> m_viewGeneration{0};
    int m_viewInFlightGen = 0;                 // generation of the worker run currently watched
    int m_pendingViewSortColumn = -1;          // sort recorded for the in-flight run
    Qt::SortOrder m_pendingViewSortOrder = Qt::AscendingOrder;

    // View + chrome
    QTableView *m_view = nullptr;
    QScrollBar *m_absScroll = nullptr;     // absolute vertical scrollbar
    bool m_syncingScroll = false;          // re-entrancy guard for scroll sync
    QComboBox *m_delimiterCombo = nullptr;
    QCheckBox *m_headerToggle = nullptr;
    QCheckBox *m_wrapToggle = nullptr;
    QToolButton *m_insertButton = nullptr; // toolbar "Insert ▾" (append row/column)
    QToolButton *m_clearFiltersButton = nullptr; // toolbar "Clear all filters"
    QLabel *m_encodingLabel = nullptr;
    QLabel *m_noticeLabel = nullptr;       // transient large-file / clamp notice
    QTimer *m_noticeTimer = nullptr;

    // Filter strip: a band parented to the VIEW, living in a top margin the
    // CsvTableView reserves (below the horizontal header, above the data). Holds
    // one QLineEdit per column aligned to its header section. Rebuilt on
    // column-count change, re-laid on resize/move/scroll.
    QWidget *m_filterStrip = nullptr;
    QVector<QLineEdit *> m_filterFields;
    QTimer *m_filterDebounce = nullptr;    // ~150 ms debounce before recompute
    bool m_oversizeNoticeShown = false;    // one-time "too large to sort/filter" notice

    // Find bar
    QFrame *m_findBar = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QToolButton *m_findPrev = nullptr;
    QToolButton *m_findNext = nullptr;
    QToolButton *m_findClose = nullptr;
    QLabel *m_findCount = nullptr;
    QTimer *m_findDebounce = nullptr;

    // Conflict banner
    QFrame *m_banner = nullptr;
    QLabel *m_bannerLabel = nullptr;
    QPushButton *m_bannerButton = nullptr;

    // Wrap
    bool m_wrap = false;
    QTimer *m_wrapTimer = nullptr;

    // Selection (VIEW-ROW coordinates: m_anchorRow/m_caretRow are view rows that
    // map to data rows through CsvTableModel::viewRowToDataRow — the identity when
    // no sort/filter view is active, so this is byte-for-byte the prior absolute
    // behavior on the no-view path).
    quint64 m_anchorRow = 0;
    int m_anchorCol = 0;
    quint64 m_caretRow = 0;
    int m_caretCol = 0;
    bool m_hasSelection = false;

    // External watcher
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_watchDebounce = nullptr;
    bool m_suppressWatch = false;          // true around our own save

    // Theme
    QPalette m_palette;
    bool m_isDark = false;
    bool m_applyingPalette = false;        // re-entrancy guard for changeEvent(PaletteChange)

    // Undo/redo for structural + content edits (clear, delete row/column).
    QUndoStack *m_undoStack = nullptr;

    static constexpr qint64 kCopyWorkerThreshold = 4 * 1024 * 1024;  // 4 MB output → worker
    static constexpr qint64 kCopyWarnThreshold = 50 * 1024 * 1024;   // 50 MB → warn
    static constexpr int kViewSyncThreshold = 50000;                 // ≤ 50k candidate rows → sync recompute
    static constexpr int kFilterStripPadding = 6;                    // extra px around the filter line edits
};

#endif // CSVPREVIEWWIDGET_H
