/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QtTest>

#include "control/ControlCommands.h"
#include "control/ControlHttp.h"

using control::Arg;
using control::ArgType;
using control::Command;
using control::CommandContext;
using control::CommandRegistry;

namespace {

Arg arg(const QString &name, ArgType type, bool required, QStringList values = {})
{
    Arg a;
    a.name = name;
    a.type = type;
    a.required = required;
    a.description = name + QStringLiteral(" it is");
    a.values = std::move(values);
    return a;
}

Command openCommand()
{
    Command c;
    c.name = QStringLiteral("files.open");
    c.summary = QStringLiteral("Open a file in a tab");
    c.args = {arg(QStringLiteral("path"), ArgType::String, true), arg(QStringLiteral("line"), ArgType::Int, false),
              arg(QStringLiteral("preview"), ArgType::Bool, false),
              arg(QStringLiteral("unsaved"), ArgType::Enum, false, {QStringLiteral("save"), QStringLiteral("discard")})};
    c.handler = [](const CommandContext &ctx) { ctx.ok(); };
    return c;
}

QString validate(QJsonObject args)
{
    return CommandRegistry::validate(openCommand(), args);
}

} // namespace

class TestControlCommands : public QObject
{
    Q_OBJECT

private slots:
    void validArgumentsPass();
    void anUnknownArgumentNamesWhatTheCommandTakes();
    void aMissingRequiredArgumentIsNamed();
    void wrongTypesAreRefused();
    void anEnumTakesOnlyItsValues();
    void nullCountsAsAbsent();
    void getQueryValuesBecomeTheirTypes();
    void aContextAnswersOnce();
    void aHandlerThatKeepsNoCopyAndDoesNotAnswerIsAbandoned();
    void theReferenceListsEveryCommandByDomain();
};

void TestControlCommands::validArgumentsPass()
{
    QVERIFY(validate({{QStringLiteral("path"), QStringLiteral("a.txt")}}).isEmpty());
    QVERIFY(validate({{QStringLiteral("path"), QStringLiteral("a.txt")},
                      {QStringLiteral("line"), 12},
                      {QStringLiteral("preview"), true},
                      {QStringLiteral("unsaved"), QStringLiteral("discard")}})
                .isEmpty());
}

void TestControlCommands::anUnknownArgumentNamesWhatTheCommandTakes()
{
    const QString error = validate({{QStringLiteral("file"), QStringLiteral("a.txt")}});
    QVERIFY(error.contains(QStringLiteral("no argument \"file\"")));
    QVERIFY(error.contains(QStringLiteral("path*")));
    QVERIFY(error.contains(QStringLiteral("line")));
}

void TestControlCommands::aMissingRequiredArgumentIsNamed()
{
    const QString error = validate({});
    QVERIFY(error.contains(QStringLiteral("needs \"path\"")));
}

void TestControlCommands::wrongTypesAreRefused()
{
    QVERIFY(validate({{QStringLiteral("path"), 3}}).contains(QStringLiteral("a string")));
    QVERIFY(validate({{QStringLiteral("path"), QStringLiteral("a")}, {QStringLiteral("line"), 1.5}})
                .contains(QStringLiteral("an integer")));
    QVERIFY(validate({{QStringLiteral("path"), QStringLiteral("a")}, {QStringLiteral("preview"), QStringLiteral("yes")}})
                .contains(QStringLiteral("true or false")));
}

void TestControlCommands::anEnumTakesOnlyItsValues()
{
    const QString error =
        validate({{QStringLiteral("path"), QStringLiteral("a")}, {QStringLiteral("unsaved"), QStringLiteral("keep")}});
    QVERIFY(error.contains(QStringLiteral("one of: save, discard")));
}

void TestControlCommands::nullCountsAsAbsent()
{
    QJsonObject args{{QStringLiteral("path"), QStringLiteral("a")}, {QStringLiteral("line"), QJsonValue::Null}};
    QVERIFY(CommandRegistry::validate(openCommand(), args).isEmpty());
    QVERIFY(!args.contains(QStringLiteral("line")));
    QVERIFY(!validate({{QStringLiteral("path"), QJsonValue::Null}}).isEmpty());
}

void TestControlCommands::getQueryValuesBecomeTheirTypes()
{
    control::http::Request request;
    request.method = "GET";
    request.query = "path=C%3A%2Fsrc%2Fa.txt&line=40&preview=true&other=1";
    QJsonObject args = CommandRegistry::queryArgs(openCommand(), request);
    QCOMPARE(args.value(QStringLiteral("path")).toString(), QStringLiteral("C:/src/a.txt"));
    QCOMPARE(args.value(QStringLiteral("line")).toDouble(), 40.0);
    QCOMPARE(args.value(QStringLiteral("preview")).toBool(), true);
    // Undeclared query keys are not arguments.
    QVERIFY(!args.contains(QStringLiteral("other")));
    QVERIFY(CommandRegistry::validate(openCommand(), args).isEmpty());
}

void TestControlCommands::aContextAnswersOnce()
{
    const Command c = openCommand();
    int responses = 0;
    int lastStatus = 0;
    QJsonObject lastBody;
    QStringList outcomes;
    CommandContext::Hooks hooks;
    hooks.respond = [&](int status, const QJsonObject &body) {
        ++responses;
        lastStatus = status;
        lastBody = body;
    };
    hooks.finished = [&](bool ok, const QString &error) { outcomes.append(ok ? QStringLiteral("ok") : error); };
    const CommandContext ctx(&c, {{QStringLiteral("path"), QStringLiteral("a")}}, hooks);
    const CommandContext copy = ctx; // async work keeps a copy
    copy.fail(409, QStringLiteral("tab is busy"), {{QStringLiteral("tab"), 3}});
    ctx.ok({{QStringLiteral("tab"), 4}});
    QCOMPARE(responses, 1);
    QCOMPARE(lastStatus, 409);
    QCOMPARE(lastBody.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(lastBody.value(QStringLiteral("error")).toString(), QStringLiteral("tab is busy"));
    QCOMPARE(lastBody.value(QStringLiteral("tab")).toInt(), 3);
    QCOMPARE(outcomes, QStringList{QStringLiteral("tab is busy")});
}

void TestControlCommands::aHandlerThatKeepsNoCopyAndDoesNotAnswerIsAbandoned()
{
    const Command c = openCommand();
    const CommandContext ctx(&c, {}, {});
    QVERIFY(ctx.abandoned());
    {
        const CommandContext kept = ctx;
        QVERIFY(!ctx.abandoned());
    }
    ctx.ok();
    QVERIFY(!ctx.abandoned());
}

void TestControlCommands::theReferenceListsEveryCommandByDomain()
{
    CommandRegistry registry;
    registry.add(openCommand());
    Command read;
    read.name = QStringLiteral("ai.goal.state");
    read.summary = QStringLiteral("Read a goal");
    read.changes = false;
    read.handler = [](const CommandContext &ctx) { ctx.ok(); };
    registry.add(read);
    QCOMPARE(registry.find(QStringLiteral("files.open"))->summary, QStringLiteral("Open a file in a tab"));
    QVERIFY(!registry.find(QStringLiteral("files.close")));

    const QString ref = QString::fromUtf8(registry.reference());
    QVERIFY(ref.contains(QStringLiteral("### files")));
    QVERIFY(ref.contains(QStringLiteral("### ai.goal")));
    QVERIFY(ref.contains(QStringLiteral("- `ai.goal.state` (read): Read a goal")));
    QVERIFY(ref.contains(QStringLiteral("  - `path*` (string): path it is")));
    QVERIFY(ref.contains(QStringLiteral("  - `unsaved` (save|discard)")));
    // Domains are sorted, so the reference is stable.
    QVERIFY(ref.indexOf(QStringLiteral("### ai.goal")) < ref.indexOf(QStringLiteral("### files")));
}

QTEST_GUILESS_MAIN(TestControlCommands)
#include "test_control_commands.moc"
