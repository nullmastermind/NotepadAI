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
#include <QJsonArray>
#include <QJsonObject>

#include "AcpProtocol.h"

using AcpProtocol::AcpContentBlock;
using AcpProtocol::AcpPermissionOption;
using AcpProtocol::AcpPermissionRequest;

class TestAcpProtocolSerialization : public QObject
{
    Q_OBJECT

private slots:
    void textContentBlockRoundtrip();
    void imageContentBlockRoundtrip();
    void permissionOptionRoundtrip();
    void permissionRequestRoundtrip();
    void textContentBlockToChunkText();
    void resourceLinkContentBlockToChunkText();
    void resourceLinkFallsBackToUriWhenNameMissing();
    void resourceLinkFallsBackToNameWhenUriMissing();
    void resourceLinkFallsBackToTitleWhenNameMissing();
    void terminalOutputDeltaFromMeta();
    void stripTerminalContentBlocks();
    void appendToolCallTextDeltaAccumulates();
    void toolCallStatusForUiMapsOmpPending();
    void toolCallMutatedPathReadsOmpFields();
    void injectToolCallPathFromLocationsAndHashline();
    void ensureDiffContentSynthesizesWriteAndDetails();
    void ensureDiffContentAttachesCompactAndAcceptsFlatRawOutput();
    void preserveDiffBlocksKeepsPreviousWhenIncomingHasNone();
    void isGoalCommandNameMatchesGoalOnly();
    void ensureHostGoalCommandPrependsWhenMissing();
    void commandsIncludeGoalIgnoresHostInject();



};

void TestAcpProtocolSerialization::textContentBlockRoundtrip()
{
    AcpContentBlock b;
    b.kind = AcpContentBlock::Kind::Text;
    b.text = QStringLiteral("hello world");
    const QJsonObject json = AcpProtocol::contentBlockToJson(b);
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    AcpContentBlock decoded = AcpProtocol::contentBlockFromJson(json);
    QCOMPARE(int(decoded.kind), int(AcpContentBlock::Kind::Text));
    QCOMPARE(decoded.text, QStringLiteral("hello world"));
}

void TestAcpProtocolSerialization::imageContentBlockRoundtrip()
{
    AcpContentBlock b;
    b.kind = AcpContentBlock::Kind::Image;
    b.imageData = QByteArray("\x89PNG\r\n\x1a\n", 8);
    b.mimeType = QStringLiteral("image/png");
    const QJsonObject json = AcpProtocol::contentBlockToJson(b);
    QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("image"));
    QCOMPARE(json.value(QStringLiteral("mimeType")).toString(), QStringLiteral("image/png"));
    // base64 of the PNG signature.
    const QString b64 = QString::fromLatin1(b.imageData.toBase64());
    QCOMPARE(json.value(QStringLiteral("data")).toString(), b64);
    AcpContentBlock decoded = AcpProtocol::contentBlockFromJson(json);
    QCOMPARE(int(decoded.kind), int(AcpContentBlock::Kind::Image));
    QCOMPARE(decoded.imageData, b.imageData);
    QCOMPARE(decoded.mimeType, b.mimeType);
}

void TestAcpProtocolSerialization::permissionOptionRoundtrip()
{
    AcpPermissionOption o;
    o.id = QStringLiteral("opt-1");
    o.label = QStringLiteral("Allow");
    o.kind = QStringLiteral("allow_once");
    const QJsonObject json = AcpProtocol::permissionOptionToJson(o);
    AcpPermissionOption decoded = AcpProtocol::permissionOptionFromJson(json);
    QCOMPARE(decoded.id, o.id);
    QCOMPARE(decoded.label, o.label);
    QCOMPARE(decoded.kind, o.kind);
}

void TestAcpProtocolSerialization::permissionRequestRoundtrip()
{
    AcpPermissionRequest r;
    r.requestId = QStringLiteral("42");
    r.title = QStringLiteral("Run command?");
    r.description = QStringLiteral("rm -rf /");
    AcpPermissionOption a;
    a.id = QStringLiteral("a");
    a.label = QStringLiteral("Allow once");
    a.kind = QStringLiteral("allow_once");
    AcpPermissionOption d;
    d.id = QStringLiteral("d");
    d.label = QStringLiteral("Deny");
    d.kind = QStringLiteral("deny");
    r.options = {a, d};
    const QJsonObject json = AcpProtocol::permissionRequestToJson(r);
    AcpPermissionRequest decoded = AcpProtocol::permissionRequestFromJson(json);
    QCOMPARE(decoded.requestId, r.requestId);
    QCOMPARE(decoded.title, r.title);
    QCOMPARE(decoded.description, r.description);
    QCOMPARE(decoded.options.size(), 2);
    QCOMPARE(decoded.options.at(0).id, QStringLiteral("a"));
    QCOMPARE(decoded.options.at(0).kind, QStringLiteral("allow_once"));
    QCOMPARE(decoded.options.at(1).id, QStringLiteral("d"));
}

void TestAcpProtocolSerialization::textContentBlockToChunkText()
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("text"));
    o.insert(QStringLiteral("text"), QStringLiteral("hello"));
    QCOMPARE(AcpProtocol::contentBlockToChunkText(o), QStringLiteral("hello"));
}

void TestAcpProtocolSerialization::resourceLinkContentBlockToChunkText()
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("resource_link"));
    o.insert(QStringLiteral("name"), QStringLiteral("pi-session-abc.html"));
    o.insert(QStringLiteral("uri"), QStringLiteral("file:///tmp/pi-session-abc.html"));
    o.insert(QStringLiteral("mimeType"), QStringLiteral("text/html"));
    o.insert(QStringLiteral("title"), QStringLiteral("Session exported"));
    QCOMPARE(AcpProtocol::contentBlockToChunkText(o),
             QStringLiteral("[pi-session-abc.html](file:///tmp/pi-session-abc.html)"));
}

void TestAcpProtocolSerialization::resourceLinkFallsBackToUriWhenNameMissing()
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("resource_link"));
    o.insert(QStringLiteral("uri"), QStringLiteral("file:///tmp/out.html"));
    QCOMPARE(AcpProtocol::contentBlockToChunkText(o),
             QStringLiteral("file:///tmp/out.html"));
}

void TestAcpProtocolSerialization::resourceLinkFallsBackToNameWhenUriMissing()
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("resource_link"));
    o.insert(QStringLiteral("name"), QStringLiteral("out.html"));
    QCOMPARE(AcpProtocol::contentBlockToChunkText(o), QStringLiteral("out.html"));
}

void TestAcpProtocolSerialization::resourceLinkFallsBackToTitleWhenNameMissing()
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("resource_link"));
    o.insert(QStringLiteral("title"), QStringLiteral("Session exported"));
    o.insert(QStringLiteral("uri"), QStringLiteral("file:///tmp/out.html"));
    QCOMPARE(AcpProtocol::contentBlockToChunkText(o),
             QStringLiteral("[Session exported](file:///tmp/out.html)"));
}

void TestAcpProtocolSerialization::terminalOutputDeltaFromMeta()
{
    QJsonObject meta;
    QJsonObject output;
    output.insert(QStringLiteral("terminal_id"), QStringLiteral("t1"));
    output.insert(QStringLiteral("data"), QStringLiteral("hello"));
    meta.insert(QStringLiteral("terminal_output"), output);
    QCOMPARE(AcpProtocol::terminalOutputDeltaFromMeta(meta), QStringLiteral("hello"));
    QCOMPARE(AcpProtocol::terminalOutputDeltaFromMeta(QJsonObject()), QString());
}

void TestAcpProtocolSerialization::stripTerminalContentBlocks()
{
    QJsonObject term;
    term.insert(QStringLiteral("type"), QStringLiteral("terminal"));
    term.insert(QStringLiteral("terminalId"), QStringLiteral("t1"));
    QJsonObject text;
    text.insert(QStringLiteral("type"), QStringLiteral("text"));
    text.insert(QStringLiteral("text"), QStringLiteral("keep"));
    QJsonArray content;
    content.append(term);
    content.append(text);
    AcpProtocol::stripTerminalContentBlocks(content);
    QCOMPARE(content.size(), 1);
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("keep"));
}

void TestAcpProtocolSerialization::appendToolCallTextDeltaAccumulates()
{
    QJsonArray content;
    QJsonObject term;
    term.insert(QStringLiteral("type"), QStringLiteral("terminal"));
    content.append(term);
    AcpProtocol::appendToolCallTextDelta(content, QStringLiteral("foo"));
    AcpProtocol::appendToolCallTextDelta(content, QStringLiteral("bar"));
    QCOMPARE(content.size(), 1);
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("text"));
    QCOMPARE(content.at(0).toObject().value(QStringLiteral("text")).toString(),
             QStringLiteral("foobar"));
}

void TestAcpProtocolSerialization::toolCallStatusForUiMapsOmpPending()
{
    QCOMPARE(AcpProtocol::toolCallStatusForUi(QStringLiteral("pending")),
             QStringLiteral("running"));
    QCOMPARE(AcpProtocol::toolCallStatusForUi(QStringLiteral("in_progress")),
             QStringLiteral("running"));
    QCOMPARE(AcpProtocol::toolCallStatusForUi(QStringLiteral("running")),
             QStringLiteral("running"));
    QCOMPARE(AcpProtocol::toolCallStatusForUi(QStringLiteral("completed")),
             QStringLiteral("completed"));
    QCOMPARE(AcpProtocol::toolCallStatusForUi(QStringLiteral("failed")),
             QStringLiteral("failed"));
}

void TestAcpProtocolSerialization::toolCallMutatedPathReadsOmpFields()
{
    QJsonObject raw;
    raw.insert(QStringLiteral("path"), QStringLiteral("omp.txt"));
    QCOMPARE(AcpProtocol::toolCallMutatedPath(raw, QJsonArray(), QJsonObject()),
             QStringLiteral("omp.txt"));

    QJsonObject claude;
    claude.insert(QStringLiteral("file_path"), QStringLiteral("claude.cpp"));
    claude.insert(QStringLiteral("path"), QStringLiteral("omp.txt"));
    QCOMPARE(AcpProtocol::toolCallMutatedPath(claude, QJsonArray(), QJsonObject()),
             QStringLiteral("claude.cpp"));

    QJsonObject hash;
    hash.insert(QStringLiteral("input"), QStringLiteral("[conflict.txt#A1B2]\nPUT 1:"));
    QCOMPARE(AcpProtocol::toolCallMutatedPath(hash, QJsonArray(), QJsonObject()),
             QStringLiteral("conflict.txt"));

    QJsonArray locations;
    QJsonObject loc;
    loc.insert(QStringLiteral("path"), QStringLiteral("from-loc.ts"));
    locations.append(loc);
    QCOMPARE(AcpProtocol::toolCallMutatedPath(QJsonObject(), QJsonArray(), QJsonObject(), locations),
             QStringLiteral("from-loc.ts"));

    QJsonArray content;
    QJsonObject diff;
    diff.insert(QStringLiteral("type"), QStringLiteral("diff"));
    diff.insert(QStringLiteral("path"), QStringLiteral("from-diff.rs"));
    content.append(diff);
    QCOMPARE(AcpProtocol::toolCallMutatedPath(QJsonObject(), content, QJsonObject()),
             QStringLiteral("from-diff.rs"));

    QJsonObject details;
    details.insert(QStringLiteral("path"), QStringLiteral("from-details.md"));
    QJsonObject rawOut;
    rawOut.insert(QStringLiteral("details"), details);
    QCOMPARE(AcpProtocol::toolCallMutatedPath(QJsonObject(), QJsonArray(), rawOut),
             QStringLiteral("from-details.md"));
}

void TestAcpProtocolSerialization::injectToolCallPathFromLocationsAndHashline()
{
    QJsonObject raw;
    QJsonArray locations;
    QJsonObject loc;
    loc.insert(QStringLiteral("path"), QStringLiteral("loc.cpp"));
    locations.append(loc);
    AcpProtocol::injectToolCallPath(raw, locations);
    QCOMPARE(raw.value(QStringLiteral("path")).toString(), QStringLiteral("loc.cpp"));

    QJsonObject existing;
    existing.insert(QStringLiteral("file_path"), QStringLiteral("keep.cpp"));
    AcpProtocol::injectToolCallPath(existing, locations);
    QVERIFY(!existing.contains(QStringLiteral("path")));
    QCOMPARE(existing.value(QStringLiteral("file_path")).toString(),
             QStringLiteral("keep.cpp"));

    QJsonObject emptyPath;
    emptyPath.insert(QStringLiteral("path"), QString());
    AcpProtocol::injectToolCallPath(emptyPath, locations);
    QCOMPARE(emptyPath.value(QStringLiteral("path")).toString(), QStringLiteral("loc.cpp"));

    QJsonObject hash;
    hash.insert(QStringLiteral("input"), QStringLiteral("[probe.txt#dead]\nx"));
    AcpProtocol::injectToolCallPath(hash, QJsonArray());
    QCOMPARE(hash.value(QStringLiteral("path")).toString(), QStringLiteral("probe.txt"));
    QJsonObject nullPath;
    nullPath.insert(QStringLiteral("path"), QJsonValue::Null);
    AcpProtocol::injectToolCallPath(nullPath, locations);
    QCOMPARE(nullPath.value(QStringLiteral("path")).toString(), QStringLiteral("loc.cpp"));
}

void TestAcpProtocolSerialization::ensureDiffContentSynthesizesWriteAndDetails()
{
    QJsonObject raw;
    raw.insert(QStringLiteral("path"), QStringLiteral("new.txt"));
    raw.insert(QStringLiteral("content"), QStringLiteral("hello\n"));
    const QJsonArray synthesized =
        AcpProtocol::ensureDiffContent(QJsonArray(), raw, QJsonObject());
    QCOMPARE(synthesized.size(), 1);
    const QJsonObject writeDiff = synthesized.at(0).toObject();
    QCOMPARE(writeDiff.value(QStringLiteral("type")).toString(), QStringLiteral("diff"));
    QCOMPARE(writeDiff.value(QStringLiteral("path")).toString(), QStringLiteral("new.txt"));
    QVERIFY(writeDiff.value(QStringLiteral("oldText")).isNull());
    QCOMPARE(writeDiff.value(QStringLiteral("newText")).toString(), QStringLiteral("hello\n"));

    QJsonObject details;
    details.insert(QStringLiteral("path"), QStringLiteral("edit.txt"));
    details.insert(QStringLiteral("oldText"), QStringLiteral("a\n"));
    details.insert(QStringLiteral("newText"), QStringLiteral("b\n"));
    details.insert(QStringLiteral("diff"), QStringLiteral("@@ -1 +1 @@\n-a\n+b\n"));
    QJsonObject rawOut;
    rawOut.insert(QStringLiteral("details"), details);
    const QJsonArray fromDetails =
        AcpProtocol::ensureDiffContent(QJsonArray(), QJsonObject(), rawOut);
    QCOMPARE(fromDetails.size(), 1);
    const QJsonObject editDiff = fromDetails.at(0).toObject();
    QCOMPARE(editDiff.value(QStringLiteral("path")).toString(), QStringLiteral("edit.txt"));
    QCOMPARE(editDiff.value(QStringLiteral("oldText")).toString(), QStringLiteral("a\n"));
    QCOMPARE(editDiff.value(QStringLiteral("diff")).toString(),
             QStringLiteral("@@ -1 +1 @@\n-a\n+b\n"));

    QJsonArray already;
    already.append(writeDiff);
    const QJsonArray unchanged =
        AcpProtocol::ensureDiffContent(already, raw, QJsonObject());
    QCOMPARE(unchanged.size(), 1);
}

void TestAcpProtocolSerialization::ensureDiffContentAttachesCompactAndAcceptsFlatRawOutput()
{
    QJsonObject fullFile;
    fullFile.insert(QStringLiteral("type"), QStringLiteral("diff"));
    fullFile.insert(QStringLiteral("path"), QStringLiteral("big.cpp"));
    fullFile.insert(QStringLiteral("oldText"), QStringLiteral("a\nb\nc\n"));
    fullFile.insert(QStringLiteral("newText"), QStringLiteral("a\nB\nc\n"));
    QJsonArray content;
    content.append(fullFile);

    QJsonObject details;
    details.insert(QStringLiteral("diff"), QStringLiteral("@@ -1,3 +1,3 @@\n a\n-b\n+B\n c\n"));
    QJsonObject rawOut;
    rawOut.insert(QStringLiteral("details"), details);
    const QJsonArray attached = AcpProtocol::ensureDiffContent(content, QJsonObject(), rawOut);
    QCOMPARE(attached.size(), 1);
    QCOMPARE(attached.at(0).toObject().value(QStringLiteral("diff")).toString(),
             QStringLiteral("@@ -1,3 +1,3 @@\n a\n-b\n+B\n c\n"));

    QJsonObject flat;
    flat.insert(QStringLiteral("path"), QStringLiteral("flat.txt"));
    flat.insert(QStringLiteral("diff"), QStringLiteral("@@ -1 +1 @@\n-a\n+b\n"));
    const QJsonArray fromFlat =
        AcpProtocol::ensureDiffContent(QJsonArray(), QJsonObject(), flat);
    QCOMPARE(fromFlat.size(), 1);
    QCOMPARE(fromFlat.at(0).toObject().value(QStringLiteral("path")).toString(),
             QStringLiteral("flat.txt"));
    QCOMPARE(fromFlat.at(0).toObject().value(QStringLiteral("diff")).toString(),
             QStringLiteral("@@ -1 +1 @@\n-a\n+b\n"));

    QJsonObject skip;
    skip.insert(QStringLiteral("type"), QStringLiteral("diff"));
    skip.insert(QStringLiteral("path"), QStringLiteral("kept.cpp"));
    QJsonArray already;
    already.append(skip);
    const QJsonArray identity =
        AcpProtocol::ensureDiffContent(already, QJsonObject(), QJsonObject());
    QCOMPARE(identity.size(), 1);
    QVERIFY(!identity.at(0).toObject().contains(QStringLiteral("diff")));
}

void TestAcpProtocolSerialization::preserveDiffBlocksKeepsPreviousWhenIncomingHasNone()
{
    QJsonArray previous;
    QJsonObject diff;
    diff.insert(QStringLiteral("type"), QStringLiteral("diff"));
    diff.insert(QStringLiteral("path"), QStringLiteral("kept.txt"));
    previous.append(diff);

    QJsonArray incoming;
    QJsonObject text;
    text.insert(QStringLiteral("type"), QStringLiteral("text"));
    text.insert(QStringLiteral("text"), QStringLiteral("Wrote kept.txt"));
    incoming.append(text);

    const QJsonArray merged = AcpProtocol::preserveDiffBlocks(incoming, previous);
    QCOMPARE(merged.size(), 2);
    QCOMPARE(merged.at(1).toObject().value(QStringLiteral("path")).toString(),
             QStringLiteral("kept.txt"));

    const QJsonArray keepIncoming = AcpProtocol::preserveDiffBlocks(previous, incoming);
    QCOMPARE(keepIncoming.size(), 1);
    QCOMPARE(keepIncoming.at(0).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("diff"));
}


void TestAcpProtocolSerialization::isGoalCommandNameMatchesGoalOnly()
{
    QVERIFY(AcpProtocol::isGoalCommandName(QStringLiteral("goal")));
    QVERIFY(AcpProtocol::isGoalCommandName(QStringLiteral("/goal")));
    QVERIFY(AcpProtocol::isGoalCommandName(QStringLiteral("  /Goal  ")));
    QVERIFY(!AcpProtocol::isGoalCommandName(QStringLiteral("goals")));
    QVERIFY(!AcpProtocol::isGoalCommandName(QStringLiteral("/compact")));
    QVERIFY(!AcpProtocol::isGoalCommandName(QString()));
}

void TestAcpProtocolSerialization::ensureHostGoalCommandPrependsWhenMissing()
{
    QList<AcpProtocol::AcpCommandInfo> empty;
    AcpProtocol::ensureHostGoalCommand(empty);
    QCOMPARE(empty.size(), 1);
    QCOMPARE(empty.first().name, QStringLiteral("goal"));
    QCOMPARE(empty.first().description, QStringLiteral("Set a goal for this session"));
    QCOMPARE(empty.first().inputHint, QStringLiteral("criterion"));

    AcpProtocol::AcpCommandInfo compact;
    compact.name = QStringLiteral("compact");
    QList<AcpProtocol::AcpCommandInfo> cmds{compact};
    AcpProtocol::ensureHostGoalCommand(cmds);
    QCOMPARE(cmds.size(), 2);
    QCOMPARE(cmds.first().name, QStringLiteral("goal"));
    QCOMPARE(cmds.at(1).name, QStringLiteral("compact"));

    AcpProtocol::AcpCommandInfo agentGoal;
    agentGoal.name = QStringLiteral("/goal");
    agentGoal.description = QStringLiteral("agent copy");
    QList<AcpProtocol::AcpCommandInfo> already{agentGoal, compact};
    AcpProtocol::ensureHostGoalCommand(already);
    QCOMPARE(already.size(), 2);
    QCOMPARE(already.first().description, QStringLiteral("agent copy"));
}

void TestAcpProtocolSerialization::commandsIncludeGoalIgnoresHostInject()
{
    QList<AcpProtocol::AcpCommandInfo> empty;
    QVERIFY(!AcpProtocol::commandsIncludeGoal(empty));
    AcpProtocol::AcpCommandInfo compact;
    compact.name = QStringLiteral("compact");
    QVERIFY(!AcpProtocol::commandsIncludeGoal({compact}));
    AcpProtocol::AcpCommandInfo goal;
    goal.name = QStringLiteral("/goal");
    QVERIFY(AcpProtocol::commandsIncludeGoal({compact, goal}));
}


QTEST_GUILESS_MAIN(TestAcpProtocolSerialization)
#include "test_acp_protocol_serialization.moc"
