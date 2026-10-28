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

#include "AcpPermissionPrompt.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

AcpPermissionPrompt::AcpPermissionPrompt(const AcpProtocol::AcpPermissionRequest &request,
                                         QWidget *parent)
    : QFrame(parent)
    , m_requestId(request.requestId)
{
    // Control API: chat:<session>/permissionPrompt/<option kind>.
    setObjectName(QStringLiteral("permissionPrompt"));
    setFrameShape(QFrame::StyledPanel);
    setStyleSheet(QStringLiteral(
        "AcpPermissionPrompt { background: palette(alternate-base); "
        "border: 1px solid palette(highlight); border-radius: 4px; }"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 6, 8, 6);
    outer->setSpacing(4);

    auto *title = new QLabel(request.title.isEmpty()
                                 ? tr("Agent requests permission")
                                 : request.title,
                             this);
    QFont tf = title->font();
    tf.setBold(true);
    title->setFont(tf);
    title->setWordWrap(true);
    outer->addWidget(title);

    if (!request.description.isEmpty()) {
        auto *desc = new QLabel(request.description, this);
        desc->setWordWrap(true);
        desc->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
        outer->addWidget(desc);
    }

    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(6);
    btnRow->addStretch();

    QSet<QString> names;
    for (const auto &opt : request.options) {
        auto *btn = new QPushButton(opt.label.isEmpty() ? opt.id : opt.label, this);
        // The ACP kind (allow_once, allow_always, reject_once...) reads the same
        // for every agent; the option id when two options share a kind.
        const QString name = !opt.kind.isEmpty() && !names.contains(opt.kind) ? opt.kind : opt.id;
        names.insert(name);
        btn->setObjectName(name);
        if (opt.kind == QLatin1String("allow_always")) {
            btn->setStyleSheet(QStringLiteral(
                "QPushButton { background: palette(highlight); "
                "color: palette(highlighted-text); }"));
        }
        const QString optionId = opt.id;
        const QString outcome = (opt.kind == QLatin1String("deny"))
                                    ? QStringLiteral("cancelled")
                                    : QStringLiteral("selected");
        connect(btn, &QPushButton::clicked, this, [this, optionId, outcome]() {
            emit choiceMade(m_requestId, outcome, optionId);
        });
        btnRow->addWidget(btn);
    }

    outer->addLayout(btnRow);
}
