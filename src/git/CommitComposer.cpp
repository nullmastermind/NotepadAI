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

#include "CommitComposer.h"

#include <QCheckBox>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

CommitMessageEdit::CommitMessageEdit(QWidget *parent) : QPlainTextEdit(parent)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setTabChangesFocus(true);
}

void CommitMessageEdit::paintEvent(QPaintEvent *event)
{
    QPainter p(viewport());
    const QFontMetricsF fm(font());
    const qreal charW = fm.horizontalAdvance(QLatin1Char('m'));
    const QColor col50 = palette().color(QPalette::Mid);
    const QColor col72 = palette().color(QPalette::Mid).darker(140);

    auto drawRuler = [&](int col, const QColor &c) {
        const qreal x = qRound(charW * col) + 4;
        p.setPen(QPen(c, 1, Qt::DotLine));
        p.drawLine(QPointF(x, 0), QPointF(x, viewport()->height()));
    };
    drawRuler(50, col50);
    drawRuler(72, col72);
    p.end();
    QPlainTextEdit::paintEvent(event);
}

void CommitMessageEdit::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & Qt::ControlModifier))
    {
        emit submitRequested();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_generationActive) {
        emit cancelRequested();
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

CommitComposer::CommitComposer(QWidget *parent) : QWidget(parent)
{
    m_edit = new CommitMessageEdit(this);
    m_edit->setPlaceholderText(tr("Commit message…"));
    m_edit->setAccessibleName(tr("Commit message"));

    m_charCount = new QLabel(QStringLiteral("0 / 50"), this);
    m_charCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_charCount->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); font-size: 11px; }"));

    m_amend = new QCheckBox(tr("Amend"), this);
    m_amend->setToolTip(tr("Amend the last commit"));
    m_signoff = new QCheckBox(tr("Sign off"), this);
    m_signoff->setToolTip(tr("Add Signed-off-by trailer"));
    m_tracked = new QCheckBox(tr("Commit Tracked"), this);
    m_tracked->setToolTip(tr("Commit only modifications to tracked files (skip untracked, equivalent to git commit -a)"));

    m_commitBtn = new QPushButton(tr("Commit (Ctrl+Enter)"), this);
    m_commitBtn->setAccessibleName(tr("Commit"));
    m_commitBtn->setDefault(false);
    m_commitBtn->setAutoDefault(false);

    m_aiBtn = new QToolButton(this);
    m_aiBtn->setText(QString::fromUtf8("\xE2\x9C\xA8"));   // U+2728 ✨ sparkles
    m_aiBtn->setAccessibleName(tr("Generate commit message with AI"));
    m_aiBtn->setToolTip(tr("Generate commit message with AI"));
    m_aiBtn->setAutoRaise(true);
    m_aiBtn->setFocusPolicy(Qt::TabFocus);

    auto *checks = new QHBoxLayout();
    checks->setContentsMargins(0, 0, 0, 0);
    checks->addWidget(m_amend);
    checks->addWidget(m_signoff);
    checks->addWidget(m_tracked);
    checks->addStretch(1);
    checks->addWidget(m_charCount);

    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 0, 0, 0);
    footer->addStretch(1);
    footer->addWidget(m_aiBtn);
    footer->addWidget(m_commitBtn);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    lay->addWidget(m_edit, 1);
    lay->addLayout(checks);
    lay->addLayout(footer);

    connect(m_edit, &QPlainTextEdit::textChanged, this, [this]() {
        updateCharCount();
        emit messageChanged();
    });
    connect(m_edit, &CommitMessageEdit::submitRequested,
            m_commitBtn, &QPushButton::click);
    connect(m_edit, &CommitMessageEdit::cancelRequested,
            this, &CommitComposer::aiCancelRequested);
    connect(m_commitBtn, &QPushButton::clicked, this, &CommitComposer::submitRequested);
    connect(m_aiBtn, &QToolButton::clicked, this, &CommitComposer::aiTriggerRequested);
    connect(m_amend, &QCheckBox::toggled, this, &CommitComposer::amendToggled);
    connect(m_signoff, &QCheckBox::toggled, this, &CommitComposer::signoffToggled);
    connect(m_tracked, &QCheckBox::toggled, this, &CommitComposer::trackedOnlyToggled);

    updateCharCount();
}

void CommitComposer::updateCharCount()
{
    const QString txt = m_edit->toPlainText();
    const int firstNl = txt.indexOf(QLatin1Char('\n'));
    const int subjectLen = firstNl < 0 ? txt.length() : firstNl;
    QString style = QStringLiteral("color: palette(mid); font-size: 11px;");
    if (subjectLen > 72)      style = QStringLiteral("color: #c0392b; font-size: 11px;");
    else if (subjectLen > 50) style = QStringLiteral("color: #d39e00; font-size: 11px;");
    m_charCount->setStyleSheet(QStringLiteral("QLabel { %1 }").arg(style));
    m_charCount->setText(QStringLiteral("%1 / 50").arg(subjectLen));
}

QString CommitComposer::message() const           { return m_edit->toPlainText(); }

QString CommitComposer::subjectLine() const
{
    const QString txt = m_edit->toPlainText();
    const int nl = txt.indexOf(QChar::LineFeed);
    const QString first = (nl < 0) ? txt : txt.left(nl);
    return first.trimmed();
}

void CommitComposer::setMessage(const QString &s) { m_edit->setPlainText(s); }
bool CommitComposer::amendChecked() const         { return m_amend->isChecked(); }
bool CommitComposer::signoffChecked() const       { return m_signoff->isChecked(); }
bool CommitComposer::trackedOnly() const          { return m_tracked->isChecked(); }
void CommitComposer::setAmendChecked(bool v)      { m_amend->setChecked(v); }
void CommitComposer::setSignoffChecked(bool v)    { m_signoff->setChecked(v); }
void CommitComposer::setTrackedOnly(bool v)       { m_tracked->setChecked(v); }
void CommitComposer::setSubmitEnabled(bool v)     { m_commitBtn->setEnabled(v); }
void CommitComposer::setPlaceholderText(const QString &t) { m_edit->setPlaceholderText(t); }
void CommitComposer::clear() { m_edit->clear(); }

void CommitComposer::setGenerationActive(bool active)
{
    m_edit->setGenerationActive(active);
}
