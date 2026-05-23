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

#include "GitHistoryItemDelegate.h"

#include "GitHistoryModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFontMetrics>
#include <QLocale>
#include <QPainter>
#include <QPalette>

namespace {

constexpr int kPaddingX = 8;
constexpr int kPaddingTop = 4;
constexpr int kPaddingBottom = 4;
constexpr int kLineGap = 2;

// Draw text with highlighted substring (case-insensitive). The match runs
// are painted bold over normal text. Falls back to plain drawText when
// filter is empty.
void drawTextWithHighlight(QPainter *p, const QRect &r, const QString &text,
                           const QString &filter,
                           const QPalette &palette, bool selected,
                           const QFont &baseFont, Qt::TextElideMode elide)
{
    QFontMetrics fm(baseFont);
    const QString elided = fm.elidedText(text, elide, r.width());

    if (filter.isEmpty()) {
        p->setFont(baseFont);
        p->setPen(selected ? palette.color(QPalette::HighlightedText)
                           : palette.color(QPalette::Text));
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, elided);
        return;
    }

    // Walk through `elided` and find each occurrence of `filter`. Use a
    // case-insensitive search built on QStringView.
    const QString lower = elided.toLower();
    const QString flo = filter.toLower();
    qsizetype pos = 0;
    int x = r.x();
    const QColor base = selected ? palette.color(QPalette::HighlightedText)
                                 : palette.color(QPalette::Text);
    QFont bold = baseFont;
    bold.setBold(true);
    while (pos < elided.size()) {
        const qsizetype hit = lower.indexOf(flo, pos);
        if (hit < 0) {
            // Tail.
            const QString tail = elided.mid(pos);
            p->setFont(baseFont);
            p->setPen(base);
            p->drawText(QRect(x, r.y(), r.right() - x + 1, r.height()),
                        Qt::AlignVCenter | Qt::AlignLeft, tail);
            return;
        }
        // Pre-match.
        if (hit > pos) {
            const QString pre = elided.mid(pos, hit - pos);
            p->setFont(baseFont);
            p->setPen(base);
            p->drawText(QRect(x, r.y(), r.right() - x + 1, r.height()),
                        Qt::AlignVCenter | Qt::AlignLeft, pre);
            x += fm.horizontalAdvance(pre);
        }
        // Match (use elided's chars in matched range to preserve case).
        const QString matchStr = elided.mid(hit, flo.size());
        p->setFont(bold);
        p->setPen(selected ? palette.color(QPalette::HighlightedText)
                           : palette.color(QPalette::Link));
        p->drawText(QRect(x, r.y(), r.right() - x + 1, r.height()),
                    Qt::AlignVCenter | Qt::AlignLeft, matchStr);
        x += QFontMetrics(bold).horizontalAdvance(matchStr);
        pos = hit + flo.size();
    }
}

} // namespace

GitHistoryItemDelegate::GitHistoryItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void GitHistoryItemDelegate::setFilterQuery(const QString &query)
{
    m_filter = query;
}

QSize GitHistoryItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                       const QModelIndex &index) const
{
    Q_UNUSED(index);
    QFontMetrics fm(option.font);
    const int lineH = fm.height();
    QFont small = option.font;
    small.setPointSizeF(qMax(7.0, small.pointSizeF() - 1.0));
    QFontMetrics fm2(small);
    const int line2H = fm2.height();
    return QSize(0, kPaddingTop + lineH + kLineGap + line2H + kPaddingBottom);
}

void GitHistoryItemDelegate::paint(QPainter *painter,
                                    const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    painter->save();
    const QRect r = option.rect;
    const bool selected = option.state & QStyle::State_Selected;

    // Background fill (selection / alternate row).
    if (selected) {
        painter->fillRect(r, option.palette.brush(QPalette::Highlight));
    } else if (option.features & QStyleOptionViewItem::Alternate) {
        painter->fillRect(r, option.palette.brush(QPalette::AlternateBase));
    }

    const QString subject = index.data(GitHistoryModel::SubjectRole).toString();
    const QString author  = index.data(GitHistoryModel::AuthorNameRole).toString();
    const QString shortSha = index.data(GitHistoryModel::ShortShaRole).toString();
    const qint64 ctime   = index.data(GitHistoryModel::CtimeRole).toLongLong();
    const bool isMerge   = index.data(GitHistoryModel::IsMergeRole).toBool();

    const QString relTime = formatRelativeTime(ctime);

    // Layout.
    QFont topFont = option.font;
    QFontMetrics topFm(topFont);
    QFont bottomFont = option.font;
    bottomFont.setPointSizeF(qMax(7.0, bottomFont.pointSizeF() - 1.0));
    QFontMetrics botFm(bottomFont);

    const QRect topRect(r.x() + kPaddingX,
                        r.y() + kPaddingTop,
                        r.width() - 2 * kPaddingX,
                        topFm.height());
    const QRect botRect(r.x() + kPaddingX,
                        topRect.bottom() + kLineGap,
                        r.width() - 2 * kPaddingX,
                        botFm.height());

    // Top line: subject (+ optional [merge] badge in front).
    QString topLine = subject;
    if (isMerge) {
        // Tiny badge using brackets; no extra widget allocations per row.
        topLine = QCoreApplication::translate("GitHistoryItemDelegate",
                                              "[merge] ") + subject;
    }
    drawTextWithHighlight(painter, topRect, topLine, m_filter,
                          option.palette, selected, topFont, Qt::ElideRight);

    // Bottom line: author • relTime • shortSha (muted).
    QString bottomLine;
    if (!author.isEmpty()) bottomLine += author;
    if (!relTime.isEmpty()) {
        if (!bottomLine.isEmpty()) bottomLine += QStringLiteral(" · ");
        bottomLine += relTime;
    }
    if (!shortSha.isEmpty()) {
        if (!bottomLine.isEmpty()) bottomLine += QStringLiteral(" · ");
        bottomLine += shortSha;
    }
    painter->setFont(bottomFont);
    painter->setPen(selected
                    ? option.palette.color(QPalette::HighlightedText)
                    : option.palette.color(QPalette::PlaceholderText));
    const QString elidedBottom = botFm.elidedText(bottomLine, Qt::ElideRight,
                                                   botRect.width());
    painter->drawText(botRect, Qt::AlignVCenter | Qt::AlignLeft, elidedBottom);

    painter->restore();
}

QString GitHistoryItemDelegate::formatRelativeTime(qint64 ctime)
{
    if (ctime <= 0) return {};
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 d = now - ctime;
    if (d < 0) d = 0;

    if (d < 60) {
        return QCoreApplication::translate("GitHistoryItemDelegate", "just now");
    }
    if (d < 3600) {
        const int n = static_cast<int>(d / 60);
        return QCoreApplication::translate("GitHistoryItemDelegate",
                                            "%n minute(s) ago", nullptr, n);
    }
    if (d < 86400) {
        const int n = static_cast<int>(d / 3600);
        return QCoreApplication::translate("GitHistoryItemDelegate",
                                            "%n hour(s) ago", nullptr, n);
    }
    if (d < 7 * 86400) {
        const int n = static_cast<int>(d / 86400);
        return QCoreApplication::translate("GitHistoryItemDelegate",
                                            "%n day(s) ago", nullptr, n);
    }
    if (d < 30 * 86400) {
        const int n = static_cast<int>(d / (7 * 86400));
        return QCoreApplication::translate("GitHistoryItemDelegate",
                                            "%n week(s) ago", nullptr, n);
    }
    // > ~30 days: absolute short-format date in user locale.
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(ctime);
    return QLocale::system().toString(dt.date(), QLocale::ShortFormat);
}
