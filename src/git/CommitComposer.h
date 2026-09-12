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

#ifndef COMMIT_COMPOSER_H
#define COMMIT_COMPOSER_H

#include <QPlainTextEdit>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QToolButton;

class CommitMessageEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CommitMessageEdit(QWidget *parent = nullptr);

    // Allow the composer (or another sibling widget) to advertise whether a
    // generation is currently in flight. When true, the Esc key is intercepted
    // and emits cancelRequested() instead of being passed to the base class —
    // matching the trigger control's cancel affordance.
    void setGenerationActive(bool active) { m_generationActive = active; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
signals:
    void submitRequested();
    void cancelRequested();

private:
    bool m_generationActive = false;
};

class CommitComposer : public QWidget
{
    Q_OBJECT
public:
    explicit CommitComposer(QWidget *parent = nullptr);

    QString message() const;

    // First line of the message after trim(); empty when blank or whitespace-
    // only. Used as the AI subject hint.
    QString subjectLine() const;

    void setMessage(const QString &);
    bool amendChecked() const;
    bool signoffChecked() const;
    bool trackedOnly() const;
    void setAmendChecked(bool);
    void setSignoffChecked(bool);
    void setTrackedOnly(bool);
    void setSubmitEnabled(bool);
    void setPlaceholderText(const QString &);
    void clear();

    // Exposed for the AI generator so it can append streamed tokens directly
    // to the document end without disturbing the user's cursor / selection.
    QPlainTextEdit *edit() const { return m_edit; }

    // The "Generate commit message with AI" trigger control (toolbutton in the
    // footer, next to the Commit button). nullptr until built.
    QToolButton *aiButton() const { return m_aiBtn; }

    // Commit button — used to anchor the empty-message ACP agent picker menu.
    QPushButton *commitButton() const { return m_commitBtn; }

    // Forward generation-active state into the inner QPlainTextEdit so Esc
    // gets intercepted only while a stream is in flight.
    void setGenerationActive(bool active);

signals:
    void messageChanged();
    void submitRequested();
    void aiTriggerRequested();    // user clicked the AI button or hit shortcut
    void aiCancelRequested();     // user pressed Esc while streaming
    void amendToggled(bool);
    void signoffToggled(bool);
    void trackedOnlyToggled(bool);

private slots:
    void updateCharCount();

private:
    CommitMessageEdit *m_edit;
    QLabel *m_charCount;
    QCheckBox *m_amend;
    QCheckBox *m_signoff;
    QCheckBox *m_tracked;
    QPushButton *m_commitBtn;
    QToolButton *m_aiBtn;
};

#endif // COMMIT_COMPOSER_H
