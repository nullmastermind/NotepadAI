/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// csv.* and hex.* (docs/adr/0007): the CSV preview grid and the hex preview,
// through the same functions their toolbars and context menus call.

#include "Registrar.h"

#include "CsvPreviewWidget.h"
#include "CsvTableModel.h"
#include "ScintillaNext.h"
#include "dialogs/MainWindow.h"
#include "preview/hex/HexFormat.h"
#include "preview/hex/HexPreviewWidget.h"
#include "preview/hex/HexSearch.h"
#include "preview/hex/HexSource.h"

#include "DockWidget.h"

#include <QFutureWatcher>
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent>

#include <algorithm>
#include <memory>

namespace control::commands {

namespace {

constexpr int kCsvReadMax = 5000;
constexpr int kCsvWaitMs = 120000;
constexpr qint64 kHexReadMax = 64 * 1024;

template <typename T>
T *contentOf(ads::CDockWidget *dock)
{
    QWidget *content = dock ? dock->widget() : nullptr;
    if (!content)
        return nullptr;
    if (auto *t = qobject_cast<T *>(content))
        return t;
    return content->findChild<T *>();
}

// The preview of type T in tab `tab` (answers the failure).
template <typename T>
T *previewArg(const CommandContext &ctx, MainWindow *w, const QString &what)
{
    const int id = int(ctx.integer(QStringLiteral("tab")));
    ads::CDockWidget *dock = Registrar::tabById(w, id);
    if (!dock) {
        ctx.fail(404, QStringLiteral("no tab %1; tabs.list lists them").arg(id));
        return nullptr;
    }
    T *preview = contentOf<T>(dock);
    if (!preview) {
        // A .csv opens as text, as for the user; its table is the Preview of it.
        const bool editor = qobject_cast<ScintillaNext *>(dock->widget()) != nullptr;
        ctx.fail(409, QStringLiteral("tab %1 is kind '%2', not %3%4")
                          .arg(id)
                          .arg(Registrar::tabKind(dock), what,
                               editor ? QStringLiteral("; preview.open on it opens the preview tab") : QString()));
    }
    return preview;
}

QString delimiterName(QChar d)
{
    return d == QLatin1Char('\t') ? QStringLiteral("tab") : QString(d);
}

QJsonObject csvStateJson(CsvPreviewWidget *csv)
{
    CsvTableModel *model = csv->tableModel();
    QJsonObject o{{QStringLiteral("path"), Registrar::displayPath(csv->filePath())},
                  {QStringLiteral("loading"), !model || csv->isIndexing()}};
    if (!model)
        return o;
    const bool readOnly = csv->refreshReadOnly();
    QJsonArray headers;
    for (int c = 0; c < model->columnCount(); ++c)
        headers.append(model->columnLabel(c));
    o.insert(QStringLiteral("delimiter"), delimiterName(csv->delimiter()));
    o.insert(QStringLiteral("header"), model->headerMode());
    o.insert(QStringLiteral("rows"), qint64(csv->viewRows()));
    o.insert(QStringLiteral("dataRows"), qint64(model->dataRowCount()));
    o.insert(QStringLiteral("columns"), model->columnCount());
    o.insert(QStringLiteral("headers"), headers);
    o.insert(QStringLiteral("encoding"), csv->encodingName());
    o.insert(QStringLiteral("modified"), csv->isModified());
    o.insert(QStringLiteral("readOnly"), readOnly);
    o.insert(QStringLiteral("sortFilterAvailable"), csv->sortFilterAvailable());
    if (csv->sortColumn() >= 0)
        o.insert(QStringLiteral("sort"),
                 QJsonObject{{QStringLiteral("column"), csv->sortColumn()},
                             {QStringLiteral("order"), csv->sortOrder() == Qt::AscendingOrder ? QStringLiteral("asc")
                                                                                              : QStringLiteral("desc")}});
    QJsonArray filters;
    const QStringList texts = csv->filters();
    for (qsizetype c = 0; c < texts.size(); ++c) {
        if (!texts.at(c).isEmpty())
            filters.append(QJsonObject{{QStringLiteral("column"), qint64(c)}, {QStringLiteral("text"), texts.at(c)}});
    }
    o.insert(QStringLiteral("filters"), filters);
    // Rows are in the displayed order while a sort or filter is active.
    o.insert(QStringLiteral("viewOrder"), model->viewActive());
    if (csv->viewRecomputing())
        o.insert(QStringLiteral("recomputing"), true);
    return o;
}

// Answers with the state once `csv` emits `signal` (or at once when `waiting`
// is false); a 504 after kCsvWaitMs.
template <typename Signal>
void answerWhen(const CommandContext &ctx, CsvPreviewWidget *csv, bool waiting, Signal signal, const QJsonObject &extra = {})
{
    auto answer = [ctx, csv = QPointer<CsvPreviewWidget>(csv), extra] {
        if (ctx.answered())
            return;
        if (!csv) {
            ctx.fail(404, QStringLiteral("the tab was closed"));
            return;
        }
        QJsonObject o = csvStateJson(csv);
        for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
            o.insert(it.key(), it.value());
        ctx.ok(o);
    };
    if (!waiting) {
        answer();
        return;
    }
    auto *guard = new QObject(csv);
    // Queued past the other slots on the same signal (the post-save overlay
    // clear runs on the index watcher's finish too).
    QObject::connect(csv, signal, guard, [answer, guard] {
        QTimer::singleShot(0, guard, [answer, guard] {
            answer();
            guard->deleteLater();
        });
    });
    QTimer::singleShot(kCsvWaitMs, guard, [ctx, guard] {
        if (!ctx.answered())
            ctx.fail(504, QStringLiteral("the grid did not finish in time; csv.state shows where it is"));
        guard->deleteLater();
    });
}

// Edits refuse while the grid is still loading.
bool csvReady(const CommandContext &ctx, CsvPreviewWidget *csv)
{
    if (csv->tableModel() && !csv->isIndexing())
        return true;
    ctx.fail(409, QStringLiteral("the file is still loading; wait until csv.state shows loading: false"));
    return false;
}

// Edit result: 409 with the reason, else the state.
void answerEdit(const CommandContext &ctx, CsvPreviewWidget *csv, const QString &error)
{
    if (!error.isEmpty()) {
        ctx.fail(409, error);
        return;
    }
    ctx.reveal(csv);
    ctx.ok(csvStateJson(csv));
}

QString printable(const QByteArray &bytes)
{
    QString out;
    out.reserve(bytes.size());
    for (const char c : bytes)
        out.append(c >= 0x20 && c < 0x7f ? QChar::fromLatin1(c) : QLatin1Char('.'));
    return out;
}

} // namespace

void Registrar::csv()
{
    const Arg csvTab = required(QStringLiteral("tab"), ArgType::Int, QStringLiteral("the CSV preview tab id (tabs.list)"));
    const QString csvWhat = QStringLiteral("a CSV preview");

    add(QStringLiteral("csv.state"),
        QStringLiteral("Read a CSV preview's shape: delimiter, header row, rows, columns, header names, sort, filters, "
                       "modified, read-only"),
        false, {csvTab}, [w = w, csvWhat](const CommandContext &ctx) {
            if (auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat))
                ctx.ok(csvStateJson(csv));
        });

    add(QStringLiteral("csv.read"),
        QStringLiteral("Read rows of a CSV preview as the grid shows them (sorted/filtered order when one is active)"),
        false,
        {csvTab, optional(QStringLiteral("row"), ArgType::Int, QStringLiteral("first row, 0-based; default 0")),
         optional(QStringLiteral("count"), ArgType::Int, QStringLiteral("how many rows; default 100, at most 5000")),
         optional(QStringLiteral("columns"), ArgType::Array, QStringLiteral("only these 0-based columns; default all"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            CsvTableModel *model = csv->tableModel();
            const quint64 rows = csv->viewRows();
            const quint64 from = quint64(std::max<qint64>(0, ctx.integer(QStringLiteral("row"))));
            const qint64 count = std::clamp<qint64>(ctx.integer(QStringLiteral("count"), 100), 0, kCsvReadMax);
            QList<int> columns;
            for (const QJsonValue &v : ctx.array(QStringLiteral("columns"))) {
                const int c = v.toInt(-1);
                if (c < 0 || c >= model->columnCount()) {
                    ctx.fail(400, QStringLiteral("there is no column %1 (%2 columns)").arg(c).arg(model->columnCount()));
                    return;
                }
                columns.append(c);
            }
            if (columns.isEmpty()) {
                for (int c = 0; c < model->columnCount(); ++c)
                    columns.append(c);
            }
            QJsonArray headers;
            for (int c : columns)
                headers.append(model->columnLabel(c));
            QJsonArray out;
            const quint64 end = std::min<quint64>(rows, from + quint64(count));
            for (quint64 r = from; r < end; ++r) {
                QJsonArray values;
                for (int c : columns)
                    values.append(csv->cellText(r, c));
                out.append(QJsonObject{{QStringLiteral("row"), qint64(r)}, {QStringLiteral("values"), values}});
            }
            ctx.ok({{QStringLiteral("rows"), out},
                    {QStringLiteral("headers"), headers},
                    {QStringLiteral("totalRows"), qint64(rows)},
                    {QStringLiteral("viewOrder"), model->viewActive()},
                    {QStringLiteral("more"), end < rows}});
        });

    add(QStringLiteral("csv.setCell"), QStringLiteral("Set a cell's text, as typing into it does (Ctrl+S / csv.save writes it)"),
        true,
        {csvTab, required(QStringLiteral("row"), ArgType::Int, QStringLiteral("row as csv.read numbers it")),
         required(QStringLiteral("column"), ArgType::Int, QStringLiteral("0-based column")),
         required(QStringLiteral("value"), ArgType::String, QStringLiteral("the new text"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            answerEdit(ctx, csv,
                       csv->setCell(quint64(std::max<qint64>(0, ctx.integer(QStringLiteral("row")))),
                                    int(ctx.integer(QStringLiteral("column"))), ctx.str(QStringLiteral("value"))));
        });

    add(QStringLiteral("csv.insertRows"),
        QStringLiteral("Insert blank rows, as the row header menu's Insert does (not while sorted/filtered)"), true,
        {csvTab, required(QStringLiteral("at"), ArgType::Int, QStringLiteral("0-based data row they go before; the row count appends")),
         optional(QStringLiteral("count"), ArgType::Int, QStringLiteral("how many; default 1, at most 10000"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            answerEdit(ctx, csv,
                       csv->insertRows(quint64(std::max<qint64>(0, ctx.integer(QStringLiteral("at")))),
                                       int(std::clamp<qint64>(ctx.integer(QStringLiteral("count"), 1), 1, 10000))));
        });

    add(QStringLiteral("csv.deleteRows"),
        QStringLiteral("Delete rows, as the row header menu's Delete does (one undo step per run; not while sorted/filtered)"),
        true, {csvTab, required(QStringLiteral("rows"), ArgType::Array, QStringLiteral("0-based data rows"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            QList<quint64> rows;
            for (const QJsonValue &v : ctx.array(QStringLiteral("rows"))) {
                if (!v.isDouble() || v.toDouble() < 0) {
                    ctx.fail(400, QStringLiteral("rows must be 0-based row numbers"));
                    return;
                }
                rows.append(quint64(v.toDouble()));
            }
            answerEdit(ctx, csv, csv->deleteRows(rows));
        });

    add(QStringLiteral("csv.insertColumns"),
        QStringLiteral("Insert blank columns, as the column header menu's Insert does (not while sorted/filtered)"), true,
        {csvTab,
         required(QStringLiteral("at"), ArgType::Int, QStringLiteral("0-based column they go before; the column count appends")),
         optional(QStringLiteral("count"), ArgType::Int, QStringLiteral("how many; default 1, at most 1000"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            answerEdit(ctx, csv,
                       csv->insertColumns(int(std::max<qint64>(0, ctx.integer(QStringLiteral("at")))),
                                          int(std::clamp<qint64>(ctx.integer(QStringLiteral("count"), 1), 1, 1000))));
        });

    add(QStringLiteral("csv.deleteColumns"),
        QStringLiteral("Delete columns, as the column header menu's Delete does (not while sorted/filtered)"), true,
        {csvTab, required(QStringLiteral("columns"), ArgType::Array, QStringLiteral("0-based columns"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            QList<int> cols;
            for (const QJsonValue &v : ctx.array(QStringLiteral("columns"))) {
                if (!v.isDouble() || v.toInt(-1) < 0) {
                    ctx.fail(400, QStringLiteral("columns must be 0-based column numbers"));
                    return;
                }
                cols.append(v.toInt());
            }
            answerEdit(ctx, csv, csv->deleteColumns(cols));
        });

    add(QStringLiteral("csv.renameColumn"),
        QStringLiteral("Change a column's header text, as double-clicking the header does (header row on)"), true,
        {csvTab, required(QStringLiteral("column"), ArgType::Int, QStringLiteral("0-based column")),
         required(QStringLiteral("name"), ArgType::String, QStringLiteral("the new header text"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            answerEdit(ctx, csv, csv->renameColumn(int(ctx.integer(QStringLiteral("column"))), ctx.str(QStringLiteral("name"))));
        });

    add(QStringLiteral("csv.clearCells"), QStringLiteral("Empty cells, as Delete on a selection does (one undo step)"), true,
        {csvTab, required(QStringLiteral("cells"), ArgType::Array,
                          QStringLiteral("[{row, column}] as csv.read numbers them"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            QList<QPair<quint64, int>> cells;
            for (const QJsonValue &v : ctx.array(QStringLiteral("cells"))) {
                const QJsonObject o = v.toObject();
                const qint64 row = qint64(o.value(QStringLiteral("row")).toDouble(-1));
                const int col = o.value(QStringLiteral("column")).toInt(-1);
                if (row < 0 || col < 0) {
                    ctx.fail(400, QStringLiteral("each cell is {row, column} with 0-based numbers"));
                    return;
                }
                cells.append({quint64(row), col});
            }
            answerEdit(ctx, csv, csv->clearCells(cells));
        });

    add(QStringLiteral("csv.sort"), QStringLiteral("Sort the grid by a column, as clicking its header does (none clears it)"),
        true,
        {csvTab, required(QStringLiteral("column"), ArgType::Int, QStringLiteral("0-based column")),
         oneOf(QStringLiteral("order"), {QStringLiteral("asc"), QStringLiteral("desc"), QStringLiteral("none")},
               QStringLiteral("asc, desc, or none to clear the sort"), true)},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            const QString order = ctx.str(QStringLiteral("order"));
            if (const QString error = csv->sortBy(int(ctx.integer(QStringLiteral("column"))), order != QLatin1String("none"),
                                                  order == QLatin1String("desc") ? Qt::DescendingOrder : Qt::AscendingOrder);
                !error.isEmpty()) {
                ctx.fail(409, error);
                return;
            }
            ctx.reveal(csv);
            answerWhen(ctx, csv, csv->viewRecomputing(), &CsvPreviewWidget::viewRecomputed);
        });

    add(QStringLiteral("csv.filter"),
        QStringLiteral("Filter rows by a column's text, as typing into its filter field does (\"\" clears it)"), true,
        {csvTab, optional(QStringLiteral("column"), ArgType::Int, QStringLiteral("0-based column to filter")),
         optional(QStringLiteral("text"), ArgType::String,
                  QStringLiteral("rows whose cell contains this text stay (case-insensitive); \"\" clears the column's filter")),
         optional(QStringLiteral("clearAll"), ArgType::Bool, QStringLiteral("clear every filter instead (the sort stays)"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            if (ctx.flag(QStringLiteral("clearAll"))) {
                csv->clearFilters();
            } else {
                if (!ctx.has(QStringLiteral("column")) || !ctx.has(QStringLiteral("text"))) {
                    ctx.fail(400, QStringLiteral("pass column and text, or clearAll: true"));
                    return;
                }
                if (const QString error = csv->setFilter(int(ctx.integer(QStringLiteral("column"))), ctx.str(QStringLiteral("text")));
                    !error.isEmpty()) {
                    ctx.fail(409, error);
                    return;
                }
            }
            ctx.reveal(csv);
            answerWhen(ctx, csv, csv->viewRecomputing(), &CsvPreviewWidget::viewRecomputed);
        });

    add(QStringLiteral("csv.setDelimiter"),
        QStringLiteral("Re-read the file with another delimiter, as the Delimiter box does (pending edits are dropped)"), true,
        {csvTab,
         oneOf(QStringLiteral("delimiter"), {QStringLiteral(","), QStringLiteral(";"), QStringLiteral("tab"), QStringLiteral("|")},
               QStringLiteral("the field separator"), true),
         oneOf(QStringLiteral("unsaved"), {QStringLiteral("discard")},
               QStringLiteral("required when the grid has unsaved edits: they are dropped"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            if (csv->isModified() && !ctx.has(QStringLiteral("unsaved"))) {
                ctx.fail(409, QStringLiteral("the grid has unsaved edits a new delimiter drops; save them with csv.save "
                                             "or pass unsaved: \"discard\""),
                         {{QStringLiteral("needs"), QStringLiteral("unsaved")}});
                return;
            }
            const QString name = ctx.str(QStringLiteral("delimiter"));
            const QChar d = name == QLatin1String("tab") ? QLatin1Char('\t') : name.at(0);
            if (d == csv->delimiter()) {
                ctx.ok(csvStateJson(csv));
                return;
            }
            if (const QString error = csv->setDelimiterChar(d); !error.isEmpty()) {
                ctx.fail(409, error);
                return;
            }
            ctx.reveal(csv);
            answerWhen(ctx, csv, true, &CsvPreviewWidget::indexed);
        });

    add(QStringLiteral("csv.setHeader"), QStringLiteral("Turn \"First row as header\" on or off"), true,
        {csvTab, required(QStringLiteral("on"), ArgType::Bool, QStringLiteral("true: the first row names the columns"))},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            csv->setHeaderRow(ctx.flag(QStringLiteral("on")));
            ctx.reveal(csv);
            ctx.ok(csvStateJson(csv));
        });

    add(QStringLiteral("csv.save"), QStringLiteral("Write the grid's edits to the file, as Ctrl+S does"), true, {csvTab},
        [w = w, csvWhat](const CommandContext &ctx) {
            auto *csv = previewArg<CsvPreviewWidget>(ctx, w, csvWhat);
            if (!csv || !csvReady(ctx, csv))
                return;
            if (!csv->isModified()) {
                QJsonObject o = csvStateJson(csv);
                o.insert(QStringLiteral("saved"), false);
                ctx.ok(o);
                return;
            }
            if (csv->refreshReadOnly()) {
                ctx.fail(409, QStringLiteral("the file is open in an editor with unsaved changes; save or revert that "
                                             "editor first (files.save / files.reload)"));
                return;
            }
            if (const QString error = csv->save(); !error.isEmpty()) {
                ctx.fail(422, error);
                return;
            }
            ctx.reveal(csv);
            // The save re-indexes the file; the edits clear once it lands.
            answerWhen(ctx, csv, true, &CsvPreviewWidget::indexed, {{QStringLiteral("saved"), true}});
        });

    // --- hex.* ------------------------------------------------------------------
    const Arg hexTab = required(QStringLiteral("tab"), ArgType::Int, QStringLiteral("the hex preview tab id (tabs.list)"));
    const QString hexWhat = QStringLiteral("a hex preview");

    add(QStringLiteral("hex.read"), QStringLiteral("Read bytes of a hex preview's file"), false,
        {hexTab, optional(QStringLiteral("offset"), ArgType::Int, QStringLiteral("first byte, 0-based; default 0")),
         optional(QStringLiteral("length"), ArgType::Int, QStringLiteral("how many bytes; default 256, at most 65536"))},
        [w = w, hexWhat](const CommandContext &ctx) {
            auto *hex = previewArg<HexPreviewWidget>(ctx, w, hexWhat);
            if (!hex)
                return;
            const qint64 size = hex->byteSize();
            const qint64 offset = std::clamp<qint64>(ctx.integer(QStringLiteral("offset")), 0, size);
            const qint64 length = std::clamp<qint64>(ctx.integer(QStringLiteral("length"), 256), 0, kHexReadMax);
            QByteArray bytes(qsizetype(std::min(length, size - offset)), Qt::Uninitialized);
            // Open, read, close: the tab never holds the file (docs: asset previews).
            const qint64 got = bytes.isEmpty() ? 0 : HexSource::fileReader(hex->filePath())(offset, bytes.data(), bytes.size());
            if (got < 0) {
                ctx.fail(422, QStringLiteral("the file could not be read"));
                return;
            }
            bytes.truncate(qsizetype(got));
            ctx.ok({{QStringLiteral("offset"), offset},
                    {QStringLiteral("length"), qint64(bytes.size())},
                    {QStringLiteral("size"), size},
                    {QStringLiteral("hex"), QString::fromLatin1(bytes.toHex(' '))},
                    {QStringLiteral("text"), printable(bytes)}});
        });

    add(QStringLiteral("hex.find"),
        QStringLiteral("Find bytes or text in a hex preview's file and select the match, as its Find box does (wraps around)"),
        true,
        {hexTab, optional(QStringLiteral("bytes"), ArgType::String, QStringLiteral("hex bytes, e.g. \"4D 5A 90\"")),
         optional(QStringLiteral("text"), ArgType::String, QStringLiteral("instead: UTF-8 text")),
         optional(QStringLiteral("from"), ArgType::Int, QStringLiteral("search from this offset; default 0")),
         optional(QStringLiteral("backward"), ArgType::Bool, QStringLiteral("search towards the start; default false"))},
        [w = w, hexWhat](const CommandContext &ctx) {
            auto *hex = previewArg<HexPreviewWidget>(ctx, w, hexWhat);
            if (!hex)
                return;
            const bool isHex = ctx.has(QStringLiteral("bytes"));
            if (isHex == ctx.has(QStringLiteral("text"))) {
                ctx.fail(400, QStringLiteral("pass bytes or text"));
                return;
            }
            QByteArray pattern;
            QString error;
            if (!HexFormat::parsePattern(ctx.str(isHex ? QStringLiteral("bytes") : QStringLiteral("text")), isHex, &pattern, &error)) {
                ctx.fail(400, error);
                return;
            }
            const qint64 size = hex->byteSize();
            const qint64 from = std::clamp<qint64>(ctx.integer(QStringLiteral("from")), 0, size);
            const bool backward = ctx.flag(QStringLiteral("backward"));
            const HexSource::Reader read = HexSource::fileReader(hex->filePath());
            auto *watcher = new QFutureWatcher<std::pair<qint64, bool>>(hex);
            QObject::connect(watcher, &QFutureWatcher<std::pair<qint64, bool>>::finished, hex,
                             [ctx, watcher, hex = QPointer<HexPreviewWidget>(hex), length = pattern.size()] {
                                 watcher->deleteLater();
                                 const auto [offset, wrapped] = watcher->result();
                                 if (!hex) {
                                     ctx.fail(404, QStringLiteral("the tab was closed"));
                                 } else if (offset == HexSearch::kReadError) {
                                     ctx.fail(422, QStringLiteral("the file could not be read"));
                                 } else if (offset < 0) {
                                     ctx.fail(404, QStringLiteral("not found"));
                                 } else {
                                     ctx.reveal(hex);
                                     hex->selectRange(offset, length);
                                     ctx.ok({{QStringLiteral("offset"), offset},
                                             {QStringLiteral("length"), qint64(length)},
                                             {QStringLiteral("wrapped"), wrapped}});
                                 }
                             });
            // The Find box's search: from `from` to the end (or start), then the rest.
            watcher->setFuture(QtConcurrent::run([read, size, pattern, from, backward]() -> std::pair<qint64, bool> {
                const std::atomic_bool cancel{false};
                qint64 r = backward ? HexSearch::find(read, size, pattern, 0, from, true, cancel)
                                    : HexSearch::find(read, size, pattern, from, size, false, cancel);
                if (r != HexSearch::kNotFound)
                    return {r, false};
                r = backward ? HexSearch::find(read, size, pattern, from, size, true, cancel)
                             : HexSearch::find(read, size, pattern, 0, from, false, cancel);
                return {r, true};
            }));
        });

    add(QStringLiteral("hex.goto"), QStringLiteral("Scroll a hex preview to an offset and select bytes there, as Go to does"),
        true,
        {hexTab, required(QStringLiteral("offset"), ArgType::Int, QStringLiteral("0-based byte offset")),
         optional(QStringLiteral("length"), ArgType::Int, QStringLiteral("bytes to select; default 1"))},
        [w = w, hexWhat](const CommandContext &ctx) {
            auto *hex = previewArg<HexPreviewWidget>(ctx, w, hexWhat);
            if (!hex)
                return;
            const qint64 size = hex->byteSize();
            if (size == 0) {
                ctx.fail(409, QStringLiteral("the file is empty"));
                return;
            }
            const qint64 offset = std::clamp<qint64>(ctx.integer(QStringLiteral("offset")), 0, size - 1);
            const qint64 length = std::clamp<qint64>(ctx.integer(QStringLiteral("length"), 1), 1, size - offset);
            ctx.reveal(hex);
            hex->selectRange(offset, length);
            ctx.ok({{QStringLiteral("offset"), offset}, {QStringLiteral("length"), length}, {QStringLiteral("size"), size}});
        });
}

} // namespace control::commands
