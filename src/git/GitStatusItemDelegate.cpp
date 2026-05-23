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

#include "GitStatusItemDelegate.h"

#include "GitDiffPalette.h"
#include "GitStatusModel.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>

GitStatusItemDelegate::GitStatusItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{}

void GitStatusItemDelegate::setDarkPalette(bool dark)
{
    m_isDark = dark;
}

void GitStatusItemDelegate::paint(QPainter *painter,
                                  const QStyleOptionViewItem &option,
                                  const QModelIndex &index) const
{
    const bool isSection = index.data(GitStatusModel::IsSectionRole).toBool();
    if (isSection || !index.isValid()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // Let the style draw the row background (selection / hover / alternation).
    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);

    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);

    const QString primary = index.data(Qt::DisplayRole).toString();
    // Selection: standard Qt behaviour is HighlightedText overrides the
    // model's ForegroundRole. Our delegate paints text manually, so we
    // have to reproduce that — otherwise the change-color (e.g. Modified
    // blue) painted on top of the Highlight blue is unreadable.
    const bool selected = opt.state & QStyle::State_Selected;
    QColor primaryColor;
    if (selected) {
        primaryColor = opt.palette.color(QPalette::HighlightedText);
    } else {
        const QBrush primaryBrush = index.data(Qt::ForegroundRole).value<QBrush>();
        primaryColor = primaryBrush.style() == Qt::NoBrush
            ? opt.palette.color(QPalette::Text)
            : primaryBrush.color();
    }

    const int added   = index.data(GitStatusModel::AddedLinesRole).toInt();
    const int deleted = index.data(GitStatusModel::DeletedLinesRole).toInt();
    const bool isBinary = index.data(GitStatusModel::IsBinaryRole).toBool();

    QString numstatBinary;
    QString openParen;
    QString plusPart;
    QString minusPart;
    QString closeParen;
    if (isBinary) {
        numstatBinary = QStringLiteral(" (bin)");
    } else if (added >= 0 && deleted >= 0 && (added != 0 || deleted != 0)) {
        // Format: "name (+6 -3)" — plus first, then minus, parens neutral.
        openParen  = QStringLiteral(" (");
        plusPart   = QStringLiteral("+%1").arg(added);
        minusPart  = QStringLiteral(" -%1").arg(deleted);
        closeParen = QStringLiteral(")");
    }

    // Second parens — submodule inner-diff aggregate. Only when this is a
    // submodule entry AND the controller has populated stats AND the totals
    // are non-zero (skip "(+0 -0)" since it carries no information).
    const bool isSubmodule = index.data(GitStatusModel::IsSubmoduleRole).toBool();
    const int subAdded   = index.data(GitStatusModel::SubAddedLinesRole).toInt();
    const int subDeleted = index.data(GitStatusModel::SubDeletedLinesRole).toInt();
    QString subOpen, subPlus, subMinus, subClose;
    if (isSubmodule && subAdded >= 0 && subDeleted >= 0
        && (subAdded != 0 || subDeleted != 0)) {
        subOpen  = QStringLiteral(" (");
        subPlus  = QStringLiteral("+%1").arg(subAdded);
        subMinus = QStringLiteral(" -%1").arg(subDeleted);
        subClose = QStringLiteral(")");
    }

    const QFontMetrics fm(opt.font);
    painter->save();
    painter->setFont(opt.font);
    painter->setClipRect(textRect);

    const int baseline = textRect.top() + (textRect.height() + fm.ascent() - fm.descent()) / 2;
    int x = textRect.left();
    const int right = textRect.right();

    // Primary segment (change-colored) — elide if needed to leave room for numstat.
    const int suffixWidth = fm.horizontalAdvance(
        numstatBinary + openParen + plusPart + minusPart + closeParen
        + subOpen + subPlus + subMinus + subClose);
    const int primaryAvail = qMax(0, right - x - suffixWidth);
    const QString elidedPrimary = fm.elidedText(primary, Qt::ElideMiddle, primaryAvail);
    painter->setPen(primaryColor);
    painter->drawText(x, baseline, elidedPrimary);
    x += fm.horizontalAdvance(elidedPrimary);

    if (!numstatBinary.isEmpty()) {
        painter->setPen(opt.palette.color(QPalette::PlaceholderText));
        painter->drawText(x, baseline, numstatBinary);
    } else if (!plusPart.isEmpty()) {
        const auto &pal = GitDiffPalette::current(m_isDark);
        const QColor parenColor = opt.palette.color(QPalette::PlaceholderText);
        painter->setPen(parenColor);
        painter->drawText(x, baseline, openParen);
        x += fm.horizontalAdvance(openParen);
        painter->setPen(pal.fgPlus);
        painter->drawText(x, baseline, plusPart);
        x += fm.horizontalAdvance(plusPart);
        painter->setPen(pal.fgMinus);
        painter->drawText(x, baseline, minusPart);
        x += fm.horizontalAdvance(minusPart);
        painter->setPen(parenColor);
        painter->drawText(x, baseline, closeParen);
        x += fm.horizontalAdvance(closeParen);
    }

    if (!subPlus.isEmpty()) {
        const auto &pal = GitDiffPalette::current(m_isDark);
        const QColor parenColor = opt.palette.color(QPalette::PlaceholderText);
        painter->setPen(parenColor);
        painter->drawText(x, baseline, subOpen);
        x += fm.horizontalAdvance(subOpen);
        painter->setPen(pal.fgPlus);
        painter->drawText(x, baseline, subPlus);
        x += fm.horizontalAdvance(subPlus);
        painter->setPen(pal.fgMinus);
        painter->drawText(x, baseline, subMinus);
        x += fm.horizontalAdvance(subMinus);
        painter->setPen(parenColor);
        painter->drawText(x, baseline, subClose);
    }

    // Focus rect if applicable
    if (opt.state & QStyle::State_HasFocus) {
        QStyleOptionFocusRect fo;
        fo.QStyleOption::operator=(opt);
        fo.rect = style->subElementRect(QStyle::SE_ItemViewItemFocusRect, &opt, opt.widget);
        fo.state |= QStyle::State_KeyboardFocusChange | QStyle::State_Item;
        fo.backgroundColor = opt.palette.color(
            (opt.state & QStyle::State_Selected) ? QPalette::Highlight
                                                  : QPalette::Window);
        style->drawPrimitive(QStyle::PE_FrameFocusRect, &fo, painter, opt.widget);
    }

    painter->restore();
}

QSize GitStatusItemDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize s = QStyledItemDelegate::sizeHint(option, index);
    // Reserve a little extra so numstat suffix never clips. Submodules can
    // carry two parens groups (pointer + inner diff), so reserve for both.
    if (!index.data(GitStatusModel::IsSectionRole).toBool()) {
        const QFontMetrics fm(option.font);
        const bool isSubmodule = index.data(GitStatusModel::IsSubmoduleRole).toBool();
        const QString reserve = isSubmodule
            ? QStringLiteral(" (+99999 -99999) (+99999 -99999)")
            : QStringLiteral(" (+99999 -99999)");
        s.setWidth(s.width() + fm.horizontalAdvance(reserve));
    }
    return s;
}
