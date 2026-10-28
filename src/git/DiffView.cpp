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

#include <cstdint>
#include "DiffView.h"

#include "ApplicationSettings.h"
#include "DarkPalette.h"
#include "DiffDocumentBuilder.h"
#include "DiffLanguages.h"
#include "DiffLanguagesLua.h"
#include "DiffStickyHeader.h"
#include "GitCommitDetail.h"
#include "DiffViewApp.h"
#include "GitStatusItemDelegate.h"
#include "ProfileScope.h"
#include "ScintillaNext.h"
#include "TerminalIcons.h"

#include "Scintilla.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHash>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QElapsedTimer>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>
#include <cstring>
#include <iterator>

using DiffDoc::LinkKind;
using DiffDoc::RowKind;
using DiffDoc::Style;

namespace {

// Marker ids. 18-20 belonged to the old painter; 20-26 are taken by other
// decorators (TaskRunnerGutter, bookmarks, git gutter).
constexpr int kMarkAdded = 14;
constexpr int kMarkDeleted = 15;
constexpr int kMarkHunk = 16;
constexpr int kMarkHeader = 17;
constexpr int kMarkRule = 13;

constexpr int kHandleWidth = 4;
constexpr int kMinTreeWidth = 120;
constexpr int kLoadingDelayMs = 80;

// U+2007 FIGURE SPACE: as wide as a digit in any font, so the number columns
// line up in the single right-aligned margin.
constexpr char kFigure[] = "\xE2\x80\x87";
constexpr int kFigureBytes = 3;

inline sptr_t bgr(const QColor &c)
{
    return sptr_t(c.red()) | (sptr_t(c.green()) << 8) | (sptr_t(c.blue()) << 16);
}

int digitsOf(qint32 v)
{
    int d = 1;
    while (v >= 10) {
        v /= 10;
        ++d;
    }
    return d;
}

ApplicationSettings *appSettings()
{
    return DiffViewApp::settings();
}

bool isBodyKind(RowKind k)
{
    return k == RowKind::Context || k == RowKind::Added || k == RowKind::Deleted || k == RowKind::NoNewline;
}

// A language colour as shown on the current canvas. Lua authors colours for a
// light background; dark canvases get the shared lift (cached, UI thread).
QColor slotColour(quint32 rgb, bool dark)
{
    const QColor light = QColor::fromRgb(QRgb(0xFF000000u | rgb));
    if (!dark) return light;
    static QHash<quint32, QRgb> cache;
    const auto it = cache.constFind(rgb);
    if (it != cache.constEnd()) return QColor::fromRgb(*it);
    const QRgb out = DarkPalette::lightenForDarkBackground(light).rgb();
    cache.insert(rgb, out);
    return QColor::fromRgb(out);
}

// The one row shown while a file's body is built.
std::shared_ptr<const DiffDoc::Rows> loadingRows()
{
    static const std::shared_ptr<const DiffDoc::Rows> rows = [] {
        auto r = std::make_shared<DiffDoc::Rows>();
        const QByteArray text = QByteArrayLiteral("  ") + DiffView::tr("Loading…").toUtf8();
        r->text = text + '\n';
        r->styles.fill(static_cast<char>(Style::Placeholder), r->text.size());
        r->kinds.append(RowKind::Placeholder);
        r->oldLn.append(-1);
        r->newLn.append(-1);
        return std::shared_ptr<const DiffDoc::Rows>(std::move(r));
    }();
    return rows;
}

// Toolbar strip: a bottom rule separates it from the diff.
class DiffToolbar : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Window));
        p.fillRect(QRect(0, height() - 1, width(), 1), palette().color(QPalette::Mid));
    }
};

// Drag handle between the file tree and the diff; a 1px rule on its left.
class DiffTreeHandle : public QWidget
{
public:
    explicit DiffTreeHandle(QWidget *parent) : QWidget(parent) { setCursor(Qt::SizeHorCursor); }

    std::function<void()> onPress;
    std::function<void(int)> onDrag;      // horizontal delta since the press
    std::function<void()> onRelease;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), palette().color(QPalette::Window));
        p.fillRect(QRect(0, 0, 1, height()), palette().color(QPalette::Mid));
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) return;
        m_pressX = qRound(e->globalPosition().x());
        if (onPress) onPress();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_pressX < 0 || !(e->buttons() & Qt::LeftButton)) return;
        if (onDrag) onDrag(qRound(e->globalPosition().x()) - m_pressX);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || m_pressX < 0) return;
        m_pressX = -1;
        if (onRelease) onRelease();
    }

private:
    int m_pressX = -1;
};

enum class KeyAction : std::uint8_t { None, NextHunk, PrevHunk, NextFile, PrevFile, FocusFilter, Jump };

KeyAction keyActionOf(const QKeyEvent *e, bool onEditor)
{
    const Qt::KeyboardModifiers mods = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier
                                                          | Qt::AltModifier | Qt::MetaModifier);
    switch (e->key()) {
        case Qt::Key_F7:
            if (mods == Qt::NoModifier) return KeyAction::NextHunk;
            if (mods == Qt::ShiftModifier) return KeyAction::PrevHunk;
            if (mods == Qt::ControlModifier) return KeyAction::NextFile;
            if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) return KeyAction::PrevFile;
            return KeyAction::None;
        case Qt::Key_F4:
            return onEditor && mods == Qt::NoModifier ? KeyAction::Jump : KeyAction::None;
        case Qt::Key_T:
            return onEditor && mods == Qt::NoModifier ? KeyAction::FocusFilter : KeyAction::None;
        default:
            return KeyAction::None;
    }
}

} // namespace

DiffView::DiffView(ScintillaNext *editor, Mode mode)
    : QObject(editor),
      m_editor(editor),
      m_mode(mode),
      m_colors(DiffViewColors::make(false))
{
    if (ApplicationSettings *s = appSettings()) {
        m_treeWidth = qMax(kMinTreeWidth, s->diffTreeWidth());
        m_treeVisible = s->diffTreeVisible();
    }
    m_decorateTimer = new QTimer(this);
    m_decorateTimer->setSingleShot(true);
    m_decorateTimer->setInterval(0);
    connect(m_decorateTimer, &QTimer::timeout, this, &DiffView::decorateSlice);
    configureEditor();
}

DiffView::~DiffView() = default;

DiffView *DiffView::of(const ScintillaNext *editor)
{
    return editor ? editor->findChild<DiffView *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

ScintillaNext *DiffView::editor() const
{
    return m_editor.data();
}

qintptr DiffView::send(unsigned int msg, quintptr w, qintptr l) const
{
    return m_editor->send(msg, w, l);
}

void DiffView::setDark(bool dark)
{
    m_dark = dark;
    m_colors = DiffViewColors::make(dark);
    if (!m_editor) return;
    applyStyles();
    if (m_doc) updateSticky();  // repaint the pinned row in the new colours
    if (m_treeModel) m_treeModel->setDarkPalette(dark);
    if (m_treeDelegate) m_treeDelegate->setDarkPalette(dark);
    if (m_treeView) m_treeView->viewport()->update();
}

// ---------------------------------------------------------------------------
// Editor setup and styles

void DiffView::configureEditor()
{
    ScintillaNext *e = m_editor.data();
    if (!e) return;

    send(SCI_SETUNDOCOLLECTION, 0);
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETILEXER, 0, 0);          // container styling: we write every style byte
    send(SCI_SETEOLMODE, SC_EOL_LF);
    send(SCI_SETREADONLY, 1);
    send(SCI_SETSCROLLWIDTH, 1);
    send(SCI_SETSCROLLWIDTHTRACKING, 1);
    send(SCI_SETCARETLINEVISIBLE, 0);
    send(SCI_RESETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK);
    // The editor-wide View toggles must not leak in (EditorManager skips diff
    // views when they change, so these stay off).
    send(SCI_SETVIEWWS, SCWS_INVISIBLE);
    send(SCI_SETVIEWEOL, 0);
    send(SCI_SETINDENTATIONGUIDES, SC_IV_NONE);

    // One right-aligned text margin carries both number columns (margin text
    // is per line and shared by every text margin). Every margin mask is 0 so
    // the line-background markers paint in the text area.
    const int margins = static_cast<int>(send(SCI_GETMARGINS));
    for (int m = 0; m < margins; ++m) {
        send(SCI_SETMARGINWIDTHN, m, 0);
        send(SCI_SETMARGINMASKN, m, 0);
        send(SCI_SETMARGINSENSITIVEN, m, 0);
    }
    send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_RTEXT);
    send(SCI_SETMARGINLEFT, 0, 0);

    for (int id : {kMarkAdded, kMarkDeleted, kMarkHunk, kMarkHeader}) {
        send(SCI_MARKERDEFINE, id, SC_MARK_BACKGROUND);
        send(SCI_MARKERSETLAYER, id, SC_LAYER_BASE);
    }
    send(SCI_MARKERDEFINE, kMarkRule, SC_MARK_UNDERLINE);

    m_indAddWord = e->allocateIndicator(QStringLiteral("diff_view_add_word"));
    m_indDelWord = e->allocateIndicator(QStringLiteral("diff_view_del_word"));
    m_indStrike = e->allocateIndicator(QStringLiteral("diff_view_strike"));
    m_indLink = e->allocateIndicator(QStringLiteral("diff_view_link"));
    m_indChevron = e->allocateIndicator(QStringLiteral("diff_view_chevron"));
    for (int id : {m_indAddWord, m_indDelWord}) {
        send(SCI_INDICSETSTYLE, id, INDIC_STRAIGHTBOX);
        send(SCI_INDICSETALPHA, id, 255);
        send(SCI_INDICSETOUTLINEALPHA, id, 0);
        send(SCI_INDICSETUNDER, id, 1);
    }
    send(SCI_INDICSETSTYLE, m_indStrike, INDIC_STRIKE);
    send(SCI_INDICSETSTYLE, m_indLink, INDIC_HIDDEN);
    send(SCI_INDICSETHOVERSTYLE, m_indLink, INDIC_PLAIN);
    send(SCI_INDICSETSTYLE, m_indChevron, INDIC_HIDDEN);
    send(SCI_INDICSETHOVERSTYLE, m_indChevron, INDIC_TEXTFORE);

    if (m_mode == Mode::Commit) send(SCI_SETMOUSEDWELLTIME, 500);

    ApplicationSettings *settings = appSettings();
    send(SCI_SETWRAPMODE, settings && settings->diffWrapLines() ? SC_WRAP_WORD : SC_WRAP_NONE);

    connect(e, &ScintillaEditBase::notify, this, [this](Scintilla::NotificationData *scn) {
        if (!scn) return;
        if (scn->nmhdr.code == Scintilla::Notification::IndicatorClick) {
            m_pressedLink = linkAt(static_cast<qintptr>(scn->position));
        } else if (scn->nmhdr.code == Scintilla::Notification::IndicatorRelease) {
            const int link = m_pressedLink;
            m_pressedLink = -1;
            if (link < 0 || linkAt(static_cast<qintptr>(scn->position)) != link) return;
            // Past Scintilla's mouse-up handling: a link may replace the text.
            const quint64 serial = m_docSerial;
            QTimer::singleShot(0, this, [this, link, serial] {
                if (serial == m_docSerial) activateLink(link);
            });
        }
    });
    connect(e, &ScintillaEditBase::updateUi, this, [this](Scintilla::Update) { onUpdateUi(); });
    connect(e, &ScintillaEditBase::zoom, this, [this](int) {
        updateMarginWidth();
        refreshMetrics();
        layoutChrome();
        updateSticky();         // refreshMetrics dropped the pinned row
    });
    connect(e, &ScintillaEditBase::doubleClick, this,
            [this](Scintilla::Position, Scintilla::Position) { jumpToSource(); });
    connect(e, &ScintillaEditBase::dwellStart, this, [this](int x, int y) {
        if (!m_doc || m_doc->tips.isEmpty() || !m_editor) return;
        const qintptr pos = send(SCI_POSITIONFROMPOINTCLOSE, x, y);
        if (pos < 0) return;
        for (const DiffDoc::Tip &tip : m_doc->tips) {
            if (pos >= tip.pos && pos < tip.pos + tip.len) {
                QToolTip::showText(m_editor->viewport()->mapToGlobal(QPoint(x, y)), tip.text, m_editor->viewport());
                return;
            }
        }
    });
    connect(e, &ScintillaEditBase::dwellEnd, this, [](int, int) { QToolTip::hideText(); });

    if (settings) {
        connect(settings, &ApplicationSettings::diffWrapLinesChanged, this, [this](bool on) {
            if (!m_editor) return;
            send(SCI_SETWRAPMODE, on ? SC_WRAP_WORD : SC_WRAP_NONE);
            if (m_wrap) m_wrap->setChecked(on);
        });
        connect(settings, &ApplicationSettings::diffIgnoreWhitespaceChanged, this, [this](bool on) {
            if (m_whitespace) m_whitespace->setChecked(on);
            emit ignoreWhitespaceChanged(on);
        });
    }

    e->installEventFilter(this);
    e->viewport()->installEventFilter(this);
    buildChrome();
    applyStyles();
    layoutChrome();
}

void DiffView::applyStyles()
{
    if (!m_editor) return;
    const DiffViewColors &c = m_colors;

    send(SCI_STYLESETFORE, STYLE_DEFAULT, bgr(c.text));
    send(SCI_STYLESETBACK, STYLE_DEFAULT, bgr(c.canvas));
    send(SCI_STYLECLEARALL);

    struct Look {
        Style style;
        QColor fg;
        QColor back;
        bool bold;
    };
    const QColor none;
    const Look looks[] = {
        {Style::Default, c.text, none, false},
        {Style::Context, c.text, none, false},
        {Style::Added, c.text, none, false},
        {Style::Deleted, c.text, none, false},
        {Style::NoNewline, c.muted, none, false},
        {Style::HunkRange, c.hunkText, none, false},
        {Style::HunkContext, c.text, none, false},
        {Style::Chevron, c.muted, none, false},
        {Style::StatusAdded, c.statusAdded, none, true},
        {Style::StatusModified, c.statusModified, none, true},
        {Style::StatusDeleted, c.statusDeleted, none, true},
        {Style::StatusRenamed, c.statusRenamed, none, true},
        {Style::Dir, c.muted, none, false},
        {Style::Name, c.text, none, true},
        {Style::NameStruck, c.muted, none, true},
        {Style::Badge, c.muted, none, false},
        {Style::Plus, c.plus, none, false},
        {Style::Minus, c.minus, none, false},
        {Style::SqAdd, c.plus, none, false},
        {Style::SqDel, c.minus, none, false},
        {Style::SqNeutral, c.squareNeutral, none, false},
        {Style::Placeholder, c.muted, none, false},
        {Style::Link, c.link, none, false},
        {Style::CardTitle, c.text, none, true},
        {Style::CardBody, c.text, none, false},
        {Style::CardMeta, c.muted, none, false},
        {Style::HeaderPlain, c.muted, none, false},
        {Style::MarginContext, c.muted, none, false},
        {Style::MarginAdded, c.text, c.addNumber, false},
        {Style::MarginDeleted, c.text, c.delNumber, false},
        {Style::MarginHunk, c.hunkText, c.hunkLine, false},
        {Style::MarginHeader, c.muted, c.header, false},
    };
    static_assert(std::size(looks) == static_cast<size_t>(Style::Count));

    DiffStickyHeader::Looks sticky{};
    for (const Look &l : looks) {
        const int id = static_cast<int>(l.style);
        send(SCI_STYLESETFORE, id, bgr(l.fg));
        if (l.back.isValid()) send(SCI_STYLESETBACK, id, bgr(l.back));
        if (l.bold) send(SCI_STYLESETBOLD, id, 1);
        sticky[id] = DiffStickyHeader::StyleLook{l.fg, l.bold, l.style == Style::NameStruck};
    }
    send(SCI_STYLESETFORE, STYLE_LINENUMBER, bgr(c.muted));
    send(SCI_STYLESETBACK, STYLE_LINENUMBER, bgr(c.canvas));

    if (m_doc && m_doc->input) {
        for (const DiffDoc::Language &lang : m_doc->input->languages) {
            for (const DiffDoc::SlotStyle &s : lang.slotStyles) {
                send(SCI_STYLESETFORE, s.slot, bgr(slotColour(s.fg, m_dark)));
                if (s.fontStyle & 1) send(SCI_STYLESETBOLD, s.slot, 1);
                if (s.fontStyle & 2) send(SCI_STYLESETITALIC, s.slot, 1);
                if (s.fontStyle & 4) send(SCI_STYLESETUNDERLINE, s.slot, 1);
            }
        }
    }

    send(SCI_MARKERSETBACK, kMarkAdded, bgr(c.addLine));
    send(SCI_MARKERSETBACK, kMarkDeleted, bgr(c.delLine));
    send(SCI_MARKERSETBACK, kMarkHunk, bgr(c.hunkLine));
    send(SCI_MARKERSETBACK, kMarkHeader, bgr(c.header));
    send(SCI_MARKERSETBACK, kMarkRule, bgr(c.rule));

    send(SCI_INDICSETFORE, m_indAddWord, bgr(c.addWord));
    send(SCI_INDICSETFORE, m_indDelWord, bgr(c.delWord));
    send(SCI_INDICSETFORE, m_indStrike, bgr(c.muted));
    send(SCI_INDICSETHOVERFORE, m_indLink, bgr(c.link));
    send(SCI_INDICSETHOVERFORE, m_indChevron, bgr(c.link));

    if (m_sticky) m_sticky->setLooks(sticky, c.header, c.rule);
    refreshMetrics();
}

void DiffView::refreshMetrics()
{
    if (!m_editor) return;
    char family[128] = {};
    send(SCI_STYLEGETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(family));
    const qreal size = static_cast<qreal>(send(SCI_STYLEGETSIZEFRACTIONAL, STYLE_DEFAULT)) / SC_FONT_SIZE_MULTIPLIER;
    const int zoom = static_cast<int>(send(SCI_GETZOOM));
    QFont font(QString::fromUtf8(family));
    font.setPointSizeF(qMax<qreal>(2.0, size + zoom));
    m_font = font;
    m_lineHeight = qMax(1, static_cast<int>(send(SCI_TEXTHEIGHT, 0)));
    int base = static_cast<int>(send(SCI_GETMARGINLEFT));
    const int margins = static_cast<int>(send(SCI_GETMARGINS));
    for (int m = 0; m < margins; ++m) base += static_cast<int>(send(SCI_GETMARGINWIDTHN, m));
    m_textBase = base;
    m_stickyFile = -1;      // repaint the pinned row with the new metrics
    if (m_sticky) m_sticky->setMetrics(m_font, m_lineHeight, m_textBase - static_cast<int>(send(SCI_GETXOFFSET)));
}

// ---------------------------------------------------------------------------
// Building and applying documents

DiffView::Content DiffView::build(Source source, Mode mode, const std::shared_ptr<const DiffLanguages::Catalog> &catalog,
                                  qint64 nowSecs)
{
    PROFILE_SCOPE("DiffView::build");
    std::shared_ptr<const GitDiffParser::Result> parsed = source.parsed;
    if (!parsed) {
        const QByteArray patch = source.producePatch ? source.producePatch() : source.patch;
        parsed = std::make_shared<const GitDiffParser::Result>(GitDiffParser::parse(patch));
    }

    auto input = std::make_shared<DiffDoc::Input>();
    input->parsed = parsed;
    input->reasons = DiffDocumentBuilder::classifyFiles(*parsed, source.linguistGenerated);
    if (catalog) {
        DiffLanguages::Assignment a = DiffLanguages::assign(*catalog, *parsed);
        input->languages = std::move(a.languages);
        input->oldLanguage = std::move(a.oldLanguage);
        input->newLanguage = std::move(a.newLanguage);
    }
    input->commit = std::move(source.commit);
    input->issueUrlBase = std::move(source.issueUrlBase);
    input->nowSecs = nowSecs;

    Content content;
    content.doc = DiffDocumentBuilder::build(std::move(input));
    if (mode == Mode::Commit && parsed->files.size() >= 2) content.tree = DiffFileTreeModel::build(parsed->files);
    return content;
}

void DiffView::show(Source source, bool keepPosition)
{
    const quint64 generation = ++m_generation;
    std::shared_ptr<const DiffLanguages::Catalog> catalog;
    ApplicationSettings *settings = appSettings();
    if (settings && settings->syntaxHighlightDiffEnabled()) catalog = DiffLanguages::catalog();
    const qint64 now = QDateTime::currentSecsSinceEpoch();

    auto *watcher = new QFutureWatcher<Content>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, keepPosition] {
        watcher->deleteLater();
        if (generation != m_generation || !m_editor) return;
        apply(watcher->result(), keepPosition);
    });
    watcher->setFuture(QtConcurrent::run([source = std::move(source), mode = m_mode, catalog, now]() mutable {
        return build(std::move(source), mode, catalog, now);
    }));
}

void DiffView::showContent(Content content, bool keepPosition)
{
    ++m_generation;
    apply(std::move(content), keepPosition);
}

void DiffView::cancel()
{
    ++m_generation;
}

void DiffView::apply(Content content, bool keepPosition)
{
    PROFILE_SCOPE("DiffView::apply");
    if (!m_editor || !content.doc) return;

    qint32 keepTop = -1;
    qintptr keepCaret = 0;
    qintptr keepX = 0;
    if (keepPosition && m_doc) {
        keepTop = topDocLine();
        keepCaret = send(SCI_GETCURRENTPOS);
        keepX = send(SCI_GETXOFFSET);
    }

    m_doc = std::move(content.doc);
    m_tree = std::move(content.tree);
    ++m_docSerial;
    const DiffDoc::Document &doc = *m_doc;

    m_links.clear();
    m_pressedLink = -1;
    m_stickyFile = -1;
    m_files.clear();
    m_files.resize(doc.files.size());
    for (qsizetype f = 0; f < doc.files.size(); ++f) {
        m_files[f].bodyRows = doc.files[f].bodyRows;
        m_files[f].collapsed = doc.files[f].collapsed;
    }
    const qint32 rows = doc.rowCount();
    m_lines.resize(rows);
    LineInfo *line = m_lines.data();
    for (qint32 r = 0; r < rows; ++r)
        line[r] = LineInfo{doc.oldLn[r], doc.newLn[r], doc.kinds[r], false, false};
    m_decorateTimer->stop();
    m_decorateNext = -1;

    send(SCI_SETREADONLY, 0);
    send(SCI_SETILEXER, 0, 0);
    send(SCI_CLEARALL);
    send(SCI_MARKERDELETEALL, static_cast<quintptr>(-1));
    send(SCI_MARGINTEXTCLEARALL);
    applyStyles();

    {
        PROFILE_SCOPE("DiffView::apply.text");
        send(SCI_APPENDTEXT, doc.text.size(), reinterpret_cast<sptr_t>(doc.text.constData()));
        send(SCI_STARTSTYLING, 0);
        send(SCI_SETSTYLINGEX, doc.styles.size(), reinterpret_cast<sptr_t>(doc.styles.constData()));
    }

    rebuildTables();
    {
        // Links and strikes now (clicks and hover need them anywhere);
        // markers and word indicators by startDecoration below.
        PROFILE_SCOPE("DiffView::apply.indicators");
        paintRows(doc, 0, false, false);
        if (!doc.strikes.isEmpty()) {
            send(SCI_SETINDICATORCURRENT, m_indStrike);
            send(SCI_SETINDICATORVALUE, 1);
            for (const DiffDoc::Range &r : doc.strikes) send(SCI_INDICATORFILLRANGE, r.pos, r.len);
        }
    }
    finishEdit();
    send(SCI_EMPTYUNDOBUFFER);

    updateMarginWidth();
    refreshMetrics();

    if (keepTop >= 0) {
        send(SCI_SETEMPTYSELECTION, qMin<qintptr>(keepCaret, doc.text.size()));
        scrollLineToTop(qMin(keepTop, qMax(0, rows - 1)));
        send(SCI_SETXOFFSET, keepX);
    } else {
        send(SCI_SETEMPTYSELECTION, 0);
        send(SCI_SETFIRSTVISIBLELINE, 0);
        send(SCI_SETXOFFSET, 0);
    }

    if (m_treeModel) {
        if (m_filter) m_filter->clear();
        m_treeModel->setTree(m_tree);
        if (m_tree) m_treeView->expandAll();
    }
    layoutChrome();
    startDecoration();
    m_fileAtTop = -2;           // force the tree / signal update
    fillMargins();
    updateSticky();

    emit contentApplied(Content{m_doc, m_tree});
}

void DiffView::rebuildTables()
{
    m_headerLines.clear();
    m_hunkLines.clear();
    m_headerLines.reserve(m_files.size());
    const qint32 n = static_cast<qint32>(m_lines.size());
    const LineInfo *line = m_lines.constData();
    for (qint32 l = 0; l < n; ++l) {
        if (line[l].kind == RowKind::FileHeader)
            m_headerLines.append(l);
        else if (line[l].kind == RowKind::HunkHeader)
            m_hunkLines.append(l);
    }
}

void DiffView::stampMarkers(qint32 firstLine, qint32 count, bool clearFirst)
{
    const qint32 end = qMin<qint32>(firstLine + count, static_cast<qint32>(m_lines.size()));
    for (qint32 l = qMax(0, firstLine); l < end; ++l) {
        if (clearFirst) send(SCI_MARKERDELETE, l, -1);
        int marker = -1;
        switch (m_lines[l].kind) {
            case RowKind::Added: marker = kMarkAdded; break;
            case RowKind::Deleted: marker = kMarkDeleted; break;
            case RowKind::HunkHeader: marker = kMarkHunk; break;
            case RowKind::FileHeader: marker = kMarkHeader; break;
            case RowKind::Separator: marker = kMarkRule; break;
            default: break;
        }
        if (marker >= 0) send(SCI_MARKERADD, l, marker);
    }
}

void DiffView::paintRows(const DiffDoc::Rows &rows, qintptr base, bool clearFirst, bool words)
{
    const qintptr len = rows.text.size();
    if (clearFirst && len > 0) {
        for (int ind : {m_indAddWord, m_indDelWord, m_indStrike, m_indLink, m_indChevron}) {
            send(SCI_SETINDICATORCURRENT, ind);
            send(SCI_INDICATORCLEARRANGE, base, len);
        }
    }
    auto fill = [&](int ind, const QVector<DiffDoc::Range> &ranges) {
        if (ranges.isEmpty()) return;
        send(SCI_SETINDICATORCURRENT, ind);
        send(SCI_SETINDICATORVALUE, 1);
        for (const DiffDoc::Range &r : ranges) send(SCI_INDICATORFILLRANGE, base + r.pos, r.len);
    };
    if (words) {
        fill(m_indAddWord, rows.addWords);
        fill(m_indDelWord, rows.delWords);
    }
    for (const DiffDoc::Link &link : rows.links) {
        const int id = static_cast<int>(m_links.size());
        m_links.append(LinkRef{link.kind, link.file, link.payload});
        const bool chevron = link.kind == LinkKind::Chevron;
        if (chevron && link.file >= 0 && link.file < m_files.size()) m_files[link.file].chevronLink = id;
        setLinkIndicator(chevron ? m_indChevron : m_indLink, base + link.pos, link.len, id);
    }
}

void DiffView::setLinkIndicator(int indicator, qintptr pos, qintptr len, int link)
{
    send(SCI_SETINDICATORCURRENT, indicator);
    send(SCI_SETINDICATORVALUE, link + 1);
    send(SCI_INDICATORFILLRANGE, pos, len);
}

void DiffView::visibleWindow(qint32 &from, qint32 &to) const
{
    // Doc lines on screen plus one page either side.
    const qintptr firstVisible = send(SCI_GETFIRSTVISIBLELINE);
    const qintptr screen = qMax<qintptr>(1, send(SCI_LINESONSCREEN));
    from = static_cast<qint32>(send(SCI_DOCLINEFROMVISIBLE, firstVisible));
    to = static_cast<qint32>(send(SCI_DOCLINEFROMVISIBLE, firstVisible + screen));
    const qint32 page = to - from + 1;
    from = qMax(0, from - page);
    to = qMin(static_cast<qint32>(m_lines.size()) - 1, to + page);
}

void DiffView::startDecoration()
{
    if (m_lines.isEmpty()) return;
    qint32 from = 0;
    qint32 to = 0;
    visibleWindow(from, to);
    decorateLines(from, to + 1);
    m_decorateNext = 0;
    m_decorateTimer->start();
}

// Lines [from, to) not decorated yet: one marker per row, word indicators by
// position. Positions are the applied document's: nothing edits the text
// while the pump runs (replaceBodies flushes it first).
void DiffView::decorateLines(qint32 from, qint32 to)
{
    if (!m_doc) return;
    to = qMin<qint32>(to, static_cast<qint32>(m_lines.size()));
    LineInfo *lines = m_lines.data();
    auto fillWords = [this](int ind, const QVector<DiffDoc::Range> &ranges, qintptr start, qintptr end) {
        auto it = std::lower_bound(ranges.cbegin(), ranges.cend(), start,
                                   [](const DiffDoc::Range &r, qintptr pos) { return r.pos < pos; });
        if (it == ranges.cend() || it->pos >= end) return;
        send(SCI_SETINDICATORCURRENT, ind);
        send(SCI_SETINDICATORVALUE, 1);
        for (; it != ranges.cend() && it->pos < end; ++it) send(SCI_INDICATORFILLRANGE, it->pos, it->len);
    };
    for (qint32 l = qMax(0, from); l < to;) {
        if (lines[l].decorated) {
            ++l;
            continue;
        }
        const qint32 runStart = l;
        while (l < to && !lines[l].decorated) lines[l++].decorated = true;
        stampMarkers(runStart, l - runStart, false);
        const qintptr start = send(SCI_POSITIONFROMLINE, runStart);
        const qintptr end = send(SCI_POSITIONFROMLINE, l);
        fillWords(m_indAddWord, m_doc->addWords, start, end);
        fillWords(m_indDelWord, m_doc->delWords, start, end);
    }
}

void DiffView::decorateSlice()
{
    if (m_decorateNext < 0 || !m_editor) return;
    PROFILE_SCOPE("DiffView::decorateSlice");
    constexpr qint32 kChunk = 64;                // about 0.25 ms in Debug: overshoot stays small
    constexpr qint64 kBudgetNs = 4'000'000;     // 4 ms, then yield to input and paint
    const qint32 n = static_cast<qint32>(m_lines.size());
    QElapsedTimer t;
    t.start();
    while (m_decorateNext < n && t.nsecsElapsed() < kBudgetNs) {
        decorateLines(m_decorateNext, m_decorateNext + kChunk);
        m_decorateNext += kChunk;
    }
    if (m_decorateNext < n)
        m_decorateTimer->start();
    else
        m_decorateNext = -1;
}

void DiffView::flushDecoration()
{
    if (m_decorateNext < 0) return;
    m_decorateTimer->stop();
    decorateLines(m_decorateNext, static_cast<qint32>(m_lines.size()));
    m_decorateNext = -1;
}

void DiffView::markAllStyled()
{
    // Edits move Scintilla's end-styled mark back; nothing is left to style.
    send(SCI_STARTSTYLING, send(SCI_GETLENGTH));
}

void DiffView::finishEdit()
{
    markAllStyled();
    send(SCI_SETREADONLY, 1);
    send(SCI_SETSAVEPOINT);
}

// ---------------------------------------------------------------------------
// Line-number margin

void DiffView::updateMarginWidth()
{
    if (!m_editor) return;
    if (!m_doc || m_doc->files.isEmpty()) {
        m_oldDigits = m_newDigits = 0;
        send(SCI_SETMARGINWIDTHN, 0, 0);
        return;
    }
    const int oldDigits = digitsOf(qMax(1, m_doc->maxOldLn));
    const int newDigits = digitsOf(qMax(1, m_doc->maxNewLn));
    if (oldDigits != m_oldDigits || newDigits != m_newDigits) {
        m_oldDigits = oldDigits;
        m_newDigits = newDigits;
        for (LineInfo &l : m_lines) l.filled = false;
    }
    // Both columns, the gap, a trailing space and one cell of left padding.
    char sample[32];
    const int cells = qMin(static_cast<int>(sizeof(sample)) - 1, m_oldDigits + m_newDigits + 4);
    std::memset(sample, '0', cells);
    sample[cells] = 0;
    const int width = static_cast<int>(send(SCI_TEXTWIDTH, static_cast<int>(Style::MarginContext),
                                            reinterpret_cast<sptr_t>(sample)));
    send(SCI_SETMARGINWIDTHN, 0, width);
}

int DiffView::formatMargin(const LineInfo &line, char *out) const
{
    char *p = out;
    auto pad = [&p](int n) {
        for (int i = 0; i < n; ++i) {
            std::memcpy(p, kFigure, kFigureBytes);
            p += kFigureBytes;
        }
    };
    auto number = [&](qint32 v, int cells) {
        if (v <= 0) {
            pad(cells);
            return;
        }
        char digits[12];
        int n = 0;
        while (v > 0 && n < 12) {
            digits[n++] = static_cast<char>('0' + v % 10);
            v /= 10;
        }
        pad(cells - n);
        while (n > 0) *p++ = digits[--n];
    };
    number(line.kind == RowKind::Added ? -1 : line.oldLn, m_oldDigits);
    pad(2);
    number(line.kind == RowKind::Deleted ? -1 : line.newLn, m_newDigits);
    pad(1);
    *p = 0;
    return static_cast<int>(p - out);
}

void DiffView::fillMargins()
{
    if (!m_editor || m_lines.isEmpty() || m_oldDigits == 0) return;
    qint32 from = 0;
    qint32 to = 0;
    visibleWindow(from, to);

    char buf[96];
    LineInfo *lines = m_lines.data();
    for (qint32 l = from; l <= to; ++l) {
        LineInfo &li = lines[l];
        if (li.filled) continue;
        li.filled = true;
        int style = -1;
        const char *text = nullptr;
        switch (li.kind) {
            case RowKind::Context:
                style = static_cast<int>(Style::MarginContext);
                break;
            case RowKind::Added:
                style = static_cast<int>(Style::MarginAdded);
                break;
            case RowKind::Deleted:
                style = static_cast<int>(Style::MarginDeleted);
                break;
            case RowKind::HunkHeader:
                style = static_cast<int>(Style::MarginHunk);
                text = kFigure;
                break;
            case RowKind::FileHeader:
                style = static_cast<int>(Style::MarginHeader);
                text = kFigure;
                break;
            default:
                break;
        }
        if (style < 0) {
            send(SCI_MARGINSETTEXT, l, 0);
            continue;
        }
        if (!text) {
            formatMargin(li, buf);
            text = buf;
        }
        send(SCI_MARGINSETTEXT, l, reinterpret_cast<sptr_t>(text));
        send(SCI_MARGINSETSTYLE, l, style);
    }
}

// ---------------------------------------------------------------------------
// Collapse / expand

std::shared_ptr<const DiffDoc::Rows> DiffView::placeholderOf(int file)
{
    FileState &st = m_files[file];
    if (!st.placeholder && m_doc && m_doc->input)
        st.placeholder = std::make_shared<const DiffDoc::Rows>(DiffDocumentBuilder::buildPlaceholder(*m_doc->input, file));
    return st.placeholder;
}

void DiffView::swapChevron(int file, bool collapsed)
{
    const qint32 header = m_headerLines.value(file, -1);
    if (header < 0) return;
    const qintptr pos = send(SCI_POSITIONFROMLINE, header);
    const char *chevron = collapsed ? DiffDoc::kChevronClosed : DiffDoc::kChevronOpen;
    send(SCI_SETTARGETRANGE, pos, pos + DiffDoc::kChevronBytes);
    send(SCI_REPLACETARGET, DiffDoc::kChevronBytes, reinterpret_cast<sptr_t>(chevron));
    send(SCI_STARTSTYLING, pos);
    send(SCI_SETSTYLING, DiffDoc::kChevronBytes, static_cast<int>(Style::Chevron));
    if (m_files[file].chevronLink >= 0)
        setLinkIndicator(m_indChevron, pos, DiffDoc::kChevronBytes, m_files[file].chevronLink);
    if (file == m_stickyFile) m_stickyFile = -1;
}

void DiffView::replaceBody(int file, std::shared_ptr<const DiffDoc::Rows> rows, bool collapsed)
{
    replaceBodies({{file, std::move(rows)}}, collapsed);
}

void DiffView::replaceBodies(const QVector<std::pair<int, std::shared_ptr<const DiffDoc::Rows>>> &bodies, bool collapsed)
{
    if (!m_editor || bodies.isEmpty()) return;
    PROFILE_SCOPE("DiffView::replaceBodies");
    flushDecoration();          // its positions are the unedited document's
    auto sorted = bodies;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    // Where the top of the viewport lands: rows above an edit keep their
    // place, rows below shift with it, a top inside a replaced body goes to
    // that file's header.
    const qint32 oldTop = topDocLine();
    qint32 newTop = oldTop;
    {
        qint32 shift = 0;
        bool anchored = false;
        for (const auto &[f, rows] : sorted) {
            const qint32 h = m_headerLines[f];
            const qint32 old = m_files[f].bodyRows;
            if (oldTop > h + old) {
                shift += rows->rowCount() - old;
                continue;
            }
            newTop = (oldTop > h ? h : oldTop) + shift;
            anchored = true;
            break;
        }
        if (!anchored) newTop = oldTop + shift;
    }

    // Edit last to first so earlier line numbers stay valid.
    send(SCI_SETREADONLY, 0);
    for (auto it = sorted.crbegin(); it != sorted.crend(); ++it) {
        const int f = it->first;
        const DiffDoc::Rows &rows = *it->second;
        const qint32 first = m_headerLines[f] + 1;
        const qintptr start = send(SCI_POSITIONFROMLINE, first);
        const qintptr end = send(SCI_POSITIONFROMLINE, first + m_files[f].bodyRows);
        send(SCI_SETTARGETRANGE, start, end);
        send(SCI_REPLACETARGET, rows.text.size(), reinterpret_cast<sptr_t>(rows.text.constData()));
        send(SCI_STARTSTYLING, start);
        send(SCI_SETSTYLINGEX, rows.styles.size(), reinterpret_cast<sptr_t>(rows.styles.constData()));
        paintRows(rows, start, true);
        swapChevron(f, collapsed);
    }

    // Splice the line table once.
    qsizetype grown = m_lines.size();
    for (const auto &[f, rows] : sorted) grown += rows->rowCount() - m_files[f].bodyRows;
    QVector<LineInfo> lines;
    lines.resize(grown);
    LineInfo *out = lines.data();
    qint32 cursor = 0;
    for (const auto &[f, rows] : sorted) {
        const qint32 first = m_headerLines[f] + 1;
        out = std::copy(m_lines.cbegin() + cursor, m_lines.cbegin() + first, out);
        for (qint32 r = 0; r < rows->rowCount(); ++r)
            *out++ = LineInfo{rows->oldLn[r], rows->newLn[r], rows->kinds[r], false};
        cursor = first + m_files[f].bodyRows;
    }
    std::copy(m_lines.cbegin() + cursor, m_lines.cend(), out);
    m_lines = std::move(lines);

    for (const auto &[f, rows] : sorted) {
        FileState &st = m_files[f];
        st.bodyRows = rows->rowCount();
        st.current = rows;
        st.collapsed = collapsed;
    }
    rebuildTables();

    // Deleted lines merge their markers into the header row and inserted
    // lines start bare, leaving the next row's markers on the first new row:
    // restamp header .. row after the body and refill their margin text.
    for (const auto &[f, rows] : sorted) {
        const qint32 h = m_headerLines[f];
        const qint32 count = rows->rowCount() + 2;
        stampMarkers(h, count, true);
        const qint32 end = qMin<qint32>(h + count, static_cast<qint32>(m_lines.size()));
        for (qint32 l = h; l < end; ++l) m_lines[l].filled = false;
    }
    finishEdit();

    scrollLineToTop(qBound(0, newTop, qMax(0, static_cast<qint32>(m_lines.size()) - 1)));
    fillMargins();
    updateSticky();
}

void DiffView::startBodyJob(const QVector<int> &files)
{
    if (!m_doc || !m_doc->input || files.isEmpty()) return;
    const quint64 serial = m_docSerial;
    std::shared_ptr<const DiffDoc::Input> input = m_doc->input;

    QTimer::singleShot(kLoadingDelayMs, this, [this, serial, files] {
        if (serial != m_docSerial) return;
        QVector<std::pair<int, std::shared_ptr<const DiffDoc::Rows>>> pending;
        for (int f : files)
            if (m_files[f].loading && m_files[f].collapsed) pending.append({f, loadingRows()});
        if (!pending.isEmpty()) replaceBodies(pending, false);
    });

    using Bodies = QVector<std::shared_ptr<const DiffDoc::Rows>>;
    auto *watcher = new QFutureWatcher<Bodies>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, serial, files] {
        watcher->deleteLater();
        if (serial != m_docSerial || !m_editor) return;
        const Bodies bodies = watcher->result();
        QVector<std::pair<int, std::shared_ptr<const DiffDoc::Rows>>> ready;
        for (qsizetype i = 0; i < files.size() && i < bodies.size(); ++i) {
            FileState &st = m_files[files[i]];
            st.body = bodies[i];
            if (!st.loading) continue;          // collapsed again meanwhile
            st.loading = false;
            ready.append({files[i], bodies[i]});
        }
        if (!ready.isEmpty()) replaceBodies(ready, false);
    });
    watcher->setFuture(QtConcurrent::run([input, files] {
        PROFILE_SCOPE("DiffView::buildBodies");
        Bodies out;
        out.reserve(files.size());
        for (int f : files)
            out.append(std::make_shared<const DiffDoc::Rows>(DiffDocumentBuilder::buildBody(*input, f)));
        return out;
    }));
}

void DiffView::expandFile(int file)
{
    if (!m_doc || file < 0 || file >= m_files.size() || !m_doc->files[file].expandable) return;
    FileState &st = m_files[file];
    if (!st.collapsed || st.loading) return;
    if (st.body) {
        replaceBody(file, st.body, false);
        return;
    }
    st.loading = true;
    startBodyJob({file});
}

void DiffView::collapseFile(int file)
{
    if (!m_doc || file < 0 || file >= m_files.size() || !m_doc->files[file].expandable) return;
    FileState &st = m_files[file];
    st.loading = false;
    if (st.collapsed) return;        // placeholder still shown (a load may have been cancelled)
    replaceBody(file, placeholderOf(file), true);
}

void DiffView::toggleFile(int file)
{
    if (file < 0 || file >= m_files.size()) return;
    const FileState &st = m_files[file];
    if (st.collapsed && !st.loading)
        expandFile(file);
    else
        collapseFile(file);
}

void DiffView::setFileCollapsed(int file, bool collapsed)
{
    if (collapsed)
        collapseFile(file);
    else
        expandFile(file);
}

void DiffView::setAllCollapsed(bool collapsed)
{
    if (!m_doc) return;
    QVector<std::pair<int, std::shared_ptr<const DiffDoc::Rows>>> now;
    QVector<int> missing;
    for (int f = 0; f < m_files.size(); ++f) {
        if (!m_doc->files[f].expandable) continue;
        FileState &st = m_files[f];
        if (collapsed) {
            st.loading = false;
            if (!st.collapsed) now.append({f, placeholderOf(f)});
        } else if (st.collapsed && !st.loading) {
            if (st.body) {
                now.append({f, st.body});
            } else {
                st.loading = true;
                missing.append(f);
            }
        }
    }
    replaceBodies(now, collapsed);
    if (!missing.isEmpty()) startBodyJob(missing);
}

// ---------------------------------------------------------------------------
// Navigation

int DiffView::fileOfLine(qint32 line) const
{
    const auto it = std::upper_bound(m_headerLines.cbegin(), m_headerLines.cend(), line);
    return static_cast<int>(it - m_headerLines.cbegin()) - 1;
}

qint32 DiffView::topDocLine() const
{
    return static_cast<qint32>(send(SCI_DOCLINEFROMVISIBLE, send(SCI_GETFIRSTVISIBLELINE)));
}

void DiffView::scrollLineToTop(qint32 line)
{
    send(SCI_SETFIRSTVISIBLELINE, send(SCI_VISIBLEFROMDOCLINE, line));
}

qint32 DiffView::navLine() const
{
    // The caret, unless it was left off-screen by scrolling: then the top.
    const qint32 caret = static_cast<qint32>(send(SCI_LINEFROMPOSITION, send(SCI_GETCURRENTPOS)));
    const qint32 top = topDocLine();
    const qint32 bottom = static_cast<qint32>(
        send(SCI_DOCLINEFROMVISIBLE, send(SCI_GETFIRSTVISIBLELINE) + send(SCI_LINESONSCREEN)));
    return caret >= top && caret <= bottom ? caret : top;
}

void DiffView::moveTo(qint32 line, bool header)
{
    send(SCI_SETEMPTYSELECTION, send(SCI_POSITIONFROMLINE, line));
    // A row inside a file sits one row down, clear of the pinned header.
    const int f = fileOfLine(line);
    scrollLineToTop(!header && f >= 0 && line > m_headerLines[f] ? line - 1 : line);
}

void DiffView::nextHunk(int direction)
{
    if (!m_editor || m_hunkLines.isEmpty()) return;
    const qint32 at = navLine();
    qint32 target = -1;
    if (direction > 0) {
        const auto it = std::upper_bound(m_hunkLines.cbegin(), m_hunkLines.cend(), at);
        if (it != m_hunkLines.cend()) target = *it;
    } else {
        const auto it = std::lower_bound(m_hunkLines.cbegin(), m_hunkLines.cend(), at);
        if (it != m_hunkLines.cbegin()) target = *(it - 1);
    }
    if (target >= 0) moveTo(target, false);
}

void DiffView::nextFile(int direction)
{
    if (!m_editor || m_headerLines.isEmpty()) return;
    const qint32 at = navLine();
    qint32 target = -1;
    if (direction > 0) {
        const auto it = std::upper_bound(m_headerLines.cbegin(), m_headerLines.cend(), at);
        if (it != m_headerLines.cend()) target = *it;
    } else {
        const auto it = std::lower_bound(m_headerLines.cbegin(), m_headerLines.cend(), at);
        if (it != m_headerLines.cbegin()) target = *(it - 1);
    }
    if (target >= 0) moveTo(target, true);
}

void DiffView::revealFile(int file)
{
    if (!m_editor || file < 0 || file >= m_headerLines.size()) return;
    // The header row does not move when its own body is inserted.
    moveTo(m_headerLines[file], true);
    expandFile(file);
}

void DiffView::jumpToSource()
{
    if (!m_editor || !m_doc || m_lines.isEmpty()) return;
    const qint32 line = static_cast<qint32>(send(SCI_LINEFROMPOSITION, send(SCI_GETCURRENTPOS)));
    const int f = fileOfLine(line);
    if (f < 0 || line >= m_lines.size()) return;
    const LineInfo &li = m_lines[line];
    const DiffDoc::File &file = m_doc->files[f];

    if (m_mode == Mode::Commit && file.status == 'D') {
        openFile(f, qMax(1, li.oldLn), Side::ParentRevision);
        return;
    }
    const Side side = m_mode == Mode::Commit ? Side::Revision : Side::Working;
    const qint32 n = static_cast<qint32>(m_lines.size());
    auto newSide = [&](qint32 l) {
        const LineInfo &x = m_lines[l];
        return (x.kind == RowKind::Context || x.kind == RowKind::Added) && x.newLn > 0 ? x.newLn : -1;
    };

    qint32 target = newSide(line);
    if (target < 0 && (isBodyKind(li.kind) || li.kind == RowKind::HunkHeader)) {
        // Deleted rows: the next new-side row of the hunk, else one past the
        // previous one.
        for (qint32 l = line + 1; l < n && isBodyKind(m_lines[l].kind); ++l) {
            target = newSide(l);
            if (target > 0) break;
        }
        if (target < 0 && li.kind != RowKind::HunkHeader) {
            for (qint32 l = line - 1; l > m_headerLines[f] && isBodyKind(m_lines[l].kind); --l) {
                const qint32 v = newSide(l);
                if (v > 0) {
                    target = v + 1;
                    break;
                }
            }
        }
    }
    openFile(f, qMax(1, target), side);
}

// ---------------------------------------------------------------------------
// Scroll-driven updates

void DiffView::onUpdateUi()
{
    PROFILE_SCOPE("DiffView::onUpdateUi");
    if (m_decorateNext >= 0 && m_editor) {     // scrolled ahead of the pump
        qint32 from = 0;
        qint32 to = 0;
        visibleWindow(from, to);
        decorateLines(from, to + 1);
    }
    fillMargins();
    updateSticky();
}

void DiffView::updateSticky()
{
    if (!m_editor || !m_sticky) return;
    const qint32 top = topDocLine();
    const int f = fileOfLine(top);
    if (f != m_fileAtTop) {
        m_fileAtTop = f;
        syncTreeSelection();
        emit fileAtTopChanged(f);
    }
    const bool show = f >= 0 && top > m_headerLines[f] && top <= m_headerLines[f] + m_files[f].bodyRows;
    if (!show) {
        if (m_sticky->isVisible()) m_sticky->hide();
        return;
    }
    if (f != m_stickyFile) {
        m_stickyFile = f;
        const DiffDoc::File &file = m_doc->files[f];
        QByteArray text = file.headerText;
        if (text.size() >= DiffDoc::kChevronBytes) {
            const bool closed = m_files[f].collapsed;
            std::memcpy(text.data(), closed ? DiffDoc::kChevronClosed : DiffDoc::kChevronOpen,
                        DiffDoc::kChevronBytes);
        }
        m_sticky->setRow(text, file.headerStyles);
        m_sticky->setToolTip(QString::fromUtf8(file.path));
    }
    m_sticky->setMetrics(m_font, m_lineHeight, m_textBase - static_cast<int>(send(SCI_GETXOFFSET)));
    if (!m_sticky->isVisible()) {
        m_sticky->show();
        m_sticky->raise();
    }
}

// ---------------------------------------------------------------------------
// Links, menus, opening files

int DiffView::linkAt(qintptr pos) const
{
    if (!m_editor || pos < 0) return -1;
    qintptr v = send(SCI_INDICATORVALUEAT, m_indChevron, pos);
    if (v <= 0) v = send(SCI_INDICATORVALUEAT, m_indLink, pos);
    const qintptr id = v - 1;
    return id >= 0 && id < m_links.size() ? static_cast<int>(id) : -1;
}

DiffView::Side DiffView::revisionSide(int file) const
{
    return m_doc && file >= 0 && file < m_doc->files.size() && m_doc->files[file].status == 'D'
               ? Side::ParentRevision
               : Side::Revision;
}

void DiffView::activateLink(int link)
{
    if (link < 0 || link >= m_links.size()) return;
    const LinkRef ref = m_links[link];
    switch (ref.kind) {
        case LinkKind::Chevron:
            toggleFile(ref.file);
            break;
        case LinkKind::Path:
            openFile(ref.file, 1, m_mode == Mode::Commit ? revisionSide(ref.file) : Side::Working);
            break;
        case LinkKind::LoadDiff:
            expandFile(ref.file);
            break;
        case LinkKind::CopySha:
            QApplication::clipboard()->setText(QString::fromLatin1(ref.payload));
            if (m_editor) QToolTip::showText(QCursor::pos(), tr("Copied"), m_editor->viewport());
            break;
        case LinkKind::Parent:
            emit openCommitRequested(ref.payload);
            break;
        case LinkKind::Url: {
            const QUrl url(QString::fromUtf8(ref.payload));
            const QString scheme = url.scheme().toLower();
            if (url.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https")))
                QDesktopServices::openUrl(url);
            break;
        }
    }
}

QString DiffView::relPathOf(int file) const
{
    return m_doc && file >= 0 && file < m_doc->files.size() ? QString::fromUtf8(m_doc->files[file].path) : QString();
}

QString DiffView::absolutePathOf(int file) const
{
    QString rel = relPathOf(file);
    if (m_repoRoot.isEmpty() || rel.isEmpty()) return rel;
    if (m_repoRoot.contains(QLatin1String("://")))     // remote workspace: keep POSIX separators
        return m_repoRoot + (m_repoRoot.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + rel;
    return QDir::toNativeSeparators(QDir(m_repoRoot).filePath(rel));
}

void DiffView::openFile(int file, int line, Side side)
{
    const QString rel = relPathOf(file);
    if (!rel.isEmpty()) emit openFileRequested(rel, line, side);
}

void DiffView::showHeaderMenu(int file, const QPoint &globalPos)
{
    if (!m_editor) return;
    QMenu menu(m_editor);
    // Actions run past QMenu::exec: opening a file may switch tabs. A new
    // document by then makes `file` mean another file: drop the action.
    auto later = [this](std::function<void()> fn) {
        return [this, fn = std::move(fn)] {
            const quint64 serial = m_docSerial;
            QTimer::singleShot(0, this, [this, fn, serial] {
                if (serial == m_docSerial) fn();
            });
        };
    };
    if (m_doc && file >= 0 && file < m_files.size() && m_doc->files[file].expandable) {
        menu.addAction(m_files[file].collapsed ? tr("Expand file") : tr("Collapse file"),
                       later([this, file] { toggleFile(file); }));
        menu.addSeparator();
    }
    menu.addAction(tr("Copy path"), later([this, file] {
        QApplication::clipboard()->setText(absolutePathOf(file));
    }));
    menu.addAction(tr("Copy relative path"), later([this, file] {
        QApplication::clipboard()->setText(relPathOf(file));
    }));
    menu.addSeparator();
    menu.addAction(tr("Open file"), later([this, file] { openFile(file, 1, Side::Working); }));
    if (m_mode == Mode::Commit)
        menu.addAction(tr("Open file at this revision"),
                       later([this, file] { openFile(file, 1, revisionSide(file)); }));
    menu.addAction(tr("Reveal in file tree"), later([this, file] {
        const QString rel = relPathOf(file);
        if (!rel.isEmpty()) emit revealRequested(rel);
    }));
    menu.exec(globalPos);
}

// ---------------------------------------------------------------------------
// Chrome: toolbar, sticky header, file tree

void DiffView::buildChrome()
{
    ScintillaNext *e = m_editor.data();
    ApplicationSettings *settings = appSettings();

    m_sticky = new DiffStickyHeader(e);
    m_sticky->hide();
    connect(m_sticky, &DiffStickyHeader::chevronClicked, this, [this] { toggleFile(m_stickyFile); });
    connect(m_sticky, &DiffStickyHeader::pathClicked, this, [this] {
        if (m_stickyFile >= 0)
            openFile(m_stickyFile, 1, m_mode == Mode::Commit ? revisionSide(m_stickyFile) : Side::Working);
    });
    connect(m_sticky, &DiffStickyHeader::copyPathClicked, this, [this] {
        if (m_stickyFile >= 0) QApplication::clipboard()->setText(absolutePathOf(m_stickyFile));
    });
    connect(m_sticky, &DiffStickyHeader::openClicked, this, [this] {
        if (m_stickyFile >= 0)
            openFile(m_stickyFile, 1, m_mode == Mode::Commit ? revisionSide(m_stickyFile) : Side::Working);
    });

    m_toolbar = new DiffToolbar(e);
    auto *row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(4, 4, 4, 4);
    row->setSpacing(4);
    auto button = [this, row](const QString &tip, bool checkable) {
        auto *b = new QToolButton(m_toolbar);
        b->setAutoRaise(true);
        b->setToolTip(tip);
        b->setIconSize(QSize(16, 16));
        b->setCheckable(checkable);
        row->addWidget(b);
        return b;
    };
    m_treeButton = button(tr("Show file tree (T filters files)"), true);
    m_treeButton->setChecked(m_treeVisible);
    m_treeButton->setVisible(m_mode == Mode::Commit);
    m_expandAll = button(tr("Expand all files"), false);
    m_collapseAll = button(tr("Collapse all files"), false);
    row->addStretch(1);
    m_whitespace = button(tr("Hide whitespace changes"), true);
    m_whitespace->setChecked(settings && settings->diffIgnoreWhitespace());
    m_wrap = button(tr("Wrap lines"), true);
    m_wrap->setChecked(settings && settings->diffWrapLines());
    m_prevHunk = button(tr("Previous change (Shift+F7)"), false);
    m_nextHunk = button(tr("Next change (F7)"), false);

    connect(m_treeButton, &QToolButton::toggled, this, [this](bool on) { setTreeShown(on); });
    connect(m_expandAll, &QToolButton::clicked, this, [this] { setAllCollapsed(false); });
    connect(m_collapseAll, &QToolButton::clicked, this, [this] { setAllCollapsed(true); });
    connect(m_whitespace, &QToolButton::toggled, this, [](bool on) {
        ApplicationSettings *s = appSettings();
        if (s && s->diffIgnoreWhitespace() != on) s->setDiffIgnoreWhitespace(on);
    });
    connect(m_wrap, &QToolButton::toggled, this, [](bool on) {
        ApplicationSettings *s = appSettings();
        if (s && s->diffWrapLines() != on) s->setDiffWrapLines(on);
    });
    connect(m_prevHunk, &QToolButton::clicked, this, [this] { nextHunk(-1); });
    connect(m_nextHunk, &QToolButton::clicked, this, [this] { nextHunk(1); });
    m_toolbar->installEventFilter(this);

    if (m_mode == Mode::Commit) {
        m_treeHost = new QWidget(e);
        m_treeHost->setAutoFillBackground(true);
        m_treeHost->setBackgroundRole(QPalette::Base);
        auto *column = new QVBoxLayout(m_treeHost);
        column->setContentsMargins(4, 4, 0, 0);
        column->setSpacing(4);
        m_filter = new QLineEdit(m_treeHost);
        m_filter->setObjectName(QStringLiteral("editDiffFileFilter"));
        m_filter->setPlaceholderText(tr("Filter files…"));
        m_filter->setClearButtonEnabled(true);
        column->addWidget(m_filter);

        m_treeModel = new DiffFileTreeModel(this);
        m_treeProxy = DiffFileTreeModel::createFilterProxy(this);
        m_treeProxy->setSourceModel(m_treeModel);
        m_treeDelegate = new GitStatusItemDelegate(this);
        m_treeView = new QTreeView(m_treeHost);
        m_treeView->setObjectName(QStringLiteral("treeDiffFiles"));
        m_treeView->setModel(m_treeProxy);
        m_treeView->setItemDelegate(m_treeDelegate);
        m_treeView->setHeaderHidden(true);
        m_treeView->setUniformRowHeights(true);
        m_treeView->setFrameShape(QFrame::NoFrame);
        m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_treeView->setTextElideMode(Qt::ElideMiddle);
        column->addWidget(m_treeView, 1);

        auto reveal = [this](const QModelIndex &proxyIndex) {
            const int file = proxyIndex.data(DiffFileTreeModel::FileIndexRole).toInt();
            if (file >= 0) revealFile(file);
        };
        connect(m_treeView, &QTreeView::clicked, this, reveal);
        connect(m_treeView, &QTreeView::activated, this, reveal);
        // Same menu as a file header, so a file can be folded from the tree.
        m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_treeView, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            const QModelIndex index = m_treeView->indexAt(pos);
            const int file = index.isValid() ? index.data(DiffFileTreeModel::FileIndexRole).toInt() : -1;
            if (file >= 0) showHeaderMenu(file, m_treeView->viewport()->mapToGlobal(pos));
        });
        connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
            m_treeProxy->setFilterFixedString(text);
            m_treeView->expandAll();
        });
        m_filter->installEventFilter(this);
        m_treeView->installEventFilter(this);

        auto *handle = new DiffTreeHandle(e);
        handle->onPress = [this] { m_dragStartWidth = m_treeWidth; };
        handle->onDrag = [this](int dx) {
            if (!m_editor) return;
            const int maxWidth = qMax(kMinTreeWidth, m_editor->width() / 2);
            m_treeWidth = qBound(kMinTreeWidth, m_dragStartWidth + dx, maxWidth);
            layoutChrome();
        };
        handle->onRelease = [this] {
            ApplicationSettings *s = appSettings();
            if (s && s->diffTreeWidth() != m_treeWidth) s->setDiffTreeWidth(m_treeWidth);
        };
        m_treeHandle = handle;
    }

    retintChrome();
    layoutChrome();
}

bool DiffView::treeAvailable() const
{
    return m_mode == Mode::Commit && m_tree && m_doc && m_doc->files.size() >= 2;
}

void DiffView::setTreeShown(bool shown)
{
    m_treeVisible = shown;
    ApplicationSettings *s = appSettings();
    if (s && s->diffTreeVisible() != shown) s->setDiffTreeVisible(shown);
    layoutChrome();
}

void DiffView::layoutChrome()
{
    ScintillaNext *e = m_editor.data();
    if (!e || !m_toolbar) return;
    const int toolbarHeight = m_toolbar->sizeHint().height();
    const bool tree = treeAvailable() && m_treeVisible;
    const QMargins margins(tree ? m_treeWidth + kHandleWidth : 0, toolbarHeight, 0, 0);
    if (margins != m_chromeMargins) {
        m_chromeMargins = margins;
        e->setChromeMargins(margins);
    }
    if (m_treeButton) {
        m_treeButton->setEnabled(treeAvailable());
        const QSignalBlocker block(m_treeButton);
        m_treeButton->setChecked(m_treeVisible);
    }

    const QRect content = e->contentsRect();
    const QRect vp = e->viewport()->geometry();
    m_toolbar->setGeometry(content.left(), content.top(), vp.right() + 1 - content.left(), toolbarHeight);
    // Built after the editor may already be on screen: a child created under a
    // visible parent stays hidden until shown, leaving only its reserved margin.
    if (m_toolbar->isHidden()) m_toolbar->show();
    m_toolbar->raise();
    if (m_treeHost) {
        m_treeHost->setVisible(tree);
        m_treeHandle->setVisible(tree);
        if (tree) {
            m_treeHost->setGeometry(content.left(), vp.top(), m_treeWidth, vp.height());
            m_treeHandle->setGeometry(content.left() + m_treeWidth, vp.top(), kHandleWidth, vp.height());
            m_treeHost->raise();
            m_treeHandle->raise();
        }
    }
    if (m_sticky) {
        m_sticky->setGeometry(vp.left(), vp.top(), vp.width(), m_lineHeight);
        m_sticky->raise();
    }
}

void DiffView::syncTreeSelection()
{
    if (!m_treeView || !m_treeModel || !m_treeView->isVisible() || m_fileAtTop < 0) return;
    const QModelIndex index = m_treeProxy->mapFromSource(m_treeModel->indexOfFile(m_fileAtTop));
    if (!index.isValid()) return;
    // setCurrentIndex scrolls only when the row is off-screen and never takes focus.
    m_treeView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
}

void DiffView::retintChrome()
{
    if (!m_toolbar) return;
    const QColor color = m_toolbar->palette().color(QPalette::ButtonText);
    auto tint = [&color](QToolButton *b, const char *path) {
        if (b) b->setIcon(terminalTintedIcon(QString::fromLatin1(path), color));
    };
    tint(m_treeButton, ":/icons/panel-left.svg");
    tint(m_expandAll, ":/icons/chevrons-up-down.svg");
    tint(m_collapseAll, ":/icons/chevrons-down-up.svg");
    tint(m_whitespace, ":/icons/whitespace.svg");
    tint(m_wrap, ":/icons/wrap-text.svg");
    tint(m_prevHunk, ":/icons/chevron-up.svg");
    tint(m_nextHunk, ":/icons/chevron-down.svg");
}

void DiffView::focusFilter()
{
    if (!m_filter || !treeAvailable()) return;
    if (!m_treeVisible) setTreeShown(true);
    m_filter->setFocus(Qt::ShortcutFocusReason);
    m_filter->selectAll();
}

bool DiffView::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_editor) return false;
    const QEvent::Type type = event->type();

    // View keys are taken through ShortcutOverride so they win over window
    // shortcuts (F7 also toggles the search results dock) only while focus
    // is in this view.
    if (type == QEvent::ShortcutOverride || type == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const bool onEditor = watched == m_editor;
        if (watched == m_filter && ke->key() == Qt::Key_Escape && ke->modifiers() == Qt::NoModifier) {
            if (type == QEvent::KeyPress) {
                m_filter->clear();
                m_editor->setFocus(Qt::OtherFocusReason);
            }
            event->accept();
            return true;
        }
        if (watched == m_editor || watched == m_filter || watched == m_treeView) {
            KeyAction action = keyActionOf(ke, onEditor);
            if (action == KeyAction::FocusFilter && !treeAvailable()) action = KeyAction::None;
            if (action != KeyAction::None) {
                event->accept();
                if (type == QEvent::KeyPress) {
                    switch (action) {
                        case KeyAction::NextHunk: nextHunk(1); break;
                        case KeyAction::PrevHunk: nextHunk(-1); break;
                        case KeyAction::NextFile: nextFile(1); break;
                        case KeyAction::PrevFile: nextFile(-1); break;
                        case KeyAction::FocusFilter: focusFilter(); break;
                        case KeyAction::Jump: jumpToSource(); break;
                        case KeyAction::None: break;
                    }
                }
                return true;
            }
        }
        return false;
    }

    if (watched == m_editor->viewport()) {
        if (type == QEvent::Resize) {
            layoutChrome();
        } else if (type == QEvent::ContextMenu && m_doc) {
            auto *ce = static_cast<QContextMenuEvent *>(event);
            if (ce->reason() != QContextMenuEvent::Mouse) return false;
            // Nearest position: a click past the end of the header text still hits its row.
            const qintptr pos = send(SCI_POSITIONFROMPOINT, ce->pos().x(), ce->pos().y());
            const qint32 line = static_cast<qint32>(send(SCI_LINEFROMPOSITION, pos));
            const qintptr lineTop = send(SCI_POINTYFROMPOSITION, 0, send(SCI_POSITIONFROMLINE, line));
            if (ce->pos().y() < lineTop || ce->pos().y() >= lineTop + m_lineHeight) return false;
            if (line >= m_lines.size() || m_lines[line].kind != RowKind::FileHeader) return false;
            showHeaderMenu(fileOfLine(line), ce->globalPos());
            return true;
        }
    } else if (watched == m_toolbar && type == QEvent::PaletteChange) {
        retintChrome();
    }
    return false;
}
