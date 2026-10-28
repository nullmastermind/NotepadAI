#include "HexPreviewWidget.h"

#include "HexFormat.h"
#include "HexSearch.h"
#include "HexView.h"
#include "ImageDecoder.h"

#include <QComboBox>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLineEdit>
#include <QLocale>
#include <QTimer>
#include <QToolButton>
#include <QtConcurrent>

namespace {

constexpr int kSearchBannerDelayMs = 300;
constexpr qint64 kInspectorBytes = 8;

} // namespace

HexPreviewWidget::HexPreviewWidget(const QString &typeId, const QString &displayName, QWidget *parent)
    : AssetPreviewWidget(typeId, displayName, parent)
    , m_searchCancel(std::make_shared<std::atomic_bool>(false))
{
    m_view = new HexView;
    m_view->setSource(&m_source);
    setContentWidget(m_view);

    m_goto = new QLineEdit;
    m_goto->setPlaceholderText(tr("Go to offset"));
    m_goto->setToolTip(tr("Hex (0x1F or 1f), decimal, or +/- relative to the cursor (Ctrl+G)"));
    m_goto->setFixedWidth(130);
    addToolWidget(m_goto);
    addToolGap();

    m_find = new QLineEdit;
    m_find->setPlaceholderText(tr("Find"));
    m_find->setToolTip(tr("Enter: next, Shift+Enter: previous (F3 / Shift+F3)"));
    m_find->setClearButtonEnabled(true);
    m_find->setFixedWidth(200);
    addToolWidget(m_find);
    m_findMode = new QComboBox;
    m_findMode->addItem(tr("Hex"), true);
    m_findMode->addItem(tr("Text"), false);
    m_findMode->setToolTip(tr("Search for hex bytes or UTF-8 text"));
    addToolWidget(m_findMode);
    QToolButton *prev = addToolButton(tr("◀"), tr("Previous match (Shift+F3)"));
    QToolButton *next = addToolButton(tr("▶"), tr("Next match (F3)"));
    addToolGap();

    m_bytesPerRow = new QComboBox;
    m_bytesPerRow->setToolTip(tr("Bytes per row"));
    for (int bytes : {8, 16, 32})
        m_bytesPerRow->addItem(tr("%1 bytes").arg(bytes), bytes);
    m_bytesPerRow->setCurrentIndex(1);
    addToolWidget(m_bytesPerRow);

    m_searchBanner = new QTimer(this);
    m_searchBanner->setSingleShot(true);
    m_searchBanner->setInterval(kSearchBannerDelayMs);
    connect(m_searchBanner, &QTimer::timeout, this, [this]() {
        showBanner(BannerKind::Neutral, tr("Searching…"), tr("Cancel"), [this]() {
            cancelSearch();
            hideBanner();
        });
    });

    connect(m_goto, &QLineEdit::returnPressed, this, &HexPreviewWidget::goTo);
    connect(m_find, &QLineEdit::returnPressed, this, [this]() {
        find(QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier));
    });
    connect(prev, &QToolButton::clicked, this, [this]() { find(true); });
    connect(next, &QToolButton::clicked, this, [this]() { find(false); });
    connect(m_bytesPerRow, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        m_view->setBytesPerRow(m_bytesPerRow->currentData().toInt());
    });
    connect(m_view, &HexView::cursorChanged, this, &HexPreviewWidget::updateInfo);
    connect(m_view, &HexView::findRequested, this, [this]() {
        m_find->setFocus();
        m_find->selectAll();
    });
    connect(m_view, &HexView::findNextRequested, this, &HexPreviewWidget::find);
    connect(m_view, &HexView::gotoRequested, this, [this]() {
        m_goto->setFocus();
        m_goto->selectAll();
    });
    connect(&m_search, &QFutureWatcher<SearchResult>::finished, this, &HexPreviewWidget::onSearchFinished);
}

HexPreviewWidget::~HexPreviewWidget()
{
    // The worker holds its own reader and flag; it only needs to stop early.
    m_searchCancel->store(true);
}

void HexPreviewWidget::startLoad(bool reload)
{
    Q_UNUSED(reload);   // the view keeps its cursor and scroll; sourceChanged clamps them
    cancelSearch();
    const QFileInfo info(filePath());
    if (!info.exists()) {
        showBanner(BannerKind::Error, tr("The file no longer exists."));
        m_source.reset({}, 0);
        m_view->sourceChanged();
        return;
    }
    hideBanner();
    m_source.reset(HexSource::fileReader(filePath()), info.size());
    m_view->sourceChanged();
    updateInfo();
}

qint64 HexPreviewWidget::byteSize() const
{
    return QFileInfo(filePath()).size();
}

void HexPreviewWidget::selectRange(qint64 offset, qint64 length)
{
    if (m_source.size() == 0)
        startLoad(false);
    hideBanner();
    m_view->select(offset, length);
}

void HexPreviewWidget::goTo()
{
    const std::optional<qint64> offset = HexFormat::parseOffset(m_goto->text(), m_view->cursorOffset());
    if (!offset) {
        showBanner(BannerKind::Warning, tr("\"%1\" is not an offset.").arg(m_goto->text()));
        return;
    }
    hideBanner();
    if (*offset >= m_source.size() && m_source.size() > 0)
        showBanner(BannerKind::Neutral, tr("The file ends at 0x%1; showing its last byte.")
                                            .arg(HexFormat::offsetText(m_source.size() - 1, 1)));
    m_view->setCursorOffset(*offset, false);
    m_view->setFocus();
}

void HexPreviewWidget::cancelSearch()
{
    m_searchCancel->store(true);
    m_searchCancel = std::make_shared<std::atomic_bool>(false);
    ++m_searchSerial;
    m_searchBanner->stop();
}

void HexPreviewWidget::find(bool backward)
{
    QByteArray pattern;
    QString error;
    if (!HexFormat::parsePattern(m_find->text(), m_findMode->currentData().toBool(), &pattern, &error)) {
        showBanner(BannerKind::Warning, error);
        return;
    }
    hideBanner();
    cancelSearch();
    const qint64 size = m_source.size();
    // Forward from the byte after the selection start, backward from before it,
    // then the rest of the file (wrap-around).
    const qint64 anchor = m_view->selectionStart();
    const qint64 from = backward ? anchor : std::min(size, anchor + 1);
    m_patternLength = pattern.size();
    const std::shared_ptr<std::atomic_bool> cancel = m_searchCancel;
    const HexSource::Reader read = HexSource::fileReader(filePath());
    m_search.setFuture(QtConcurrent::run([serial = m_searchSerial, read, size, pattern, from, backward, cancel]() {
        SearchResult r;
        r.serial = serial;
        r.offset = backward ? HexSearch::find(read, size, pattern, 0, from, true, *cancel)
                            : HexSearch::find(read, size, pattern, from, size, false, *cancel);
        if (r.offset == HexSearch::kNotFound) {
            r.wrapped = true;
            r.offset = backward ? HexSearch::find(read, size, pattern, from, size, true, *cancel)
                                : HexSearch::find(read, size, pattern, 0, from, false, *cancel);
        }
        return r;
    }));
    m_searchBanner->start();
}

void HexPreviewWidget::onSearchFinished()
{
    m_searchBanner->stop();
    const SearchResult r = m_search.result();
    if (r.serial != m_searchSerial || r.offset == HexSearch::kCancelled)
        return;
    if (r.offset == HexSearch::kReadError) {
        showBanner(BannerKind::Error, tr("The file could not be read."));
        return;
    }
    if (r.offset < 0) {
        showBanner(BannerKind::Neutral, tr("Not found: %1").arg(m_find->text()));
        return;
    }
    if (r.wrapped)
        showBanner(BannerKind::Neutral, tr("Search wrapped around the file."));
    else
        hideBanner();
    m_view->select(r.offset, m_patternLength);
}

void HexPreviewWidget::updateInfo()
{
    const QLocale locale;
    const qint64 size = m_source.size();
    QStringList parts;
    parts << tr("Binary") << QStringLiteral("%1 (%2 bytes)")
                                 .arg(ImageDecoder::formatBytes(size), locale.toString(size));
    if (size > 0) {
        const qint64 cursor = m_view->cursorOffset();
        parts << tr("offset 0x%1 (%2)").arg(HexFormat::offsetText(cursor, 1), locale.toString(cursor));
        const qint64 selected = m_view->selectionLength();
        if (selected > 1)
            parts << tr("%1 bytes selected").arg(locale.toString(selected));
        char bytes[kInspectorBytes];
        const qint64 got = m_source.read(cursor, bytes, kInspectorBytes);
        const QString values = HexFormat::inspector(QByteArrayView(bytes, std::max<qint64>(0, got)));
        if (!values.isEmpty())
            parts << values;
    }
    setInfoText(parts.join(HexFormat::separator()));
}
