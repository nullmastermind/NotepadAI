/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "BrowserTabPin.h"

#include "DockManager.h"
#include "DockWidget.h"
#include "DockWidgetTab.h"

#include <QLabel>
#include <QLayout>
#include <QList>
#include <QMouseEvent>
#include <QSpacerItem>
#include <QStyle>
#include <QVariant>

namespace {

QList<QSpacerItem *> layoutSpacers(QLayout *layout)
{
    QList<QSpacerItem *> out;
    if (!layout)
        return out;
    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem *item = layout->itemAt(i);
        if (!item || item->widget())
            continue;
        if (QSpacerItem *sp = item->spacerItem())
            out.append(sp);
    }
    return out;
}

// ADS keeps icon-to-title and close-button spacers after the favicon. Those
// land on the right and make a pinned tab look left-heavy. Zero them while
// pinned and mirror the left layout margin on the right. Restore on unpin.
void applyPinnedLayoutBalance(QWidget *tab, bool pinned)
{
    QLayout *layout = tab->layout();
    if (!layout)
        return;

    if (pinned) {
        if (!tab->property("nnUnpinnedLayoutMargins").isValid())
            tab->setProperty("nnUnpinnedLayoutMargins",
                             QVariant::fromValue(layout->contentsMargins()));

        QList<int> saved = tab->property("nnUnpinnedSpacers").value<QList<int>>();
        const QList<QSpacerItem *> spacers = layoutSpacers(layout);
        if (spacers.size() > saved.size()) {
            const int added = spacers.size() - saved.size();
            for (int i = 0; i < added; ++i)
                saved.insert(i, spacers[i]->sizeHint().width());
            tab->setProperty("nnUnpinnedSpacers", QVariant::fromValue(saved));
        } else if (!tab->property("nnUnpinnedSpacers").isValid()) {
            saved.clear();
            for (QSpacerItem *sp : spacers)
                saved.append(sp->sizeHint().width());
            tab->setProperty("nnUnpinnedSpacers", QVariant::fromValue(saved));
        }

        const QMargins m = tab->property("nnUnpinnedLayoutMargins").value<QMargins>();
        layout->setContentsMargins(m.left(), m.top(), m.left(), m.bottom());
        for (QSpacerItem *sp : spacers)
            sp->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        layout->invalidate();
        return;
    }

    if (tab->property("nnUnpinnedLayoutMargins").isValid()) {
        layout->setContentsMargins(tab->property("nnUnpinnedLayoutMargins").value<QMargins>());
        tab->setProperty("nnUnpinnedLayoutMargins", QVariant());
    }
    if (tab->property("nnUnpinnedSpacers").isValid()) {
        const QList<int> saved = tab->property("nnUnpinnedSpacers").value<QList<int>>();
        const QList<QSpacerItem *> spacers = layoutSpacers(layout);
        for (int i = 0; i < spacers.size(); ++i) {
            const int w = i < saved.size() ? saved.at(i) : 0;
            spacers[i]->changeSize(w, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        }
        tab->setProperty("nnUnpinnedSpacers", QVariant());
    }
    layout->invalidate();
}

} // namespace

namespace {

// Parent to the tab so the filter dies with it. Acts only while nnPinned is
// set; unpinned middle-clicks fall through to ADS.
class PinnedTabMiddleClickCloser : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::MouseButtonRelease)
            return false;

        auto *tab = qobject_cast<ads::CDockWidgetTab *>(watched);
        if (!tab)
            return false;

        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() != Qt::MiddleButton)
            return false;

        ads::CDockWidget *dw = tab->dockWidget();
        if (!dw || !dw->property("nnPinned").toBool())
            return false;
        if (!ads::CDockManager::testConfigFlag(ads::CDockManager::MiddleMouseButtonClosesTab))
            return false;
        // Release outside the tab cancels, same as ADS.
        if (!tab->rect().contains(me->position().toPoint()))
            return false;

        dw->requestCloseDockWidget();
        return true;
    }
};

} // namespace

void installPinnedTabMiddleClickClose(QWidget *tab)
{
    auto *dockTab = qobject_cast<ads::CDockWidgetTab *>(tab);
    if (!dockTab || dockTab->property("nnPinMiddleClick").toBool())
        return;
    dockTab->setProperty("nnPinMiddleClick", true);
    dockTab->installEventFilter(new PinnedTabMiddleClickCloser(dockTab));
}

void applyBrowserTabPinChrome(QWidget *tab, bool pinned, const QString &fullTitle)
{
    if (!tab)
        return;

    const QString titleText = pinned ? QString() : fullTitle;
    auto *dockTab = qobject_cast<ads::CDockWidgetTab *>(tab);
    if (dockTab)
        dockTab->setText(titleText);

    if (QLabel *label = tab->findChild<QLabel *>(QStringLiteral("dockWidgetTabLabel"))) {
        // CElidingLabel::setText is not QLabel::setText. Fake test widgets are
        // plain QLabel; real ADS tabs already got setText via CDockWidgetTab.
        if (!dockTab)
            label->setText(titleText);
        label->setVisible(!pinned);
    }

    if (QWidget *close = tab->findChild<QWidget *>(QStringLiteral("tabCloseButton")))
        close->setVisible(!pinned);

    tab->setToolTip(fullTitle);
    applyPinnedLayoutBalance(tab, pinned);

    if (pinned) {
        if (!tab->property("nnUnpinnedMaxWidth").isValid())
            tab->setProperty("nnUnpinnedMaxWidth", tab->maximumWidth());
        if (!tab->property("nnUnpinnedMinWidth").isValid())
            tab->setProperty("nnUnpinnedMinWidth", tab->minimumWidth());
        const int w = browserTabPinnedMaxWidth(tab);
        tab->setMinimumWidth(w);
        tab->setMaximumWidth(w);
    } else {
        if (tab->property("nnUnpinnedMinWidth").isValid()) {
            tab->setMinimumWidth(tab->property("nnUnpinnedMinWidth").toInt());
            tab->setProperty("nnUnpinnedMinWidth", QVariant());
        }
        if (tab->property("nnUnpinnedMaxWidth").isValid()) {
            tab->setMaximumWidth(tab->property("nnUnpinnedMaxWidth").toInt());
            tab->setProperty("nnUnpinnedMaxWidth", QVariant());
        }
    }

    tab->updateGeometry();
}

int browserTabPinMoveTarget(const QVector<bool> &pinned, int fromIndex, bool pinning)
{
    Q_UNUSED(pinning);
    if (fromIndex < 0 || fromIndex >= pinned.size())
        return fromIndex;

    int pinnedCount = 0;
    for (int i = 0; i < pinned.size(); ++i) {
        if (i != fromIndex && pinned[i])
            ++pinnedCount;
    }
    return pinnedCount;
}

QString browserPinKeyForQuickBrowser(const QString &url)
{
    if (url.isEmpty())
        return {};
    return QStringLiteral("qb:") + url;
}

QString browserPinKeyForMiniApp(const QString &appId)
{
    if (appId.isEmpty())
        return {};
    return QStringLiteral("ma:") + appId;
}

QString browserPinKeyIdentity(const QString &key)
{
    if (key.startsWith(QLatin1String("qb:")))
        return key.mid(3);
    if (key.startsWith(QLatin1String("ma:")))
        return key.mid(3);
    return {};
}

bool isQuickBrowserPinKey(const QString &key)
{
    return key.startsWith(QLatin1String("qb:")) && key.size() > 3;
}

bool isMiniAppPinKey(const QString &key)
{
    return key.startsWith(QLatin1String("ma:")) && key.size() > 3;
}

QStringList addBrowserPinKey(QStringList keys, const QString &key)
{
    if (key.isEmpty())
        return keys;
    keys.append(key);
    return keys;
}

QStringList removeBrowserPinKey(QStringList keys, const QString &key)
{
    if (key.isEmpty())
        return keys;
    keys.removeOne(key);
    return keys;
}

QStringList replaceBrowserPinKey(QStringList keys, const QString &oldKey, const QString &newKey)
{
    if (oldKey == newKey)
        return keys;
    const int idx = keys.indexOf(oldKey);
    if (idx >= 0) {
        if (newKey.isEmpty())
            keys.removeAt(idx);
        else
            keys.replace(idx, newKey);
        return keys;
    }
    return addBrowserPinKey(keys, newKey);
}

QStringList pruneStaleMiniAppPinKeys(const QStringList &keys, const QStringList &knownMiniAppIds)
{
    QStringList out;
    out.reserve(keys.size());
    for (const QString &key : keys) {
        if (isQuickBrowserPinKey(key)) {
            out.append(key);
            continue;
        }
        if (isMiniAppPinKey(key) && knownMiniAppIds.contains(browserPinKeyIdentity(key)))
            out.append(key);
    }
    return out;
}

int browserTabPinnedMaxWidth(const QWidget *tab)
{
    if (!tab)
        return 0;

    const int icon = tab->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, tab);
    const QMargins cm = tab->contentsMargins();
    int chrome = cm.left() + cm.right();
    if (tab->width() > 0) {
        const int fromRect = tab->width() - tab->contentsRect().width();
        if (fromRect > chrome)
            chrome = fromRect;
    }

    const QLayout *layout = tab->layout();
    if (!layout)
        return icon + chrome;

    const QMargins m = layout->contentsMargins();
    int walked = m.left() + m.right();
    for (int i = 0; i < layout->count(); ++i) {
        const QLayoutItem *item = layout->itemAt(i);
        if (!item)
            continue;
        if (const QWidget *w = item->widget()) {
            if (w->isHidden())
                continue;
            walked += w->sizeHint().width();
        } else {
            walked += item->sizeHint().width();
        }
    }

    const int floor = icon + m.left() + m.right();
    return qMax(walked, floor) + chrome;
}
