/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#include "AcpUsageIndicator.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QStyle>

namespace {
constexpr const char *kStyleSheet =
    "QProgressBar { border: 1px solid palette(mid); border-radius: 2px; background: palette(base); text-align: center; }"
    "QProgressBar::chunk { background-color: #4caf50; }"
    "QProgressBar[level=\"warning\"]::chunk { background-color: #f0ad4e; }"
    "QProgressBar[level=\"error\"]::chunk { background-color: #d9534f; }";
}

AcpUsageIndicator::AcpUsageIndicator(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_label = new QLabel(this);
    m_label->setAccessibleName(tr("Token usage"));
    m_label->setAccessibleDescription(tr("Input and output tokens used in this session"));
    m_label->hide();

    m_bar = new QProgressBar(this);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(6);
    m_bar->setMinimumWidth(60);
    m_bar->setMaximumWidth(120);
    m_bar->setAccessibleName(tr("Context window usage"));
    m_bar->setAccessibleDescription(tr("Fraction of the model's context window consumed"));
    m_bar->setStyleSheet(QString::fromLatin1(kStyleSheet));
    m_bar->hide();

    layout->addWidget(m_label);
    layout->addWidget(m_bar);
}

QString AcpUsageIndicator::formatThousands(int value)
{
    return QString::number(value / 1000.0, 'f', 1) + QStringLiteral("k");
}

void AcpUsageIndicator::setUsage(const std::optional<AcpProtocol::AcpUsage> &usage)
{
    if (!usage.has_value()) {
        m_label->hide();
        m_bar->hide();
        return;
    }

    const AcpProtocol::AcpUsage &u = *usage;

    if (u.inputTokens.has_value() || u.outputTokens.has_value()) {
        const int in = u.inputTokens.value_or(0);
        const int out = u.outputTokens.value_or(0);
        m_label->setText(tr("In: %1 / Out: %2")
                             .arg(formatThousands(in),
                                  formatThousands(out)));
        m_label->show();
    } else {
        m_label->hide();
    }

    if (u.maxTokens.has_value() && *u.maxTokens > 0) {
        const int in = u.inputTokens.value_or(0);
        const int maxTok = *u.maxTokens;
        m_bar->setRange(0, maxTok);
        m_bar->setValue(qMin(in, maxTok));
        const double frac = static_cast<double>(in) / static_cast<double>(maxTok);
        QString level;
        if (frac >= 1.0) {
            level = QStringLiteral("error");
        } else if (frac >= 0.8) {
            level = QStringLiteral("warning");
        }
        m_bar->setProperty("level", level);
        // Re-polish so the new property triggers a stylesheet recomputation.
        m_bar->style()->unpolish(m_bar);
        m_bar->style()->polish(m_bar);
        m_bar->show();
    } else {
        m_bar->hide();
    }
}
