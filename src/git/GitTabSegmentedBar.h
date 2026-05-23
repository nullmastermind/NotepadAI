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

#ifndef GIT_TAB_SEGMENTED_BAR_H
#define GIT_TAB_SEGMENTED_BAR_H

#include <QString>
#include <QStringList>
#include <QWidget>

class QButtonGroup;
class QToolButton;

// A flat horizontal "segmented control" of equal-width toggle buttons with
// an underline drawn under the active segment. Designed for the git dock's
// Changes/History switcher per ui-dna.md (no chrome color hex, all roles
// from QPalette, 1px borders only).
//
// API mirrors QTabBar: setSegments({"Changes", "History"}), currentChanged
// signal, setCurrentIndex.
class GitTabSegmentedBar : public QWidget
{
    Q_OBJECT
public:
    explicit GitTabSegmentedBar(QWidget *parent = nullptr);

    // Replace the segment list. Buttons are recreated; the first segment
    // becomes current.
    void setSegments(const QStringList &labels);

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);

signals:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    void layoutSegments();

    QStringList            m_labels;
    QList<QToolButton *>   m_buttons;
    QButtonGroup *         m_group = nullptr;
    int                    m_currentIndex = -1;
};

#endif // GIT_TAB_SEGMENTED_BAR_H
