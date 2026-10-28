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

#include "CsvPreviewWidget.h"
#include "CsvDocument.h"
#include "CsvTableModel.h"
#include "CsvSortFilter.h"
#include "CsvFilterStrip.h"
#include "CsvEditCommands.h"
#include "NotepadNextApplication.h"
#include "ApplicationSettings.h"
#include "EditorManager.h"
#include "ScintillaNext.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QComboBox>
#include <QToolButton>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QFrame>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QProgressBar>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenu>
#include <QUndoStack>
#include <QSaveFile>
#include <QGuiApplication>
#include <QFontMetrics>
#include <QItemSelectionModel>
#include <QItemSelection>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <climits>
#include <functional>
#include <memory>

// QTableView that reserves a top band (below the horizontal header, above the
// data) for the per-column filter strip. QTableView::updateGeometries() resets
// the viewport margins to (verticalHeaderWidth, horizontalHeaderHeight, 0, 0) on
// every call, so a one-shot setViewportMargins is clobbered. The override lets
// base run, then re-applies the margin WITH the strip band added on top of the
// header height. A full-suppression re-entry guard (m_inUpdate) is mandatory:
// our setViewportMargins resizes the viewport, which fires a resizeEvent →
// updateGeometries, and base's own internal setViewportMargins does the same —
// without the guard the base-resets-to-hh / we-set-to-hh+strip pair oscillates
// forever. With the guard, the re-entrant calls are no-ops and OUR margin (set
// last in the outermost call) is the resting state. The strip widget is parented
// to the VIEW (not the viewport) so it lives in that reserved band — the
// line-number-area idiom. The notify callback (a plain std::function, so no moc
// for this file-local helper) lets the widget re-place the field row after each
// layout. Known minor cosmetic: base computes the vertical scroll range against
// the viewport BEFORE our band is applied, so the range is generous by ~one row;
// it self-corrects on the next genuine geometry change and is well within budget.
namespace {
class CsvTableView : public QTableView
{
public:
    explicit CsvTableView(QWidget *parent = nullptr) : QTableView(parent) {}

    void setStripHeight(int h)
    {
        if (m_stripHeight == h) return;
        m_stripHeight = h;
        updateGeometries(); // re-reserve / release the band now
    }
    int stripHeight() const { return m_stripHeight; }
    void setGeometriesCallback(std::function<void()> cb) { m_onGeometries = std::move(cb); }

protected:
    void updateGeometries() override
    {
        if (m_inUpdate) return;       // suppress ALL re-entry (see class comment)
        m_inUpdate = true;
        QTableView::updateGeometries(); // base places headers + sets its margins
        if (m_stripHeight > 0 && !horizontalHeader()->isHidden()) {
            const int hh = horizontalHeader()->height();
            const int vw = verticalHeader()->isHidden() ? 0 : verticalHeader()->width();
            const QMargins m = viewportMargins();
            setViewportMargins(vw, hh + m_stripHeight, m.right(), m.bottom());
            // Base positioned the vertical header (row numbers) against the
            // PRE-band viewport top (just below the horizontal header). Our band
            // pushes the data viewport down by m_stripHeight, so realign the
            // vertical header to the viewport's NEW top/height — otherwise every
            // row number paints one band too high (e.g. "1" beside the filter
            // field, "2" beside the first data row). setViewportMargins ran
            // layoutChildren synchronously, so viewport()->geometry() already
            // reflects the band here.
            if (!verticalHeader()->isHidden()) {
                const QRect vg = viewport()->geometry();
                const QRect vh = verticalHeader()->geometry();
                verticalHeader()->setGeometry(vh.left(), vg.top(), vh.width(), vg.height());
            }
        }
        m_inUpdate = false;
        if (m_onGeometries) m_onGeometries();
    }

private:
    int m_stripHeight = 0;
    bool m_inUpdate = false;
    std::function<void()> m_onGeometries;
};
} // namespace


CsvPreviewWidget::CsvPreviewWidget(NotepadNextApplication *app, QWidget *parent)
    : PreviewContentWidget(parent)
    , m_app(app)
    , m_doc(std::make_unique<CsvDocument>())
{
    m_palette = app ? app->palette() : palette();
    m_isDark = app && app->isEffectiveThemeDark();
    m_undoStack = new QUndoStack(this);
    buildUi();

    if (app && app->getSettings()) {
        auto *settings = app->getSettings();
        connect(settings, &ApplicationSettings::fontNameChanged, this, [this]() { applyFonts(); });
        connect(settings, &ApplicationSettings::fontSizeChanged, this, [this]() { applyFonts(); });
        connect(settings, &ApplicationSettings::fontHintingChanged, this, [this]() { applyFonts(); });
    }
}

CsvPreviewWidget::~CsvPreviewWidget()
{
    // Cancel any in-flight workers and wait so their lambdas don't touch a
    // destroyed document, and (for the view worker) so its finished() slot can't
    // run against a destroyed `this`. Bump the view generation too so a result
    // already queued behind waitForFinished is treated as stale by the slot.
    m_cancelIndex.store(true, std::memory_order_relaxed);
    if (m_indexWatcher) { m_indexWatcher->waitForFinished(); }
    // The shared barrier cancels + joins the three document-reading workers (copy,
    // find, view) using the per-launch view token; it does not touch the index
    // watcher (joined above, before the document is destroyed with the widget).
    cancelAndJoinDocumentReaders();
}

void CsvPreviewWidget::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildToolbar();
    buildBanner();
    buildFindBar();

    m_view = new CsvTableView(this);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_view->setAlternatingRowColors(true);
    m_view->setWordWrap(false);
    m_view->setTextElideMode(Qt::ElideRight);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    m_view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->horizontalHeader()->setStretchLastSection(false);
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    // Header context menus drive row/column insertion.
    m_view->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->verticalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view->horizontalHeader(), &QWidget::customContextMenuRequested,
            this, &CsvPreviewWidget::showColumnHeaderMenu);
    connect(m_view->verticalHeader(), &QWidget::customContextMenuRequested,
            this, &CsvPreviewWidget::showRowHeaderMenu);
    // The internal vertical scrollbar is hidden; the absolute scrollbar (added
    // alongside) drives windowBase across the full row range.
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Absolute scrollbar lives to the right of the view, range [0, totalRows).
    m_absScroll = new QScrollBar(Qt::Vertical, this);
    m_absScroll->setVisible(false);
    connect(m_absScroll, &QScrollBar::valueChanged, this, &CsvPreviewWidget::onAbsoluteScroll);

    auto *viewRow = new QHBoxLayout;
    viewRow->setContentsMargins(0, 0, 0, 0);
    viewRow->setSpacing(0);
    viewRow->addWidget(m_view, 1);
    viewRow->addWidget(m_absScroll);
    root->addLayout(viewRow, 1);

    // Context menu (copy variants).
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        QAction *copy = menu.addAction(tr("Copy"));
        QAction *copyCsv = menu.addAction(tr("Copy as CSV"));
        QAction *copyMd = menu.addAction(tr("Copy as Markdown"));
        // Structural inserts anchored on the clicked cell.
        const QModelIndex idx = m_view->indexAt(pos);
        QAction *insRowAbove = nullptr, *insRowBelow = nullptr;
        QAction *insColBefore = nullptr, *insColAfter = nullptr;
        // Structural inserts are disabled while a sort/filter view is active: a
        // permutation array can't survive rows/cols inserted beneath it (clear
        // sort & filters first). Cell editing stays enabled.
        const bool editable = m_model && !m_model->isReadOnly() && !m_model->viewActive();
        if (editable) {
            menu.addSeparator();
            insRowAbove = menu.addAction(tr("Insert Row Above"));
            insRowBelow = menu.addAction(tr("Insert Row Below"));
            insColBefore = menu.addAction(tr("Insert Column Before"));
            insColAfter = menu.addAction(tr("Insert Column After"));
        }
        QAction *chosen = menu.exec(m_view->viewport()->mapToGlobal(pos));
        if (chosen == copy) copySelection(CopyFormat::Tsv);
        else if (chosen == copyCsv) copySelection(CopyFormat::Csv);
        else if (chosen == copyMd) copySelection(CopyFormat::Markdown);
        else if (chosen && chosen == insRowAbove)
            insertRowAt(idx.isValid() ? m_model->toAbsRow(idx.row()) : 0);
        else if (chosen && chosen == insRowBelow)
            insertRowAt(idx.isValid() ? m_model->toAbsRow(idx.row()) + 1 : m_model->dataRowCount());
        else if (chosen && chosen == insColBefore)
            insertColumnAt(idx.isValid() ? idx.column() : 0);
        else if (chosen && chosen == insColAfter)
            insertColumnAt(idx.isValid() ? idx.column() + 1 : m_model->virtualColumnCount());
    });

    setFocusPolicy(Qt::StrongFocus);
}

void CsvPreviewWidget::showRowHeaderMenu(const QPoint &pos)
{
    if (!m_model || m_model->isReadOnly()) return;
    QMenu menu(this);
    const int section = m_view->verticalHeader()->logicalIndexAt(pos);
    // Structural row edits are disabled while a sort/filter view is active (a
    // permutation array can't survive inserts/deletes beneath it). Clear sort &
    // filters first; cell editing remains available.
    const bool structuralOk = !m_model->viewActive();
    QAction *above = menu.addAction(tr("Insert Row Above"));
    QAction *below = menu.addAction(tr("Insert Row Below"));
    QAction *append = menu.addAction(tr("Append Row at End"));
    above->setEnabled(structuralOk);
    below->setEnabled(structuralOk);
    append->setEnabled(structuralOk);
    // Delete acts on the rows intersecting the current selection. If the clicked
    // section isn't in the selection, select that single row first so the action
    // is predictable. Disabled when there is nothing to delete.
    menu.addSeparator();
    QAction *del = menu.addAction(tr("Delete Row(s)"));
    del->setEnabled(structuralOk && m_model->dataRowCount() > 0);
    if (!structuralOk)
        menu.addAction(tr("Clear sort & filters to edit rows"))->setEnabled(false);
    QAction *chosen = menu.exec(m_view->verticalHeader()->mapToGlobal(pos));
    if (!chosen) return;
    // section is a proxy row; convert to absolute data row. Invalid section
    // (right-click below the last row) falls back to append.
    const bool valid = section >= 0;
    const quint64 absRow = valid ? m_model->toAbsRow(section) : m_model->dataRowCount();
    if (chosen == above) insertRowAt(valid ? absRow : 0);
    else if (chosen == below) insertRowAt(valid ? absRow + 1 : m_model->dataRowCount());
    else if (chosen == append) insertRowAt(m_model->dataRowCount());
    else if (chosen == del) {
        // Anchor the delete on the clicked row if it isn't already selected.
        if (valid) {
            quint64 minRow, maxRow; int minCol, maxCol;
            boundingBox(minRow, maxRow, minCol, maxCol);
            if (!m_hasSelection || absRow < minRow || absRow > maxRow)
                selectRow(section);
        }
        deleteSelectedRows();
    }
}

void CsvPreviewWidget::showColumnHeaderMenu(const QPoint &pos)
{
    if (!m_model || m_model->isReadOnly()) return;
    QMenu menu(this);
    const int section = m_view->horizontalHeader()->logicalIndexAt(pos);
    const bool valid = section >= 0;
    // Rename the header text — only meaningful in header mode on a real column.
    QAction *rename = nullptr;
    if (valid && m_model->headerMode()) {
        rename = menu.addAction(tr("Rename Column…"));
        menu.addSeparator();
    }
    QAction *before = menu.addAction(tr("Insert Column Before"));
    QAction *after = menu.addAction(tr("Insert Column After"));
    QAction *append = menu.addAction(tr("Append Column at End"));
    menu.addSeparator();
    QAction *del = menu.addAction(tr("Delete Column(s)"));
    // Structural column edits gated off under an active view (rename is a content
    // edit, left enabled above). Clear sort & filters first.
    const bool structuralOk = !m_model->viewActive();
    before->setEnabled(structuralOk);
    after->setEnabled(structuralOk);
    append->setEnabled(structuralOk);
    del->setEnabled(structuralOk && m_model->virtualColumnCount() > 0);
    if (!structuralOk)
        menu.addAction(tr("Clear sort & filters to edit columns"))->setEnabled(false);
    QAction *chosen = menu.exec(m_view->horizontalHeader()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == rename) renameColumnHeader(section);
    else if (chosen == before) insertColumnAt(valid ? section : 0);
    else if (chosen == after) insertColumnAt(valid ? section + 1 : m_model->virtualColumnCount());
    else if (chosen == append) insertColumnAt(m_model->virtualColumnCount());
    else if (chosen == del) {
        if (valid) {
            quint64 minRow, maxRow; int minCol, maxCol;
            boundingBox(minRow, maxRow, minCol, maxCol);
            if (!m_hasSelection || section < minCol || section > maxCol)
                selectColumn(section);
        }
        deleteSelectedColumns();
    }
}

void CsvPreviewWidget::renameColumnHeader(int col)
{
    if (!m_model || m_model->isReadOnly() || !m_model->headerMode() || col < 0)
        return;
    const QString current = m_model->columnLabel(col);
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Rename Column"),
        tr("Header for column %1:").arg(col + 1), QLineEdit::Normal, current, &ok);
    if (!ok) return;
    m_model->setHeaderText(col, text);
}

void CsvPreviewWidget::insertRowAt(quint64 dataRow)
{
    if (!m_model || m_model->isReadOnly()) return;
    const quint64 dr = m_model->dataRowCount();
    if (dataRow > dr) dataRow = dr;  // clamp (append)

    // The model's begin/endInsertRows needs the target row in the current window.
    // Rebase first if it is off-screen (window movement is the only sanctioned
    // reset; the insert signal itself never resets). For an append past the
    // window end, rebase so the tail is visible.
    const quint64 base = m_model->windowBase();
    const quint64 window = m_model->windowSize();
    if (dataRow < base || dataRow > base + window - 1) {
        const quint64 half = window / 2;
        rebaseTo(dataRow > half ? dataRow - half : 0);
    }

    m_model->insertBlankDataRow(dataRow);

    // Shift the stored selection so it tracks the SAME logical cells across the
    // insert (both model row indices and these absolute coords move +1 at the
    // same boundary, so projectSelection maps back onto the same cells).
    if (m_hasSelection) {
        if (m_anchorRow >= dataRow) ++m_anchorRow;
        if (m_caretRow >= dataRow) ++m_caretRow;
    } else {
        // No prior selection: place the caret on the new blank row.
        m_anchorRow = m_caretRow = dataRow;
        m_anchorCol = m_caretCol = 0;
        m_hasSelection = true;
    }
    clampCaretVisible();
    projectSelection(/*repaint=*/true);
}

void CsvPreviewWidget::insertColumnAt(int col)
{
    if (!m_model || m_model->isReadOnly()) return;
    const int vcc = m_model->virtualColumnCount();
    if (col < 0) col = 0;
    if (col > vcc) col = vcc;  // clamp (append)

    m_model->insertBlankColumn(col);

    if (m_hasSelection) {
        if (m_anchorCol >= col) ++m_anchorCol;
        if (m_caretCol >= col) ++m_caretCol;
    } else {
        m_anchorCol = m_caretCol = col;
        m_anchorRow = m_caretRow = m_model->windowBase();
        m_hasSelection = true;
    }
    projectSelection(/*repaint=*/true);
}

void CsvPreviewWidget::changeDelimiter()
{
    if (!m_doc) return;

    // A delimiter change moves field boundaries, so any pending overlay edits
    // (keyed by row/col) no longer map to meaningful cells — discard them.
    // The sort/filter view is likewise invalid (columns change): reset the
    // strip texts, sort indicator, and the model's view order before reindex.
    // reindex() → startIndexing() runs cancelAndJoinDocumentReaders() (cancel +
    // JOIN copy/find/view + generation bump) before mutating the index, so no
    // separate cancel is needed here — and setDelimiter() does not unmap.
    if (m_model) m_model->clearOverlay();
    resetSortFilterState();
    m_undoStack->clear(); // commands reference now-stale coordinates
    const QChar d = m_delimiterCombo->currentData().toChar();
    m_doc->setDelimiter(d.toLatin1());
    reindex();
}

void CsvPreviewWidget::buildToolbar()
{
    auto *bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("csvToolbar"));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    layout->addWidget(new QLabel(tr("Delimiter:"), bar));
    m_delimiterCombo = new QComboBox(bar);
    m_delimiterCombo->addItem(tr("Comma  ,"), QChar(','));
    m_delimiterCombo->addItem(tr("Semicolon  ;"), QChar(';'));
    m_delimiterCombo->addItem(tr("Tab  \\t"), QChar('\t'));
    m_delimiterCombo->addItem(tr("Pipe  |"), QChar('|'));
    layout->addWidget(m_delimiterCombo);
    connect(m_delimiterCombo, QOverload<int>::of(&QComboBox::activated), this,
            [this](int) { changeDelimiter(); });

    m_headerToggle = new QCheckBox(tr("First row as header"), bar);
    m_headerToggle->setChecked(true);
    layout->addWidget(m_headerToggle);
    connect(m_headerToggle, &QCheckBox::toggled, this, [this](bool on) {
        if (m_model) {
            // setHeaderMode drops the model's view order (row 0 moves between
            // header and data). Mirror that in the widget chrome: clear filter
            // texts, the sort indicator, and rebuild/hide the strip BEFORE the
            // model swap so no stale field/indicator survives the toggle.
            resetSortFilterState();
            m_model->setHeaderMode(on);
            fitColumnsToContent();
            syncAbsoluteScrollRange();
            updateSortFilterAvailability();
            // setHeaderMode's endResetModel() already repaints — skip the forced one.
            projectSelection(false);
        }
    });

    m_wrapToggle = new QCheckBox(tr("Word wrap"), bar);
    m_wrapToggle->setChecked(false);
    layout->addWidget(m_wrapToggle);
    connect(m_wrapToggle, &QCheckBox::toggled, this, &CsvPreviewWidget::setWordWrapEnabled);

    // "Insert ▾" — the discoverable surface for structural inserts (the header
    // context menus cover positional insert-above/below/before/after; this
    // appends at the end without needing a selection or right-click). Gated on
    // an editable model via the menu's aboutToShow.
    m_insertButton = new QToolButton(bar);
    m_insertButton->setText(tr("Insert"));
    m_insertButton->setPopupMode(QToolButton::InstantPopup);
    auto *insertMenu = new QMenu(m_insertButton);
    QAction *addRow = insertMenu->addAction(tr("Append Row"));
    QAction *addCol = insertMenu->addAction(tr("Append Column"));
    connect(addRow, &QAction::triggered, this, [this]() {
        if (m_model && !m_model->isReadOnly()) insertRowAt(m_model->dataRowCount());
    });
    connect(addCol, &QAction::triggered, this, [this]() {
        if (m_model && !m_model->isReadOnly()) insertColumnAt(m_model->virtualColumnCount());
    });
    connect(insertMenu, &QMenu::aboutToShow, this, [this, addRow, addCol]() {
        // Structural inserts disabled while a sort/filter view is active.
        const bool editable = m_model && !m_model->isReadOnly() && !m_model->viewActive();
        addRow->setEnabled(editable);
        addCol->setEnabled(editable);
    });
    m_insertButton->setMenu(insertMenu);
    layout->addWidget(m_insertButton);

    // "Clear all filters" — empties every filter field and recomputes (the sort,
    // if any, is preserved). Hidden until the filter strip exists with an active
    // field (ui-dna: chrome hides until it has a job). Enabled state tracked by
    // updateSortFilterAvailability / refreshFilterFieldStyle.
    m_clearFiltersButton = new QToolButton(bar);
    m_clearFiltersButton->setText(tr("Clear filters"));
    m_clearFiltersButton->setToolTip(tr("Clear all column filters"));
    m_clearFiltersButton->setAutoRaise(true);
    m_clearFiltersButton->hide();
    connect(m_clearFiltersButton, &QToolButton::clicked, this, &CsvPreviewWidget::clearAllFilters);
    layout->addWidget(m_clearFiltersButton);

    layout->addStretch(1);

    m_encodingLabel = new QLabel(bar);
    m_encodingLabel->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_encodingLabel);

    m_noticeLabel = new QLabel(bar);
    m_noticeLabel->setForegroundRole(QPalette::PlaceholderText);
    m_noticeLabel->hide();
    layout->addWidget(m_noticeLabel);

    m_noticeTimer = new QTimer(this);
    m_noticeTimer->setSingleShot(true);
    connect(m_noticeTimer, &QTimer::timeout, this, [this]() { if (m_noticeLabel) m_noticeLabel->hide(); });

    // Filter-typing debounce: a keystroke (re)starts it; on timeout one combined
    // recompute runs. ~150 ms matches the find-bar debounce.
    m_filterDebounce = new QTimer(this);
    m_filterDebounce->setSingleShot(true);
    m_filterDebounce->setInterval(150);
    connect(m_filterDebounce, &QTimer::timeout, this, &CsvPreviewWidget::recomputeView);

    // Add the toolbar to the root layout (top row).
    static_cast<QVBoxLayout *>(this->layout())->addWidget(bar);
}

void CsvPreviewWidget::buildBanner()
{
    m_banner = new QFrame(this);
    m_banner->setObjectName(QStringLiteral("csvConflictBanner"));
    auto *layout = new QHBoxLayout(m_banner);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    m_bannerLabel = new QLabel(m_banner);
    m_bannerLabel->setWordWrap(true);
    layout->addWidget(m_bannerLabel, 1);

    m_bannerButton = new QPushButton(tr("Open editor tab"), m_banner);
    layout->addWidget(m_bannerButton);
    connect(m_bannerButton, &QPushButton::clicked, this, [this]() {
        if (!m_app || m_path.isEmpty()) return;
        if (ScintillaNext *editor = m_app->getEditorManager()->getEditorByFilePath(m_path)) {
            // Surface the editor: focusing it raises its dock area.
            editor->grabFocus();
            editor->activateWindow();
        }
    });

    // Soft-yellow status accent, rounded 4px (per ui-dna status surfaces). Color
    // is paired with text + button so it is never the sole carrier of meaning.
    m_banner->setStyleSheet(QStringLiteral(
        "#csvConflictBanner { background: #fff3cd; border: 1px solid #ffeeba; border-radius: 4px; }"
        "#csvConflictBanner QLabel { color: #664d03; }"));
    m_banner->hide();
    static_cast<QVBoxLayout *>(this->layout())->addWidget(m_banner);
}

void CsvPreviewWidget::buildFindBar()
{
    m_findBar = new QFrame(this);
    m_findBar->setObjectName(QStringLiteral("csvFindBar"));
    auto *layout = new QHBoxLayout(m_findBar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(tr("Find"));
    m_findEdit->setClearButtonEnabled(true);
    layout->addWidget(m_findEdit, 1);

    m_findCount = new QLabel(m_findBar);
    m_findCount->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_findCount);

    m_findPrev = new QToolButton(m_findBar);
    m_findPrev->setText(QStringLiteral("▲")); // up triangle
    m_findPrev->setToolTip(tr("Previous match"));
    m_findPrev->setAutoRaise(true);
    layout->addWidget(m_findPrev);

    m_findNext = new QToolButton(m_findBar);
    m_findNext->setText(QStringLiteral("▼")); // down triangle
    m_findNext->setToolTip(tr("Next match"));
    m_findNext->setAutoRaise(true);
    layout->addWidget(m_findNext);

    m_findClose = new QToolButton(m_findBar);
    m_findClose->setText(QStringLiteral("✕")); // x
    m_findClose->setToolTip(tr("Close find"));
    m_findClose->setAutoRaise(true);
    layout->addWidget(m_findClose);

    m_findDebounce = new QTimer(this);
    m_findDebounce->setSingleShot(true);
    m_findDebounce->setInterval(150);
    connect(m_findDebounce, &QTimer::timeout, this, &CsvPreviewWidget::onFindTextChanged);

    connect(m_findEdit, &QLineEdit::textChanged, this, [this]() { m_findDebounce->start(); });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this]() { findNext(true); });
    connect(m_findNext, &QToolButton::clicked, this, [this]() { findNext(true); });
    connect(m_findPrev, &QToolButton::clicked, this, [this]() { findNext(false); });
    connect(m_findClose, &QToolButton::clicked, this, &CsvPreviewWidget::closeFindBar);

    m_findBar->hide();
    static_cast<QVBoxLayout *>(this->layout())->addWidget(m_findBar);
}

void CsvPreviewWidget::loadFromFile(const QString &path)
{
    m_path = QFileInfo(path).absoluteFilePath();
    m_displayName = QFileInfo(path).fileName();
    emit titleChanged(m_displayName);

    // New file: cancel + JOIN any in-flight document-reading worker (copy/find/
    // view) BEFORE m_doc->open() below — open() calls unmap() at its top, so a
    // still-running worker would dereference a freed mapping (use-after-unmap).
    // The join also makes the view worker's finished() slot drop its now-stale
    // result (generation bumped). Then clear all view chrome — sort indicator,
    // filter texts + indicators, the strip, and the model's view order. The
    // model's documentReloaded() also drops the order; clearing here covers the
    // first load (no prior model) and the widget-side chrome. (PROOF 3 / §8.)
    cancelAndJoinDocumentReaders();
    resetSortFilterState();

    const CsvDocument::OpenStatus st = m_doc->open(m_path);
    switch (st) {
    case CsvDocument::OpenStatus::TooLarge:
        showNotice(tr("File is larger than the 2 GB preview limit and cannot be opened."));
        return;
    case CsvDocument::OpenStatus::CannotOpen:
        showNotice(tr("Could not open the file for preview."));
        return;
    case CsvDocument::OpenStatus::MapFailed:
        showNotice(tr("Could not memory-map the file for preview."));
        return;
    case CsvDocument::OpenStatus::Empty:
        // An empty (zero-byte) file is still a valid, editable CSV: build the
        // normal 0-row model so Insert (append row/column) can grow it from
        // nothing. buildIndex on an unmapped empty doc is a no-op that yields
        // totalRows=0, maxFieldCount=1 — the worker path below handles it.
        showNotice(tr("Empty file — use Insert to add rows or columns."));
        break;
    case CsvDocument::OpenStatus::Ok:
        break;
    }

    // Reflect the sniffed delimiter in the combo without re-indexing.
    {
        const QChar d(m_doc->delimiter());
        const QSignalBlocker block(m_delimiterCombo);
        for (int i = 0; i < m_delimiterCombo->count(); ++i) {
            if (m_delimiterCombo->itemData(i).toChar() == d) { m_delimiterCombo->setCurrentIndex(i); break; }
        }
    }

    if (m_doc->fileSize() > CsvDocument::kSoftNotice)
        showNotice(tr("Large file — indexing may take about a second."));

    installWatcher();
    startIndexing();
}

void CsvPreviewWidget::cancelAndJoinDocumentReaders()
{
    // Single barrier for every background worker that reads the document's mmap
    // (parseRowUncached) — copy, find, AND the sort/filter view recompute. Setting
    // a cancel flag is NOT enough: the flags are relaxed and the worker may not
    // have observed them yet, so we must also waitForFinished() before the caller
    // unmaps/reopens m_doc. Otherwise a still-running worker dereferences a freed
    // mapping (use-after-unmap → crash). Generations are bumped so any result
    // already queued behind the join is dropped as stale by the finished() slots.
    m_cancelCopy.store(true, std::memory_order_relaxed);
    m_cancelFind.store(true, std::memory_order_relaxed);
    if (m_viewCancel) m_viewCancel->store(true, std::memory_order_relaxed); // current view token
    m_findGeneration.fetch_add(1, std::memory_order_relaxed);
    m_viewGeneration.fetch_add(1, std::memory_order_relaxed);
    if (m_copyWatcher) m_copyWatcher->waitForFinished();
    if (m_findWatcher) m_findWatcher->waitForFinished();
    if (m_viewWatcher) m_viewWatcher->waitForFinished();
}

void CsvPreviewWidget::startIndexing()
{
    if (m_indexing) {
        // Cancel the running pass first; the finished handler will re-launch via
        // reindex() if needed. For the initial load this branch never hits.
        m_cancelIndex.store(true, std::memory_order_relaxed);
        if (m_indexWatcher) m_indexWatcher->waitForFinished();
    }
    // A re-index mutates the document under the view + any copy/find/view worker.
    // Cancel + JOIN concurrent readers (shared barrier) and detach the model so
    // nothing reads a half-built index. The barrier covers copy, find, and the
    // sort/filter recompute (all read the mmap); generations are bumped so any
    // result already queued behind the join is dropped as stale.
    cancelAndJoinDocumentReaders();
    if (m_model) m_model->setReindexing(true);

    m_cancelIndex.store(false, std::memory_order_relaxed);
    m_indexing = true;

    if (!m_indexWatcher) {
        m_indexWatcher = new QFutureWatcher<bool>(this);
        connect(m_indexWatcher, &QFutureWatcher<bool>::finished, this, &CsvPreviewWidget::onIndexingFinished);
    }

    CsvDocument *doc = m_doc.get();
    // Capture a raw pointer to the atomic flag (stable for the widget's lifetime;
    // the dtor waits on the watcher before destroying the document).
    std::atomic<bool> *cancel = &m_cancelIndex;
    QFuture<bool> fut = QtConcurrent::run([doc, cancel]() -> bool {
        return doc->buildIndex(*cancel, nullptr);
    });
    m_indexWatcher->setFuture(fut);
    if (m_encodingLabel) m_encodingLabel->setText(tr("Indexing…"));
}

void CsvPreviewWidget::onIndexingFinished()
{
    m_indexing = false;
    if (!m_indexWatcher->result()) // cancelled
        return;

    if (!m_model) {
        m_model = new CsvTableModel(m_doc.get(), this);
        m_model->setHeaderMode(m_headerToggle->isChecked());
        connect(m_model, &CsvTableModel::overlayDirtyChanged, this, [this](bool dirty) {
            emit dirtyChanged(dirty);
            emit titleChanged(dirty ? QStringLiteral("• ") + m_displayName : m_displayName);
        });
        // Edit-commit conflict re-check: when an edit lands in the overlay,
        // confirm the file isn't open dirty in an editor (gate == handler).
        connect(m_model, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &, const QModelIndex &, const QVector<int> &roles) {
            if (roles.contains(Qt::EditRole))
                onCellEdited();
        });
        // Keep the per-column filter strip in sync with the model's column count at
        // ALL times — not just at index time (buildFilterStrip in onIndexingFinished
        // runs once per index). A live column insert/delete (menu, toolbar, undo)
        // must add/remove the matching filter field immediately. Position-aware so a
        // mid-insert keeps each typed filter glued to its own column (see handlers).
        // modelReset covers the undo-of-delete path (restoreSnapshot resets rather
        // than emitting columnsRemoved) but is count-guarded so the frequent
        // reset emitters (sort/filter apply, scroll-rebase) stay O(1) no-ops.
        connect(m_model, &QAbstractItemModel::columnsInserted, this,
                [this](const QModelIndex &, int first, int last) { onFilterColumnsInserted(first, last); });
        connect(m_model, &QAbstractItemModel::columnsRemoved, this,
                [this](const QModelIndex &, int first, int last) { onFilterColumnsRemoved(first, last); });
        connect(m_model, &QAbstractItemModel::modelReset, this,
                [this]() { rebuildFilterStripIfCountChanged(); });
        m_view->setModel(m_model);
        connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this](const QModelIndex &cur, const QModelIndex &) {
            if (cur.isValid()) {
                // Keep the absolute caret in sync with keyboard/mouse navigation
                // that Qt drives directly (so copy/find anchor correctly).
                m_caretRow = m_model->toAbsRow(cur.row());
                m_caretCol = cur.column();
                if (!m_hasSelection) {
                    m_anchorRow = m_caretRow;
                    m_anchorCol = m_caretCol;
                    m_hasSelection = true;
                }
            }
        });
        // Header single-click now drives the tri-state SORT cycle (column-select
        // relocated to Ctrl+click, read inside onHeaderClicked). setSectionsClickable
        // stays on so sectionClicked fires.
        m_view->horizontalHeader()->setSectionsClickable(true);
        // QTableView's ctor wires the header's sectionPressed→selectColumn and
        // sectionEntered→_q_selectColumn (Qt 6.5 qtableview.cpp:1373-1374), so a
        // plain header press ALSO runs QTableViewPrivate::selectColumn — a range
        // select from currentSelectionStartIndex's column to the clicked one
        // (qtableview.cpp:3463-3469). This widget drives its own caret/selection
        // and relocates column-select to Ctrl+click, so that built-in auto-select
        // is never wanted: it fights the sort cycle and, with a stale anchor,
        // paints a spurious multi-column band. Sever both connections; sectionClicked
        // (→onHeaderClicked) is unaffected. DO NOT re-add these on an upstream merge.
        //
        // String-based disconnect() returns false (and does NOTHING) if the slot
        // signature ever drifts — a future Qt bump or moc change would then silently
        // reintroduce the select-all bug. Assert the disconnects actually took so a
        // dev build trips loudly instead. (The SIGNAL/SLOT strings are exactly the
        // pair Qt itself disconnects in setSortingEnabled, qtableview.cpp:2708-2711.)
        const bool pressedDisc = disconnect(m_view->horizontalHeader(), SIGNAL(sectionPressed(int)),
                                            m_view, SLOT(selectColumn(int)));
        const bool enteredDisc = disconnect(m_view->horizontalHeader(), SIGNAL(sectionEntered(int)),
                                            m_view, SLOT(_q_selectColumn(int)));
        Q_ASSERT_X(pressedDisc, "CsvPreviewWidget",
                   "QTableView sectionPressed->selectColumn not severed; Qt slot signature changed?");
        Q_ASSERT_X(enteredDisc, "CsvPreviewWidget",
                   "QTableView sectionEntered->_q_selectColumn not severed; Qt slot signature changed?");
        Q_UNUSED(pressedDisc);  // release builds (NDEBUG) compile out the asserts
        Q_UNUSED(enteredDisc);
        connect(m_view->horizontalHeader(), &QHeaderView::sectionClicked, this,
                &CsvPreviewWidget::onHeaderClicked);
        // Re-layout the filter strip when columns are resized/moved or the view
        // scrolls horizontally so each field stays above its column.
        connect(m_view->horizontalHeader(), &QHeaderView::sectionResized, this,
                [this](int, int, int) { layoutFilterStrip(); });
        connect(m_view->horizontalHeader(), &QHeaderView::sectionMoved, this,
                [this](int, int, int) { layoutFilterStrip(); });
        connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this,
                [this](int) { layoutFilterStrip(); });
        // Row-header click → select the whole data row (proxy section → data row).
        connect(m_view->verticalHeader(), &QHeaderView::sectionClicked, this,
                [this](int section) { if (section >= 0) selectRow(section); });
        // Double-click a column DIVIDER (handle) → deliberate, user-initiated
        // full-content fit via Qt's resizeColumnToContents (sizeHintForColumn
        // scans the whole 256K-row window, O(rowCount) — acceptable only on this
        // explicit gesture, distinct from the bounded default fitColumnsToContent).
        connect(m_view->horizontalHeader(), &QHeaderView::sectionHandleDoubleClicked, this,
                [this](int col) { if (col >= 0) m_view->resizeColumnToContents(col); });
        // Double-click a column LABEL → rename its header (header mode only).
        connect(m_view->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this,
                [this](int col) { if (col >= 0 && m_model && m_model->headerMode()) renameColumnHeader(col); });
        connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this,
                &CsvPreviewWidget::onViewScrolled);
        // Absolute-coordinate selection input (anchor-click, shift-extend, drag).
        m_view->viewport()->installEventFilter(this);
        m_view->installEventFilter(this);
    } else {
        m_model->documentReloaded();
    }
    // A fresh index (initial, delimiter change, reload, post-save) invalidates any
    // captured undo snapshots — they reference the prior coordinate state.
    if (m_undoStack) m_undoStack->clear();

    configureView();
    syncAbsoluteScrollRange();
    updateEncodingLabel();
    applyFonts();
    // Fit AFTER applyFonts so the header font is settled and configureView's
    // default-section-size reset can't shrink the columns we just sized.
    fitColumnsToContent();

    // Build the per-column filter strip for the current column count and gate the
    // sort/filter affordances on canSortFilter() (hidden + one-time notice above
    // the window size). A fresh index means no view is active yet.
    buildFilterStrip();
    updateSortFilterAvailability();

    if (m_doc->rowClampActive())
        showNotice(tr("File has more than %1 rows; trailing rows are not shown.").arg(INT_MAX));

    // Re-evaluate the conflict guard now that content is live.
    conflictActive(true);
    emit indexed();
}

void CsvPreviewWidget::reindex()
{
    // Re-run the index pass (delimiter change / external reload). Keep the
    // overlay only for an external silent reload path; delimiter changes discard
    // it implicitly because field boundaries move (handled by callers).
    startIndexing();
}

void CsvPreviewWidget::configureView()
{
    if (!m_view || !m_model) return;
    // Uniform row height from font metrics (wrap off default).
    const QFontMetrics fm(m_view->font());
    const int h = fm.height() + 6;
    m_view->verticalHeader()->setDefaultSectionSize(h);
    // A sensible default column width; columns remain interactively resizable.
    m_view->horizontalHeader()->setDefaultSectionSize(120);
}

void CsvPreviewWidget::fitColumnsToContent()
{
    if (!m_view || !m_model) return;
    QHeaderView *header = m_view->horizontalHeader();
    const int cols = m_model->columnCount();
    if (cols <= 0) return;

    // Default sizing: every column is wide enough for BOTH its header label and
    // the cell data actually on screen. We sample only the top `kSampleRows`
    // window rows (never the whole file), so this stays O(sample × cols) — off
    // the row-scale hot path; a full-column content fit is reserved for the
    // divider double-click. Two guards keep it cheap even on pathological data:
    //   (1) per-cell measurement is capped at `kProbeChars` leading characters
    //       (a long field can't make horizontalAdvance() O(field length)), and
    //   (2) a column stops scanning once its content width reaches the cap.
    // The cap bounds only the CELL contribution so one freakishly wide cell
    // never blows out the layout; the header is never capped, so a long header
    // always shows in full (the column's reason to exist).
    constexpr int kSampleRows = 100;
    constexpr int kProbeChars = 96;
    constexpr int kMaxContentWidth = 400;
    const int minWidth = header->defaultSectionSize();

    const QFontMetrics headerFm(header->font());
    const QFontMetrics cellFm(m_view->font());
    const int headerPad = headerFm.averageCharWidth() * 2 + 12;
    const int cellPad = cellFm.averageCharWidth() * 2 + 8;
    const int sampleRows = std::min(kSampleRows, m_model->rowCount());

    for (int c = 0; c < cols; ++c) {
        // Header: the section size hint already folds in font, sort margin and
        // style padding; back it with a direct label measure as a floor.
        const QString label = m_model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
        const int headerWidth = std::max(header->sectionSizeHint(c),
                                         headerFm.horizontalAdvance(label) + headerPad);

        // Cell content from the sampled window rows, capped.
        int contentWidth = 0;
        for (int r = 0; r < sampleRows; ++r) {
            const QString cell = m_model->index(r, c).data(Qt::DisplayRole).toString();
            if (cell.isEmpty()) continue;
            const int len = std::min(cell.size(), qsizetype(kProbeChars));
            const int w = cellFm.horizontalAdvance(cell, len) + cellPad;
            if (w > contentWidth) {
                contentWidth = w;
                if (contentWidth >= kMaxContentWidth) { contentWidth = kMaxContentWidth; break; }
            }
        }

        header->resizeSection(c, std::max({minWidth, headerWidth, contentWidth}));
    }
}

void CsvPreviewWidget::syncAbsoluteScrollRange()
{
    if (!m_model || !m_absScroll) return;
    const quint64 dataRows = m_model->dataRowCount();
    const quint64 window = m_model->windowSize();

    if (dataRows <= window) {
        // Whole file fits one window: no re-base ever; the view's own scrolling
        // covers it. Hide the absolute scrollbar and let the view scroll.
        m_absScroll->setVisible(false);
        m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        return;
    }

    // Multi-window: the absolute scrollbar spans [0, dataRows) (clamped INT_MAX);
    // its value is the absolute TOP row. The view's own bar handles intra-window.
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_absScroll->setVisible(true);
    const int maxTop = static_cast<int>(qMin<quint64>(dataRows - 1, static_cast<quint64>(INT_MAX) - 1));
    const QSignalBlocker block(m_absScroll);
    m_absScroll->setRange(0, maxTop);
    m_absScroll->setPageStep(static_cast<int>(qMin<quint64>(window, INT_MAX)));
    m_absScroll->setSingleStep(1);
    m_absScroll->setValue(static_cast<int>(qMin<quint64>(m_model->windowBase(), INT_MAX)));
}

void CsvPreviewWidget::onAbsoluteScroll(int absTop)
{
    if (m_syncingScroll || !m_model) return;
    const quint64 want = static_cast<quint64>(absTop);
    const quint64 window = m_model->windowSize();
    const quint64 base = m_model->windowBase();

    // Keep the requested absolute top row visible. Re-base only when it leaves
    // the current window (margin = a fraction of the window so we don't re-base
    // every row near the edge).
    if (want < base || want >= base + window) {
        // Center the requested row in the new window for smooth continued scroll.
        const quint64 half = window / 2;
        const quint64 newBase = want > half ? want - half : 0;
        rebaseTo(newBase);
    }
    // Position the view's internal offset onto the requested row within window.
    const int proxyTop = static_cast<int>(want - m_model->windowBase());
    m_syncingScroll = true;
    m_view->verticalScrollBar()->setValue(proxyTop);
    m_syncingScroll = false;
}

void CsvPreviewWidget::onViewScrolled(int viewValue)
{
    if (m_syncingScroll || !m_model || !m_absScroll->isVisible()) return;
    // Mirror the view's intra-window scroll position into the absolute bar. When
    // the view reaches the bottom edge of the window and more data exists below,
    // advance the base by half a window so scrolling continues seamlessly. The
    // re-base is guarded by m_syncingScroll to avoid a scrollbar feedback loop.
    const quint64 window = m_model->windowSize();
    auto *vbar = m_view->verticalScrollBar();
    if (vbar->value() >= vbar->maximum()
        && m_model->windowBase() + window < m_model->dataRowCount()) {
        m_syncingScroll = true;
        rebaseTo(m_model->windowBase() + window / 2);
        syncAbsoluteScrollRange();
        m_syncingScroll = false;
        return;
    }
    const quint64 absTop = m_model->windowBase() + static_cast<quint64>(viewValue);
    const QSignalBlocker block(m_absScroll);
    m_absScroll->setValue(static_cast<int>(qMin<quint64>(absTop, INT_MAX)));
}

void CsvPreviewWidget::rebaseTo(quint64 desiredBase, bool centerScroll)
{
    if (!m_model) return;
    Q_UNUSED(centerScroll);
    // LOAD-BEARING INVARIANT: re-base only ever fires for files larger than one
    // window (dataRowCount() > kWindowSize). The filter strip's reserved band is
    // non-zero ONLY when canSortFilter() (dataRowCount() <= kWindowSize) is true.
    // The two are mutually exclusive, which is what lets CsvTableView::
    // updateGeometries() stay a pure pass-through during the re-base reset (its
    // m_stripHeight is 0 in this regime, so it never re-applies a margin that would
    // shift the viewport mid-rebase). Likewise a sort/filter view is never active
    // here (canSortFilter() is false), so viewRowToDataRow is the identity and the
    // window math below is unchanged. If a future change lets the strip coexist
    // with windowing, the updateGeometries pass-through and this rebase path must
    // be revisited together. Assert the model agrees we are in the windowed regime.
    Q_ASSERT(!m_model->canSortFilter() && !m_model->viewActive());
    if (!m_model->setWindowBase(desiredBase))
        return; // no movement
    // Re-project the absolute selection onto the new window in one pass.
    // repaint=false: setWindowBase's endResetModel() already schedules the
    // viewport repaint, so forcing a second here would double-paint the rebase
    // (notably on the per-tick absolute-scrollbar drag-across-window path).
    projectSelection(false);
}

void CsvPreviewWidget::projectSelection(bool repaint)
{
    if (!m_model || !m_view || !m_hasSelection) return;
    auto *sel = m_view->selectionModel();
    if (!sel) return;

    quint64 minRow, maxRow; int minCol, maxCol;
    boundingBox(minRow, maxRow, minCol, maxCol);

    const quint64 base = m_model->windowBase();
    const int rowCount = m_model->rowCount();
    const int colCount = m_model->columnCount();
    if (rowCount <= 0 || colCount <= 0) return;
    // Intersect the absolute rect with the actually-populated window rows.
    const quint64 lastInWindow = base + static_cast<quint64>(rowCount) - 1;
    const quint64 lo = qMax(minRow, base);
    const quint64 hi = qMin(maxRow, lastInWindow);
    const int clampedMinCol = qBound(0, minCol, colCount - 1);
    const int clampedMaxCol = qBound(0, maxCol, colCount - 1);

    QItemSelection selection;
    if (lo <= hi && maxRow >= base && minRow <= lastInWindow) {
        const QModelIndex tl = m_model->index(static_cast<int>(lo - base), clampedMinCol);
        const QModelIndex br = m_model->index(static_cast<int>(hi - base), clampedMaxCol);
        selection.select(tl, br);
    }
    const QSignalBlocker block(sel);
    sel->clear();
    if (!selection.isEmpty())
        sel->select(selection, QItemSelectionModel::Select);

    // Keep the current index on the caret when it is in-window. This also
    // DEFENDS the caret against the unblocked native path below: the press event
    // falls through to QTableView::mousePressEvent, which calls setCurrentIndex
    // on the clicked cell with signals unblocked. Because we pre-set current to
    // that SAME index here, Qt's setCurrentIndex early-returns without emitting
    // currentChanged, so the line-384 sync-back lambda never fires from native
    // and can't clobber the absolute caret. (On a click, caretProxy is always
    // >= 0 — the caret was just set from a visible cell — so this branch always
    // runs in the click flow; the skip-branch only happens on scroll/rebase,
    // which no native mousePress follows.) Do not remove this setCurrentIndex.
    const int caretProxy = m_model->toProxyRow(m_caretRow);
    if (caretProxy >= 0) {
        const QModelIndex cur = m_model->index(caretProxy, m_caretCol);
        sel->setCurrentIndex(cur, QItemSelectionModel::NoUpdate);
    }

    // projectSelection() ran clear()+select() under a QSignalBlocker (so the
    // currentChanged sync-back lambda can't clobber the absolute caret mid-
    // projection). On the CLICK/drag path the press event then falls through to
    // QTableView::mousePressEvent, which issues ClearAndSelect for the clicked
    // cell with signals UNBLOCKED — keeping the MODEL single-region, but its
    // selectionChanged repaints only native's own delta, so the region we
    // dropped under the blocker is orphaned on screen. On the keyboard/selectAll
    // paths the event is consumed, so the blocked mutation is the ONLY selection
    // change and nothing repaints at all. Both need one full viewport repaint:
    // every visible cell re-queries isSelected() against the settled model.
    // O(visible cells), never row-scale. Skipped (repaint=false) only on the
    // model-reset callers (rebaseTo / header-toggle) where endResetModel()
    // already schedules the paint — avoids a redundant second repaint on the
    // per-tick absolute-scrollbar drag-across-window path.
    if (repaint)
        m_view->viewport()->update();
}

// ---------------------------------------------------------------------------
// Selection (absolute data-row coordinates)
// ---------------------------------------------------------------------------

void CsvPreviewWidget::setAnchorCaret(quint64 row, int col)
{
    m_anchorRow = m_caretRow = row;
    m_anchorCol = m_caretCol = col;
    m_hasSelection = true;
}

void CsvPreviewWidget::extendCaret(quint64 row, int col)
{
    m_caretRow = row;
    m_caretCol = col;
    m_hasSelection = true;
}

void CsvPreviewWidget::boundingBox(quint64 &minRow, quint64 &maxRow, int &minCol, int &maxCol) const
{
    minRow = qMin(m_anchorRow, m_caretRow);
    maxRow = qMax(m_anchorRow, m_caretRow);
    minCol = qMin(m_anchorCol, m_caretCol);
    maxCol = qMax(m_anchorCol, m_caretCol);
}

quint64 CsvPreviewWidget::viewRowCount() const
{
    if (!m_model) return 0;
    // Under an active sort/filter view the selectable rows are the DISPLAYED rows
    // (rowCount() == m_viewOrder.size()); otherwise the full data-row count. The
    // identity (== dataRowCount()) when no view is active — every selection/nav
    // clamp below reduces to its prior form on the no-view path.
    return m_model->viewActive() ? static_cast<quint64>(m_model->rowCount())
                                 : m_model->dataRowCount();
}

void CsvPreviewWidget::selectAll()
{
    if (!m_model) return;
    const quint64 dr = viewRowCount();
    if (dr == 0) return;
    m_anchorRow = 0;
    m_anchorCol = 0;
    m_caretRow = dr - 1;
    m_caretCol = qMax(0, m_model->columnCount() - 1);
    m_hasSelection = true;
    projectSelection();
}

void CsvPreviewWidget::selectColumn(int col)
{
    if (!m_model || col < 0) return;
    const quint64 dr = viewRowCount();
    if (dr == 0) return;
    m_anchorRow = 0;
    m_anchorCol = col;
    m_caretRow = dr - 1;
    m_caretCol = col;
    m_hasSelection = true;
    projectSelection();
}

void CsvPreviewWidget::selectRow(int dataRow)
{
    if (!m_model || dataRow < 0) return;
    const int cols = m_model->columnCount();
    if (cols == 0) return;
    // `dataRow` is a proxy section → absolute data row. Span all columns.
    const quint64 absRow = m_model->toAbsRow(dataRow);
    m_anchorRow = m_caretRow = absRow;
    m_anchorCol = 0;
    m_caretCol = cols - 1;
    m_hasSelection = true;
    projectSelection();
}

void CsvPreviewWidget::clearSelectedCells()
{
    if (!m_model || m_model->isReadOnly() || !m_hasSelection) return;
    quint64 minRow, maxRow; int minCol, maxCol;
    boundingBox(minRow, maxRow, minCol, maxCol);
    if (!m_model->viewActive()) {
        // No view: minRow/maxRow ARE data rows (identity) → one rectangle command,
        // byte-for-byte the prior behavior.
        m_undoStack->push(new ClearCellsCommand(m_model, minRow, maxRow, minCol, maxCol));
    } else {
        // Active view: the selected VIEW rows map to scattered data rows. Clear
        // each displayed row's data row in its own command, grouped into ONE undo
        // step. Cell-content clearing stays enabled under a view (no structural
        // change); the view order is untouched (no auto re-filter).
        m_undoStack->beginMacro(tr("Clear Cells"));
        for (quint64 v = minRow; v <= maxRow; ++v) {
            const quint64 dataRow = m_model->viewRowToDataRow(static_cast<int>(v));
            m_undoStack->push(new ClearCellsCommand(m_model, dataRow, dataRow, minCol, maxCol));
        }
        m_undoStack->endMacro();
    }
    projectSelection(/*repaint=*/true);
}

void CsvPreviewWidget::deleteSelectedRows()
{
    if (!m_model || m_model->isReadOnly() || !m_hasSelection) return;
    const quint64 dr = m_model->dataRowCount();
    if (dr == 0) return;
    quint64 minRow, maxRow; int minCol, maxCol;
    boundingBox(minRow, maxRow, minCol, maxCol);
    maxRow = qMin(maxRow, dr - 1);
    if (minRow > maxRow) return;
    // The widget keeps the range in-window; rebase if the top scrolled off.
    if (minRow < m_model->windowBase())
        rebaseTo(minRow);
    m_undoStack->push(new DeleteRowsCommand(m_model, minRow, maxRow));
    // Selection collapses onto the row that now occupies the deleted position.
    const quint64 newDr = m_model->dataRowCount();
    if (newDr == 0) {
        m_hasSelection = false;
    } else {
        const quint64 r = qMin(minRow, newDr - 1);
        m_anchorRow = m_caretRow = r;
        m_anchorCol = qMin<int>(m_anchorCol, m_model->columnCount() - 1);
        m_caretCol = qMin<int>(m_caretCol, m_model->columnCount() - 1);
    }
    projectSelection(/*repaint=*/true);
}

void CsvPreviewWidget::deleteSelectedColumns()
{
    if (!m_model || m_model->isReadOnly() || !m_hasSelection) return;
    const int vcc = m_model->virtualColumnCount();
    if (vcc == 0) return;
    quint64 minRow, maxRow; int minCol, maxCol;
    boundingBox(minRow, maxRow, minCol, maxCol);
    minCol = qMax(0, minCol);
    maxCol = qMin(maxCol, vcc - 1);
    if (minCol > maxCol) return;
    m_undoStack->push(new DeleteColumnsCommand(m_model, minCol, maxCol));
    const int newCols = m_model->columnCount();
    if (newCols == 0) {
        m_hasSelection = false;
    } else {
        const int c = qMin(minCol, newCols - 1);
        m_anchorCol = m_caretCol = c;
    }
    projectSelection(/*repaint=*/true);
}

void CsvPreviewWidget::clampCaretVisible()
{
    if (!m_model) return;
    const quint64 base = m_model->windowBase();
    const quint64 window = m_model->windowSize();
    if (m_caretRow < base || m_caretRow >= base + window) {
        const quint64 half = window / 2;
        rebaseTo(m_caretRow > half ? m_caretRow - half : 0);
        syncAbsoluteScrollRange();
    }
    const int proxy = m_model->toProxyRow(m_caretRow);
    if (proxy >= 0)
        m_view->scrollTo(m_model->index(proxy, m_caretCol));
}

// ---------------------------------------------------------------------------
// Copy
// ---------------------------------------------------------------------------

static QString markdownEscapeCell(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const QChar ch : value) {
        if (ch == QLatin1Char('|')) out.append(QStringLiteral("\\|"));
        else if (ch == QLatin1Char('\n')) out.append(QStringLiteral("<br>"));
        else if (ch == QLatin1Char('\r')) { /* drop bare CR, paired CRLF handled by \n */ }
        else out.append(ch); // backticks left as-is, whitespace preserved
    }
    return out;
}

qint64 CsvPreviewWidget::estimateCopyBytes(quint64 minRow, quint64 maxRow, int minCol, int maxCol) const
{
    // Rough: rows * cols * avg-cell. Used only to decide sync vs worker.
    const quint64 rows = maxRow - minRow + 1;
    const int cols = maxCol - minCol + 1;
    return static_cast<qint64>(rows) * cols * 16;
}

QString CsvPreviewWidget::serializeSelection(CopyFormat fmt) const
{
    quint64 minRow, maxRow; int minCol, maxCol;
    boundingBox(minRow, maxRow, minCol, maxCol);
    if (!m_model) return {};

    QString out;
    const char delim = (fmt == CopyFormat::Csv) ? m_doc->delimiter() : '\t';

    if (fmt == CopyFormat::Markdown) {
        // Header row from real names / A-B-C of the selected columns.
        out.append(QLatin1Char('|'));
        for (int c = minCol; c <= maxCol; ++c) {
            out.append(QLatin1Char(' '));
            out.append(markdownEscapeCell(m_model->columnLabel(c)));
            out.append(QStringLiteral(" |"));
        }
        out.append(QLatin1Char('\n'));
        out.append(QLatin1Char('|'));
        for (int c = minCol; c <= maxCol; ++c)
            out.append(QStringLiteral(" --- |"));
        out.append(QLatin1Char('\n'));
    }

    for (quint64 r = minRow; r <= maxRow; ++r) {
        // r is a VIEW row; translate to the data row before reading the cell so
        // copy emits rows in displayed (sorted/filtered) order. Identity when no
        // view is active. The int cast is safe — r < rowCount() ≤ INT_MAX.
        const quint64 dataRow = m_model->viewRowToDataRow(static_cast<int>(r));
        if (fmt == CopyFormat::Markdown) out.append(QLatin1Char('|'));
        for (int c = minCol; c <= maxCol; ++c) {
            const QString cell = m_model->absoluteCell(dataRow, c);
            if (fmt == CopyFormat::Markdown) {
                out.append(QLatin1Char(' '));
                out.append(markdownEscapeCell(cell));
                out.append(QStringLiteral(" |"));
            } else if (fmt == CopyFormat::Csv) {
                out.append(CsvDocument::serializeField(cell, delim));
                if (c < maxCol) out.append(QLatin1Char(delim));
            } else { // Tsv
                // TSV: strip tabs/newlines inside a cell to keep it grid-pasteable.
                QString clean = cell;
                clean.replace(QLatin1Char('\t'), QLatin1Char(' '));
                clean.replace(QLatin1Char('\n'), QLatin1Char(' '));
                clean.replace(QLatin1Char('\r'), QString());
                out.append(clean);
                if (c < maxCol) out.append(QLatin1Char('\t'));
            }
        }
        out.append(QLatin1Char('\n'));
    }
    return out;
}

void CsvPreviewWidget::copySelection(CopyFormat fmt)
{
    if (!m_model || !m_hasSelection) return;
    quint64 minRow, maxRow; int minCol, maxCol;
    boundingBox(minRow, maxRow, minCol, maxCol);

    if (estimateCopyBytes(minRow, maxRow, minCol, maxCol) > kCopyWorkerThreshold) {
        startWorkerCopy(fmt, minRow, maxRow, minCol, maxCol);
        return;
    }
    QGuiApplication::clipboard()->setText(serializeSelection(fmt));
}

void CsvPreviewWidget::startWorkerCopy(CopyFormat fmt, quint64 minRow, quint64 maxRow, int minCol, int maxCol)
{
    const qint64 est = estimateCopyBytes(minRow, maxRow, minCol, maxCol);
    if (est > kCopyWarnThreshold) {
        const auto answer = QMessageBox::question(this, tr("Large copy"),
            tr("This selection is large (~%1 MB). Copy anyway?").arg(est / (1024 * 1024)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    m_cancelCopy.store(false, std::memory_order_relaxed);
    if (!m_copyWatcher) {
        m_copyWatcher = new QFutureWatcher<QString>(this);
        connect(m_copyWatcher, &QFutureWatcher<QString>::finished, this, [this]() {
            if (m_cancelCopy.load(std::memory_order_relaxed)) return;
            QGuiApplication::clipboard()->setText(m_copyWatcher->result());
            showNotice(tr("Copied."));
        });
    }

    // Snapshot everything the worker needs; read via the document's thread-safe
    // sequential primitives + the model's overlay (immutable during the copy).
    // Snapshot everything the worker needs. The overlay QHash is copied (cheap
    // COW) so the worker never races the UI thread mutating the live overlay,
    // and the document's parseRowUncached/serializeField are const + thread-safe
    // (immutable mmap, no LRU). Concurrent re-index is prevented: startIndexing
    // cancels+joins this worker before mutating the document.
    CsvDocument *doc = m_doc.get();
    const char delim = (fmt == CopyFormat::Csv) ? m_doc->delimiter() : '\t';
    const bool headerMode = m_model->headerMode();
    const QHash<quint64, QString> overlay = m_model->overlay(); // COW snapshot
    // Snapshot the structural-insert layer so the worker translates virtual →
    // physical with the SAME primitive the model uses (no drift, stays async).
    // The UI thread is barred from re-indexing/mutating until this worker is
    // cancelled+joined, so these frozen lists are safe to read off-thread.
    const QList<quint64> insRows = m_model->insertedFileRowsSnapshot();
    const QList<int> insCols = m_model->insertedColsSnapshot();
    const QList<quint64> delRows = m_model->deletedFileRowsSnapshot();
    const QList<int> delCols = m_model->deletedColsSnapshot();
    // Frozen view order: minRow/maxRow are VIEW rows; the worker maps each to its
    // data row through this snapshot (empty ⇒ identity, no view active). Captured
    // by value so the worker never reads the live array.
    const QVector<quint32> viewOrder = m_model->viewOrderSnapshot();
    QStringList colLabels;
    for (int c = minCol; c <= maxCol; ++c)
        colLabels << m_model->columnLabel(c);
    std::atomic<bool> *cancel = &m_cancelCopy;

    QFuture<QString> fut = QtConcurrent::run(
        [doc, fmt, delim, headerMode, overlay, insRows, insCols, delRows, delCols, viewOrder, colLabels, minRow, maxRow, minCol, maxCol, cancel]() -> QString {
            QString out;
            // viewOrder maps a VIEW row to its data row (empty ⇒ identity). dataRow
            // / c are then VIRTUAL coordinates: overlay is virtual-keyed; the
            // document is physical, so translate both (inserted+deleted composed)
            // and treat -1 as a blank inserted cell.
            auto viewToData = [&](quint64 viewRow) -> quint64 {
                return viewOrder.isEmpty() ? viewRow
                                           : static_cast<quint64>(viewOrder.at(static_cast<qsizetype>(viewRow)));
            };
            auto cellAt = [&](quint64 dataRow, int c) -> QString {
                const quint64 vFileRow = headerMode ? dataRow + 1 : dataRow;
                auto it = overlay.constFind(CsvTableModel::overlayKey(vFileRow, c));
                if (it != overlay.constEnd()) return it.value();
                const qint64 physFileRow = CsvTableModel::physicalComposed(insRows, delRows, vFileRow);
                const qint64 physCol = CsvTableModel::physicalComposed(insCols, delCols, c);
                if (physFileRow < 0 || physCol < 0) return QString();
                const QVector<QString> fields = doc->parseRowUncached(static_cast<quint64>(physFileRow));
                return physCol < fields.size() ? fields.at(static_cast<int>(physCol)) : QString();
            };
            if (fmt == CopyFormat::Markdown) {
                out.append(QLatin1Char('|'));
                for (int c = minCol; c <= maxCol; ++c) {
                    out.append(QLatin1Char(' '));
                    out.append(markdownEscapeCell(colLabels.value(c - minCol)));
                    out.append(QStringLiteral(" |"));
                }
                out.append(QLatin1Char('\n')).append(QLatin1Char('|'));
                for (int c = minCol; c <= maxCol; ++c) out.append(QStringLiteral(" --- |"));
                out.append(QLatin1Char('\n'));
            }
            for (quint64 r = minRow; r <= maxRow; ++r) {
                if (cancel->load(std::memory_order_relaxed)) return QString();
                const quint64 dataRow = viewToData(r);
                if (fmt == CopyFormat::Markdown) out.append(QLatin1Char('|'));
                for (int c = minCol; c <= maxCol; ++c) {
                    const QString cell = cellAt(dataRow, c);
                    if (fmt == CopyFormat::Markdown) {
                        out.append(QLatin1Char(' ')).append(markdownEscapeCell(cell)).append(QStringLiteral(" |"));
                    } else if (fmt == CopyFormat::Csv) {
                        out.append(CsvDocument::serializeField(cell, delim));
                        if (c < maxCol) out.append(QLatin1Char(delim));
                    } else {
                        QString clean = cell;
                        clean.replace(QLatin1Char('\t'), QLatin1Char(' '));
                        clean.replace(QLatin1Char('\n'), QLatin1Char(' '));
                        clean.replace(QLatin1Char('\r'), QString());
                        out.append(clean);
                        if (c < maxCol) out.append(QLatin1Char('\t'));
                    }
                }
                out.append(QLatin1Char('\n'));
            }
            return out;
        });
    m_copyWatcher->setFuture(fut);
    showNotice(tr("Copying…"));
}

// ---------------------------------------------------------------------------
// Edit / save / conflict guard
// ---------------------------------------------------------------------------

bool CsvPreviewWidget::conflictActive(bool refresh)
{
    if (!m_app || m_path.isEmpty()) return false;
    ScintillaNext *editor = m_app->getEditorManager()->getEditorByFilePath(m_path);
    const bool conflict = editor && editor->modify();
    if (refresh) {
        if (m_model) m_model->setReadOnly(conflict);
        if (conflict) {
            setBannerVisible(true, tr("This file is open in an editor with unsaved changes. "
                                      "The preview is read-only until the editor is saved or reverted."));
        } else {
            setBannerVisible(false);
        }
    }
    return conflict;
}

QString CsvPreviewWidget::saveToDisk()
{
    // Persist when there is ANY pending edit — overlay cells OR structural inserts.
    if (!m_model || !m_doc || (!m_model->hasOverlay() && !m_model->hasStructuralEdits()))
        return QString();
    // Re-check the conflict guard at the moment of save (gate == handler).
    if (conflictActive(true)) {
        const QString blocked = tr("Save blocked — the file is open in an editor with unsaved changes.");
        showNotice(blocked);
        return blocked;
    }

    const QString tmpPath = m_path; // QSaveFile writes a temp in the same dir then renames
    QSaveFile out(tmpPath);
    if (!out.open(QIODevice::WriteOnly)) {
        const QString error = tr("Could not open the file for writing.");
        showNotice(error);
        return error;
    }

    // Decide the save strategy ONCE at save start. The verbatim byte-slice
    // optimization is only valid when the backing buffer is already in the
    // OUTPUT encoding. For a transcoded document (UTF-16/UTF-32 → held UTF-8
    // buffer) the backing bytes are UTF-8 while the output BOM/EOL/edited rows
    // are in the ORIGINAL codec — slicing verbatim would splice UTF-8 bytes into
    // a UTF-16/32 file (silent corruption). So when transcoded, EVERY row is
    // re-serialized from its parsed fields and encoded in the original codec,
    // and the terminator is encoded too, yielding one consistently-encoded file.
    const bool transcoded = m_doc->isTranscoded();

    const QByteArray bom = m_doc->bomBytes();
    if (!bom.isEmpty()) out.write(bom);

    const QByteArray eolRaw = m_doc->eolBytes();
    // Non-transcoded codecs (UTF-8/SBCS/DBCS) encode LF/CR as those same ASCII
    // bytes, so the raw EOL bytes are written verbatim. Transcoded files need the
    // terminator encoded in the original codec too (e.g. 0x0A 0x00 for UTF-16LE).
    const QByteArray eolBytes = transcoded
        ? m_doc->encodeForSave(QString::fromLatin1(eolRaw))
        : eolRaw;

    const QHash<quint64, QString> &overlay = m_model->overlay();
    const char delim = m_doc->delimiter();
    // VIRTUAL walk: iterate virtual file rows so inserted blank rows are emitted
    // in order. The byte-offset cursor (`offset`) advances ONLY on physical rows
    // (an inserted blank has no document bytes); virtual order preserves physical
    // order — inserts interleave blanks, never reorder — so the cursor stays
    // monotonic and no physical row is skipped.
    const quint64 vTotalRows = m_model->virtualTotalFileRows();
    const int vCols = m_model->virtualColumnCount();
    const bool hasColInserts = m_model->hasInsertedColumns();

    // Stream row by row, O(F) sequential walk, O(1) memory (one row buffer max):
    //  - non-transcoded, no inserted columns, unedited physical row → verbatim
    //    byte slice (the fast path);
    //  - inserted blank row → emit blank/overlay virtual columns, cursor NOT moved;
    //  - otherwise re-serialize from parsed fields + overlay, in virtual columns.
    qint64 offset = m_doc->firstRowOffset();
    QVector<QString> fields;
    const char *base = m_doc->data();
    for (quint64 vr = 0; vr < vTotalRows; ++vr) {
        const qint64 rowStart = offset;
        const qint64 physRow = m_model->virtualFileRowToPhysical(vr); // -1 == blank
        const bool blankRow = (physRow < 0);

        // Verbatim slicing is valid only for a non-transcoded buffer, a physical
        // row, no inserted columns (those must be injected per row), and no
        // overlay edit on this virtual row.
        bool verbatim = !transcoded && !blankRow && !hasColInserts;
        if (verbatim) {
            for (int c = 0; c < vCols; ++c) {
                if (overlay.contains(CsvTableModel::overlayKey(vr, c))) { verbatim = false; break; }
            }
        }

        qint64 rowEnd = rowStart; // unchanged for a blank row (cursor not advanced)
        if (verbatim) {
            // Verbatim byte slice — preserves original quoting/encoding exactly.
            rowEnd = m_doc->rowEnd(rowStart); // one quote-aware scan, includes terminator
            qint64 contentEnd = rowEnd;
            // Trim trailing CR/LF that rowEnd included; field bytes are untouched.
            while (contentEnd > rowStart &&
                   (base[contentEnd - 1] == '\n' || base[contentEnd - 1] == '\r'))
                --contentEnd;
            out.write(base + rowStart, contentEnd - rowStart);
            offset = rowEnd; // advance: physical row consumed
        } else if (blankRow) {
            // Inserted blank row: emit virtual columns from the overlay only
            // (every physical column is absent). Cursor is NOT advanced.
            QString line;
            for (int vc = 0; vc < vCols; ++vc) {
                auto it = overlay.constFind(CsvTableModel::overlayKey(vr, vc));
                const QString value = (it != overlay.constEnd()) ? it.value() : QString();
                line.append(CsvDocument::serializeField(value, delim));
                if (vc < vCols - 1) line.append(QLatin1Char(delim));
            }
            out.write(m_doc->encodeForSave(line));
        } else {
            // Re-serialize a physical row in VIRTUAL columns: overlay value if
            // present, else the parsed physical field (inserted columns map to
            // physical -1 → blank). The sequential parse also yields rowEnd.
            rowEnd = m_doc->parseRowSequential(rowStart, fields);
            QString line;
            for (int vc = 0; vc < vCols; ++vc) {
                auto it = overlay.constFind(CsvTableModel::overlayKey(vr, vc));
                QString value;
                if (it != overlay.constEnd()) {
                    value = it.value();
                } else {
                    const int physCol = m_model->virtualColToPhysical(vc);
                    if (physCol >= 0 && physCol < fields.size())
                        value = fields.at(physCol);
                }
                line.append(CsvDocument::serializeField(value, delim));
                if (vc < vCols - 1) line.append(QLatin1Char(delim));
            }
            out.write(m_doc->encodeForSave(line));
            offset = rowEnd; // advance: physical row consumed
        }

        // Terminator policy. Between rows always emit EOL. For the FINAL virtual
        // row the question is whether the file should end with a terminator:
        //  - If the final row is an INSERTED BLANK, the user explicitly added a
        //    trailing row. The preceding row already got its EOL above, and we
        //    terminate the blank too, so the file ends "…\n\n". The reindexer
        //    counts a row per terminator (CsvDocument.cpp:357), so this reloads as
        //    a real trailing empty row — the append round-trips (verified by
        //    tests/test_csv_table_model_insert saveWalk_appendTrailingBlank).
        //  - Otherwise preserve the original file's trailing-terminator presence
        //    by probing the (unchanged) backing buffer's last byte.
        const bool lastRow = (vr + 1 == vTotalRows);
        if (!lastRow) {
            out.write(eolBytes);
        } else if (blankRow) {
            out.write(eolBytes);
        } else {
            const qint64 dataSize = m_doc->dataSize();
            if (dataSize > 0 && (base[dataSize - 1] == '\n' || base[dataSize - 1] == '\r'))
                out.write(eolBytes);
        }
    }

    // Join document-reading workers (copy/find/view) before the unmap → commit →
    // re-open sequence below. The write loop above only read m_doc on the UI
    // thread (safe — the mmap is immutable), but unmap()/remap()/open() tear the
    // mapping down, so a still-running background reader would hit freed pages.
    cancelAndJoinDocumentReaders();

    // Suppress our own write triggering the external-change watcher: both drop
    // the watch path (so the FS event is never queued) and latch a flag (belt +
    // suspenders against an already-queued event).
    m_suppressWatch = true;
    if (m_watcher && m_watcher->files().contains(m_path))
        m_watcher->removePath(m_path);

#ifdef Q_OS_WIN
    // Windows: the mmap holds a handle that blocks the rename. Release it first.
    m_doc->unmap();
#endif

    if (!out.commit()) {
        showNotice(tr("Failed to save the file."));
        m_suppressWatch = false;
#ifdef Q_OS_WIN
        m_doc->remap(); // restore the mapping so the preview keeps working
        m_model->documentReloaded();
#endif
        return tr("Failed to save the file.");
    }

    // Re-open + re-index on the worker so byte offsets match the new on-disk
    // layout. Keep the overlay visible until the re-index completes (the model
    // still serves overlay values), then clear it on success.
#ifdef Q_OS_WIN
    m_doc->open(m_path); // re-map the renamed file
#else
    m_doc->open(m_path);
#endif

    // Synchronous-light re-sniff already done by open(); kick the index pass.
    // On completion we clear the overlay + dirty indicator.
    if (!m_indexWatcher) {
        m_indexWatcher = new QFutureWatcher<bool>(this);
        connect(m_indexWatcher, &QFutureWatcher<bool>::finished, this, &CsvPreviewWidget::onIndexingFinished);
    }
    // One-shot post-save handler: clear overlay after the fresh index lands.
    auto *conn = new QMetaObject::Connection;
    *conn = connect(m_indexWatcher, &QFutureWatcher<bool>::finished, this, [this, conn]() {
        disconnect(*conn);
        delete conn;
        if (m_model) m_model->clearOverlay();
        m_suppressWatch = false;
        // Re-arm the external watcher on the freshly written file.
        if (m_watcher && !m_path.isEmpty() && !m_watcher->files().contains(m_path)
            && QFileInfo::exists(m_path))
            m_watcher->addPath(m_path);
        showNotice(tr("Saved."));
    });
    startIndexing();
    return QString();
}

// ---------------------------------------------------------------------------
// External-change watcher (self-owned, debounced)
// ---------------------------------------------------------------------------

void CsvPreviewWidget::installWatcher()
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        m_watchDebounce = new QTimer(this);
        m_watchDebounce->setSingleShot(true);
        m_watchDebounce->setInterval(150);
        connect(m_watchDebounce, &QTimer::timeout, this, &CsvPreviewWidget::onFileChangedExternally);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &) {
            if (m_suppressWatch) return; // our own save
            m_watchDebounce->start();
        });
    }
    if (!m_path.isEmpty() && !m_watcher->files().contains(m_path))
        m_watcher->addPath(m_path);
}

void CsvPreviewWidget::onFileChangedExternally()
{
    if (m_suppressWatch) return;
    // Some editors replace the file (delete+create); re-add the watch if dropped.
    if (m_watcher && !m_watcher->files().contains(m_path) && QFileInfo::exists(m_path))
        m_watcher->addPath(m_path);

    if (!m_model) return;
    if (!m_model->hasOverlay()) {
        // Silent re-index. Join document-reading workers before open() unmaps.
        cancelAndJoinDocumentReaders();
        m_doc->open(m_path);
        startIndexing();
        return;
    }
    // Pending edits: prompt reload vs keep.
    const auto answer = QMessageBox::question(this, tr("File changed on disk"),
        tr("The file changed on disk and you have unsaved preview edits. "
           "Reload from disk (discarding your edits) or keep your current edits?"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Discard) {
        m_model->clearOverlay();
        // Join document-reading workers before open() unmaps the old mapping.
        cancelAndJoinDocumentReaders();
        m_doc->open(m_path);
        startIndexing();
    }
    // Cancel == keep current edits; do nothing.
}

// ---------------------------------------------------------------------------
// Find bar (incremental + async count)
// ---------------------------------------------------------------------------

void CsvPreviewWidget::openFindBar()
{
    if (!m_findBar) return;
    m_findBar->show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void CsvPreviewWidget::closeFindBar()
{
    if (!m_findBar) return;
    m_findBar->hide();
    m_cancelFind.store(true, std::memory_order_relaxed);
    m_findGeneration.fetch_add(1, std::memory_order_relaxed);
    if (m_view) m_view->setFocus();
}

void CsvPreviewWidget::onFindTextChanged()
{
    // Debounced: bump the generation so any in-flight async count is discarded,
    // then run an incremental forward-search from the current caret.
    m_findGeneration.fetch_add(1, std::memory_order_relaxed);
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) {
        m_findCount->clear();
        return;
    }
    findNext(true);
    findAllAsync();
}

bool CsvPreviewWidget::searchFrom(quint64 startRow, int startCol, bool forward,
                                  const QString &needle, quint64 &hitRow, int &hitCol) const
{
    if (!m_model || needle.isEmpty()) return false;
    // Iterate in VIEW-row space so find scans only the filtered-in rows in
    // displayed order; translate each view row to its data row before reading the
    // cell. viewRowCount()/viewRowToDataRow are the identity when no view active.
    const quint64 dataRows = viewRowCount();
    if (dataRows == 0) return false;
    const int cols = m_model->columnCount();
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;

    // Linear forward/backward scan with wrap-around. Incremental: returns the
    // FIRST hit, so the cost is O(distance to next hit), not O(file).
    quint64 r = startRow;
    int c = startCol;
    const quint64 totalCells = dataRows; // bound the wrap by rows scanned
    quint64 rowsScanned = 0;
    while (rowsScanned <= totalCells) {
        const quint64 dataRow = m_model->viewRowToDataRow(static_cast<int>(r));
        if (forward) {
            for (; c < cols; ++c) {
                if (m_model->absoluteCell(dataRow, c).contains(needle, cs)) { hitRow = r; hitCol = c; return true; }
            }
            // Next row.
            ++r;
            if (r >= dataRows) r = 0;
            c = 0;
        } else {
            for (; c >= 0; --c) {
                if (m_model->absoluteCell(dataRow, c).contains(needle, cs)) { hitRow = r; hitCol = c; return true; }
            }
            if (r == 0) r = dataRows - 1; else --r;
            c = cols - 1;
        }
        ++rowsScanned;
    }
    return false;
}

void CsvPreviewWidget::findNext(bool forward)
{
    if (!m_model) return;
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) return;

    // Start just past the current caret so repeated Next advances. m_caretRow is
    // a VIEW row; wrap bounds use the displayed row count (identity when no view).
    quint64 startRow = m_caretRow;
    int startCol = m_caretCol + (forward ? 1 : -1);
    const int cols = m_model->columnCount();
    const quint64 viewRows = viewRowCount();
    if (viewRows == 0) return;
    if (forward && startCol >= cols) { startCol = 0; ++startRow; if (startRow >= viewRows) startRow = 0; }
    if (!forward && startCol < 0) { startCol = cols - 1; if (startRow == 0) startRow = viewRows - 1; else --startRow; }

    quint64 hitRow; int hitCol;
    if (searchFrom(startRow, startCol, forward, needle, hitRow, hitCol)) {
        focusHit(hitRow, hitCol);
    } else {
        m_findCount->setText(tr("No matches"));
    }
}

void CsvPreviewWidget::focusHit(quint64 row, int col)
{
    if (!m_model) return;
    setAnchorCaret(row, col);
    const quint64 base = m_model->windowBase();
    const quint64 window = m_model->windowSize();
    if (row < base || row >= base + window) {
        // Re-base centered on the hit (single repaint).
        const quint64 half = window / 2;
        rebaseTo(row > half ? row - half : 0);
        syncAbsoluteScrollRange();
    }
    const int proxy = m_model->toProxyRow(row);
    if (proxy >= 0) {
        const QModelIndex idx = m_model->index(proxy, col);
        m_view->setCurrentIndex(idx);
        m_view->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    }
    projectSelection();
}

void CsvPreviewWidget::findAllAsync()
{
    if (!m_model) return;
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) { m_findCount->clear(); return; }

    const int gen = m_findGeneration.load(std::memory_order_relaxed);
    m_cancelFind.store(false, std::memory_order_relaxed);

    if (!m_findWatcher) {
        m_findWatcher = new QFutureWatcher<qint64>(this);
        connect(m_findWatcher, &QFutureWatcher<qint64>::finished, this, [this]() {
            // Drop stale results (generation moved on while we were counting).
            if (m_cancelFind.load(std::memory_order_relaxed)) return;
            const qint64 n = m_findWatcher->result();
            if (n < 0) return; // cancelled mid-run
            m_findCount->setText(tr("%n match(es)", "", static_cast<int>(qMin<qint64>(n, INT_MAX))));
        });
    }

    CsvDocument *doc = m_doc.get();
    const quint64 dataRows = viewRowCount();
    const int cols = m_model->columnCount();
    const bool headerMode = m_model->headerMode();
    const QHash<quint64, QString> overlay = m_model->overlay(); // COW snapshot
    // Frozen structural snapshot — same composed translator as the model, async.
    const QList<quint64> insRows = m_model->insertedFileRowsSnapshot();
    const QList<int> insCols = m_model->insertedColsSnapshot();
    const QList<quint64> delRows = m_model->deletedFileRowsSnapshot();
    const QList<int> delCols = m_model->deletedColsSnapshot();
    // Frozen view order: iterate VIEW rows so the count reflects only filtered-in
    // rows in displayed order. Empty ⇒ identity (every data row, no view).
    const QVector<quint32> viewOrder = m_model->viewOrderSnapshot();
    std::atomic<bool> *cancel = &m_cancelFind;
    std::atomic<int> *genPtr = &m_findGeneration;

    QFuture<qint64> fut = QtConcurrent::run(
        [doc, needle, dataRows, cols, headerMode, overlay, insRows, insCols, delRows, delCols, viewOrder, cancel, genPtr, gen]() -> qint64 {
            qint64 count = 0;
            for (quint64 vr = 0; vr < dataRows; ++vr) {
                if (cancel->load(std::memory_order_relaxed)) return -1;
                if (genPtr->load(std::memory_order_relaxed) != gen) return -1; // superseded
                // vr is a VIEW row → data row through the snapshot; then VIRTUAL.
                const quint64 r = viewOrder.isEmpty() ? vr
                    : static_cast<quint64>(viewOrder.at(static_cast<qsizetype>(vr)));
                const quint64 vFileRow = headerMode ? r + 1 : r;
                const qint64 physFileRow = CsvTableModel::physicalComposed(insRows, delRows, vFileRow);
                QVector<QString> fields;
                if (physFileRow >= 0)
                    fields = doc->parseRowUncached(static_cast<quint64>(physFileRow));
                for (int c = 0; c < cols; ++c) {
                    auto it = overlay.constFind(CsvTableModel::overlayKey(vFileRow, c));
                    QString cell;
                    if (it != overlay.constEnd()) {
                        cell = it.value();
                    } else if (physFileRow >= 0) {
                        const qint64 physCol = CsvTableModel::physicalComposed(insCols, delCols, c);
                        if (physCol >= 0 && physCol < fields.size())
                            cell = fields.at(static_cast<int>(physCol));
                    }
                    if (cell.contains(needle, Qt::CaseInsensitive))
                        ++count;
                }
            }
            return count;
        });
    m_findWatcher->setFuture(fut);
}

// ---------------------------------------------------------------------------
// Wrap / resize
// ---------------------------------------------------------------------------

void CsvPreviewWidget::setWordWrapEnabled(bool on)
{
    m_wrap = on;
    if (!m_view) return;
    m_view->setWordWrap(on);
    if (on) {
        m_view->setTextElideMode(Qt::ElideNone);
        if (!m_wrapTimer) {
            m_wrapTimer = new QTimer(this);
            m_wrapTimer->setSingleShot(true);
            m_wrapTimer->setInterval(100);
            connect(m_wrapTimer, &QTimer::timeout, this, &CsvPreviewWidget::resizeVisibleRows);
            // Debounce row resizing on scroll while wrap is on.
            connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() {
                if (m_wrap) m_wrapTimer->start();
            });
        }
        resizeVisibleRows();
    } else {
        m_view->setTextElideMode(Qt::ElideRight);
        // Restore the uniform row height (NEVER a global resizeRowsToContents).
        const QFontMetrics fm(m_view->font());
        const int h = fm.height() + 6;
        m_view->verticalHeader()->setDefaultSectionSize(h);
    }
}

void CsvPreviewWidget::resizeVisibleRows()
{
    if (!m_view || !m_model || !m_wrap) return;
    // Resize ONLY the currently visible rows — never the global O(N) call.
    const QModelIndex topIdx = m_view->indexAt(m_view->rect().topLeft());
    const QModelIndex bottomIdx = m_view->indexAt(m_view->rect().bottomLeft());
    int first = topIdx.isValid() ? topIdx.row() : 0;
    int last = bottomIdx.isValid() ? bottomIdx.row() : qMin(first + 64, m_model->rowCount() - 1);
    if (last < first) last = qMin(m_model->rowCount() - 1, first + 64);
    for (int r = first; r <= last && r < m_model->rowCount(); ++r)
        m_view->resizeRowToContents(r);
}

// ---------------------------------------------------------------------------
// Theming / chrome
// ---------------------------------------------------------------------------

void CsvPreviewWidget::applyFonts()
{
    if (!m_app || !m_app->getSettings()) return;
    auto *settings = m_app->getSettings();
    QFont cellFont(settings->fontName(), settings->fontSize());
    // Match the editor's glyph-hinting policy so thin fonts (e.g. Lilex) stay
    // sharp in CSV cells. See EditorManager / PlatQt for the Scintilla side.
    cellFont.setHintingPreference(settings->fontHinting()
        ? QFont::PreferFullHinting
        : QFont::PreferNoHinting);
    if (m_model)
        m_model->setCellFont(cellFont);
    if (m_view) {
        // Cells + row-number (vertical) header use the Default Font; the column
        // (horizontal) header keeps the system font.
        m_view->setFont(cellFont);
        m_view->verticalHeader()->setFont(cellFont);
        m_view->horizontalHeader()->setFont(QApplication::font());
        configureView();
    }
}

void CsvPreviewWidget::applyTheme(const QPalette &palette, bool isDark)
{
    m_palette = palette;
    m_isDark = isDark;
    applyPaletteToChrome();
    retintIcons();
}

void CsvPreviewWidget::applyPaletteToChrome()
{
    // Push the current palette onto every chrome surface. Guarded so that the
    // setPalette() calls (which re-enter changeEvent with QEvent::PaletteChange)
    // don't recurse back into here.
    if (m_applyingPalette) return;
    m_applyingPalette = true;

    setPalette(m_palette);
    if (m_view) {
        m_view->setPalette(m_palette);
        m_view->viewport()->setPalette(m_palette);
        // Headers derive selection/text colors from the same palette roles
        // (base/alternate-base/text/highlight/mid) per ui-dna.
        m_view->horizontalHeader()->setPalette(m_palette);
        m_view->verticalHeader()->setPalette(m_palette);
    }
    if (m_findBar) m_findBar->setPalette(m_palette);
    // Filter strip: re-apply the palette and recompute each field's active-filter
    // indicator (its border is palette(mid)-derived) so the affordance tracks
    // light/dark switches.
    if (m_filterStrip) {
        m_filterStrip->setPalette(m_palette);
        for (QLineEdit *f : m_filterFields) {
            f->setPalette(m_palette);
            refreshFilterFieldStyle(f);
        }
    }

    m_applyingPalette = false;
}

void CsvPreviewWidget::retintIcons()
{
    // The widget owns no QIcon chrome: the tab icon is tinted by
    // PreviewTabManager off the palette (re-tinted on theme switch), and the
    // find-bar Prev/Next/Close buttons use currentColor-following text glyphs
    // (▲ ▼ ✕). Re-apply the current palette to those glyph buttons so their
    // ButtonText foreground tracks light/dark switches even if a parent palette
    // push didn't fully propagate.
    for (QToolButton *btn : {m_findPrev, m_findNext, m_findClose}) {
        if (btn) btn->setPalette(m_palette);
    }
}

void CsvPreviewWidget::updateEncodingLabel()
{
    if (!m_encodingLabel || !m_doc) return;
    QString eol;
    switch (m_doc->eol()) {
    case CsvDocument::Eol::Lf:   eol = QStringLiteral("LF"); break;
    case CsvDocument::Eol::CrLf: eol = QStringLiteral("CRLF"); break;
    case CsvDocument::Eol::Cr:   eol = QStringLiteral("CR"); break;
    }
    m_encodingLabel->setText(QStringLiteral("%1 · %2 · %3 rows")
        .arg(QString::fromLatin1(m_doc->codecDisplayName()))
        .arg(eol)
        .arg(m_model ? QString::number(m_model->dataRowCount()) : QStringLiteral("?")));
}

void CsvPreviewWidget::showNotice(const QString &text)
{
    if (!m_noticeLabel) return;
    m_noticeLabel->setText(text);
    m_noticeLabel->show();
    m_noticeTimer->start(4000);
}

void CsvPreviewWidget::setBannerVisible(bool visible, const QString &text)
{
    if (!m_banner) return;
    if (visible && !text.isEmpty())
        m_bannerLabel->setText(text);
    m_banner->setVisible(visible);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void CsvPreviewWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Find)) {
        openFindBar();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Save)) {
        saveToDisk();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copySelection(CopyFormat::Tsv);
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::SelectAll)) {
        selectAll();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F3) {
        findNext(!(event->modifiers() & Qt::ShiftModifier));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_findBar && m_findBar->isVisible()) {
        closeFindBar();
        event->accept();
        return;
    }
    PreviewContentWidget::keyPressEvent(event);
}

bool CsvPreviewWidget::eventFilter(QObject *watched, QEvent *event)
{
    // Shortcut-override guard: MainWindow defines global WindowShortcut QActions
    // (e.g. actionDelete = "Del", plus Ctrl+C/A/S/F/Z/Y) that Qt's shortcut system
    // fires BEFORE a keypress reaches the focused CSV view — which is why Delete
    // was being swallowed by MainWindow's "clear editor" action while Backspace
    // (no global action) worked. Accepting the ShortcutOverride for the keys this
    // widget owns suppresses the global shortcut so the key arrives as a normal
    // KeyPress in the block below. Keep this claim set in sync with that block.
    if (m_view && watched == m_view && event->type() == QEvent::ShortcutOverride && m_model) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const bool claimed =
            ke->matches(QKeySequence::Find) || ke->matches(QKeySequence::Save) ||
            ke->matches(QKeySequence::Copy) || ke->matches(QKeySequence::SelectAll) ||
            ke->matches(QKeySequence::Undo) || ke->matches(QKeySequence::Redo) ||
            ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace ||
            ke->key() == Qt::Key_F3 ||
            (ke->key() == Qt::Key_Escape && m_findBar && m_findBar->isVisible());
        if (claimed) {
            event->accept();   // override the global shortcut → delivered as KeyPress
            return true;
        }
    }

    // Keyboard shift-navigation / Ctrl+Home/End extend the ABSOLUTE selection
    // (the view's QItemSelectionModel can't see the modifier in currentChanged).
    if (m_view && watched == m_view && event->type() == QEvent::KeyPress && m_model) {
        auto *ke = static_cast<QKeyEvent *>(event);
        // App-level shortcuts the table would otherwise consume (Ctrl+A/C) or
        // never see (Ctrl+F/S, F3, Esc) — intercept before the view handles them.
        if (ke->matches(QKeySequence::Find)) { openFindBar(); return true; }
        if (ke->matches(QKeySequence::Save)) { saveToDisk(); return true; }
        if (ke->matches(QKeySequence::Copy)) { copySelection(CopyFormat::Tsv); return true; }
        if (ke->matches(QKeySequence::SelectAll)) { selectAll(); return true; }
        if (ke->matches(QKeySequence::Undo)) { if (m_undoStack) m_undoStack->undo(); projectSelection(true); return true; }
        if (ke->matches(QKeySequence::Redo)) { if (m_undoStack) m_undoStack->redo(); projectSelection(true); return true; }
        if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
            clearSelectedCells(); return true;
        }
        if (ke->key() == Qt::Key_F3) { findNext(!(ke->modifiers() & Qt::ShiftModifier)); return true; }
        if (ke->key() == Qt::Key_Escape && m_findBar && m_findBar->isVisible()) { closeFindBar(); return true; }

        const bool shift = ke->modifiers() & Qt::ShiftModifier;
        const bool ctrl = ke->modifiers() & Qt::ControlModifier;
        // Navigation clamps in VIEW-row space (displayed count); identity when no
        // view active. m_caretRow is a view row throughout.
        const quint64 dataRows = viewRowCount();
        const int cols = m_model->columnCount();
        if (dataRows > 0 && cols > 0 && (shift || ctrl)) {
            quint64 row = m_caretRow;
            int col = m_caretCol;
            bool handled = true;
            switch (ke->key()) {
            case Qt::Key_Down:  row = qMin(row + 1, dataRows - 1); break;
            case Qt::Key_Up:    row = row > 0 ? row - 1 : 0; break;
            case Qt::Key_Left:  col = qMax(0, col - 1); break;
            case Qt::Key_Right: col = qMin(cols - 1, col + 1); break;
            case Qt::Key_PageDown: row = qMin(row + 64, dataRows - 1); break;
            case Qt::Key_PageUp:   row = row > 64 ? row - 64 : 0; break;
            case Qt::Key_Home:  if (ctrl) { row = 0; } col = 0; break;
            case Qt::Key_End:   if (ctrl) { row = dataRows - 1; } col = cols - 1; break;
            default: handled = false; break;
            }
            if (handled) {
                if (shift) extendCaret(row, col);
                else setAnchorCaret(row, col);
                clampCaretVisible();
                projectSelection();
                event->accept();
                return true;
            }
        }
    }

    if (m_view && watched == m_view->viewport() && m_model) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const QModelIndex idx = m_view->indexAt(me->pos());
                if (idx.isValid()) {
                    const quint64 absRow = m_model->toAbsRow(idx.row());
                    if (me->modifiers() & Qt::ShiftModifier)
                        extendCaret(absRow, idx.column());
                    else
                        setAnchorCaret(absRow, idx.column());
                    projectSelection();
                }
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() & Qt::LeftButton) {
                // Drag past the bottom/top edge → auto-scroll + re-base + extend.
                const QRect vp = m_view->viewport()->rect();
                if (me->pos().y() > vp.bottom() - 4) {
                    const quint64 next = qMin(m_caretRow + 8, viewRowCount() - 1);
                    extendCaret(next, m_caretCol);
                    clampCaretVisible();
                    projectSelection();
                    return true;
                } else if (me->pos().y() < vp.top() + 4) {
                    const quint64 prev = m_caretRow > 8 ? m_caretRow - 8 : 0;
                    extendCaret(prev, m_caretCol);
                    clampCaretVisible();
                    projectSelection();
                    return true;
                }
                const QModelIndex idx = m_view->indexAt(me->pos());
                if (idx.isValid()) {
                    extendCaret(m_model->toAbsRow(idx.row()), idx.column());
                    projectSelection();
                }
            }
        }
    }
    return PreviewContentWidget::eventFilter(watched, event);
}

void CsvPreviewWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange) {
        // A live palette switch (e.g. system light/dark toggle propagating down
        // the widget tree). Pick up the new palette and re-apply it to the table
        // view / find bar / banner, then re-tint the glyph chrome. The guard in
        // applyPaletteToChrome() prevents the inner setPalette() calls from
        // re-entering this handler.
        if (!m_applyingPalette) {
            m_palette = palette();
            applyPaletteToChrome();
            retintIcons();
        }
    }
    PreviewContentWidget::changeEvent(event);
}

void CsvPreviewWidget::onCellEdited()
{
    // Conflict guard at edit-commit time.
    conflictActive(true);
}

// ---------------------------------------------------------------------------
// Sort / filter view layer
// ---------------------------------------------------------------------------

void CsvPreviewWidget::onHeaderClicked(int col)
{
    if (!m_model || col < 0) return;
    // Ctrl+click keeps the prior whole-column select; plain click drives the
    // tri-state sort cycle. (The plain click no longer selects the column.)
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        selectColumn(col);
        return;
    }
    if (!m_model->canSortFilter()) {
        if (!m_oversizeNoticeShown) {
            showNotice(tr("Sort and filter are unavailable for files larger than %1 rows.")
                           .arg(CsvTableModel::kWindowSize));
            m_oversizeNoticeShown = true;
        }
        return;
    }
    // Advance the cycle for THIS column. Base the cycle on the PENDING sort
    // intent (m_pendingViewSortColumn/Order), not the model's recorded sort:
    // under the worker path the model's sortColumn() only updates when the worker
    // finishes, so rapid mid-worker clicks must cycle off the in-flight intent.
    if (m_pendingViewSortColumn != col) {
        applySort(col, /*active=*/true, Qt::AscendingOrder);
    } else if (m_pendingViewSortOrder == Qt::AscendingOrder) {
        applySort(col, /*active=*/true, Qt::DescendingOrder);
    } else {
        applySort(col, /*active=*/false, Qt::AscendingOrder); // cleared
    }
}

void CsvPreviewWidget::applySort(int col, bool active, Qt::SortOrder order)
{
    if (!m_view || !m_model) return;
    QHeaderView *header = m_view->horizontalHeader();
    if (active) {
        header->setSortIndicatorShown(true);
        header->setSortIndicator(col, order);
    } else {
        header->setSortIndicatorShown(false);
    }
    // One combined recompute folds this sort together with any active filters.
    // recomputeView() reads the desired sort from m_pendingViewSortColumn/Order.
    m_pendingViewSortColumn = active ? col : -1;
    m_pendingViewSortOrder = order;
    recomputeView();
}

void CsvPreviewWidget::buildFilterStrip()
{
    if (!m_view || !m_model) return;
    // (Re)create one QLineEdit per column. The strip is a child of the VIEW (not
    // the viewport) so it lives in the reserved top band the CsvTableView keeps
    // below the horizontal header and above the data — the line-number-area idiom.
    if (!m_filterStrip) {
        m_filterStrip = new QWidget(m_view);
        m_filterStrip->setObjectName(QStringLiteral("csvFilterStrip"));
        // Re-place the strip whenever the view re-lays out (scroll, resize,
        // header geometry). Set once; harmless if buildFilterStrip re-runs.
        static_cast<CsvTableView *>(m_view)->setGeometriesCallback([this]() { layoutFilterStrip(); });
    }
    const int cols = m_model->columnCount();
    // Reconcile the field count to the column count via the extracted core (tail
    // grow/shrink — buildFilterStrip is the wholesale (re)build where position is
    // irrelevant; mid-insert position is handled by onFilterColumnsInserted).
    CsvFilterStrip::reconcileCount(
        m_filterFields, cols,
        [this]() { return createFilterField(); },
        [](QLineEdit *f) { delete f; });
    for (QLineEdit *f : m_filterFields)
        refreshFilterFieldStyle(f);
    updateFilterStripGeometry();
    layoutFilterStrip();
}

// One fully-initialized filter field: placeholder, clear-button, and the
// textChanged wiring that drives the active-style indicator + debounced
// recompute. Both buildFilterStrip() (tail append) and onFilterColumnsInserted()
// (mid splice) go through here so a spliced field is byte-for-byte identical to a
// built one — no half-wired field that silently fails to filter. Caller owns
// placement in m_filterFields and the subsequent geometry pass.
QLineEdit *CsvPreviewWidget::createFilterField()
{
    auto *f = new QLineEdit(m_filterStrip);
    f->setPlaceholderText(tr("Filter…"));
    f->setClearButtonEnabled(true);
    // A keystroke (re)starts the debounce; the active-field indicator updates
    // immediately so the affordance tracks typing without waiting on recompute.
    connect(f, &QLineEdit::textChanged, this, [this, f]() {
        refreshFilterFieldStyle(f);
        updateSortFilterAvailability();
        m_filterDebounce->start();
    });
    refreshFilterFieldStyle(f);   // neutral (empty) style up front, matching build
    return f;
}

// Model emitted columnsInserted(first,last): splice that many blank fields in AT
// `first` so every existing field keeps riding its own column. Without this, a
// tail-append would leave a typed filter glued to the column index the insert
// just displaced (a one-column misalignment) — the model already shifts cell
// data via rekeyOverlayForColInsert; the strip must mirror that shift. The strip
// may not exist yet (canSortFilter() false / no columns) — guard and bail. The
// splice arithmetic is the extracted CsvFilterStrip core (unit-tested); this
// handler only guards + drives the geometry pass.
void CsvPreviewWidget::onFilterColumnsInserted(int first, int last)
{
    if (!m_view || !m_model || !m_filterStrip) return;
    CsvFilterStrip::spliceInserted(m_filterFields, first, last,
                                   [this]() { return createFilterField(); });
    // Re-place every field above its (now shifted) column section.
    updateFilterStripGeometry();
    layoutFilterStrip();
}

// Model emitted columnsRemoved(first,last): delete the QLineEdit objects over the
// range AND erase their slots, so no widget leaks and the surviving fields stay
// index-aligned with their columns. The destroy+erase arithmetic is the extracted
// CsvFilterStrip core (unit-tested); this handler only guards + relays geometry.
void CsvPreviewWidget::onFilterColumnsRemoved(int first, int last)
{
    if (!m_view || !m_model || !m_filterStrip) return;
    CsvFilterStrip::spliceRemoved(m_filterFields, first, last,
                                  [](QLineEdit *f) { delete f; });
    updateFilterStripGeometry();
    layoutFilterStrip();
}

// modelReset handler. modelReset fires on MANY paths — setViewOrder (every sort/
// filter apply), clearViewOrder (every clear), setWindowBase (every scroll-rebase),
// setHeaderMode, documentReloaded, setReindexing, restoreSnapshot (undo/redo of a
// column delete). Only the LAST changes the column count without a paired
// columnsInserted/Removed (it's bracketed in begin/endResetModel). So guard on the
// count: an O(1) early-out leaves the hot sort/filter/scroll paths untouched (no
// field churn, crucially no destroying the QLineEdit the user is typing into mid-
// recompute) and only the genuine count delta — undo/redo-of-delete — does work.
//
// When the count DID change, the reset is a wholesale upheaval: restoreSnapshot
// re-adds the deleted column at its ORIGINAL (middle) physical index and gives NO
// old→new column index map (it swaps the model's private sparse sets en bloc).
// buildFilterStrip() reconciles tail-only (append/takeLast), so it would leave any
// surviving filter text on a now-shifted column — and a still-armed debounce would
// then recompute that text against the WRONG column (the exact misalignment the
// live splice/erase handlers prevent). Across an opaque reset there is no correct
// positional remap, so discard stale filter text entirely and stop the armed
// debounce — the SAME contract the delimiter-change path uses (resetSortFilterState
// at the QComboBox::activated handler), which is the established "columns changed
// wholesale" behavior. Then rebuild the strip aligned to the restored column count.
void CsvPreviewWidget::rebuildFilterStripIfCountChanged()
{
    if (!m_view || !m_model || !m_filterStrip) return;
    if (m_filterFields.size() == m_model->columnCount()) return;  // hot-path no-op
    resetSortFilterState();   // drop stale filter text + stop the armed debounce (no positional remap exists across a reset)
    buildFilterStrip();       // reconcile field count to the restored columns (now all-empty, so alignment is moot)
}

void CsvPreviewWidget::updateFilterStripGeometry()
{
    if (!m_view || !m_filterStrip) return;
    auto *view = static_cast<CsvTableView *>(m_view);
    // Hide the strip (and reclaim the band) when sort/filter is unavailable or
    // there are no columns — ui-dna "hide chrome until populated".
    const bool show = m_model && m_model->canSortFilter() && m_model->columnCount() > 0;
    if (!show) {
        m_filterStrip->hide();
        view->setStripHeight(0);
        return;
    }
    const QFontMetrics fm(m_filterStrip->font());
    const int stripH = fm.height() + kFilterStripPadding * 2;
    view->setStripHeight(stripH); // reserves the band via updateGeometries()
    m_filterStrip->show();
    m_filterStrip->raise();
}

void CsvPreviewWidget::layoutFilterStrip()
{
    if (!m_view || !m_filterStrip || !m_filterStrip->isVisible() || !m_model) return;
    auto *view = static_cast<CsvTableView *>(m_view);
    QHeaderView *header = m_view->horizontalHeader();
    const int stripH = view->stripHeight();
    if (stripH <= 0) return;
    // The band sits directly below the horizontal header. Read the header's actual
    // geometry (accounts for any frame offset) for the band's top/left edges;
    // sectionViewportPosition is measured from the same left edge, so each field
    // aligns to its column.
    const QRect hg = header->geometry();
    const int top = header->isHidden() ? hg.top() : hg.bottom() + 1;
    const int left = hg.left();
    const int vpWidth = m_view->viewport()->width();
    m_filterStrip->setGeometry(left, top, vpWidth, stripH);
    const int pad = kFilterStripPadding / 2;
    const int cols = qMin(static_cast<int>(m_filterFields.size()), m_model->columnCount());
    for (int c = 0; c < m_filterFields.size(); ++c) {
        QLineEdit *f = m_filterFields.at(c);
        if (c >= cols) { f->hide(); continue; }
        const int x = header->sectionViewportPosition(c);
        const int w = header->sectionSize(c);
        if (w <= 0 || x + w <= 0 || x >= vpWidth) { f->hide(); continue; } // off-screen
        f->setGeometry(x + pad, pad, qMax(0, w - pad * 2), stripH - pad * 2);
        f->show();
    }
}

void CsvPreviewWidget::refreshFilterFieldStyle(QLineEdit *field)
{
    if (!field) return;
    // Active (non-empty) filter: a palette-driven border PLUS a non-color
    // affordance (a bold funnel glyph as a leading marker) so color is never the
    // sole carrier (ui-dna accessibility baseline). Border uses palette(mid) per
    // ui-dna (mid is reserved for borders/separators).
    const bool active = !field->text().isEmpty();
    if (active) {
        const QColor border = m_palette.color(QPalette::Mid);
        field->setStyleSheet(QStringLiteral("QLineEdit { border: 1px solid %1; border-radius: 3px; "
                                             "font-weight: bold; }")
                                 .arg(border.name(QColor::HexRgb)));
        field->setToolTip(tr("Filtering: \"%1\"").arg(field->text()));
    } else {
        field->setStyleSheet(QString());
        field->setToolTip(QString());
    }
}

QStringList CsvPreviewWidget::currentFilters() const
{
    QStringList filters;
    const int cols = m_model ? m_model->columnCount() : 0;
    filters.reserve(cols);
    for (int c = 0; c < cols; ++c)
        filters << (c < m_filterFields.size() ? m_filterFields.at(c)->text() : QString());
    return filters;
}

bool CsvPreviewWidget::anyFilterActive() const
{
    for (QLineEdit *f : m_filterFields)
        if (!f->text().isEmpty())
            return true;
    return false;
}

void CsvPreviewWidget::clearAllFilters()
{
    bool any = false;
    for (QLineEdit *f : m_filterFields) {
        if (!f->text().isEmpty()) {
            const QSignalBlocker block(f); // we recompute once below, not per field
            f->clear();
            refreshFilterFieldStyle(f);
            any = true;
        }
    }
    updateSortFilterAvailability();
    if (any)
        recomputeView(); // preserves the active sort (read from the model)
}

void CsvPreviewWidget::updateSortFilterAvailability()
{
    const bool canSF = m_model && m_model->canSortFilter();
    const bool viewActive = m_model && m_model->viewActive();
    if (m_clearFiltersButton)
        m_clearFiltersButton->setVisible(canSF && anyFilterActive());
    // Structural inserts via the toolbar button are off while a view is active.
    if (m_insertButton)
        m_insertButton->setEnabled(m_model && !m_model->isReadOnly() && !viewActive);
    updateFilterStripGeometry();
    layoutFilterStrip();
}

void CsvPreviewWidget::resetSortFilterState()
{
    // Clears the sort/filter view chrome + model order. Self-sufficient on every
    // caller (new-file, header-toggle, delimiter): (0) invalidate any in-flight
    // recompute worker so a result computed against the OLD layout can never be
    // applied after the reset — cancel the current per-launch token and bump the
    // generation (the finished() slot's generation guard then drops it). This is
    // the STALENESS guard and is sufficient on its own for non-unmapping callers
    // (header-toggle); callers that go on to unmap/reopen the document
    // (loadFromFile, save, external-change) additionally call
    // cancelAndJoinDocumentReaders() to WAIT for the worker (the use-after-unmap
    // guard). (1) drop the model's view order, (2) empty all filter fields + their
    // indicators + rebuild/hide the strip, (3) clear the header sort indicator +
    // m_sortColumn.
    if (m_viewCancel) m_viewCancel->store(true, std::memory_order_relaxed);
    m_viewGeneration.fetch_add(1, std::memory_order_relaxed);
    m_filterDebounce->stop();
    if (m_model)
        m_model->clearViewOrder();                 // (1) + (3) clears m_sortColumn in the model
    for (QLineEdit *f : m_filterFields) {          // (2)
        const QSignalBlocker block(f);
        f->clear();
        refreshFilterFieldStyle(f);
    }
    if (m_view) {                                  // (3) header indicator
        QHeaderView *header = m_view->horizontalHeader();
        header->setSortIndicatorShown(false);
        header->setSortIndicator(-1, Qt::AscendingOrder);
    }
    m_pendingViewSortColumn = -1;
    m_pendingViewSortOrder = Qt::AscendingOrder;
    if (m_clearFiltersButton)
        m_clearFiltersButton->hide();
    updateFilterStripGeometry();
    layoutFilterStrip();
}

void CsvPreviewWidget::recomputeView()
{
    if (!m_model || !m_doc) return;
    if (!m_model->canSortFilter()) return;

    const int sortCol = m_pendingViewSortColumn;
    const Qt::SortOrder sortOrder = m_pendingViewSortOrder;
    const QStringList filters = currentFilters();
    const bool anyFilter = anyFilterActive();

    // Nothing to do ⇒ identity. Drop any existing view so the no-view fast path
    // (incl. multi-GB files) is restored byte-for-byte.
    if (sortCol < 0 && !anyFilter) {
        m_model->clearViewOrder();
        updateSortFilterAvailability();
        projectSelection(false);
        return;
    }

    // Supersede any in-flight worker with a PER-LAUNCH cancel token (correction
    // for the single-shared-flag race): set the OLD token true (the in-flight
    // worker keeps seeing true through to the end — it is never reset), then mint a
    // FRESH false token for THIS run. A new worker therefore can never observe a
    // stale true and abort itself, and a superseded worker stays cancelled. Bump
    // the generation so a result arriving after this point is dropped by
    // onViewRecomputeFinished.
    const int gen = m_viewGeneration.fetch_add(1, std::memory_order_relaxed) + 1;
    if (m_viewCancel) m_viewCancel->store(true, std::memory_order_relaxed);
    auto cancelToken = std::make_shared<std::atomic<bool>>(false);
    m_viewCancel = cancelToken;

    const quint64 dataRows = m_model->dataRowCount();
    const int cols = m_model->columnCount();
    const bool headerMode = m_model->headerMode();

    // Build the cell reader from frozen snapshots — the SAME capture the copy/find
    // workers use (COW overlay + frozen structural lists + parseRowUncached), so
    // it never re-parses the file and is safe off-thread. dataRow is a DATA row;
    // translate to virtual file row (header shift) then virtual→physical.
    CsvDocument *doc = m_doc.get();
    const QHash<quint64, QString> overlay = m_model->overlay();
    const QList<quint64> insRows = m_model->insertedFileRowsSnapshot();
    const QList<int> insCols = m_model->insertedColsSnapshot();
    const QList<quint64> delRows = m_model->deletedFileRowsSnapshot();
    const QList<int> delCols = m_model->deletedColsSnapshot();
    auto makeReader = [doc, overlay, insRows, insCols, delRows, delCols, headerMode]() {
        return [doc, overlay, insRows, insCols, delRows, delCols, headerMode]
               (quint64 dataRow, int c) -> QString {
            const quint64 vFileRow = headerMode ? dataRow + 1 : dataRow;
            auto it = overlay.constFind(CsvTableModel::overlayKey(vFileRow, c));
            if (it != overlay.constEnd()) return it.value();
            const qint64 physFileRow = CsvTableModel::physicalComposed(insRows, delRows, vFileRow);
            const qint64 physCol = CsvTableModel::physicalComposed(insCols, delCols, c);
            if (physFileRow < 0 || physCol < 0) return QString();
            const QVector<QString> fields = doc->parseRowUncached(static_cast<quint64>(physFileRow));
            return physCol < fields.size() ? fields.at(static_cast<int>(physCol)) : QString();
        };
    };

    // Sync for small sets (within one frame); worker above the threshold. The
    // candidate size here is the data-row count — the filter pass itself decides
    // how many are retained, but the bound is dataRows. The sync path runs to
    // completion on the UI thread, so it uses its own never-set local token (the
    // per-launch m_viewCancel governs only the async worker below).
    if (dataRows <= static_cast<quint64>(kViewSyncThreshold)) {
        std::atomic<bool> noCancel{false};
        QVector<quint32> order = CsvSortFilter::computeViewOrder(
            makeReader(), dataRows, cols, filters, sortCol, sortOrder, noCancel);
        m_model->setViewOrder(std::move(order), sortCol, sortOrder);
        updateSortFilterAvailability();
        projectSelection(false);
        return;
    }

    // Large set: run on a worker. Capture ONLY value snapshots + the generation
    // int + the per-launch cancel token (a shared_ptr copy, so the atomic outlives
    // any later reassignment of m_viewCancel) + a raw atomic-by-pointer for the
    // generation (exactly as the copy/find workers do). `this` is touched only in
    // onViewRecomputeFinished (UI thread), guarded by generation.
    if (!m_viewWatcher) {
        m_viewWatcher = new QFutureWatcher<QVector<quint32>>(this);
        connect(m_viewWatcher, &QFutureWatcher<QVector<quint32>>::finished, this,
                &CsvPreviewWidget::onViewRecomputeFinished);
    }
    m_viewInFlightGen = gen; // the finished() slot compares this to m_viewGeneration
    auto reader = makeReader();
    std::atomic<int> *genPtr = &m_viewGeneration;
    QFuture<QVector<quint32>> fut = QtConcurrent::run(
        [reader, dataRows, cols, filters, sortCol, sortOrder, cancelToken, genPtr, gen]() -> QVector<quint32> {
            // computeViewOrder polls the per-launch token; also bail if a newer run
            // superseded us (the UI thread bumped the generation) so we stop work
            // promptly even before the token is observed.
            if (genPtr->load(std::memory_order_relaxed) != gen) return {};
            return CsvSortFilter::computeViewOrder(reader, dataRows, cols, filters,
                                                   sortCol, sortOrder, *cancelToken);
        });
    m_viewWatcher->setFuture(fut);
    showNotice(anyFilter ? tr("Filtering…") : tr("Sorting…"));
}

void CsvPreviewWidget::onViewRecomputeFinished()
{
    if (!m_viewWatcher || !m_model) return;
    // Drop a stale result via the generation guard, which is now authoritative AND
    // sufficient: EVERY path that should invalidate this run bumps m_viewGeneration
    // — a newer recomputeView(), and cancelAndJoinDocumentReaders() (called by
    // loadFromFile / save / external-change / startIndexing / dtor). Therefore
    // "launch generation still live" ⟹ this run's per-launch token was never
    // cancelled (cancelling a run requires bumping the generation). If the
    // generation moved on, the order no longer matches the live document/intent —
    // discard it, do NOT apply to the model.
    if (m_viewInFlightGen != m_viewGeneration.load(std::memory_order_relaxed))
        return;
    QVector<quint32> order = m_viewWatcher->result();
    // A cancelled/superseded compute returns {} with no sort/filter intent — never
    // apply that as a "view" (it would hide every row); clear to identity instead.
    // An empty order WITH an active sort/filter is a genuinely empty filtered set
    // and is applied as-is (the grid correctly shows zero rows, view stays active).
    if (order.isEmpty() && m_pendingViewSortColumn < 0 && !anyFilterActive()) {
        m_model->clearViewOrder();
    } else {
        m_model->setViewOrder(std::move(order), m_pendingViewSortColumn, m_pendingViewSortOrder);
    }
    updateSortFilterAvailability();
    projectSelection(false);
    emit viewRecomputed();
}

// ---------------------------------------------------------------------------
// Control API (docs/adr/0007): the grid's menu and toolbar operations with
// their answers as arguments. Each runs the same member the menu item does.
// ---------------------------------------------------------------------------

namespace {
QString notEditable(const CsvTableModel *model)
{
    if (!model)
        return QCoreApplication::translate("CsvPreviewWidget", "the file is still loading");
    if (model->isReadOnly())
        return QCoreApplication::translate("CsvPreviewWidget",
                                           "the file is open in an editor with unsaved changes; the grid is read-only "
                                           "until that editor is saved or reverted");
    return QString();
}

QString structuralBlocked(const CsvTableModel *model)
{
    if (QString e = notEditable(model); !e.isEmpty())
        return e;
    if (model->viewActive())
        return QCoreApplication::translate("CsvPreviewWidget",
                                           "rows and columns cannot change while a sort or filter is active; clear them first");
    return QString();
}
} // namespace

QChar CsvPreviewWidget::delimiter() const
{
    return m_doc ? QChar::fromLatin1(m_doc->delimiter()) : QChar();
}

QString CsvPreviewWidget::encodingName() const
{
    return m_doc ? QString::fromLatin1(m_doc->codecDisplayName()) : QString();
}

bool CsvPreviewWidget::isModified() const
{
    return m_model && (m_model->hasOverlay() || m_model->hasStructuralEdits());
}

bool CsvPreviewWidget::sortFilterAvailable() const
{
    return m_model && m_model->canSortFilter();
}

bool CsvPreviewWidget::viewRecomputing() const
{
    return m_viewWatcher && m_viewWatcher->isRunning();
}

QString CsvPreviewWidget::cellText(quint64 viewRow, int col) const
{
    if (!m_model || viewRow >= viewRowCount())
        return QString();
    return m_model->absoluteCell(m_model->viewRowToDataRow(static_cast<int>(viewRow)), col);
}

QString CsvPreviewWidget::setCell(quint64 viewRow, int col, const QString &value)
{
    conflictActive(true);
    if (QString e = notEditable(m_model); !e.isEmpty())
        return e;
    if (viewRow >= viewRowCount() || col < 0 || col >= m_model->columnCount())
        return tr("no cell at row %1, column %2").arg(viewRow).arg(col);
    // The cell editor's path: the model index in the current window.
    if (viewRow < m_model->windowBase() || viewRow >= m_model->windowBase() + m_model->windowSize())
        rebaseTo(viewRow);
    const QModelIndex index = m_model->index(m_model->toProxyRow(viewRow), col);
    if (!m_model->setData(index, value, Qt::EditRole))
        return tr("the cell could not be edited");
    setAnchorCaret(viewRow, col);
    clampCaretVisible();
    projectSelection(true);
    return QString();
}

QString CsvPreviewWidget::insertRows(quint64 at, int count)
{
    conflictActive(true);
    if (QString e = structuralBlocked(m_model); !e.isEmpty())
        return e;
    for (int i = 0; i < count; ++i)
        insertRowAt(at);
    return QString();
}

QString CsvPreviewWidget::deleteRows(QList<quint64> rows)
{
    conflictActive(true);
    if (QString e = structuralBlocked(m_model); !e.isEmpty())
        return e;
    const quint64 total = m_model->dataRowCount();
    for (quint64 r : rows) {
        if (r >= total)
            return tr("there is no row %1 (%2 rows)").arg(r).arg(total);
    }
    // Bottom-up runs of consecutive rows, each deleted like a row-header
    // selection, so the indices of the rows still to delete do not move.
    std::sort(rows.begin(), rows.end(), std::greater<>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    qsizetype i = 0;
    while (i < rows.size()) {
        const quint64 hi = rows.at(i);
        quint64 lo = hi;
        while (i + 1 < rows.size() && rows.at(i + 1) + 1 == lo)
            lo = rows.at(++i);
        ++i;
        m_anchorRow = lo;
        m_caretRow = hi;
        m_anchorCol = 0;
        m_caretCol = qMax(0, m_model->columnCount() - 1);
        m_hasSelection = true;
        deleteSelectedRows();
    }
    return QString();
}

QString CsvPreviewWidget::insertColumns(int at, int count)
{
    conflictActive(true);
    if (QString e = structuralBlocked(m_model); !e.isEmpty())
        return e;
    for (int i = 0; i < count; ++i)
        insertColumnAt(at);
    return QString();
}

QString CsvPreviewWidget::deleteColumns(QList<int> cols)
{
    conflictActive(true);
    if (QString e = structuralBlocked(m_model); !e.isEmpty())
        return e;
    const int total = m_model->virtualColumnCount();
    for (int c : cols) {
        if (c < 0 || c >= total)
            return tr("there is no column %1 (%2 columns)").arg(c).arg(total);
    }
    std::sort(cols.begin(), cols.end(), std::greater<>());
    cols.erase(std::unique(cols.begin(), cols.end()), cols.end());
    qsizetype i = 0;
    while (i < cols.size()) {
        const int hi = cols.at(i);
        int lo = hi;
        while (i + 1 < cols.size() && cols.at(i + 1) + 1 == lo)
            lo = cols.at(++i);
        ++i;
        m_anchorCol = lo;
        m_caretCol = hi;
        m_anchorRow = m_caretRow = m_model->windowBase();
        m_hasSelection = true;
        deleteSelectedColumns();
    }
    return QString();
}

QString CsvPreviewWidget::renameColumn(int col, const QString &name)
{
    conflictActive(true);
    if (QString e = notEditable(m_model); !e.isEmpty())
        return e;
    if (!m_model->headerMode())
        return tr("the first row is not a header; turn \"First row as header\" on first");
    if (!m_model->setHeaderText(col, name))
        return tr("there is no column %1").arg(col);
    return QString();
}

QString CsvPreviewWidget::clearCells(const QList<QPair<quint64, int>> &cells)
{
    conflictActive(true);
    if (QString e = notEditable(m_model); !e.isEmpty())
        return e;
    const quint64 rows = viewRowCount();
    for (const auto &[row, col] : cells) {
        if (row >= rows || col < 0 || col >= m_model->columnCount())
            return tr("no cell at row %1, column %2").arg(row).arg(col);
    }
    // One undo step, each cell cleared like Delete on a one-cell selection.
    m_undoStack->beginMacro(tr("Clear Cells"));
    for (const auto &[row, col] : cells) {
        m_anchorRow = m_caretRow = row;
        m_anchorCol = m_caretCol = col;
        m_hasSelection = true;
        clearSelectedCells();
    }
    m_undoStack->endMacro();
    return QString();
}

QString CsvPreviewWidget::sortBy(int col, bool active, Qt::SortOrder order)
{
    if (!m_model)
        return notEditable(m_model);
    if (!m_model->canSortFilter())
        return tr("sort and filter are unavailable for files larger than %1 rows").arg(CsvTableModel::kWindowSize);
    if (col < 0 || col >= m_model->columnCount())
        return tr("there is no column %1").arg(col);
    applySort(col, active, order);
    return QString();
}

QString CsvPreviewWidget::setFilter(int col, const QString &text)
{
    if (!m_model)
        return notEditable(m_model);
    if (!m_model->canSortFilter())
        return tr("sort and filter are unavailable for files larger than %1 rows").arg(CsvTableModel::kWindowSize);
    if (col < 0 || col >= m_filterFields.size())
        return tr("there is no column %1").arg(col);
    // Typing into the column's filter field, without the debounce.
    m_filterFields.at(col)->setText(text);
    m_filterDebounce->stop();
    recomputeView();
    return QString();
}

void CsvPreviewWidget::clearFilters()
{
    clearAllFilters();
}

QString CsvPreviewWidget::setDelimiterChar(QChar delimiter)
{
    const int index = m_delimiterCombo->findData(delimiter);
    if (index < 0)
        return tr("the delimiter must be one of , ; tab |");
    if (!m_doc)
        return notEditable(nullptr);
    m_delimiterCombo->setCurrentIndex(index);
    changeDelimiter();
    return QString();
}

void CsvPreviewWidget::setHeaderRow(bool on)
{
    m_headerToggle->setChecked(on);
}

QString CsvPreviewWidget::save()
{
    return saveToDisk();
}
