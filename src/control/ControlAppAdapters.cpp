/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// Adapters for NotepadAI's own widgets (docs/adr/0006). Each one reads what
// the user sees and acts through what the user does: the tab's own close
// request, keys and input-method commits, a paste-like insert, the composer's
// Send button.

#include "ControlAppAdapters.h"

#include "ControlAdapter.h"
#include "ControlUiTree.h"

#include "AcpConversationCopy.h"
#include "AcpSessionModel.h"
#include "ScintillaNext.h"
#include "widgets/AcpSessionView.h"
#include "widgets/TerminalWidget.h"

#include "DockWidget.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QPlainTextEdit>

namespace control {

namespace {

constexpr qsizetype kMaxTextChars = 20000; // one message / selection in a read

QString capped(const QString &s, bool *cut = nullptr)
{
    if (cut)
        *cut = s.size() > kMaxTextChars;
    return s.size() > kMaxTextChars ? s.left(kMaxTextChars) : s;
}

// --- docked tabs -------------------------------------------------------------

class DockAdapter : public ControlAdapter
{
public:
    QString name() const override { return QStringLiteral("dock"); }
    bool matches(const QWidget *w) const override { return qobject_cast<const ads::CDockWidget *>(w) != nullptr; }
    QString role(const QWidget *) const override { return QStringLiteral("dock"); }
    bool walksChildren(const QWidget *) const override { return true; }

    bool reachableWhileHidden(const QWidget *w, const QString &op) const override
    {
        // A tab behind another one is hidden, yet its tab can be clicked or closed.
        const auto *dock = static_cast<const ads::CDockWidget *>(w);
        return !dock->isClosed()
               && (op == QLatin1String("select") || op == QLatin1String("close") || op == QLatin1String("show"));
    }

    QJsonObject summary(QWidget *w) const override
    {
        auto *dock = static_cast<ads::CDockWidget *>(w);
        QJsonObject o{{QStringLiteral("title"), dock->windowTitle()},
                      {QStringLiteral("kind"), kindOf(dock)},
                      {QStringLiteral("open"), !dock->isClosed()}};
        if (!dock->isClosed())
            o.insert(QStringLiteral("current"), dock->isCurrentTab());
        if (dock->isFloating())
            o.insert(QStringLiteral("floating"), true);
        return o;
    }

    std::optional<QString> perform(QWidget *w, const QString &part, const QString &op,
                                   const QJsonObject &) const override
    {
        if (!part.isEmpty())
            return std::nullopt;
        auto *dock = static_cast<ads::CDockWidget *>(w);
        if (op == QLatin1String("select") || op == QLatin1String("show")) {
            // A click on its tab (or the View menu entry, for a closed panel).
            if (dock->isClosed())
                dock->toggleView(true);
            dock->setAsCurrentTab();
            dock->raise();
            if (QWidget *content = dock->widget(); content && content->focusPolicy() != Qt::NoFocus)
                content->setFocus(Qt::MouseFocusReason);
            return QString();
        }
        if (op == QLatin1String("close")) {
            if (dock->isClosed())
                return QStringLiteral("it is already closed");
            if (!dock->features().testFlag(ads::CDockWidget::DockWidgetClosable))
                return QStringLiteral("this tab cannot be closed");
            // The tab's close button: unsaved editors still ask.
            dock->requestCloseDockWidget();
            return QString();
        }
        if (op == QLatin1String("float")) {
            if (!dock->features().testFlag(ads::CDockWidget::DockWidgetFloatable))
                return QStringLiteral("this tab cannot float");
            dock->setFloating();
            return QString();
        }
        return std::nullopt;
    }

private:
    static QString kindOf(const ads::CDockWidget *dock)
    {
        if (dock->property("nn_toolTab").toBool())
            return QStringLiteral("tool");
        if (dock->property("nn_previewTab").toBool())
            return QStringLiteral("preview");
        if (qobject_cast<const ScintillaNext *>(dock->widget()))
            return QStringLiteral("editor");
        return QStringLiteral("panel");
    }
};

// --- editors -----------------------------------------------------------------

class EditorAdapter : public ControlAdapter
{
public:
    QString name() const override { return QStringLiteral("editor"); }
    bool matches(const QWidget *w) const override { return qobject_cast<const ScintillaNext *>(w) != nullptr; }
    QString role(const QWidget *) const override { return QStringLiteral("editor"); }

    QJsonObject summary(QWidget *w) const override
    {
        auto *e = static_cast<ScintillaNext *>(w);
        const sptr_t caret = e->currentPos();
        QJsonObject o{{QStringLiteral("name"), e->getName()},
                      {QStringLiteral("modified"), e->modify()},
                      {QStringLiteral("lines"), qint64(e->lineCount())},
                      {QStringLiteral("line"), qint64(e->lineFromPosition(caret) + 1)},
                      {QStringLiteral("column"), qint64(e->column(caret) + 1)},
                      {QStringLiteral("language"), e->languageName}};
        const QString path = pathOf(e);
        if (!path.isEmpty())
            o.insert(QStringLiteral("path"), path);
        if (e->selectionEnd() > e->selectionStart())
            o.insert(QStringLiteral("selectedChars"), qint64(e->selectionEnd() - e->selectionStart()));
        if (e->readOnly())
            o.insert(QStringLiteral("readOnly"), true);
        return o;
    }

    // Lines are numbered from 1, as in the status bar. `from` (0 = 1) and
    // `count` (default 500) pick the window.
    bool read(QWidget *w, const QString &part, const ReadOptions &opts, QJsonObject &out) const override
    {
        auto *e = static_cast<ScintillaNext *>(w);
        if (part == QLatin1String("selection")) {
            bool cut = false;
            out.insert(QStringLiteral("text"),
                       capped(QString::fromUtf8(e->textRange(int(e->selectionStart()), int(e->selectionEnd()))), &cut));
            if (cut)
                out.insert(QStringLiteral("truncated"), true);
            return true;
        }
        if (!part.isEmpty())
            return false;
        out = summary(w);
        const sptr_t lines = e->lineCount();
        const sptr_t first = qBound<sptr_t>(1, opts.from < 1 ? 1 : sptr_t(opts.from), qMax<sptr_t>(1, lines));
        const sptr_t count = opts.count < 0 ? 500 : qMin<sptr_t>(sptr_t(opts.count), 20000);
        const sptr_t last = qMin<sptr_t>(lines, first + count - 1);
        const sptr_t start = e->positionFromLine(first - 1);
        const sptr_t end = last >= first ? e->lineEndPosition(last - 1) : start;
        out.insert(QStringLiteral("firstLine"), qint64(first));
        out.insert(QStringLiteral("lastLine"), qint64(qMax(first, last)));
        out.insert(QStringLiteral("text"), QString::fromUtf8(e->textRange(int(start), int(end))));
        if (e->selectionEnd() > e->selectionStart()) {
            const sptr_t a = e->selectionStart();
            const sptr_t b = e->selectionEnd();
            out.insert(QStringLiteral("selection"),
                       QJsonObject{{QStringLiteral("fromLine"), qint64(e->lineFromPosition(a) + 1)},
                                   {QStringLiteral("fromColumn"), qint64(e->column(a) + 1)},
                                   {QStringLiteral("toLine"), qint64(e->lineFromPosition(b) + 1)},
                                   {QStringLiteral("toColumn"), qint64(e->column(b) + 1)}});
        }
        return true;
    }

    std::optional<QString> perform(QWidget *w, const QString &, const QString &op, const QJsonObject &args) const override
    {
        auto *e = static_cast<ScintillaNext *>(w);
        if (op == QLatin1String("type") || op == QLatin1String("set")) {
            const QString text = args.value(op == QLatin1String("set") ? QStringLiteral("value") : QStringLiteral("text"))
                                     .toVariant()
                                     .toString();
            const bool replace = op == QLatin1String("set") || args.value(QStringLiteral("replace")).toBool();
            // One line goes in key by key (input method commit): autocomplete
            // and brace pairing react as they do for a user.
            if (op == QLatin1String("type") && !text.contains(QLatin1Char('\n')))
                return std::nullopt;
            if (e->readOnly())
                return QStringLiteral("the editor is read-only");
            insertLikePaste(e, text, replace);
            return QString();
        }
        if (op == QLatin1String("goto")) {
            // Like Go to Line (Ctrl+G).
            const sptr_t line = args.value(QStringLiteral("line")).toInteger(0);
            if (line < 1 || line > e->lineCount())
                return QStringLiteral("line must be 1..%1").arg(e->lineCount());
            const sptr_t column = args.value(QStringLiteral("column")).toInteger(1);
            e->gotoPos(e->findColumn(line - 1, qMax<sptr_t>(0, column - 1)));
            e->verticalCentreCaret();
            e->setFocus(Qt::OtherFocusReason);
            return QString();
        }
        if (op == QLatin1String("select")) {
            if (args.value(QStringLiteral("all")).toBool()) {
                e->selectAll();
                return QString();
            }
            const sptr_t fromLine = args.value(QStringLiteral("line")).toInteger(0);
            if (fromLine < 1 || fromLine > e->lineCount())
                return QStringLiteral("give \"line\" (1..%1), optional \"column\", \"toLine\", \"toColumn\"; or \"all\": true")
                    .arg(e->lineCount());
            const sptr_t toLine = qBound<sptr_t>(fromLine, args.value(QStringLiteral("toLine")).toInteger(fromLine),
                                                 e->lineCount());
            const sptr_t anchor = e->findColumn(fromLine - 1, args.value(QStringLiteral("column")).toInteger(1) - 1);
            const sptr_t caret = args.contains(QStringLiteral("toColumn"))
                                     ? e->findColumn(toLine - 1, args.value(QStringLiteral("toColumn")).toInteger(1) - 1)
                                     : e->lineEndPosition(toLine - 1);
            e->setSel(anchor, caret);
            e->scrollCaret();
            e->setFocus(Qt::OtherFocusReason);
            return QString();
        }
        return std::nullopt;
    }

private:
    static QString pathOf(const ScintillaNext *e)
    {
        if (e->isRemote())
            return e->remoteUri();
        // Forward slashes, like every other path the API returns.
        return e->isFile() ? QDir::fromNativeSeparators(e->getFilePath()) : QString();
    }

    // What a paste does (line endings follow the document), one undo step,
    // without touching the user's clipboard.
    static void insertLikePaste(ScintillaNext *e, QString text, bool replace)
    {
        text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        const QString eol = QString::fromLatin1(e->eolString());
        if (eol != QLatin1String("\n"))
            text.replace(QLatin1Char('\n'), eol);
        e->setFocus(Qt::OtherFocusReason);
        e->beginUndoAction();
        if (replace)
            e->selectAll();
        e->replaceSel(text.toUtf8().constData());
        e->endUndoAction();
        e->scrollCaret();
    }
};

// --- terminals ---------------------------------------------------------------

class TerminalAdapter : public ControlAdapter
{
public:
    QString name() const override { return QStringLiteral("terminal"); }
    bool matches(const QWidget *w) const override { return qobject_cast<const TerminalWidget *>(w) != nullptr; }
    QString role(const QWidget *) const override { return QStringLiteral("terminal"); }

    QJsonObject summary(QWidget *w) const override
    {
        auto *t = static_cast<TerminalWidget *>(w);
        QJsonObject o{{QStringLiteral("title"), t->title()}, {QStringLiteral("running"), t->isProcessRunning()}};
        if (!t->reportedCwd().isEmpty())
            o.insert(QStringLiteral("cwd"), t->reportedCwd());
        if (t->hasExited())
            o.insert(QStringLiteral("exited"), true);
        return o;
    }

    // Rows: the scrollback, then the live screen. No `count`: the live
    // screen. `from` < 0 counts back from the last row.
    bool read(QWidget *w, const QString &part, const ReadOptions &opts, QJsonObject &out) const override
    {
        auto *t = static_cast<TerminalWidget *>(w);
        if (part == QLatin1String("selection")) {
            out.insert(QStringLiteral("text"), capped(t->selectedText()));
            return true;
        }
        if (!part.isEmpty())
            return false;
        int total = 0;
        int history = 0;
        t->textRows(0, 0, &total, &history);
        qint64 from = history;
        qint64 count = total - history;
        if (opts.count >= 0) {
            count = qMin<qint64>(opts.count, 10000);
            from = opts.from < 0 ? qMax<qint64>(0, total + opts.from) : opts.from;
        }
        out = summary(w);
        const QStringList rows = t->textRows(int(qMin<qint64>(from, total)), int(count));
        out.insert(QStringLiteral("rows"), QJsonArray::fromStringList(rows));
        out.insert(QStringLiteral("from"), qMin<qint64>(from, total));
        out.insert(QStringLiteral("totalRows"), total);
        out.insert(QStringLiteral("screenStart"), history);
        const QPoint cursor = t->cursorCell();
        out.insert(QStringLiteral("cursor"),
                   QJsonObject{{QStringLiteral("row"), history + cursor.y()}, {QStringLiteral("column"), cursor.x()}});
        return true;
    }

    std::optional<QString> perform(QWidget *w, const QString &, const QString &op, const QJsonObject &args) const override
    {
        if (op != QLatin1String("type"))
            return std::nullopt;
        auto *t = static_cast<TerminalWidget *>(w);
        if (args.value(QStringLiteral("replace")).toBool())
            return QStringLiteral("a terminal has no text to replace; send keys (Ctrl+U clears a shell line)");
        if (t->hasExited())
            return QStringLiteral("the terminal's process has exited; restart it first");
        // Typed input: a newline is the Enter key.
        QString text = args.value(QStringLiteral("text")).toString();
        text.replace(QLatin1String("\r\n"), QLatin1String("\r"));
        text.replace(QLatin1Char('\n'), QLatin1Char('\r'));
        t->setFocus(Qt::OtherFocusReason);
        QInputMethodEvent ev;
        ev.setCommitString(text);
        QApplication::sendEvent(t, &ev);
        return QString();
    }
};

// --- chat sessions -----------------------------------------------------------

class ChatAdapter : public ControlAdapter
{
public:
    QString name() const override { return QStringLiteral("chat"); }
    bool matches(const QWidget *w) const override { return qobject_cast<const AcpSessionView *>(w) != nullptr; }
    QString role(const QWidget *) const override { return QStringLiteral("chat"); }
    bool walksChildren(const QWidget *) const override { return true; }

    QJsonObject summary(QWidget *w) const override
    {
        auto *view = static_cast<AcpSessionView *>(w);
        // A prompt not yet out to the agent (still connecting) counts as
        // queued, so the chat is not idle the moment it was sent.
        QJsonObject o{{QStringLiteral("waitingForUser"), view->isWaitingForUser()},
                      {QStringLiteral("queued"), view->queuedPromptCount() + (view->hasUnsentPrompt() ? 1 : 0)}};
        if (const AcpSessionModel *m = view->sessionModel()) {
            o.insert(QStringLiteral("working"), m->isProcessing());
            o.insert(QStringLiteral("entries"), int(m->timeline().size()));
        }
        return o;
    }

    // The transcript as entries (messages and top-level tool calls), oldest
    // first. No `count`: the last 30. `from` < 0 counts back from the end.
    // Part "report": the last finished turn's final answer.
    bool read(QWidget *w, const QString &part, const ReadOptions &opts, QJsonObject &out) const override
    {
        auto *view = static_cast<AcpSessionView *>(w);
        const AcpSessionModel *m = view->sessionModel();
        if (!m)
            return false;
        const auto &timeline = m->timeline();
        const auto &messages = m->messages();
        if (part == QLatin1String("report")) {
            const QVector<int> reports = AcpConversationCopy::finalReports(timeline, messages, !m->isProcessing());
            out.insert(QStringLiteral("working"), m->isProcessing());
            if (!reports.isEmpty()) {
                bool cut = false;
                out.insert(QStringLiteral("text"),
                           capped(AcpConversationCopy::messageText(messages.at(reports.last())), &cut));
                if (cut)
                    out.insert(QStringLiteral("truncated"), true);
            }
            return true;
        }
        if (!part.isEmpty())
            return false;

        const qint64 total = timeline.size();
        qint64 count = opts.count < 0 ? 30 : qMin<qint64>(opts.count, 2000);
        qint64 from = opts.count < 0 ? total - count : opts.from < 0 ? total + opts.from : opts.from;
        from = qBound<qint64>(0, from, total);
        count = qMin(count, total - from);

        QJsonArray entries;
        for (qint64 i = from; i < from + count; ++i) {
            const AcpTimelineEntry &entry = timeline.at(i);
            QJsonObject o{{QStringLiteral("index"), i}};
            if (entry.kind == AcpTimelineEntry::Kind::Message) {
                if (entry.messageIndex < 0 || entry.messageIndex >= messages.size())
                    continue;
                const AcpMessage &msg = messages.at(entry.messageIndex);
                bool cut = false;
                o.insert(QStringLiteral("type"), msg.role);
                o.insert(QStringLiteral("text"), capped(AcpConversationCopy::messageText(msg), &cut));
                if (cut)
                    o.insert(QStringLiteral("truncated"), true);
                if (msg.fromGoalAgent)
                    o.insert(QStringLiteral("fromGoalAgent"), true);
            } else {
                const auto it = m->toolCalls().constFind(entry.toolCallId);
                if (it == m->toolCalls().constEnd() || !it->parentToolCallId.isEmpty())
                    continue; // a subagent's call: shown inside its Agent card
                o.insert(QStringLiteral("type"), QStringLiteral("tool"));
                o.insert(QStringLiteral("title"), it->title);
                o.insert(QStringLiteral("status"), it->status);
                if (!it->kind.isEmpty())
                    o.insert(QStringLiteral("kind"), it->kind);
                if (const int children = int(m->subagentChildren(it->id).size()); children > 0)
                    o.insert(QStringLiteral("subagentCalls"), children);
            }
            entries.append(o);
        }
        out = summary(w);
        out.insert(QStringLiteral("entries"), entries);
        out.insert(QStringLiteral("from"), from);
        out.insert(QStringLiteral("total"), total);
        out.insert(QStringLiteral("draft"), view->peekInputText());
        return true;
    }

    std::optional<QString> perform(QWidget *w, const QString &part, const QString &op,
                                   const QJsonObject &args) const override
    {
        if (op != QLatin1String("send") || !part.isEmpty())
            return std::nullopt;
        // What the user does: type the prompt into the composer, press Send.
        auto *input = w->findChild<QPlainTextEdit *>(QStringLiteral("chatInput"));
        auto *send = w->findChild<QAbstractButton *>(QStringLiteral("chatSend"));
        if (!input || !send)
            return QStringLiteral("this chat has no composer");
        const QString text = args.value(QStringLiteral("text")).toString();
        if (text.trimmed().isEmpty())
            return QStringLiteral("give \"text\": the prompt to send");
        if (QString error = ui::type({input, {}, {}}, text, true); !error.isEmpty())
            return error;
        if (!send->isVisible() || !send->isEnabled())
            return QStringLiteral("Send is not available now (the prompt is in the composer)");
        return ui::click({send, {}, {}}, Qt::LeftButton, false);
    }
};

} // namespace

void registerAppAdapters()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    ControlAdapters &adapters = ControlAdapters::instance();
    adapters.add(std::make_unique<DockAdapter>());
    adapters.add(std::make_unique<EditorAdapter>());
    adapters.add(std::make_unique<TerminalAdapter>());
    adapters.add(std::make_unique<ChatAdapter>());
}

} // namespace control
