/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

inline constexpr char kBrowserPinnedTabsSettingsKey[] = "Browser/PinnedTabs";

// Chrome-style pin chrome for a browser/mini-app dock tab. On a real
// ads::CDockWidgetTab this is the only call needed: CDockWidgetTab::setText
// (CElidingLabel is not reachable via QLabel*), hide/show label+close, tooltip,
// and pinned max-width. Unpin restores title + close. Callers must not follow
// this with a second setText.
void applyBrowserTabPinChrome(QWidget *tab, bool pinned, const QString &fullTitle);

// Target index of a tab in a dock area after pin/unpin. `pinned` is the
// per-tab pin state at call time; `fromIndex` is excluded from the cluster
// count so it does not matter whether the caller flipped nnPinned yet.
// Pinning and unpinning both land the tab at the end of the remaining
// pinned prefix (Chrome: pinned cluster on the left).
int browserTabPinMoveTarget(const QVector<bool> &pinned, int fromIndex, bool pinning);

QString browserPinKeyForQuickBrowser(const QString &url);
QString browserPinKeyForMiniApp(const QString &appId);
QString browserPinKeyIdentity(const QString &key);
bool isQuickBrowserPinKey(const QString &key);
bool isMiniAppPinKey(const QString &key);

QStringList addBrowserPinKey(QStringList keys, const QString &key);
QStringList removeBrowserPinKey(QStringList keys, const QString &key);
QStringList replaceBrowserPinKey(QStringList keys, const QString &oldKey, const QString &newKey);

// Drop ma:<id> entries whose id is not in knownMiniAppIds. qb: keys are kept.
QStringList pruneStaleMiniAppPinKeys(const QStringList &keys, const QStringList &knownMiniAppIds);

// Width of a pinned tab after title/close/spacers are collapsed: icon plus
// symmetric layout margins plus QSS padding/border. Pin sets min and max to
// this so the favicon is centered, not left-heavy.
int browserTabPinnedMaxWidth(const QWidget *tab);

// ADS middle-click close is gated on DockWidgetClosable, which pin clears so
// the close button stays hidden. Install once per browser/mini-app tab so a
// pinned tab still closes on middle-click. No-op if `tab` is not a dock tab.
void installPinnedTabMiddleClickClose(QWidget *tab);
