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

#include "QuickFileOpenDialog.h"

#include <QVBoxLayout>
#include <QDir>
#include <QHash>
#include <QSet>
#include <QKeyEvent>
#include <QShowEvent>
#include <QStringView>
#include <QApplication>
#include <QPainter>
#include <QStyledItemDelegate>
#include <algorithm>

namespace {

// --- Scoring constants ----------------------------------------------------
// Per matched char: base +1, plus consecutive/word-boundary/camelCase bonuses.
// Basename bonus is folded INTO the score (not just a tie-break): each char
// matched inside the filename region gets kBasenamePerCharBonus, and if the
// WHOLE pattern matched as a subsequence within the basename, a large
// threshold (kBasenameSubsequenceBonus) is added so any basename match
// outranks any path-scattered match. The threshold dominates the regular
// per-char score for any realistic path (< ~64K chars), giving a hard
// "basename beats path" guarantee.
constexpr int kBaseMatch = 1;
constexpr int kConsecutiveBonus = 2;
constexpr int kWordBoundaryBonus = 3;
constexpr int kCamelBonus = 2;
constexpr int kBasenamePerCharBonus = 8;
constexpr int kBasenameSubsequenceBonus = 1 << 20;   // 1048576

inline bool isSeparatorByte(char b)
{
    return b == '/' || b == '\\' || b == '.' || b == '_' || b == '-';
}

// Index of the first byte of the basename within a folded-byte candidate:
// one past the last '/' or '\\', or 0 if the path has no separator.
inline int basenameStart(const char *fcand, int candLen)
{
    for (int k = candLen - 1; k >= 0; --k) {
        if (fcand[k] == '/' || fcand[k] == '\\')
            return k + 1;
    }
    return 0;
}

// Greedy two-phase fuzzy scorer. Matches against folded bytes (case-insensitive)
// or display QChars (case-sensitive). Returns the score (0 = no match). When
// `positions` is non-null it records matched candidate indices — that is the
// ONLY allocating path and is used exclusively for the bounded set of
// survivors (Phase 2). Phase 1 passes positions=nullptr and allocates nothing.
int scoreImpl(const char *fcand, const QChar *dcand, int candLen,
              const char *fpat, const QChar *dpat, int patLen,
              bool caseSensitive, int filenameStart,
              QVarLengthArray<qint32, 32> *positions)
{
    if (patLen == 0) return 1;
    if (patLen > candLen) return 0;

    int score = 0;
    int pi = 0;
    int prevMatch = -2;
    int basenameMatches = 0;

    for (int ci = 0; ci < candLen && pi < patLen; ++ci) {
        const bool match = caseSensitive ? (dcand[ci] == dpat[pi])
                                         : (fcand[ci] == fpat[pi]);
        if (!match) continue;

        score += kBaseMatch;
        if (ci == prevMatch + 1) score += kConsecutiveBonus;
        if (ci == 0 || isSeparatorByte(fcand[ci - 1])) score += kWordBoundaryBonus;
        if (dcand[ci].isUpper() && dpat[pi].isUpper()) score += kCamelBonus;
        if (ci >= filenameStart) {
            score += kBasenamePerCharBonus;
            ++basenameMatches;
        }
        if (positions) positions->append(ci);
        prevMatch = ci;
        ++pi;
    }

    if (pi < patLen) {
        if (positions) positions->clear();
        return 0;   // pattern not fully consumed → not a subsequence
    }
    if (basenameMatches == patLen)
        score += kBasenameSubsequenceBonus;
    return score;
}

// Heap/sort comparator. Returns true when `a` ranks BETTER than `b`
// (score desc, then shorter path, then smaller original index). Used both as
// the std::sort predicate (best first) and as the min-heap predicate (so the
// heap front is the WORST kept element, ready to evict).
inline bool candidateBetter(const QuickFileOpenCandidate &a,
                            const QuickFileOpenCandidate &b)
{
    if (a.score != b.score) return a.score > b.score;
    if (a.pathLen != b.pathLen) return a.pathLen < b.pathLen;
    return a.index < b.index;
}

// HighlightDelegate paints matched characters in an accent color. Logic is
// unchanged from the original implementation — only its model source changed.
class HighlightDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);

        painter->save();

        if (opt.state & QStyle::State_Selected)
            painter->fillRect(opt.rect, opt.palette.highlight());
        else if (opt.state & QStyle::State_MouseOver)
            painter->fillRect(opt.rect, opt.palette.highlight().color().lighter(160));

        const QString text = index.data(Qt::DisplayRole).toString();
        const auto positions = index.data(QuickFileOpenDialog::MatchPositionsRole).value<QVector<int>>();

        const QRect textRect = opt.rect.adjusted(4, 0, -4, 0);
        const QFont font = opt.font;
        const QFontMetrics fm(font);

        const QColor normalColor = (opt.state & QStyle::State_Selected)
            ? opt.palette.highlightedText().color()
            : opt.palette.text().color();
        const QColor matchColor = QColor(79, 193, 255);

        int posIdx = 0;
        const int posCount = positions.size();
        int x = textRect.left();
        const int y = textRect.top();
        const int h = textRect.height();
        int runStart = 0;
        const int textLen = text.length();

        painter->setFont(font);
        painter->setClipRect(textRect);

        while (runStart < textLen && x < textRect.right()) {
            bool isMatch = (posIdx < posCount && positions[posIdx] == runStart);

            int runEnd = runStart + 1;
            if (isMatch) {
                ++posIdx;
                while (runEnd < textLen && posIdx < posCount && positions[posIdx] == runEnd) {
                    ++posIdx;
                    ++runEnd;
                }
            } else {
                int nextMatch = (posIdx < posCount) ? positions[posIdx] : textLen;
                runEnd = nextMatch;
            }

            const QStringView run = QStringView(text).mid(runStart, runEnd - runStart);
            const int runWidth = fm.horizontalAdvance(run.toString());

            painter->setPen(isMatch ? matchColor : normalColor);
            painter->drawText(QRect(x, y, runWidth, h), Qt::AlignVCenter, run.toString());

            x += runWidth;
            runStart = runEnd;
        }

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QFontMetrics fm(opt.font);
        return QSize(opt.rect.width(), fm.height() + 4);
    }
};

} // namespace

// =========================================================================
//  Static search core (pure; unit-testable without a live dialog or git)
// =========================================================================

bool QuickFileOpenDialog::isCaseSensitivePattern(const QString &pattern)
{
    for (QChar c : pattern) {
        if (c.isUpper())
            return true;
    }
    return false;
}

int QuickFileOpenDialog::score(const QString &pattern, const QString &candidate)
{
    const bool caseSensitive = isCaseSensitivePattern(pattern);

    const QByteArray fcand = FileIndexCache::foldString(candidate);
    const QByteArray fpat = FileIndexCache::foldString(pattern);
    const int candLen = static_cast<int>(candidate.size());
    const int patLen = static_cast<int>(pattern.size());
    const int fnameStart = basenameStart(fcand.constData(), candLen);

    return scoreImpl(fcand.constData(), candidate.constData(), candLen,
                     fpat.constData(), pattern.constData(), patLen,
                     caseSensitive, fnameStart, nullptr);
}

QVector<QuickFileOpenCandidate> QuickFileOpenDialog::computeMatches(
    const FileIndexCache &snapshot, const QString &pattern, int limit)
{
    QVector<QuickFileOpenCandidate> out;
    const int n = snapshot.count();
    if (n == 0 || limit <= 0)
        return out;

    const bool caseSensitive = isCaseSensitivePattern(pattern);
    const QByteArray fpat = FileIndexCache::foldString(pattern);
    const char *fpatData = fpat.constData();
    const QChar *dpatData = pattern.constData();
    const int patLen = static_cast<int>(pattern.size());

    const char *arena = snapshot.foldedArena.constData();
    const qint32 *offsets = snapshot.offsets.constData();

    // Bounded min-heap of survivors. With comp = candidateBetter, std::*_heap
    // keeps the "greatest" element at front(); the greatest under "is better
    // than" is the element no other is better than — i.e. the WORST kept
    // candidate. That makes evicting the weakest O(log K). Final result is
    // sorted best-first afterwards. Complexity: O(N log K), and Phase 1
    // performs NO per-candidate heap allocation.
    QVector<QuickFileOpenCandidate> heap;
    heap.reserve(limit);

    for (int i = 0; i < n; ++i) {
        const int start = offsets[i];
        const int candLen = offsets[i + 1] - start;
        const char *fcand = arena + start;
        const QString &disp = snapshot.displayPaths.at(i);
        const QChar *dcand = disp.constData();
        const int fnameStart = basenameStart(fcand, candLen);

        // Phase 1: score with NO position backtrace, NO allocation.
        const int s = scoreImpl(fcand, dcand, candLen, fpatData, dpatData,
                                patLen, caseSensitive, fnameStart, nullptr);
        if (s <= 0)
            continue;

        QuickFileOpenCandidate cand;
        cand.score = s;
        cand.pathLen = candLen;
        cand.index = i;

        if (static_cast<int>(heap.size()) < limit) {
            heap.append(cand);
            std::push_heap(heap.begin(), heap.end(), candidateBetter);
        } else if (candidateBetter(cand, heap.front())) {
            std::pop_heap(heap.begin(), heap.end(), candidateBetter);
            heap.back() = cand;
            std::push_heap(heap.begin(), heap.end(), candidateBetter);
        }
    }

    // Sort survivors best-first (stable, deterministic by construction).
    std::sort(heap.begin(), heap.end(), candidateBetter);

    // Phase 2: backtrace match positions ONLY for the kept survivors.
    for (QuickFileOpenCandidate &cand : heap) {
        const int start = offsets[cand.index];
        const int candLen = offsets[cand.index + 1] - start;
        const char *fcand = arena + start;
        const QString &disp = snapshot.displayPaths.at(cand.index);
        const QChar *dcand = disp.constData();
        const int fnameStart = basenameStart(fcand, candLen);
        scoreImpl(fcand, dcand, candLen, fpatData, dpatData, patLen,
                  caseSensitive, fnameStart, &cand.positions);
    }

    out = std::move(heap);
    return out;
}

QVector<QuickFileOpenCandidate> QuickFileOpenDialog::computeEmpty(
    const FileIndexCache &snapshot, const QStringList &mru, int limit)
{
    QVector<QuickFileOpenCandidate> out;
    const int n = snapshot.count();
    if (n == 0 || limit <= 0)
        return out;

    out.reserve(qMin(n, limit));

    // MRU files first (only those present in the snapshot), in MRU order.
    // Build a path→index map only when MRU is non-empty so the empty case
    // stays a pure O(K) enumeration-order slice (no map, no hashing).
    QSet<int> usedIndices;
    if (!mru.isEmpty()) {
        QHash<QString, int> pathToIndex;
        pathToIndex.reserve(n);
        for (int i = 0; i < n; ++i)
            pathToIndex.insert(snapshot.displayPaths.at(i), i);

        for (const QString &rel : mru) {
            if (out.size() >= limit) break;
            auto it = pathToIndex.constFind(rel);
            if (it == pathToIndex.constEnd())
                continue;
            const int idx = it.value();
            if (usedIndices.contains(idx))
                continue;
            QuickFileOpenCandidate cand;
            cand.index = idx;
            cand.pathLen = snapshot.offsets[idx + 1] - snapshot.offsets[idx];
            out.append(cand);
            usedIndices.insert(idx);
        }
    }

    // Then remaining files in enumeration order until the limit.
    for (int i = 0; i < n && out.size() < limit; ++i) {
        if (!usedIndices.isEmpty() && usedIndices.contains(i))
            continue;
        QuickFileOpenCandidate cand;
        cand.index = i;
        cand.pathLen = snapshot.offsets[i + 1] - snapshot.offsets[i];
        out.append(cand);
    }

    return out;
}

// =========================================================================
//  QuickFileOpenModel — virtualized list backed by the top-K candidate vector
// =========================================================================

QuickFileOpenModel::QuickFileOpenModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void QuickFileOpenModel::setSnapshot(std::shared_ptr<const FileIndexCache> snapshot)
{
    // Results index into the snapshot, so clear them when the snapshot swaps.
    beginResetModel();
    m_snapshot = std::move(snapshot);
    m_results.clear();
    endResetModel();
}

void QuickFileOpenModel::setResults(QVector<QuickFileOpenCandidate> &&results)
{
    // ONE reset per keystroke — no clear()+appendRow churn. QListView repaints
    // only its ~visible rows.
    beginResetModel();
    m_results = std::move(results);
    endResetModel();
}

int QuickFileOpenModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_results.size());
}

QVariant QuickFileOpenModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_snapshot)
        return {};
    const int row = index.row();
    if (row < 0 || row >= m_results.size())
        return {};

    const QuickFileOpenCandidate &cand = m_results.at(row);

    if (role == Qt::DisplayRole) {
        if (cand.index >= 0 && cand.index < m_snapshot->displayPaths.size())
            return m_snapshot->displayPaths.at(cand.index);
        return {};
    }
    if (role == QuickFileOpenDialog::MatchPositionsRole) {
        // HighlightDelegate reads QVector<int>; convert the cached inline
        // positions (computed once, only for survivors) to that type.
        QVector<int> v;
        v.reserve(cand.positions.size());
        for (qint32 p : cand.positions)
            v.append(static_cast<int>(p));
        return QVariant::fromValue(v);
    }
    return {};
}

// =========================================================================
//  QuickFileOpenDialog
// =========================================================================

QuickFileOpenDialog::QuickFileOpenDialog(const QString &rootPath, QWidget *parent)
    : QDialog(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_rootPath(rootPath)
{
    setMinimumWidth(500);
    setMaximumHeight(400);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setPlaceholderText(tr("Indexing files..."));
    m_lineEdit->setClearButtonEnabled(true);
    layout->addWidget(m_lineEdit);

    m_listView = new QListView(this);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setUniformItemSizes(true);   // virtualization fast path
    m_model = new QuickFileOpenModel(this);
    m_listView->setModel(m_model);
    m_listView->setItemDelegate(new HighlightDelegate(m_listView));
    layout->addWidget(m_listView);

    m_lineEdit->installEventFilter(this);
    m_listView->installEventFilter(this);
    qApp->installEventFilter(this);

    connect(m_lineEdit, &QLineEdit::textChanged, this, &QuickFileOpenDialog::onTextChanged);
    connect(m_listView, &QListView::activated, this, &QuickFileOpenDialog::onItemActivated);
    connect(m_listView, &QListView::clicked, this, &QuickFileOpenDialog::onItemActivated);

    m_lineEdit->setFocus();
}

QuickFileOpenDialog::~QuickFileOpenDialog()
{
    // No background work is owned here — enumeration lives in MainWindow's
    // cache and outlives this WA_DeleteOnClose dialog. Closing never blocks.
    qApp->removeEventFilter(this);
}

QString QuickFileOpenDialog::selectedFilePath() const
{
    return m_selectedFile;
}

void QuickFileOpenDialog::adoptSnapshot(std::shared_ptr<const FileIndexCache> snapshot)
{
    m_snapshot = std::move(snapshot);
    m_model->setSnapshot(m_snapshot);
    m_emptyDirty = true;
    if (m_snapshot)
        m_lineEdit->setPlaceholderText(tr("Type to search files..."));
    applyFilter(m_lineEdit->text());
}

void QuickFileOpenDialog::setMruFiles(const QStringList &mru)
{
    m_mru = mru;
    m_emptyDirty = true;
    if (m_snapshot && m_lineEdit->text().isEmpty())
        applyFilter(QString());
}

void QuickFileOpenDialog::applyFilter(const QString &pattern)
{
    if (!m_snapshot)
        return;

    QVector<QuickFileOpenCandidate> results;
    if (pattern.isEmpty()) {
        if (m_emptyDirty) {
            m_emptyResult = computeEmpty(*m_snapshot, m_mru, kMaxResults);
            m_emptyDirty = false;
        }
        results = m_emptyResult;
    } else {
        results = computeMatches(*m_snapshot, pattern, kMaxResults);
    }

    const bool hadResults = !results.isEmpty();
    m_model->setResults(std::move(results));
    if (hadResults)
        m_listView->setCurrentIndex(m_model->index(0, 0));
}

void QuickFileOpenDialog::onTextChanged(const QString &text)
{
    if (m_snapshot)
        applyFilter(text);
}

void QuickFileOpenDialog::onItemActivated(const QModelIndex &index)
{
    if (!index.isValid()) return;
    const QString rel = index.data(Qt::DisplayRole).toString();
    if (rel.isEmpty()) return;
    m_selectedFile = m_rootPath + QLatin1Char('/') + rel;
    accept();
}

void QuickFileOpenDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    QDialog::keyPressEvent(event);
}

void QuickFileOpenDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    activateWindow();
    m_lineEdit->setFocus();
}

bool QuickFileOpenDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto *target = qobject_cast<QWidget *>(obj);
        if (target && !isAncestorOf(target) && target != this) {
            reject();
            return false;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
        if (obj == m_lineEdit) {
            switch (ke->key()) {
            case Qt::Key_Down: {
                int row = m_listView->currentIndex().row() + 1;
                if (row < m_model->rowCount())
                    m_listView->setCurrentIndex(m_model->index(row, 0));
                return true;
            }
            case Qt::Key_Up: {
                int row = m_listView->currentIndex().row() - 1;
                if (row >= 0)
                    m_listView->setCurrentIndex(m_model->index(row, 0));
                return true;
            }
            case Qt::Key_Return:
            case Qt::Key_Enter:
                onItemActivated(m_listView->currentIndex());
                return true;
            default:
                break;
            }
        } else if (obj == m_listView) {
            switch (ke->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                onItemActivated(m_listView->currentIndex());
                return true;
            default:
                m_lineEdit->setFocus();
                m_lineEdit->event(event);
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}





