#ifndef HEXPREVIEWWIDGET_H
#define HEXPREVIEWWIDGET_H

#include "AssetPreviewWidget.h"
#include "HexSource.h"

#include <QFutureWatcher>

#include <atomic>
#include <memory>

class HexView;
class QComboBox;
class QLineEdit;
class QTimer;

// Hex tab for files the resolver calls Binary (add-hex-preview): the paged
// HexView plus go to offset, find (on a worker, cancellable, wrapping) and
// bytes per row; the info line carries the offset, selection and the data
// inspector. Opening costs one stat: no byte is read until a row is painted.
class HexPreviewWidget : public AssetPreviewWidget
{
    Q_OBJECT

public:
    HexPreviewWidget(const QString &typeId, const QString &displayName, QWidget *parent = nullptr);
    ~HexPreviewWidget() override;

    // --- Control API (docs/adr/0007) ---
    // The bytes the tab shows (its file, read page by page; never held open).
    qint64 byteSize() const;
    // Selects [offset, offset + length) and scrolls there, as a find hit or
    // Go to does; loads the view first if the tab was never shown.
    void selectRange(qint64 offset, qint64 length);

protected:
    void startLoad(bool reload) override;

private:
    struct SearchResult {
        quint64 serial = 0;     // m_searchSerial when started
        qint64 offset = -1;     // HexSearch codes when negative
        bool wrapped = false;
    };

    void goTo();
    void find(bool backward);
    void onSearchFinished();
    void cancelSearch();
    void updateInfo();

    HexSource m_source;
    HexView *m_view = nullptr;
    QLineEdit *m_goto = nullptr;
    QLineEdit *m_find = nullptr;
    QComboBox *m_findMode = nullptr;
    QComboBox *m_bytesPerRow = nullptr;
    QTimer *m_searchBanner = nullptr;

    QFutureWatcher<SearchResult> m_search;
    std::shared_ptr<std::atomic_bool> m_searchCancel;
    qint64 m_patternLength = 0;
    quint64 m_searchSerial = 0;     // bumped by cancelSearch: queued stale results are dropped
};

#endif // HEXPREVIEWWIDGET_H
