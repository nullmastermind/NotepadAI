/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// tabs.*, files.*, editor.*, search.* (docs/adr/0007).

#include "Registrar.h"

#include "DockedEditor.h"
#include "EditorManager.h"
#include "EditorPrintPreviewRenderer.h"
#include "Finder.h"
#include "HtmlConverter.h"
#include "NotepadNextApplication.h"
#include "RecentFilesListManager.h"
#include "RtfConverter.h"
#include "ScintillaNext.h"
#include "TerminalManager.h"
#include "UndoAction.h"
#include "decorators/BookMarkDecorator.h"
#include "dialogs/ColumnEditorDialog.h"
#include "dialogs/FindReplaceDialog.h"
#include "dialogs/MainWindow.h"
#include "docks/FindInFolderDock.h"
#include "docks/FolderAsWorkspaceDock.h"
#include "docks/TerminalDock.h"
#include "remote/SshProfile.h"

#include "DockWidget.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QPrinter>
#include <QPrinterInfo>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <memory>

namespace control::commands {

namespace {

// --- Positions: 1-based lines, 1-based columns counted in characters ---------

QJsonObject at(ScintillaNext *e, sptr_t pos)
{
    const sptr_t line = e->lineFromPosition(pos);
    return {{QStringLiteral("line"), qint64(line + 1)},
            {QStringLiteral("column"), qint64(e->countCharacters(e->positionFromLine(line), pos) + 1)}};
}

QJsonObject rangeJson(ScintillaNext *e, sptr_t start, sptr_t end)
{
    QJsonObject o = at(e, start);
    const QJsonObject to = at(e, end);
    o.insert(QStringLiteral("endLine"), to.value(QStringLiteral("line")));
    o.insert(QStringLiteral("endColumn"), to.value(QStringLiteral("column")));
    return o;
}

// The position of (`lineArg`, `columnArg`), or -1 after failing the command.
// A column past the line's end is its end.
sptr_t positionArg(const CommandContext &ctx, ScintillaNext *e, const QString &lineArg, const QString &columnArg,
                   qint64 defaultLine = 1)
{
    const qint64 line = ctx.integer(lineArg, defaultLine);
    const sptr_t lines = e->lineCount();
    if (line < 1 || line > lines) {
        ctx.fail(400, QStringLiteral("%1 %2 is outside the document (lines 1-%3)").arg(lineArg).arg(line).arg(lines));
        return -1;
    }
    const sptr_t start = e->positionFromLine(line - 1);
    const sptr_t end = e->lineEndPosition(line - 1);
    const qint64 column = ctx.integer(columnArg, 1);
    if (column <= 1)
        return start;
    // SCI_POSITIONRELATIVE counts characters, not bytes; 0 past the document end.
    const sptr_t pos = e->positionRelative(start, column - 1);
    return (pos <= start || pos > end) ? end : pos;
}

void showRange(ScintillaNext *e, sptr_t start, sptr_t end)
{
    e->setSel(start, end);
    e->scrollRange(start, end);
}

bool writable(const CommandContext &ctx, ScintillaNext *e)
{
    if (e->readOnly()) {
        ctx.fail(409, e->loadState() == ScintillaNext::LoadState::Loading
                          ? QStringLiteral("the file is still loading; try again in a moment")
                          : QStringLiteral("this editor is read-only"));
        return false;
    }
    return true;
}

// As typing or pasting over a selection does (Replace Selection), so a macro
// being recorded records the edit; the caret ends after the new text.
void replaceRange(ScintillaNext *e, sptr_t start, sptr_t end, const QByteArray &text)
{
    const UndoAction undo(e);
    e->setSel(start, end);
    e->replaceSel(text.constData());
}

int searchFlags(const CommandContext &ctx)
{
    int flags = 0;
    if (ctx.flag(QStringLiteral("matchCase")))
        flags |= SCFIND_MATCHCASE;
    if (ctx.flag(QStringLiteral("wholeWord")))
        flags |= SCFIND_WHOLEWORD;
    if (ctx.flag(QStringLiteral("regex")))
        flags |= SCFIND_REGEXP;
    return flags;
}

QList<Arg> searchArgs(QList<Arg> args)
{
    args.append(Registrar::optional(QStringLiteral("matchCase"), ArgType::Bool, QStringLiteral("match case; default false")));
    args.append(Registrar::optional(QStringLiteral("wholeWord"), ArgType::Bool, QStringLiteral("whole words only; default false")));
    args.append(Registrar::optional(QStringLiteral("regex"), ArgType::Bool,
                                    QStringLiteral("`text` is a regular expression (replacements may use \\1); default false")));
    return args;
}

QString fileErrorText(QFileDevice::FileError error)
{
    switch (error) {
    case QFileDevice::PermissionsError:
        return QStringLiteral("permission denied");
    case QFileDevice::OpenError:
        return QStringLiteral("could not open the file for writing");
    case QFileDevice::WriteError:
        return QStringLiteral("could not write the file");
    case QFileDevice::ResourceError:
        return QStringLiteral("out of disk space or the connection is lost");
    default:
        return QStringLiteral("could not save (error %1)").arg(int(error));
    }
}

// Saves `e` to its file, or to `path` when set; `done(error)` with an empty
// error on success. SSH saves finish later (saved / saveFailed).
void saveEditor(ScintillaNext *e, const QString &path, const std::function<void(const QString &)> &done)
{
    if (!path.isEmpty() && (e->isRemote() || remote::isSshUri(path))) {
        done(QStringLiteral("saving to a new path is supported for local files only"));
        return;
    }
    if (path.isEmpty() && !e->isFile()) {
        done(QStringLiteral("it has never been saved; give a path"));
        return;
    }
    if (e->isRemote()) {
        auto *guard = new QObject(e);
        QObject::connect(e, &ScintillaNext::saved, guard, [guard, done] {
            guard->deleteLater();
            done(QString());
        });
        QObject::connect(e, &ScintillaNext::saveFailed, guard, [guard, done](const QString &error) {
            guard->deleteLater();
            done(error.isEmpty() ? QStringLiteral("the save failed") : error);
        });
        e->save(); // a write that cannot start reports saveFailed at once
        return;
    }
    if (!path.isEmpty()) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        const QFileDevice::FileError error = e->saveAs(path);
        done(error == QFileDevice::NoError ? QString() : fileErrorText(error));
        return;
    }
    const QFileDevice::FileError error = e->save();
    done(error == QFileDevice::NoError ? QString() : fileErrorText(error));
}

QString eolName(int mode)
{
    switch (mode) {
    case SC_EOL_CRLF:
        return QStringLiteral("crlf");
    case SC_EOL_CR:
        return QStringLiteral("cr");
    default:
        return QStringLiteral("lf");
    }
}

QString bomName(ScintillaNext::BomType bom)
{
    switch (bom) {
    case ScintillaNext::BomType::Utf8:
        return QStringLiteral("utf-8 bom");
    case ScintillaNext::BomType::Utf16LE:
        return QStringLiteral("utf-16le bom");
    case ScintillaNext::BomType::Utf16BE:
        return QStringLiteral("utf-16be bom");
    default:
        return QString();
    }
}

} // namespace

// === tabs.* ====================================================================

void Registrar::tabs()
{
    add(QStringLiteral("tabs.list"),
        QStringLiteral("List the tabs (editors, terminals, previews, browsers, diffs) with their ids"), false,
        {oneOf(QStringLiteral("kind"),
               {QStringLiteral("editor"), QStringLiteral("diff"), QStringLiteral("terminal"), QStringLiteral("browser"),
                QStringLiteral("preview"), QStringLiteral("panel"), QStringLiteral("embed")},
               QStringLiteral("only tabs of this kind (panel: the Terminal panel with no terminal open; "
                              "embed: another app's window, embed.window)"))},
        [w = w](const CommandContext &ctx) {
            const QString kind = ctx.str(QStringLiteral("kind"));
            QJsonArray list;
            const QList<ads::CDockWidget *> docks = w->getDockedEditor()->allTabs();
            for (ads::CDockWidget *dock : docks) {
                if (kind.isEmpty() || tabKind(dock) == kind)
                    list.append(tabJson(w, dock));
            }
            ctx.ok({{QStringLiteral("tabs"), list}});
        });

    add(QStringLiteral("tabs.activate"), QStringLiteral("Bring a tab to the front and focus it"), true,
        {required(QStringLiteral("tab"), ArgType::Int, QStringLiteral("tab id from tabs.list"))},
        [w = w](const CommandContext &ctx) {
            const int id = int(ctx.integer(QStringLiteral("tab")));
            ads::CDockWidget *dock = tabById(w, id);
            if (!dock) {
                ctx.fail(404, QStringLiteral("no tab %1; tabs.list lists them").arg(id));
                return;
            }
            if (auto *editor = qobject_cast<ScintillaNext *>(dock->widget()))
                w->getDockedEditor()->switchToEditor(editor);
            ctx.reveal(dock->widget());
            if (QWidget *content = dock->widget())
                content->setFocus(Qt::OtherFocusReason);
            ctx.ok(tabJson(w, dock));
        });

    add(QStringLiteral("tabs.close"),
        QStringLiteral("Close tabs. Unsaved editors need `unsaved`; a terminal running a command needs `force`"), true,
        {optional(QStringLiteral("tab"), ArgType::Int, QStringLiteral("tab id; default: the current editor")),
         optional(QStringLiteral("tabs"), ArgType::Array, QStringLiteral("several tab ids instead of `tab`")),
         oneOf(QStringLiteral("unsaved"), {QStringLiteral("save"), QStringLiteral("discard")},
               QStringLiteral("what to do with unsaved changes (save needs the file to have a path)")),
         optional(QStringLiteral("force"), ArgType::Bool, QStringLiteral("close terminals even while a command runs"))},
        [w = w](const CommandContext &ctx) {
            QList<ads::CDockWidget *> targets;
            if (ctx.has(QStringLiteral("tabs"))) {
                const QJsonArray tabsJson = ctx.array(QStringLiteral("tabs"));
                for (const auto &v : tabsJson) {
                    ads::CDockWidget *dock = tabById(w, v.toInt());
                    if (!dock) {
                        ctx.fail(404, QStringLiteral("no tab %1; tabs.list lists them").arg(v.toInt()));
                        return;
                    }
                    targets.append(dock);
                }
            } else if (ctx.has(QStringLiteral("tab"))) {
                const int id = int(ctx.integer(QStringLiteral("tab")));
                ads::CDockWidget *dock = tabById(w, id);
                if (!dock) {
                    ctx.fail(404, QStringLiteral("no tab %1; tabs.list lists them").arg(id));
                    return;
                }
                targets.append(dock);
            } else {
                ScintillaNext *editor = w->currentEditor();
                ads::CDockWidget *dock = editor ? tabOf(editor) : nullptr;
                if (!dock) {
                    ctx.fail(404, QStringLiteral("no editor is open; pass `tab`"));
                    return;
                }
                targets.append(dock);
            }

            // Refuse before closing anything, so a refusal changes nothing.
            const QString unsaved = ctx.str(QStringLiteral("unsaved"));
            const bool force = ctx.flag(QStringLiteral("force"));
            EditorManager *em = w->app->getEditorManager();
            for (ads::CDockWidget *dock : std::as_const(targets)) {
                if (auto *editor = qobject_cast<ScintillaNext *>(dock->widget())) {
                    if (em->isDiffView(editor) || editor->isSavedToDisk())
                        continue;
                    if (unsaved.isEmpty()) {
                        ctx.fail(409,
                                 QStringLiteral("tab %1 (%2) has unsaved changes; pass unsaved: \"save\" or \"discard\"")
                                     .arg(tabId(dock))
                                     .arg(dock->windowTitle()),
                                 {{QStringLiteral("needs"), QStringLiteral("unsaved")}, {QStringLiteral("tab"), tabId(dock)}});
                        return;
                    }
                    if (unsaved == QLatin1String("save") && !editor->isFile()) {
                        ctx.fail(409,
                                 QStringLiteral("tab %1 was never saved; save it with files.save {tab, path} or pass "
                                                "unsaved: \"discard\"")
                                     .arg(tabId(dock)),
                                 {{QStringLiteral("tab"), tabId(dock)}, {QStringLiteral("needs"), QStringLiteral("unsaved")}});
                        return;
                    }
                } else if (auto *term = qobject_cast<TerminalDock *>(dock->widget())) {
                    if (!force && term->isBusy()) {
                        ctx.fail(409,
                                 QStringLiteral("terminal tab %1 is running a command; pass force: true to stop it")
                                     .arg(tabId(dock)),
                                 {{QStringLiteral("needs"), QStringLiteral("force")}, {QStringLiteral("tab"), tabId(dock)}});
                        return;
                    }
                }
            }

            QJsonArray closed;
            QStringList errors;
            for (ads::CDockWidget *dock : std::as_const(targets)) {
                QPointer<ads::CDockWidget> guard(dock);
                const int id = tabId(dock);
                if (auto *editor = qobject_cast<ScintillaNext *>(dock->widget())) {
                    if (unsaved == QLatin1String("save") && !em->isDiffView(editor) && !editor->isSavedToDisk()) {
                        if (editor->isRemote()) {
                            // Close once the SSH write lands; keep the tab on failure.
                            QPointer<ScintillaNext> e(editor);
                            saveEditor(editor, QString(), [e](const QString &error) {
                                if (error.isEmpty() && e)
                                    e->close();
                            });
                            closed.append(id);
                            continue;
                        }
                        QString error;
                        saveEditor(editor, QString(), [&error](const QString &why) { error = why; });
                        if (!error.isEmpty()) {
                            errors.append(QStringLiteral("tab %1: %2").arg(id).arg(error));
                            continue;
                        }
                    }
                    editor->close();
                } else if (auto *term = qobject_cast<TerminalDock *>(dock->widget())) {
                    QString error;
                    if (!w->terminalManager->closeTerminal(term, force, &error)) {
                        errors.append(QStringLiteral("tab %1: %2").arg(id).arg(error));
                        continue;
                    }
                } else if (guard) {
                    guard->requestCloseDockWidget();
                }
                closed.append(id);
            }
            if (!errors.isEmpty()) {
                ctx.fail(422, errors.join(QStringLiteral("; ")), {{QStringLiteral("closed"), closed}});
                return;
            }
            ctx.ok({{QStringLiteral("closed"), closed}});
        });

    add(QStringLiteral("tabs.split"), QStringLiteral("Show an editor in a new pane beside or below the current one"), true,
        {tabArg(QStringLiteral("editor tab id; default: the current editor")),
         oneOf(QStringLiteral("direction"), {QStringLiteral("right"), QStringLiteral("down")},
               QStringLiteral("where the new pane goes; default right"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            if (ctx.str(QStringLiteral("direction")) == QLatin1String("down"))
                w->getDockedEditor()->splitToBottom(editor);
            else
                w->getDockedEditor()->splitToRight(editor);
            ctx.reveal(editor);
            ads::CDockWidget *dock = tabOf(editor);
            ctx.ok(dock ? tabJson(w, dock) : QJsonObject());
        });
}

// === files.* ===================================================================

void Registrar::files()
{
    add(QStringLiteral("files.open"),
        QStringLiteral("Open a file in a tab (or bring its tab to the front), optionally at a line"), true,
        {required(QStringLiteral("path"), ArgType::String,
                  QStringLiteral("absolute path, relative to the active workspace, or ssh://<profile>/<path>")),
         optional(QStringLiteral("line"), ArgType::Int, QStringLiteral("put the caret on this line (1-based)")),
         optional(QStringLiteral("column"), ArgType::Int, QStringLiteral("and this column (1-based, in characters)")),
         optional(QStringLiteral("create"), ArgType::Bool,
                  QStringLiteral("open a missing file as a new buffer that is written on the first save; default false")),
         optional(QStringLiteral("preview"), ArgType::Bool,
                  QStringLiteral("open in the transient preview tab (replaced by the next preview); default false")),
         optional(QStringLiteral("asText"), ArgType::Bool,
                  QStringLiteral("open images, media and binaries as text instead of their preview; default false"))},
        [w = w](const CommandContext &ctx) {
            const QString path = pathArg(w, ctx.str(QStringLiteral("path")));
            const bool ssh = remote::isSshUri(path);
            if (!ssh && QFileInfo(path).isDir()) {
                ctx.fail(400, QStringLiteral("%1 is a folder; open it with workspace.open").arg(path));
                return;
            }
            const QSet<ads::CDockWidget *> before = [&] {
                const QList<ads::CDockWidget *> tabs = w->getDockedEditor()->allTabs();
                return QSet<ads::CDockWidget *>(tabs.cbegin(), tabs.cend());
            }();
            EditorManager *em = w->app->getEditorManager();
            if (!ssh && !QFileInfo(path).isFile()) {
                if (!ctx.flag(QStringLiteral("create"))) {
                    ctx.fail(404, QStringLiteral("no file %1; pass create: true to start it as a new buffer").arg(path),
                             {{QStringLiteral("needs"), QStringLiteral("create")}});
                    return;
                }
                ScintillaNext *initial = w->getInitialEditor();
                ScintillaNext *created = em->createEditorFromFile(path, true);
                if (!created) {
                    ctx.fail(422, QStringLiteral("could not create %1").arg(path));
                    return;
                }
                w->getDockedEditor()->switchToEditor(created);
                if (initial)
                    initial->close();
            } else if (ctx.flag(QStringLiteral("preview"))) {
                w->previewFile(path);
            } else if (ctx.flag(QStringLiteral("asText"))) {
                w->openFileAsText(path);
            } else {
                w->openFile(path);
            }

            ads::CDockWidget *dock = nullptr;
            ScintillaNext *editor = em->getEditorByFilePath(path);
            const bool assetTab = !ctx.flag(QStringLiteral("asText")) && !w->assetPreviewTypeFor(path).isEmpty();
            if (editor && !assetTab) {
                dock = tabOf(editor);
            } else {
                const QList<ads::CDockWidget *> tabs = w->getDockedEditor()->allTabs();
                for (ads::CDockWidget *t : tabs) {
                    if (!before.contains(t))
                        dock = t;
                }
                if (!dock) {
                    // Already open in a preview tab: the one in front now.
                    for (ads::CDockWidget *t : tabs) {
                        if (t->isCurrentTab() && tabKind(t) == QLatin1String("preview"))
                            dock = t;
                    }
                }
            }
            if (!dock) {
                ctx.fail(422, QStringLiteral("%1 did not open").arg(path));
                return;
            }
            if (editor && dock == tabOf(editor) && ctx.has(QStringLiteral("line"))) {
                const qint64 line = ctx.integer(QStringLiteral("line"));
                const qint64 column = ctx.integer(QStringLiteral("column"), 1);
                auto place = [e = QPointer<ScintillaNext>(editor), line, column] {
                    if (!e)
                        return;
                    const sptr_t l = std::clamp<sptr_t>(line - 1, 0, e->lineCount() - 1);
                    const sptr_t start = e->positionFromLine(l);
                    const sptr_t pos = column > 1 ? std::min(e->positionRelative(start, column - 1), e->lineEndPosition(l))
                                                  : start;
                    e->gotoPos(std::max(pos, start));
                    e->verticalCentreCaret();
                };
                if (editor->loadState() == ScintillaNext::LoadState::Loading)
                    QObject::connect(editor, &ScintillaNext::loaded, editor, place, Qt::SingleShotConnection);
                else
                    place();
            }
            ctx.reveal(dock->widget());
            ctx.ok(tabJson(w, dock));
        });

    add(QStringLiteral("files.new"), QStringLiteral("Open a new unsaved buffer, optionally with text and a language"), true,
        {optional(QStringLiteral("text"), ArgType::String, QStringLiteral("initial text")),
         optional(QStringLiteral("language"), ArgType::String, QStringLiteral("language name from editor.languages"))},
        [w = w](const CommandContext &ctx) {
            const QString language = ctx.str(QStringLiteral("language"));
            if (!language.isEmpty() && !w->app->getLanguages().contains(language)) {
                ctx.fail(400, QStringLiteral("no language '%1'; editor.languages lists them").arg(language));
                return;
            }
            w->newFile();
            ScintillaNext *editor = w->currentEditor();
            if (!editor) {
                ctx.fail(422, QStringLiteral("no buffer opened"));
                return;
            }
            if (ctx.has(QStringLiteral("text")))
                editor->setText(ctx.str(QStringLiteral("text")).toUtf8().constData());
            if (!language.isEmpty())
                w->setLanguage(editor, language);
            ctx.reveal(editor);
            ads::CDockWidget *dock = tabOf(editor);
            ctx.ok(dock ? tabJson(w, dock) : QJsonObject());
        });

    add(QStringLiteral("files.save"),
        QStringLiteral("Save an editor to its file, or to `path` (Save As: the tab then edits that file)"), true,
        {tabArg(QStringLiteral("editor tab id; default: the current editor")),
         optional(QStringLiteral("path"), ArgType::String,
                  QStringLiteral("save under this path (needed for a buffer that was never saved)"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            const QString path = pathArg(w, ctx.str(QStringLiteral("path")));
            if (path.isEmpty() && !editor->isFile()) {
                ctx.fail(409, QStringLiteral("this buffer was never saved; pass `path`"),
                         {{QStringLiteral("needs"), QStringLiteral("path")}});
                return;
            }
            ctx.reveal(editor);
            QPointer<ScintillaNext> e(editor);
            saveEditor(editor, path, [ctx, w, e](const QString &error) {
                if (!error.isEmpty()) {
                    ctx.fail(422, error);
                    return;
                }
                ads::CDockWidget *dock = e ? tabOf(e) : nullptr;
                ctx.ok(dock ? tabJson(w, dock) : QJsonObject());
            });
        });

    add(QStringLiteral("files.saveAll"),
        QStringLiteral("Save every modified editor that has a file (never-saved buffers are listed, not saved)"), true, {},
        [w = w](const CommandContext &ctx) {
            EditorManager *em = w->app->getEditorManager();
            // SSH saves answer later; the last one to finish answers.
            struct State
            {
                QJsonArray saved, needPath;
                QStringList errors;
                int pending = 1;
            };
            auto state = std::make_shared<State>();
            auto done = [ctx, state] {
                if (--state->pending > 0)
                    return;
                QJsonObject result{{QStringLiteral("saved"), state->saved}, {QStringLiteral("needPath"), state->needPath}};
                if (!state->errors.isEmpty())
                    ctx.fail(422, state->errors.join(QStringLiteral("; ")), result);
                else
                    ctx.ok(result);
            };
            const QVector<ScintillaNext *> editors = w->editors();
            for (ScintillaNext *editor : editors) {
                if (em->isDiffView(editor) || editor->isSavedToDisk())
                    continue;
                ads::CDockWidget *dock = tabOf(editor);
                const int id = dock ? tabId(dock) : 0;
                if (!editor->isFile()) {
                    state->needPath.append(id);
                    continue;
                }
                ++state->pending;
                saveEditor(editor, QString(), [state, id, done](const QString &error) {
                    if (error.isEmpty())
                        state->saved.append(id);
                    else
                        state->errors.append(QStringLiteral("tab %1: %2").arg(id).arg(error));
                    done();
                });
            }
            done();
        });

    add(QStringLiteral("files.saveCopy"),
        QStringLiteral("Write an editor's text to another file (the tab keeps editing its own file)"), true,
        {tabArg(QStringLiteral("editor tab id; default: the current editor")),
         required(QStringLiteral("path"), ArgType::String, QStringLiteral("where to write the copy (local)"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            const QString path = pathArg(w, ctx.str(QStringLiteral("path")));
            if (remote::isSshUri(path)) {
                ctx.fail(400, QStringLiteral("copies can be written to local paths only"));
                return;
            }
            QDir().mkpath(QFileInfo(path).absolutePath());
            const QFileDevice::FileError error = editor->saveCopyAs(path);
            if (error != QFileDevice::NoError) {
                ctx.fail(422, fileErrorText(error));
                return;
            }
            ctx.reveal(editor);
            ctx.ok({{QStringLiteral("path"), path}});
        });

    add(QStringLiteral("files.reload"), QStringLiteral("Reload an editor from its file"), true,
        {tabArg(QStringLiteral("editor tab id; default: the current editor")),
         oneOf(QStringLiteral("unsaved"), {QStringLiteral("discard")},
               QStringLiteral("required when the editor has unsaved changes, which reloading drops"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            if (!editor->isFile()) {
                ctx.fail(409, QStringLiteral("this buffer has no file to reload from"));
                return;
            }
            if (editor->modify() && !ctx.has(QStringLiteral("unsaved"))) {
                ctx.fail(409, QStringLiteral("it has unsaved changes; pass unsaved: \"discard\" to drop them"),
                         {{QStringLiteral("needs"), QStringLiteral("unsaved")}});
                return;
            }
            editor->reload();
            ctx.reveal(editor);
            ads::CDockWidget *dock = tabOf(editor);
            ctx.ok(dock ? tabJson(w, dock) : QJsonObject());
        });

    add(QStringLiteral("files.rename"),
        QStringLiteral("Rename an editor's file on disk (or a new buffer's tab name); the tab follows"), true,
        {tabArg(QStringLiteral("editor tab id; default: the current editor")),
         required(QStringLiteral("to"), ArgType::String,
                  QStringLiteral("new path (a bare name stays in the same folder); for a never-saved buffer, its new "
                                 "tab name"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            const QString to = ctx.str(QStringLiteral("to"));
            if (to.trimmed().isEmpty()) {
                ctx.fail(400, QStringLiteral("`to` is empty"));
                return;
            }
            if (!editor->isFile()) {
                editor->setName(to);
                ctx.reveal(editor);
                ctx.ok({{QStringLiteral("name"), editor->getName()}});
                return;
            }
            if (editor->isRemote()) {
                ctx.fail(409, QStringLiteral("rename SSH files with workspace.rename is not supported yet; use the "
                                             "workspace tree"));
                return;
            }
            const bool bareName = !to.contains(QLatin1Char('/')) && !to.contains(QLatin1Char('\\'));
            const QString target = bareName ? editor->getFileInfo().absolutePath() + QLatin1Char('/') + to : pathArg(w, to);
            if (QFileInfo::exists(target)
                && QDir::cleanPath(target).compare(QDir::cleanPath(editor->getFilePath()), Qt::CaseInsensitive) != 0) {
                ctx.fail(409, QStringLiteral("%1 already exists").arg(target));
                return;
            }
            if (!editor->rename(target)) {
                ctx.fail(422, QStringLiteral("could not rename to %1").arg(target));
                return;
            }
            ctx.reveal(editor);
            ctx.ok({{QStringLiteral("path"), displayPath(editor->getFilePath())}});
        });

    add(QStringLiteral("files.trash"), QStringLiteral("Move an editor's file to the recycle bin and close its tab"), true,
        {tabArg(QStringLiteral("editor tab id; default: the current editor"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            if (!editor->isFile() || editor->isRemote()) {
                ctx.fail(409, QStringLiteral("only a local file can be moved to the recycle bin; for SSH files use "
                                             "workspace.delete"));
                return;
            }
            const QString path = editor->getFilePath();
            if (!editor->moveToTrash()) {
                ctx.fail(422, QStringLiteral("could not move %1 to the recycle bin").arg(path));
                return;
            }
            w->app->getRecentFilesListManager()->removeFile(path);
            editor->close();
            ctx.ok({{QStringLiteral("trashed"), displayPath(path)}});
        });

    add(QStringLiteral("files.export"), QStringLiteral("Export an editor's text with its colours as HTML or RTF"), true,
        {tabArg(QStringLiteral("editor tab id; default: the current editor")),
         oneOf(QStringLiteral("format"), {QStringLiteral("html"), QStringLiteral("rtf")}, QStringLiteral("html or rtf"), true),
         required(QStringLiteral("path"), ArgType::String, QStringLiteral("where to write it (local)"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            const QString path = pathArg(w, ctx.str(QStringLiteral("path")));
            if (remote::isSshUri(path)) {
                ctx.fail(400, QStringLiteral("exports can be written to local paths only"));
                return;
            }
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)) {
                ctx.fail(422, QStringLiteral("could not write %1: %2").arg(path, file.errorString()));
                return;
            }
            {
                QTextStream stream(&file);
                if (ctx.str(QStringLiteral("format")) == QLatin1String("rtf")) {
                    RtfConverter converter(editor);
                    converter.convert(stream);
                } else {
                    HtmlConverter converter(editor);
                    converter.convert(stream);
                }
            }
            if (!file.commit()) {
                ctx.fail(422, QStringLiteral("could not write %1: %2").arg(path, file.errorString()));
                return;
            }
            ctx.ok({{QStringLiteral("path"), path}});
        });

    // File > Print: the same page rendering as the print preview, sent to a
    // printer or written as a PDF instead of shown.
    add(QStringLiteral("files.printers"), QStringLiteral("List the printers File > Print can use"), false, {},
        [](const CommandContext &ctx) {
            QJsonArray printers;
            for (const QString &name : QPrinterInfo::availablePrinterNames())
                printers.append(name);
            ctx.ok({{QStringLiteral("printers"), printers}, {QStringLiteral("default"), QPrinterInfo::defaultPrinterName()}});
        });

    add(QStringLiteral("files.print"),
        QStringLiteral("Print an editor as File > Print does: to a printer, or to a PDF file"), true,
        {tabArg(QStringLiteral("editor tab id; default: the current editor")),
         optional(QStringLiteral("printer"), ArgType::String,
                  QStringLiteral("printer name from files.printers; default: the default printer")),
         optional(QStringLiteral("pdf"), ArgType::String, QStringLiteral("instead of a printer: write a PDF here (local)")),
         optional(QStringLiteral("copies"), ArgType::Int, QStringLiteral("number of copies; default 1"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            QPrinter printer(QPrinter::HighResolution);
            printer.setPageMargins(QMarginsF(.5, .5, .5, .5), QPageLayout::Inch);
            QJsonObject result;
            if (ctx.has(QStringLiteral("pdf"))) {
                const QString path = pathArg(w, ctx.str(QStringLiteral("pdf")));
                if (remote::isSshUri(path)) {
                    ctx.fail(400, QStringLiteral("a PDF can be written to a local path only"));
                    return;
                }
                QDir().mkpath(QFileInfo(path).absolutePath());
                printer.setOutputFormat(QPrinter::PdfFormat);
                printer.setOutputFileName(path);
                result.insert(QStringLiteral("pdf"), path);
            } else {
                const QString name = ctx.str(QStringLiteral("printer"), QPrinterInfo::defaultPrinterName());
                if (name.isEmpty() || !QPrinterInfo::availablePrinterNames().contains(name)) {
                    ctx.fail(404, name.isEmpty() ? QStringLiteral("there is no default printer; pass `printer` or `pdf`")
                                                 : QStringLiteral("no printer '%1'; files.printers lists them").arg(name),
                             {{QStringLiteral("needs"), QStringLiteral("printer")}});
                    return;
                }
                printer.setPrinterName(name);
                result.insert(QStringLiteral("printer"), name);
            }
            printer.setCopyCount(int(std::max<qint64>(1, ctx.integer(QStringLiteral("copies"), 1))));
            if (!printer.isValid()) {
                ctx.fail(422, QStringLiteral("the printer cannot be used"));
                return;
            }
            ctx.reveal(editor);
            EditorPrintPreviewRenderer renderer(editor);
            renderer.render(&printer);
            if (ctx.has(QStringLiteral("pdf")) && !QFileInfo::exists(printer.outputFileName())) {
                ctx.fail(422, QStringLiteral("could not write %1").arg(printer.outputFileName()));
                return;
            }
            ctx.ok(result);
        });

    add(QStringLiteral("files.recent"), QStringLiteral("List recently opened files, newest first"), false, {},
        [w = w](const CommandContext &ctx) {
            QJsonArray list;
            for (const QString &p : w->app->getRecentFilesListManager()->fileList())
                list.append(displayPath(p));
            ctx.ok({{QStringLiteral("files"), list}});
        });
}

// === editor.* ==================================================================

void Registrar::editor()
{
    const Arg tab = tabArg(QStringLiteral("editor tab id; default: the current editor"));

    add(QStringLiteral("editor.state"),
        QStringLiteral("Read an editor's file, language, line endings, caret, selection and modified state"), false, {tab},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e)
                return;
            ads::CDockWidget *dock = tabOf(e);
            QJsonObject o = dock ? tabJson(w, dock) : QJsonObject();
            o.insert(QStringLiteral("language"), e->languageName);
            o.insert(QStringLiteral("eol"), eolName(int(e->eOLMode())));
            if (const QString bom = bomName(e->bom()); !bom.isEmpty())
                o.insert(QStringLiteral("bom"), bom);
            o.insert(QStringLiteral("lineCount"), qint64(e->lineCount()));
            o.insert(QStringLiteral("length"), qint64(e->length()));
            o.insert(QStringLiteral("readOnly"), e->readOnly());
            o.insert(QStringLiteral("caret"), at(e, e->currentPos()));
            const sptr_t start = e->selectionStart();
            const sptr_t end = e->selectionEnd();
            if (start != end) {
                QJsonObject sel = rangeJson(e, start, end);
                const QByteArray text = e->get_text_range(int(start), int(std::min<sptr_t>(end, start + 65536)));
                sel.insert(QStringLiteral("text"), QString::fromUtf8(text));
                sel.insert(QStringLiteral("truncated"), end - start > 65536);
                o.insert(QStringLiteral("selection"), sel);
            }
            o.insert(QStringLiteral("firstVisibleLine"), qint64(e->docLineFromVisible(e->firstVisibleLine()) + 1));
            if (e->loadState() == ScintillaNext::LoadState::Loading)
                o.insert(QStringLiteral("loading"), true);
            ctx.ok(o);
        });

    add(QStringLiteral("editor.read"), QStringLiteral("Read an editor's text (its current, maybe unsaved, content)"), false,
        {tab, optional(QStringLiteral("line"), ArgType::Int, QStringLiteral("first line (1-based); default 1")),
         optional(QStringLiteral("endLine"), ArgType::Int, QStringLiteral("last line, inclusive; default the last")),
         optional(QStringLiteral("maxChars"), ArgType::Int,
                  QStringLiteral("cut at a line end after this many characters; default 200000"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e)
                return;
            const sptr_t lines = e->lineCount();
            const qint64 first = ctx.integer(QStringLiteral("line"), 1);
            const qint64 last = std::min<qint64>(ctx.integer(QStringLiteral("endLine"), lines), lines);
            if (first < 1 || first > lines || last < first) {
                ctx.fail(400, QStringLiteral("lines %1-%2 are outside the document (lines 1-%3)").arg(first).arg(last).arg(lines));
                return;
            }
            // A document never holds more characters than bytes; the cap keeps maxChars * 4 from overflowing.
            const qint64 maxChars = std::clamp<qint64>(ctx.integer(QStringLiteral("maxChars"), 200000), 1,
                                                       std::max<qint64>(1, e->length()));
            const sptr_t start = e->positionFromLine(first - 1);
            // Bytes >= characters, so reading maxChars*4 bytes is always enough.
            const sptr_t lastEnd = last == lines ? e->length() : e->positionFromLine(last);
            const sptr_t end = std::min<sptr_t>(lastEnd, start + maxChars * 4);
            QString text = QString::fromUtf8(e->get_text_range(int(start), int(end)));
            qint64 lastLine = last;
            bool truncated = false;
            if (text.size() > maxChars || end < lastEnd) {
                truncated = true;
                text.truncate(int(std::min<qint64>(maxChars, text.size())));
                const qsizetype cut = text.lastIndexOf(QLatin1Char('\n'));
                if (cut >= 0)
                    text.truncate(cut + 1);
                lastLine = first + text.count(QLatin1Char('\n')) - 1;
                if (lastLine < first)
                    lastLine = first;
            }
            QJsonObject o{{QStringLiteral("text"), text},
                          {QStringLiteral("line"), first},
                          {QStringLiteral("endLine"), lastLine},
                          {QStringLiteral("lineCount"), qint64(lines)},
                          {QStringLiteral("modified"), e->modify()}};
            if (truncated)
                o.insert(QStringLiteral("truncated"), true);
            if (ads::CDockWidget *dock = tabOf(e))
                o.insert(QStringLiteral("tab"), tabId(dock));
            ctx.ok(o);
        });

    add(QStringLiteral("editor.setText"), QStringLiteral("Replace an editor's whole text (one undo step)"), true,
        {tab, required(QStringLiteral("text"), ArgType::String, QStringLiteral("the new text"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e || !writable(ctx, e))
                return;
            replaceRange(e, 0, e->length(), ctx.str(QStringLiteral("text")).toUtf8());
            e->gotoPos(0);
            ctx.reveal(e);
            ctx.ok({{QStringLiteral("lineCount"), qint64(e->lineCount())}, {QStringLiteral("modified"), e->modify()}});
        });

    add(QStringLiteral("editor.insert"),
        QStringLiteral("Insert text at a line/column, or at the caret; the caret ends after it"), true,
        {tab, required(QStringLiteral("text"), ArgType::String, QStringLiteral("text to insert")),
         optional(QStringLiteral("line"), ArgType::Int, QStringLiteral("line (1-based); default: at the caret")),
         optional(QStringLiteral("column"), ArgType::Int,
                  QStringLiteral("column (1-based, characters; past the end = line end); default 1")),
         oneOf(QStringLiteral("at"), {QStringLiteral("caret"), QStringLiteral("start"), QStringLiteral("end")},
               QStringLiteral("instead of a line: the caret, the document start or end"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e || !writable(ctx, e))
                return;
            sptr_t pos = e->currentPos();
            if (ctx.has(QStringLiteral("line"))) {
                pos = positionArg(ctx, e, QStringLiteral("line"), QStringLiteral("column"));
                if (pos < 0)
                    return;
            } else if (ctx.str(QStringLiteral("at")) == QLatin1String("start")) {
                pos = 0;
            } else if (ctx.str(QStringLiteral("at")) == QLatin1String("end")) {
                pos = e->length();
            }
            const QByteArray text = ctx.str(QStringLiteral("text")).toUtf8();
            replaceRange(e, pos, pos, text);
            ctx.reveal(e);
            showRange(e, pos, pos + text.size());
            ctx.ok(rangeJson(e, pos, pos + text.size()));
        });

    add(QStringLiteral("editor.replaceRange"),
        QStringLiteral("Replace the text between two line/column positions (one undo step)"), true,
        {tab, required(QStringLiteral("line"), ArgType::Int, QStringLiteral("start line (1-based)")),
         optional(QStringLiteral("column"), ArgType::Int, QStringLiteral("start column (1-based); default 1")),
         required(QStringLiteral("endLine"), ArgType::Int, QStringLiteral("end line (1-based)")),
         optional(QStringLiteral("endColumn"), ArgType::Int,
                  QStringLiteral("end column, exclusive (1-based; past the end = line end, without its line break); "
                                 "default 1")),
         required(QStringLiteral("text"), ArgType::String, QStringLiteral("replacement (empty deletes)"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e || !writable(ctx, e))
                return;
            const sptr_t start = positionArg(ctx, e, QStringLiteral("line"), QStringLiteral("column"));
            if (start < 0)
                return;
            const sptr_t end = positionArg(ctx, e, QStringLiteral("endLine"), QStringLiteral("endColumn"));
            if (end < 0)
                return;
            if (end < start) {
                ctx.fail(400, QStringLiteral("the end comes before the start"));
                return;
            }
            const QByteArray text = ctx.str(QStringLiteral("text")).toUtf8();
            replaceRange(e, start, end, text);
            ctx.reveal(e);
            showRange(e, start, start + text.size());
            ctx.ok(rangeJson(e, start, start + text.size()));
        });

    add(QStringLiteral("editor.select"),
        QStringLiteral("Move the caret to a line/column, or select up to an end position, and scroll there"), true,
        {tab, required(QStringLiteral("line"), ArgType::Int, QStringLiteral("line (1-based)")),
         optional(QStringLiteral("column"), ArgType::Int, QStringLiteral("column (1-based); default 1")),
         optional(QStringLiteral("endLine"), ArgType::Int, QStringLiteral("select to this line")),
         optional(QStringLiteral("endColumn"), ArgType::Int, QStringLiteral("and this column; default 1"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e)
                return;
            const sptr_t start = positionArg(ctx, e, QStringLiteral("line"), QStringLiteral("column"));
            if (start < 0)
                return;
            sptr_t end = start;
            if (ctx.has(QStringLiteral("endLine"))) {
                end = positionArg(ctx, e, QStringLiteral("endLine"), QStringLiteral("endColumn"));
                if (end < 0)
                    return;
            }
            ctx.reveal(e);
            showRange(e, start, end);
            e->verticalCentreCaret();
            ctx.ok(rangeJson(e, std::min(start, end), std::max(start, end)));
        });

    for (const bool redo : {false, true}) {
        add(redo ? QStringLiteral("editor.redo") : QStringLiteral("editor.undo"),
            redo ? QStringLiteral("Redo the last undone edits") : QStringLiteral("Undo the last edits"), true,
            {tab, optional(QStringLiteral("times"), ArgType::Int, QStringLiteral("how many steps; default 1"))},
            [w = w, redo](const CommandContext &ctx) {
                ScintillaNext *e = editorArg(ctx, w);
                if (!e || !writable(ctx, e))
                    return;
                const qint64 times = std::clamp<qint64>(ctx.integer(QStringLiteral("times"), 1), 1, 10000);
                qint64 done = 0;
                for (; done < times && (redo ? e->canRedo() : e->canUndo()); ++done) {
                    if (redo)
                        e->redo();
                    else
                        e->undo();
                }
                ctx.reveal(e);
                e->scrollCaret();
                ctx.ok({{QStringLiteral("steps"), done},
                        {QStringLiteral("canUndo"), e->canUndo()},
                        {QStringLiteral("canRedo"), e->canRedo()},
                        {QStringLiteral("modified"), e->modify()}});
            });
    }

    add(QStringLiteral("editor.find"),
        QStringLiteral("Select the next (or previous) match after the caret, wrapping around, like Find Next"), true,
        searchArgs({tab, required(QStringLiteral("text"), ArgType::String, QStringLiteral("what to find")),
                    optional(QStringLiteral("backwards"), ArgType::Bool, QStringLiteral("find the previous match"))}),
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e)
                return;
            Finder finder(e);
            finder.setSearchFlags(searchFlags(ctx));
            finder.setWrap(true);
            finder.setSearchText(ctx.str(QStringLiteral("text")));
            const Sci_CharacterRange r = ctx.flag(QStringLiteral("backwards")) ? finder.findPrev() : finder.findNext();
            if (r.cpMin == INVALID_POSITION) {
                ctx.ok({{QStringLiteral("found"), false}});
                return;
            }
            ctx.reveal(e);
            showRange(e, r.cpMin, r.cpMax);
            QJsonObject o = rangeJson(e, r.cpMin, r.cpMax);
            o.insert(QStringLiteral("found"), true);
            o.insert(QStringLiteral("wrapped"), finder.didLatestSearchWrapAround());
            ctx.ok(o);
        });

    add(QStringLiteral("editor.findAll"), QStringLiteral("List every match in an editor with its line text"), false,
        searchArgs({tab, required(QStringLiteral("text"), ArgType::String, QStringLiteral("what to find")),
                    optional(QStringLiteral("limit"), ArgType::Int, QStringLiteral("at most this many; default 1000"))}),
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e)
                return;
            const qint64 limit = std::clamp<qint64>(ctx.integer(QStringLiteral("limit"), 1000), 1, 100000);
            Finder finder(e);
            finder.setSearchFlags(searchFlags(ctx));
            finder.setSearchText(ctx.str(QStringLiteral("text")));
            QJsonArray matches;
            qint64 count = 0;
            finder.forEachMatch([&](int start, int end) {
                if (++count <= limit) {
                    QJsonObject m = rangeJson(e, start, end);
                    const sptr_t line = e->lineFromPosition(start);
                    m.insert(QStringLiteral("lineText"),
                             QString::fromUtf8(e->get_text_range(int(e->positionFromLine(line)),
                                                                 int(std::min<sptr_t>(e->lineEndPosition(line),
                                                                                      e->positionFromLine(line) + 400)))));
                    matches.append(m);
                }
                return end;
            });
            ctx.ok({{QStringLiteral("matches"), matches},
                    {QStringLiteral("count"), count},
                    {QStringLiteral("truncated"), count > limit}});
        });

    add(QStringLiteral("editor.replaceAll"), QStringLiteral("Replace every match in an editor (one undo step)"), true,
        searchArgs({tab, required(QStringLiteral("text"), ArgType::String, QStringLiteral("what to find")),
                    required(QStringLiteral("replacement"), ArgType::String, QStringLiteral("what to put instead"))}),
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e || !writable(ctx, e))
                return;
            Finder finder(e);
            finder.setSearchFlags(searchFlags(ctx));
            finder.setSearchText(ctx.str(QStringLiteral("text")));
            const int replaced = finder.replaceAll(ctx.str(QStringLiteral("replacement")));
            ctx.reveal(e);
            ctx.ok({{QStringLiteral("replaced"), replaced}, {QStringLiteral("modified"), e->modify()}});
        });

    add(QStringLiteral("editor.mark"),
        QStringLiteral("Highlight every match in an editor, optionally bookmarking their lines (Search > Mark)"), true,
        searchArgs({tab, required(QStringLiteral("text"), ArgType::String, QStringLiteral("what to mark")),
                    optional(QStringLiteral("bookmark"), ArgType::Bool,
                             QStringLiteral("also bookmark each matching line; default false")),
                    optional(QStringLiteral("clear"), ArgType::Bool,
                             QStringLiteral("clear earlier marks and bookmarks first; default true"))}),
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e)
                return;
            const int indicator = FindReplaceDialog::markIndicatorFor(e);
            e->setIndicatorCurrent(indicator);
            auto *bookmarks = e->findChild<BookMarkDecorator *>(QString(), Qt::FindDirectChildrenOnly);
            if (bookmarks && !bookmarks->isEnabled())
                bookmarks = nullptr;
            if (ctx.flag(QStringLiteral("clear"), true)) {
                e->indicatorClearRange(0, e->length());
                if (bookmarks)
                    bookmarks->clearAllBookmarks();
            }
            const bool bookmark = ctx.flag(QStringLiteral("bookmark"));
            if (bookmark && !bookmarks) {
                ctx.fail(409, QStringLiteral("bookmarks are turned off for this editor"));
                return;
            }
            Finder finder(e);
            finder.setSearchFlags(searchFlags(ctx));
            finder.setSearchText(ctx.str(QStringLiteral("text")));
            int count = 0;
            QSet<int> lines;
            finder.forEachMatch([&](int start, int end) {
                e->indicatorFillRange(start, end - start);
                ++count;
                const int line = int(e->lineFromPosition(start));
                lines.insert(line);
                if (bookmark && !bookmarks->isBookmarkSet(line))
                    bookmarks->addBookmark(line);
                return end;
            });
            ctx.reveal(e);
            ctx.ok({{QStringLiteral("marked"), count}, {QStringLiteral("lines"), int(lines.size())}});
        });

    add(QStringLiteral("editor.clearMarks"), QStringLiteral("Clear the marks and bookmarks in an editor (Search > Clear marks)"),
        true, {tab}, [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e)
                return;
            e->setIndicatorCurrent(FindReplaceDialog::markIndicatorFor(e));
            e->indicatorClearRange(0, e->length());
            if (auto *bookmarks = e->findChild<BookMarkDecorator *>(QString(), Qt::FindDirectChildrenOnly))
                bookmarks->clearAllBookmarks();
            ctx.reveal(e);
            ctx.ok();
        });

    add(QStringLiteral("editor.columnInsert"),
        QStringLiteral("Insert text or a number sequence at one column on a range of lines (Edit > Column editor)"), true,
        {tab, required(QStringLiteral("column"), ArgType::Int,
                       QStringLiteral("display column (1-based, tabs expanded); lines shorter than it are padded")),
         optional(QStringLiteral("line"), ArgType::Int, QStringLiteral("first line (1-based); default 1")),
         optional(QStringLiteral("endLine"), ArgType::Int, QStringLiteral("last line; default the last line")),
         optional(QStringLiteral("text"), ArgType::String, QStringLiteral("text to insert on every line")),
         optional(QStringLiteral("start"), ArgType::Int, QStringLiteral("instead of text: numbers from this value")),
         optional(QStringLiteral("step"), ArgType::Int, QStringLiteral("with start: the increment; default 1"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e || !writable(ctx, e))
                return;
            const bool numbers = ctx.has(QStringLiteral("start"));
            if (numbers == ctx.has(QStringLiteral("text"))) {
                ctx.fail(400, QStringLiteral("pass either `text` or `start` (with `step`)"));
                return;
            }
            const int lineCount = int(e->lineCount());
            const int first = int(ctx.integer(QStringLiteral("line"), 1));
            const int last = int(ctx.integer(QStringLiteral("endLine"), lineCount));
            const int column = int(ctx.integer(QStringLiteral("column")));
            if (first < 1 || last < first || last > lineCount || column < 1) {
                ctx.fail(400, QStringLiteral("lines must be within 1..%1 and column at least 1").arg(lineCount));
                return;
            }
            const QString text = ctx.str(QStringLiteral("text"));
            qint64 value = ctx.integer(QStringLiteral("start"));
            const qint64 step = ctx.integer(QStringLiteral("step"), 1);
            {
                const UndoAction ua(e);
                for (int line = first - 1; line < last; ++line) {
                    ColumnEditorDialog::insertTextAtColumn(e, line, column - 1, numbers ? QString::number(value) : text);
                    value += step;
                }
            }
            ctx.reveal(e);
            ctx.ok({{QStringLiteral("lines"), last - first + 1}, {QStringLiteral("modified"), e->modify()}});
        });

    // The Language Inspector panel's style table: live, for this editor only
    // (the language's Lua style file keeps the saved look).
    auto colorName = [](sptr_t bgr) {
        return QColor(int(bgr & 0xFF), int((bgr >> 8) & 0xFF), int((bgr >> 16) & 0xFF)).name();
    };
    add(QStringLiteral("editor.styles"), QStringLiteral("List an editor's lexer styles as the Language Inspector shows them"),
        false, {tab}, [w = w, colorName](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e)
                return;
            QJsonArray styles;
            const int count = int(e->namedStyles());
            for (int i = 0; i < count; ++i) {
                styles.append(QJsonObject{{QStringLiteral("style"), i},
                                          {QStringLiteral("name"), QString::fromUtf8(e->nameOfStyle(i))},
                                          {QStringLiteral("description"), QString::fromUtf8(e->descriptionOfStyle(i))},
                                          {QStringLiteral("font"), QString::fromUtf8(e->styleFont(i))},
                                          {QStringLiteral("size"), int(e->styleSize(i))},
                                          {QStringLiteral("bold"), e->styleBold(i)},
                                          {QStringLiteral("italic"), e->styleItalic(i)},
                                          {QStringLiteral("underline"), e->styleUnderline(i)},
                                          {QStringLiteral("fore"), colorName(e->styleFore(i))},
                                          {QStringLiteral("back"), colorName(e->styleBack(i))},
                                          {QStringLiteral("eolFilled"), e->styleEOLFilled(i)},
                                          {QStringLiteral("visible"), e->styleVisible(i)}});
            }
            ctx.ok({{QStringLiteral("language"), e->languageName}, {QStringLiteral("styles"), styles}});
        });

    add(QStringLiteral("editor.setStyle"),
        QStringLiteral("Change one lexer style of an editor, as editing it in the Language Inspector does (not saved)"), true,
        {tab, required(QStringLiteral("style"), ArgType::Int, QStringLiteral("style number from editor.styles")),
         optional(QStringLiteral("font"), ArgType::String, QStringLiteral("font family")),
         optional(QStringLiteral("size"), ArgType::Int, QStringLiteral("point size")),
         optional(QStringLiteral("bold"), ArgType::Bool, QStringLiteral("bold")),
         optional(QStringLiteral("italic"), ArgType::Bool, QStringLiteral("italic")),
         optional(QStringLiteral("underline"), ArgType::Bool, QStringLiteral("underline")),
         optional(QStringLiteral("fore"), ArgType::String, QStringLiteral("text colour, #RRGGBB")),
         optional(QStringLiteral("back"), ArgType::String, QStringLiteral("background colour, #RRGGBB")),
         optional(QStringLiteral("eolFilled"), ArgType::Bool, QStringLiteral("fill the background to the line end")),
         optional(QStringLiteral("visible"), ArgType::Bool, QStringLiteral("show text in this style"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e)
                return;
            const int style = int(ctx.integer(QStringLiteral("style")));
            if (style < 0 || style >= int(e->namedStyles())) {
                ctx.fail(404, QStringLiteral("no style %1; editor.styles lists them").arg(style));
                return;
            }
            auto bgr = [&ctx](const QString &arg, sptr_t *out) {
                const QColor c(ctx.str(arg));
                if (!c.isValid()) {
                    ctx.fail(400, QStringLiteral("%1 must be a colour like #RRGGBB").arg(arg));
                    return false;
                }
                *out = sptr_t(c.red()) | (sptr_t(c.green()) << 8) | (sptr_t(c.blue()) << 16);
                return true;
            };
            sptr_t fore = 0;
            sptr_t back = 0;
            if ((ctx.has(QStringLiteral("fore")) && !bgr(QStringLiteral("fore"), &fore))
                || (ctx.has(QStringLiteral("back")) && !bgr(QStringLiteral("back"), &back)))
                return;
            if (ctx.has(QStringLiteral("font")))
                e->styleSetFont(style, ctx.str(QStringLiteral("font")).toUtf8().constData());
            if (ctx.has(QStringLiteral("size")))
                e->styleSetSize(style, int(ctx.integer(QStringLiteral("size"))));
            if (ctx.has(QStringLiteral("bold")))
                e->styleSetBold(style, ctx.flag(QStringLiteral("bold")));
            if (ctx.has(QStringLiteral("italic")))
                e->styleSetItalic(style, ctx.flag(QStringLiteral("italic")));
            if (ctx.has(QStringLiteral("underline")))
                e->styleSetUnderline(style, ctx.flag(QStringLiteral("underline")));
            if (ctx.has(QStringLiteral("fore")))
                e->styleSetFore(style, fore);
            if (ctx.has(QStringLiteral("back")))
                e->styleSetBack(style, back);
            if (ctx.has(QStringLiteral("eolFilled")))
                e->styleSetEOLFilled(style, ctx.flag(QStringLiteral("eolFilled")));
            if (ctx.has(QStringLiteral("visible")))
                e->styleSetVisible(style, ctx.flag(QStringLiteral("visible")));
            ctx.reveal(e);
            ctx.ok({{QStringLiteral("style"), style}});
        });

    add(QStringLiteral("editor.languages"), QStringLiteral("List the language names editor.setLanguage accepts"), false, {},
        [w = w](const CommandContext &ctx) {
            ctx.ok({{QStringLiteral("languages"), QJsonArray::fromStringList(w->app->getLanguages())}});
        });

    add(QStringLiteral("editor.setLanguage"), QStringLiteral("Set an editor's language (highlighting, comments)"), true,
        {tab, required(QStringLiteral("language"), ArgType::String, QStringLiteral("a name from editor.languages"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e)
                return;
            const QString language = ctx.str(QStringLiteral("language"));
            if (!w->app->getLanguages().contains(language)) {
                ctx.fail(400, QStringLiteral("no language '%1'; editor.languages lists them").arg(language));
                return;
            }
            w->setLanguage(e, language);
            ctx.reveal(e);
            ctx.ok({{QStringLiteral("language"), e->languageName}});
        });

    add(QStringLiteral("editor.setEol"), QStringLiteral("Convert an editor's line endings"), true,
        {tab, oneOf(QStringLiteral("eol"), {QStringLiteral("crlf"), QStringLiteral("lf"), QStringLiteral("cr")},
                    QStringLiteral("crlf (Windows), lf (Unix) or cr (old Mac)"), true)},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *e = editorArg(ctx, w);
            if (!e || !writable(ctx, e))
                return;
            const QString eol = ctx.str(QStringLiteral("eol"));
            const int mode = eol == QLatin1String("crlf") ? SC_EOL_CRLF : eol == QLatin1String("cr") ? SC_EOL_CR : SC_EOL_LF;
            // MainWindow::convertEOLs works on the current editor and refreshes the status bar.
            w->getDockedEditor()->switchToEditor(e);
            w->convertEOLs(mode);
            ctx.reveal(e);
            ctx.ok({{QStringLiteral("eol"), eolName(int(e->eOLMode()))}});
        });
}

// === search.* ==================================================================

void Registrar::search()
{
    add(QStringLiteral("search.inFiles"),
        QStringLiteral("Search the files of a folder (default: the active workspace) and list the matches in the Find "
                       "in Folder panel"),
        true,
        searchArgs({required(QStringLiteral("text"), ArgType::String, QStringLiteral("what to find")),
                    optional(QStringLiteral("folder"), ArgType::String,
                             QStringLiteral("folder to search (absolute or relative to the active workspace); default the "
                                            "workspace root")),
                    optional(QStringLiteral("limit"), ArgType::Int,
                             QStringLiteral("return at most this many matches; default 500 (the panel lists all)"))}),
        [w = w](const CommandContext &ctx) {
            const QString root = w->currentWorkspaceRoot();
            const QString folder = ctx.has(QStringLiteral("folder")) ? pathArg(w, ctx.str(QStringLiteral("folder"))) : root;
            if (folder.isEmpty()) {
                ctx.fail(404, QStringLiteral("no workspace is open; pass `folder` or open one with workspace.open"));
                return;
            }
            if (remote::isSshUri(folder)) {
                ctx.fail(409, QStringLiteral("searching SSH folders is not supported"));
                return;
            }
            if (!QFileInfo(folder).isDir()) {
                ctx.fail(404, QStringLiteral("no folder %1").arg(folder));
                return;
            }
            FindInFolderDock *dock = w->findInFolderDock;
            dock->setFolder(folder, remote::isSshUri(root) ? QString() : root);
            const int limit = int(std::clamp<qint64>(ctx.integer(QStringLiteral("limit"), 500), 1, 100000));
            auto *wait = new QObject(dock);
            auto answer = [ctx, dock = QPointer<FindInFolderDock>(dock), wait, folder, limit](bool complete, int totalFiles,
                                                                                          int totalMatches) {
                wait->deleteLater();
                if (!dock) {
                    ctx.fail(410, QStringLiteral("the search panel closed"));
                    return;
                }
                QJsonArray files;
                int returned = 0;
                const QVector<FolderSearchFileBatch> results = dock->results(limit);
                for (const FolderSearchFileBatch &batch : results) {
                    QJsonArray matches;
                    for (const FolderSearchMatch &m : batch.matches) {
                        matches.append(QJsonObject{{QStringLiteral("line"), m.lineNumber + 1},
                                                   {QStringLiteral("column"), m.matchStart + 1},
                                                   {QStringLiteral("endColumn"), m.matchEnd + 1},
                                                   {QStringLiteral("lineText"), m.lineText.left(400)}});
                        ++returned;
                    }
                    files.append(QJsonObject{{QStringLiteral("path"), displayPath(batch.filePath)},
                                             {QStringLiteral("matches"), matches}});
                }
                QJsonObject o{{QStringLiteral("folder"), displayPath(folder)},
                              {QStringLiteral("files"), files},
                              {QStringLiteral("returned"), returned},
                              {QStringLiteral("totalFiles"), totalFiles},
                              {QStringLiteral("totalMatches"), totalMatches}};
                if (!complete)
                    o.insert(QStringLiteral("timedOut"), true);
                ctx.ok(o);
            };
            QObject::connect(dock, &FindInFolderDock::searchFinished, wait,
                             [answer](int totalFiles, int totalMatches) { answer(true, totalFiles, totalMatches); });
            QTimer::singleShot(120000, wait, [answer] { answer(false, -1, -1); });
            ctx.reveal(dock);
            dock->search(ctx.str(QStringLiteral("text")), ctx.flag(QStringLiteral("matchCase")),
                         ctx.flag(QStringLiteral("wholeWord")), ctx.flag(QStringLiteral("regex")));
        });
}

} // namespace control::commands
