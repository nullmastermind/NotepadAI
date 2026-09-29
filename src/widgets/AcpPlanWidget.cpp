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

#include "AcpPlanWidget.h"

#include <cstdint>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

constexpr int kGlyphSize = 14;
constexpr int kVisibleRows = 3;

class PlanHeaderIcon final : public QWidget
{
public:
    explicit PlanHeaderIcon(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(kGlyphSize, kGlyphSize);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(palette().color(QPalette::PlaceholderText), 1.3, Qt::SolidLine, Qt::RoundCap);
        p.setPen(pen);
        const qreal tickEnd = 4.0;
        const qreal lineStart = 6.0;
        const qreal lineEnd = width() - 2.0;
        const qreal ys[3] = {3.5, 7.0, 10.5};
        for (qreal y : ys) {
            p.drawLine(QPointF(2.0, y), QPointF(tickEnd, y));
            p.drawLine(QPointF(lineStart, y), QPointF(lineEnd, y));
        }
    }
};

class PlanStatusGlyph final : public QWidget
{
public:
    enum Kind : std::uint8_t { Pending, Running, Done };

    explicit PlanStatusGlyph(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(kGlyphSize, kGlyphSize);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setKind(Kind k)
    {
        if (m_kind == k)
            return;
        m_kind = k;
        update();
    }

    void setAngle(int sixteenths)
    {
        m_angle = sixteenths;
        if (m_kind == Running)
            update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(2.0, 2.0, -2.0, -2.0);
        if (m_kind == Running) {
            QPen pen(palette().color(QPalette::Highlight), 1.6, Qt::SolidLine, Qt::RoundCap);
            p.setPen(pen);
            p.drawArc(r, m_angle, 270 * 16);
            return;
        }
        if (m_kind == Done) {
            QPen pen(palette().color(QPalette::PlaceholderText), 1.6,
                     Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            QPainterPath path;
            path.moveTo(r.left() + 0.5, r.center().y());
            path.lineTo(r.center().x() - 0.2, r.bottom() - 1.2);
            path.lineTo(r.right() - 0.4, r.top() + 1.0);
            p.drawPath(path);
            return;
        }
        QPen pen(palette().color(QPalette::PlaceholderText), 1.4);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(r);
    }

private:
    Kind m_kind = Pending;
    int m_angle = 0;
};

PlanStatusGlyph::Kind kindForStatus(const QString &status)
{
    if (status == QLatin1String("completed"))
        return PlanStatusGlyph::Done;
    if (status == QLatin1String("in_progress") || status == QLatin1String("running"))
        return PlanStatusGlyph::Running;
    return PlanStatusGlyph::Pending;
}

int measureRowHeight(QWidget *row, int rowWidth)
{
    if (!row)
        return kGlyphSize;
    int textH = row->fontMetrics().height();
    if (rowWidth > 0) {
        const auto labels = row->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
        if (!labels.isEmpty()) {
            QLabel *text = labels.first();
            const int textW = qMax(1, rowWidth - kGlyphSize - 6);
            const int hfw = text->wordWrap() ? text->heightForWidth(textW) : text->sizeHint().height();
            if (hfw > 0)
                textH = hfw;
        }
    }
    return qMax(kGlyphSize, textH);
}

} // namespace

AcpPlanWidget::AcpPlanWidget(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::NoFrame);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    setStyleSheet(QStringLiteral(
        "AcpPlanWidget { background: transparent; border: none;"
        " border-top: 1px solid palette(mid); }"));
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(8, 8, 8, 4);
    m_layout->setSpacing(4);

    auto *headerRow = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    headerLayout->addWidget(new PlanHeaderIcon(headerRow));

    auto *header = new QLabel(tr("Tasks"), headerRow);
    header->setStyleSheet(QStringLiteral("QLabel { color: palette(placeholder-text); }"));
    headerLayout->addWidget(header);

    headerLayout->addStretch();

    m_resumeBtn = new QPushButton(tr("Resume"), headerRow);
    m_resumeBtn->setFlat(true);
    m_resumeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: palette(link); font-size: 11px; padding: 0 4px; }"
        "QPushButton:hover { text-decoration: underline; }"));
    m_resumeBtn->setVisible(false);
    connect(m_resumeBtn, &QPushButton::clicked, this, [this]() {
        QStringList lines;
        for (const auto &e : m_entries) {
            if (e.status != QLatin1String("completed"))
                lines.append(QStringLiteral("- ") + e.text);
        }
        if (!lines.isEmpty()) {
            emit resumeRequested(
                tr("Please continue completing the following tasks:\n") + lines.join(QLatin1Char('\n')));
        }
    });
    headerLayout->addWidget(m_resumeBtn);

    m_badge = new QLabel(headerRow);
    m_badge->setStyleSheet(QStringLiteral(
        "QLabel { color: palette(placeholder-text); font-size: 11px; }"));
    headerLayout->addWidget(m_badge);

    m_layout->addWidget(headerRow);

    auto *listRow = new QHBoxLayout();
    listRow->setContentsMargins(0, 0, 0, 0);
    listRow->setSpacing(2);

    m_listViewport = new QWidget(this);
    m_listViewport->setAutoFillBackground(true);
    m_listViewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_listViewport->installEventFilter(this);
    m_listHost = new QWidget(m_listViewport);
    m_listHost->installEventFilter(this);
    m_listHost->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_listLayout = new QVBoxLayout(m_listHost);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(4);
    listRow->addWidget(m_listViewport, 1);

    m_vbar = new QScrollBar(Qt::Vertical, this);
    m_vbar->setVisible(false);
    m_vbar->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    connect(m_vbar, &QScrollBar::valueChanged, this, &AcpPlanWidget::applyListScroll);
    listRow->addWidget(m_vbar);

    m_layout->addLayout(listRow);

    m_spinTimer = new QTimer(this);
    m_spinTimer->setInterval(80);
    connect(m_spinTimer, &QTimer::timeout, this, &AcpPlanWidget::tickSpin);
}

void AcpPlanWidget::clearRows()
{
    m_spinGlyphs.clear();
    m_activeRow.clear();
    if (m_spinTimer)
        m_spinTimer->stop();
    if (!m_listLayout)
        return;
    while (QLayoutItem *li = m_listLayout->takeAt(0)) {
        if (QWidget *w = li->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete li;
    }
}

void AcpPlanWidget::setEntries(const QList<AcpProtocol::AcpPlanEntry> &entries)
{
    const int reveal = indexToReveal(entries);
    m_entries = entries;
    clearRows();
    QWidget *active = nullptr;
    int i = 0;
    for (const auto &entry : entries) {
        auto *row = new QWidget(m_listHost);
        row->installEventFilter(this);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(6);

        auto *icon = new PlanStatusGlyph(row);
        const auto kind = kindForStatus(entry.status);
        icon->setKind(kind);
        if (kind == PlanStatusGlyph::Running) {
            icon->setAngle(m_spinAngle);
            m_spinGlyphs.append(icon);
        }

        auto *text = new QLabel(entry.text, row);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (kind == PlanStatusGlyph::Done)
            text->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));

        rl->addWidget(icon, 0, Qt::AlignTop);
        rl->addWidget(text, 1);
        m_listLayout->addWidget(row);
        if (i == reveal)
            active = row;
        ++i;
    }
    m_activeRow = active;
    if (!m_spinGlyphs.isEmpty())
        m_spinTimer->start();
    updateBadge();
    updateResumeButton();
    updateListHeight();
    QTimer::singleShot(0, this, [this]() {
        updateListHeight();
        scrollActiveIntoView();
    });
}

void AcpPlanWidget::setAgentIdle(bool idle)
{
    m_agentIdle = idle;
    updateResumeButton();
}

void AcpPlanWidget::updateBadge()
{
    if (m_entries.isEmpty()) {
        m_badge->clear();
        return;
    }
    int completed = 0;
    for (const auto &e : m_entries) {
        if (e.status == QLatin1String("completed"))
            ++completed;
    }
    m_badge->setText(QStringLiteral("%1/%2").arg(completed).arg(m_entries.size()));
}

void AcpPlanWidget::updateResumeButton()
{
    if (!m_agentIdle || m_entries.isEmpty()) {
        m_resumeBtn->setVisible(false);
        return;
    }
    bool hasIncomplete = false;
    for (const auto &e : m_entries) {
        if (e.status != QLatin1String("completed")) {
            hasIncomplete = true;
            break;
        }
    }
    m_resumeBtn->setVisible(hasIncomplete);
}

void AcpPlanWidget::tickSpin()
{
    m_spinAngle = (m_spinAngle + 24 * 16) % (360 * 16);
    for (QWidget *w : m_spinGlyphs)
        static_cast<PlanStatusGlyph *>(w)->setAngle(m_spinAngle);
}

void AcpPlanWidget::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    updateListHeight();
}

int AcpPlanWidget::indexToReveal(const QList<AcpProtocol::AcpPlanEntry> &next) const
{
    for (int i = 0; i < next.size(); ++i) {
        if (kindForStatus(next.at(i).status) == PlanStatusGlyph::Running)
            return i;
    }
    const int n = qMin(next.size(), m_entries.size());
    int changed = -1;
    for (int i = 0; i < n; ++i) {
        if (next.at(i).status != m_entries.at(i).status
            || next.at(i).text != m_entries.at(i).text) {
            changed = i;
        }
    }
    if (changed >= 0)
        return changed;
    if (next.size() > m_entries.size())
        return next.size() - 1;
    return -1;
}

int AcpPlanWidget::windowHeight(int startRow) const
{
    if (!m_listLayout || m_rowHeights.isEmpty())
        return kGlyphSize;
    const int spacing = m_listLayout->spacing();
    const int end = qMin(startRow + kVisibleRows, m_rowHeights.size());
    int h = 0;
    for (int i = qMax(0, startRow); i < end; ++i) {
        h += m_rowHeights.at(i);
        if (i + 1 < end)
            h += spacing;
    }
    return qMax(h, kGlyphSize);
}

void AcpPlanWidget::updateListHeight()
{
    if (!m_listViewport || !m_listLayout || !m_listHost || !m_vbar || m_inRelayout)
        return;
    const int n = m_listLayout->count();
    if (n <= 0) {
        m_rowHeights.clear();
        m_listHost->setFixedSize(1, 1);
        m_listHost->move(0, 0);
        m_listViewport->setFixedHeight(0);
        m_vbar->setVisible(false);
        m_vbar->setRange(0, 0);
        return;
    }

    const bool needBar = n > kVisibleRows;
    int vw = m_listViewport->width();
    if (vw < 32)
        vw = qMax(0, width() - m_layout->contentsMargins().left() - m_layout->contentsMargins().right());
    if (needBar)
        vw -= m_vbar->sizeHint().width() + 2;
    if (vw < 32)
        return;

    m_inRelayout = true;

    m_rowHeights.clear();
    m_rowHeights.reserve(n);
    int total = 0;
    const int spacing = m_listLayout->spacing();
    for (int i = 0; i < n; ++i) {
        QLayoutItem *it = m_listLayout->itemAt(i);
        QWidget *w = it ? it->widget() : nullptr;
        const int rh = w ? measureRowHeight(w, vw) : kGlyphSize;
        if (w)
            w->setFixedHeight(rh);
        m_rowHeights.append(rh);
        total += rh;
        if (i + 1 < n)
            total += spacing;
    }

    m_listHost->setFixedSize(vw, qMax(total, 1));

    const int maxStart = qMax(0, n - kVisibleRows);
    m_vbar->setVisible(needBar);
    m_vbar->setRange(0, maxStart);
    m_vbar->setPageStep(kVisibleRows);
    m_vbar->setSingleStep(1);
    if (!needBar)
        m_vbar->setValue(0);
    else if (m_vbar->value() > maxStart)
        m_vbar->setValue(maxStart);
    applyListScroll(m_vbar->value());
    m_inRelayout = false;
}

void AcpPlanWidget::applyListScroll(int startRow)
{
    if (!m_listHost || !m_listLayout)
        return;
    const int spacing = m_listLayout->spacing();
    int y = 0;
    const int n = qMin(startRow, m_rowHeights.size());
    for (int i = 0; i < n; ++i)
        y += m_rowHeights.at(i) + spacing;
    m_listHost->move(0, -y);
    if (m_listViewport)
        m_listViewport->setFixedHeight(windowHeight(startRow));
}

void AcpPlanWidget::scrollActiveIntoView()
{
    if (!m_activeRow || !m_listLayout || !m_vbar)
        return;
    int row = -1;
    const int n = m_listLayout->count();
    for (int i = 0; i < n; ++i) {
        QLayoutItem *it = m_listLayout->itemAt(i);
        if (it && it->widget() == m_activeRow) {
            row = i;
            break;
        }
    }
    if (row < 0)
        return;
    const int start = m_vbar->value();
    if (row < start)
        m_vbar->setValue(row);
    else if (row >= start + kVisibleRows)
        m_vbar->setValue(qMin(m_vbar->maximum(), row - kVisibleRows + 1));
}

void AcpPlanWidget::scrollByWheel(QWheelEvent *event)
{
    if (!event || !m_vbar || !m_vbar->isVisible())
        return;
    const int dy = event->angleDelta().y();
    if (dy == 0)
        return;
    const int notches = dy / 120;
    const int delta = notches != 0 ? notches : (dy > 0 ? 1 : -1);
    m_vbar->setValue(m_vbar->value() - delta);
    event->accept();
}

void AcpPlanWidget::wheelEvent(QWheelEvent *event)
{
    if (m_vbar && m_vbar->isVisible() && event && event->angleDelta().y() != 0) {
        scrollByWheel(event);
        return;
    }
    QFrame::wheelEvent(event);
}

bool AcpPlanWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event && event->type() == QEvent::Wheel
        && (watched == m_listViewport || watched == m_listHost
            || (m_listHost && watched && m_listHost->isAncestorOf(qobject_cast<QWidget *>(watched))))) {
        scrollByWheel(static_cast<QWheelEvent *>(event));
        return true;
    }
    return QFrame::eventFilter(watched, event);
}
