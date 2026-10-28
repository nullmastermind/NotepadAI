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

#include "TerminalEmptyPanel.h"

#include "TerminalIcons.h"

#include <QFont>
#include <QFontMetrics>
#include <QPalette>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QString argb(const QColor &c)
{
    return c.name(QColor::HexArgb);
}

QColor withAlpha(QColor c, int alpha)
{
    c.setAlpha(alpha);
    return c;
}

QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("nnMuted", true);
    return label;
}

} // namespace

TerminalEmptyPanel::TerminalEmptyPanel(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_content = new QWidget(m_scroll);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(8, 8, 8, 8);
    m_scroll->setWidget(m_content);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_scroll);
    applyStyleSheet();
}

void TerminalEmptyPanel::applyStyleSheet()
{
    // Transparent surfaces always; colours only once the scheme is known.
    QString sheet = QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"
        "QLabel { background: transparent; }"
        "QPushButton[nnTaskRow=\"true\"] { background: transparent;"
        " border: 1px solid transparent; border-radius: 6px; text-align: left; }"
        "QToolButton#nnConfigureTasks { background: transparent;"
        " border: 1px solid transparent; border-radius: 4px; padding: 2px 6px; }");
    if (m_foreground.isValid() && m_selection.isValid()) {
        const QString border = argb(withAlpha(m_foreground, 60));
        sheet += QStringLiteral(
                     "QLabel[nnMuted=\"true\"] { color: %1; }"
                     "QPushButton[nnTaskRow=\"true\"]:hover { background: %2; border-color: %3; }"
                     "QPushButton[nnTaskRow=\"true\"]:focus { border-color: %3; }"
                     "QPushButton#nnNewTerminal { background: %4; border: 1px solid %5;"
                     " border-radius: 6px; padding: 6px 14px; }"
                     "QPushButton#nnNewTerminal:hover { background: %6; }"
                     "QToolButton#nnConfigureTasks { color: %7; }"
                     "QToolButton#nnConfigureTasks:hover { border-color: %3; }")
                     .arg(argb(withAlpha(m_foreground, 150)), argb(withAlpha(m_selection, 70)),
                          border, argb(withAlpha(m_selection, 90)),
                          argb(withAlpha(m_foreground, 70)), argb(withAlpha(m_selection, 140)),
                          argb(withAlpha(m_foreground, 170)));
    }
    setStyleSheet(sheet);
}

void TerminalEmptyPanel::setSchemeColors(const QColor &background, const QColor &foreground,
                                         const QColor &selection)
{
    if (m_background == background && m_foreground == foreground && m_selection == selection)
        return;
    m_background = background;
    m_foreground = foreground;
    m_selection = selection;

    // The panel is the terminal area before a terminal exists, so it wears
    // the terminal scheme (ui-dna: no hard-coded hex; these come from the
    // live TerminalColorScheme).
    if (m_background.isValid()) {
        QPalette pal = palette();
        pal.setColor(QPalette::Window, m_background);
        pal.setColor(QPalette::WindowText, m_foreground);
        pal.setColor(QPalette::ButtonText, m_foreground);
        pal.setColor(QPalette::Text, m_foreground);
        pal.setColor(QPalette::Base, m_background);
        setPalette(pal);
        m_content->setPalette(pal);
        m_scroll->viewport()->setPalette(pal);
    }
    applyStyleSheet();
    // Icons are tinted pixmaps, so they need the rebuild.
    rebuild();
}

void TerminalEmptyPanel::setContent(const QString &workspaceDisplayPath, bool remote,
                                    const QList<TerminalTask> &configuredTasks)
{
    m_workspaceDisplayPath = workspaceDisplayPath;
    m_remote = remote;
    m_configuredTasks = configuredTasks;
    rebuild();
}

void TerminalEmptyPanel::clearContent()
{
    // Recursively empty the layout. A layout item IS its layout
    // (QLayout : QLayoutItem), so each item is deleted exactly once, after
    // its children have been taken out.
    const auto clearLayout = [](QLayout *layout, auto &&self) -> void {
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (item->widget()) {
                item->widget()->hide();
                item->widget()->deleteLater();
            }
            if (item->layout())
                self(item->layout(), self);
            delete item;
        }
    };
    clearLayout(m_contentLayout, clearLayout);
}

QWidget *TerminalEmptyPanel::makeTaskRow(const QString &name, const QString &command)
{
    auto *row = new QPushButton(m_content);
    row->setCursor(Qt::PointingHandCursor);
    row->setFocusPolicy(Qt::StrongFocus);
    row->setProperty("nnTaskRow", true);

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    auto *play = new QLabel(row);
    play->setAttribute(Qt::WA_TransparentForMouseEvents);
    play->setPixmap(terminalTintedIcon(QStringLiteral(":/icons/play.svg"),
                                       withAlpha(m_foreground, 170))
                        .pixmap(14, 14));
    layout->addWidget(play);

    auto *texts = new QVBoxLayout;
    texts->setSpacing(0);
    auto *nameLabel = new QLabel(name, row);
    nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    texts->addWidget(nameLabel);
    if (!command.isEmpty() && command != name) {
        auto *cmdLabel = mutedLabel(command, row);
        cmdLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        QFont small = cmdLabel->font();
        if (small.pointSizeF() > 1)
            small.setPointSizeF(small.pointSizeF() - 1);
        else if (small.pixelSize() > 2)
            small.setPixelSize(small.pixelSize() - 2);
        cmdLabel->setFont(small);
        texts->addWidget(cmdLabel);
    }
    layout->addLayout(texts);
    layout->addStretch(1);

    // A QPushButton's size hint comes from its (empty) text, not from a
    // layout installed on it — without this the row is one line tall and
    // the name/command labels paint over each other.
    layout->activate();
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    row->setMinimumHeight(layout->minimumSize().height());
    return row;
}

void TerminalEmptyPanel::rebuild()
{
    if (!m_contentLayout)
        return;

    clearContent();
    m_scroll->verticalScrollBar()->setValue(0);

    // Centered column: side stretches keep it in the middle of a wide dock,
    // top/bottom stretches centre it while the task list is short (and
    // collapse to nothing — the scroll area takes over — when it is long).
    auto *centerRow = new QHBoxLayout;
    centerRow->setSpacing(0);
    auto *column = new QWidget(m_content);
    column->setMaximumWidth(640);
    auto *col = new QVBoxLayout(column);
    col->setContentsMargins(8, 4, 8, 4);
    col->setSpacing(6);
    centerRow->addStretch(1);
    centerRow->addWidget(column);
    centerRow->addStretch(1);

    auto *icon = new QLabel(column);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(terminalTintedIcon(QStringLiteral(":/icons/square-terminal.svg"),
                                       withAlpha(m_foreground, 130))
                        .pixmap(40, 40));
    col->addWidget(icon);

    auto *title = new QLabel(tr("No terminal open"), column);
    title->setAlignment(Qt::AlignCenter);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2);
    title->setFont(titleFont);
    col->addWidget(title);

    if (!m_workspaceDisplayPath.isEmpty()) {
        const QString elided = QFontMetrics(font()).elidedText(
            m_workspaceDisplayPath, Qt::ElideMiddle, 600);
        auto *path = mutedLabel(elided, column);
        path->setAlignment(Qt::AlignCenter);
        path->setToolTip(m_workspaceDisplayPath);
        col->addWidget(path);
    }

    auto *newRow = new QHBoxLayout;
    auto *newButton = new QPushButton(
        terminalTintedIcon(QStringLiteral(":/icons/square-terminal.svg"), m_foreground),
        tr("New terminal"), column);
    newButton->setObjectName(QStringLiteral("nnNewTerminal"));
    newButton->setMinimumWidth(180);
    newButton->setCursor(Qt::PointingHandCursor);
    connect(newButton, &QPushButton::clicked, this, &TerminalEmptyPanel::newTerminalRequested);
    newRow->addStretch(1);
    newRow->addWidget(newButton);
    newRow->addStretch(1);
    col->addLayout(newRow);

    if (!m_remote) {
        col->addSpacing(4);
        auto *header = new QHBoxLayout;
        header->setSpacing(4);
        auto *label = new QLabel(tr("Configured tasks"), column);
        QFont f = label->font();
        f.setBold(true);
        label->setFont(f);
        header->addWidget(label);
        header->addStretch(1);
        auto *edit = new QToolButton(column);
        edit->setText(tr("Configure tasks…"));
        edit->setAutoRaise(true);
        edit->setObjectName(QStringLiteral("nnConfigureTasks"));
        edit->setCursor(Qt::PointingHandCursor);
        connect(edit, &QToolButton::clicked, this, &TerminalEmptyPanel::editTasksRequested);
        header->addWidget(edit);
        col->addLayout(header);

        if (m_configuredTasks.isEmpty()) {
            auto *none = mutedLabel(tr("No tasks configured for this workspace."), column);
            col->addWidget(none);
        } else {
            for (const TerminalTask &task : m_configuredTasks) {
                QWidget *row = makeTaskRow(task.name, task.command);
                connect(static_cast<QPushButton *>(row), &QPushButton::clicked, this,
                        [this, task] { emit configuredTaskRequested(task); });
                col->addWidget(row);
            }
        }
    }

    m_contentLayout->addStretch(1);
    m_contentLayout->addLayout(centerRow);
    m_contentLayout->addStretch(1);
}
