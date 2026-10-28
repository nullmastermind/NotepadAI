/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef GRILL_ME_FORM_WIDGET_H
#define GRILL_ME_FORM_WIDGET_H

#include "GrillMeForm.h"

#include <QFrame>
#include <QVector>

#include <cstdint>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;

// An interactive question form in the chat transcript (a grill_me ask tool
// call). Pending until the user submits or dismisses; every
// other state is read-only and collapsed to its status line, expandable to
// show what was chosen. Clarify is a per-question toggle sent with Submit.
class GrillMeFormWidget : public QFrame
{
    Q_OBJECT

public:
    enum class State : std::uint8_t {
        Pending,
        Answered,
        AnsweredInChat,
        Dismissed,
        ClarifyRequested, // submitted with questions flagged for clarification
        Superseded,
        Cancelled,
        Expired,
        Invalid,
    };

    GrillMeFormWidget(GrillMeForm::Spec spec, const QVector<GrillMeForm::Answer> &initial,
                      QWidget *parent = nullptr);

    const GrillMeForm::Spec &spec() const { return m_spec; }
    State state() const { return m_state; }
    bool isPending() const { return m_state == State::Pending; }
    // `detail` overrides the default status text (e.g. the cancel reason).
    void setState(State state, const QString &detail = {});
    QVector<GrillMeForm::Answer> answers() const;
    // The form-level note below the questions (trimmed).
    QString note() const;
    void setNote(const QString &note);

    void setChatFont(const QFont &font);
    // Fills `answers` in as if picked (clarify flags too) and submits like the
    // Submit button. Returns the ids of required questions still unanswered;
    // nothing is sent then.
    QStringList fillAndSubmit(const QVector<GrillMeForm::Answer> &answers, const QString &note);
    // As the Dismiss button (with `note` sent along when not empty).
    void dismiss(const QString &note);
    void focusFirstQuestion();

signals:
    void submitted(const QVector<GrillMeForm::Answer> &answers);
    void dismissed();
    // Esc inside the form: give focus back to the composer.
    void escapePressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct Row {
        QWidget *container = nullptr;
        QLabel *requiredLabel = nullptr;
        QButtonGroup *group = nullptr;          // single / confirm / proposal
        QVector<QAbstractButton *> optionButtons; // index == option index (confirm: yes, no)
        QComboBox *combo = nullptr;              // dropdown
        QAbstractButton *otherButton = nullptr;  // radio / checkbox for Other
        QLineEdit *otherEdit = nullptr;
        QLineEdit *lineEdit = nullptr;           // text / number
        QPlainTextEdit *textEdit = nullptr;      // textarea / proposal change
        QLineEdit *noteEdit = nullptr;
        QToolButton *noteButton = nullptr;
        QToolButton *clarifyButton = nullptr;
        QWidget *answerBox = nullptr;            // the controls, dimmed while flagged
    };

    void buildUi();
    QWidget *buildQuestion(int index);
    void buildChoice(int index, Row &row, QVBoxLayout *layout);
    void buildDropdown(int index, Row &row, QVBoxLayout *layout);
    void buildSegmented(int index, Row &row, QVBoxLayout *layout);
    void buildOther(Row &row, QVBoxLayout *layout, bool checkbox);
    void applyAnswer(int index, const GrillMeForm::Answer &answer);
    GrillMeForm::Answer answerAt(int index) const;
    // The control that answers question `index` (focus target).
    QWidget *inputOf(int index) const;
    void trySubmit();
    void watch(QWidget *w);
    void updateProposalEditor(int index);
    void setClarify(int index, bool on);
    // Grows the note box with its text, one line up to kNoteMaxLines.
    void fitNote();
    void updateProgress();
    void setExpanded(bool expanded);
    void applyTheme();
    QString statusText(const QString &detail) const;
    QLabel *mutedLabel(const QString &text, Qt::TextFormat format, QWidget *parent) const;

    GrillMeForm::Spec m_spec;
    QVector<Row> m_rows;
    State m_state = State::Pending;
    bool m_building = true;
    QString m_themeKey; // palette colours the current stylesheet was built from

    QLabel *m_badge = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QToolButton *m_toggle = nullptr;
    QWidget *m_body = nullptr;
    QPlainTextEdit *m_formNoteEdit = nullptr;
    QWidget *m_footer = nullptr;
    QLabel *m_progressLabel = nullptr;
    QPushButton *m_submitButton = nullptr;
    QPushButton *m_dismissButton = nullptr;
};

#endif // GRILL_ME_FORM_WIDGET_H
