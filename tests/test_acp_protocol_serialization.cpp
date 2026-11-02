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
#include <QJsonDocument>
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
    void subagentParentToolCallIdReadsMetaKeys();
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
    void rpcErrorIsSessionBusy_alreadyProcessingMessage();
    void rpcErrorIsSessionBusy_typedReason();
    void rpcErrorIsSessionBusy_otherErrors();
    void rpcErrorIsIncompleteServerToolCall_serverToolsOnly();
    void stripPiAcpQueueNotices_dropsStatusKeepsBody();
    void toolCallStatusIsTerminal_matchesFinalStatesOnly();
    void asyncTaskEvent_spawnedFromWire();
    void asyncTaskEvent_progressAndStateFromWire();
    void asyncTaskEvent_ignoresOtherKinds();
    void asyncTaskStateIsLive_runningAndPausedOnly();
    void toolCallUpdateIsBackgrounded_readsAirMarker();
    void toolCallUpdateIsAsyncSubagentLaunch_readsToolResponse();
    void clientCapabilitiesMeta_declaresAsyncTasks();
    void formatWaitDuration_secondsMinutesHours();
    void formatToolDuration_compactUnits();
    void rawOutputToObject_wrapsNonObjects();
    void commandsIncludeAdvisor_matchesNameOnly();
    void advisorModelFor_stepsUpOneLevel();
    void advisorModelFor_readsFamilyFromValueOrLabel();
    void advisorCommandText_onAndOff();
    void advisorIsToggle_readsTheHint();
    void advisorCommandText_followsTheAdvertisedForm();
    void parseAdvisorCommand_readsTypedToggle();
    void parseAgentCapabilities_specKeysAndLegacyFallback();


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

void TestAcpProtocolSerialization::subagentParentToolCallIdReadsMetaKeys()
{
    auto updateWith = [](const QJsonObject &meta, const QString &toolCallId = {}) {
        QJsonObject u;
        u.insert(QStringLiteral("sessionUpdate"), QStringLiteral("tool_call"));
        if (!toolCallId.isEmpty())
            u.insert(QStringLiteral("toolCallId"), toolCallId);
        u.insert(QStringLiteral("_meta"), meta);
        return u;
    };
    // claude-agent-acp
    const QJsonObject claude{{QStringLiteral("claudeCode"),
                              QJsonObject{{QStringLiteral("parentToolUseId"), QStringLiteral("toolu_A")}}}};
    QCOMPARE(AcpProtocol::subagentParentToolCallId(updateWith(claude, QStringLiteral("toolu_B"))),
             QStringLiteral("toolu_A"));
    // Generic key wins over the Claude one.
    QJsonObject both = claude;
    both.insert(QStringLiteral("parentToolCallId"), QStringLiteral("p1"));
    QCOMPARE(AcpProtocol::subagentParentToolCallId(updateWith(both)), QStringLiteral("p1"));
    QCOMPARE(AcpProtocol::subagentParentToolCallId(
                 updateWith(QJsonObject{{QStringLiteral("parentToolUseId"), QStringLiteral("p2")}})),
             QStringLiteral("p2"));
    // The main agent: no meta, or meta without a parent.
    QCOMPARE(AcpProtocol::subagentParentToolCallId(QJsonObject{}), QString());
    QCOMPARE(AcpProtocol::subagentParentToolCallId(
                 updateWith(QJsonObject{{QStringLiteral("claudeCode"),
                                         QJsonObject{{QStringLiteral("toolName"), QStringLiteral("Bash")}}}})),
             QString());
    // A call never parents itself.
    QCOMPARE(AcpProtocol::subagentParentToolCallId(updateWith(claude, QStringLiteral("toolu_A"))),
             QString());
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

    // omp's xd:// device write is a tool call, not a file: no diff.
    QJsonObject device;
    device.insert(QStringLiteral("path"), QStringLiteral("xd://mcp__grill_me_ask"));
    device.insert(QStringLiteral("content"), QStringLiteral("{\"questions\":[]}"));
    QVERIFY(AcpProtocol::ensureDiffContent(QJsonArray(), device, QJsonObject()).isEmpty());

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

void TestAcpProtocolSerialization::rpcErrorIsSessionBusy_alreadyProcessingMessage()
{
    QJsonObject err;
    err.insert(QStringLiteral("code"), -32603);
    err.insert(QStringLiteral("message"),
               QStringLiteral("Agent is already processing. Use steer() or followUp() "
                              "to queue messages, or wait for completion."));
    QVERIFY(AcpProtocol::rpcErrorIsSessionBusy(err));
}

void TestAcpProtocolSerialization::rpcErrorIsSessionBusy_typedReason()
{
    QJsonObject data;
    data.insert(QStringLiteral("reason"), QStringLiteral("session_busy"));
    data.insert(QStringLiteral("hint"), QStringLiteral("steer|followUp|wait"));
    QJsonObject err;
    err.insert(QStringLiteral("code"), -32003);
    err.insert(QStringLiteral("message"), QStringLiteral("Agent is already processing."));
    err.insert(QStringLiteral("data"), data);
    QVERIFY(AcpProtocol::rpcErrorIsSessionBusy(err));
}

void TestAcpProtocolSerialization::rpcErrorIsSessionBusy_otherErrors()
{
    QJsonObject err;
    err.insert(QStringLiteral("code"), -32603);
    err.insert(QStringLiteral("message"), QStringLiteral("Session not found"));
    QVERIFY(!AcpProtocol::rpcErrorIsSessionBusy(err));
    QVERIFY(!AcpProtocol::rpcErrorIsSessionBusy(QJsonValue()));
}

void TestAcpProtocolSerialization::formatToolDuration_compactUnits()
{
    QCOMPARE(AcpProtocol::formatToolDuration(-5), QStringLiteral("<1s"));
    QCOMPARE(AcpProtocol::formatToolDuration(800), QStringLiteral("<1s"));
    QCOMPARE(AcpProtocol::formatToolDuration(40000), QStringLiteral("40s"));
    QCOMPARE(AcpProtocol::formatToolDuration(65000), QStringLiteral("1m5s"));
    QCOMPARE(AcpProtocol::formatToolDuration(3665000), QStringLiteral("1h1m5s"));
}

void TestAcpProtocolSerialization::commandsIncludeAdvisor_matchesNameOnly()
{
    QVERIFY(!AcpProtocol::commandsIncludeAdvisor({}));
    AcpProtocol::AcpCommandInfo compact;
    compact.name = QStringLiteral("compact");
    compact.description = QStringLiteral("consult an advisor");
    QVERIFY(!AcpProtocol::commandsIncludeAdvisor({compact}));
    AcpProtocol::AcpCommandInfo adv;
    adv.name = QStringLiteral("/Advisor");
    QVERIFY(AcpProtocol::commandsIncludeAdvisor({compact, adv}));
    adv.name = QStringLiteral(" advisor ");
    QVERIFY(AcpProtocol::commandsIncludeAdvisor({adv}));
}

void TestAcpProtocolSerialization::advisorModelFor_stepsUpOneLevel()
{
    QCOMPARE(AcpProtocol::advisorModelFor(QStringLiteral("haiku")), QStringLiteral("sonnet"));
    QCOMPARE(AcpProtocol::advisorModelFor(QStringLiteral("sonnet")), QStringLiteral("opus"));
    QCOMPARE(AcpProtocol::advisorModelFor(QStringLiteral("opus")), QStringLiteral("fable"));
    QCOMPARE(AcpProtocol::advisorModelFor(QStringLiteral("fable")), QStringLiteral("fable"));
}

void TestAcpProtocolSerialization::advisorModelFor_readsFamilyFromValueOrLabel()
{
    // Pinned full ids carry the family in the value.
    QCOMPARE(AcpProtocol::advisorModelFor(QStringLiteral("claude-haiku-4-5-20251001")),
             QStringLiteral("sonnet"));
    QCOMPARE(AcpProtocol::advisorModelFor(QStringLiteral("claude-opus-4-8")),
             QStringLiteral("fable"));
    QCOMPARE(AcpProtocol::advisorModelFor(QStringLiteral("claude-fable-5")),
             QStringLiteral("fable"));
    // "default" has no family in its value; the label may.
    QCOMPARE(AcpProtocol::advisorModelFor(QStringLiteral("default"), QStringLiteral("Sonnet 5.5")),
             QStringLiteral("opus"));
    // Case-insensitive.
    QCOMPARE(AcpProtocol::advisorModelFor(QStringLiteral("X"), QStringLiteral("HAIKU 4.5")),
             QStringLiteral("sonnet"));
    // Unknown family: opus, the common advisor.
    QCOMPARE(AcpProtocol::advisorModelFor(QStringLiteral("default"),
                                          QStringLiteral("Default (recommended)")),
             QStringLiteral("opus"));
    QCOMPARE(AcpProtocol::advisorModelFor(QString()), QStringLiteral("opus"));
}

void TestAcpProtocolSerialization::advisorCommandText_onAndOff()
{
    QCOMPARE(AcpProtocol::advisorCommandText(true, QStringLiteral("sonnet")),
             QStringLiteral("/advisor opus"));
    QCOMPARE(AcpProtocol::advisorCommandText(true, QStringLiteral("fable")),
             QStringLiteral("/advisor fable"));
    QCOMPARE(AcpProtocol::advisorCommandText(false, QStringLiteral("sonnet")),
             QStringLiteral("/advisor off"));
}

namespace {
// `/advisor` as each agent advertises it (captured 2026-11-02).
AcpProtocol::AcpCommandInfo advisorCommand(const QString &hint)
{
    return {QStringLiteral("advisor"), QStringLiteral("advisor"), hint};
}
const QString kClaudeAdvisorHint = QStringLiteral("[fable|opus|sonnet|off]"); // claude-agent-acp
const QString kOmpAdvisorHint = QStringLiteral("[on|off|status|dump [raw]|configure]"); // omp 18.15
} // namespace

void TestAcpProtocolSerialization::advisorIsToggle_readsTheHint()
{
    QVERIFY(AcpProtocol::advisorIsToggle({advisorCommand(kOmpAdvisorHint)}));
    QVERIFY(!AcpProtocol::advisorIsToggle({advisorCommand(kClaudeAdvisorHint)}));
    QVERIFY(!AcpProtocol::advisorIsToggle({advisorCommand(QString())}));
    QVERIFY(!AcpProtocol::advisorIsToggle({}));
    // "on" must be an alternative of its own, not part of a word.
    QVERIFY(!AcpProtocol::advisorIsToggle({advisorCommand(QStringLiteral("[sonnet|off]"))}));
}

void TestAcpProtocolSerialization::advisorCommandText_followsTheAdvertisedForm()
{
    const QList<AcpProtocol::AcpCommandInfo> omp{advisorCommand(kOmpAdvisorHint)};
    const QList<AcpProtocol::AcpCommandInfo> claude{advisorCommand(kClaudeAdvisorHint)};
    QCOMPARE(AcpProtocol::advisorCommandText(true, omp, QStringLiteral("local-ccr/composer")),
             QStringLiteral("/advisor on"));
    QCOMPARE(AcpProtocol::advisorCommandText(false, omp, QStringLiteral("local-ccr/composer")),
             QStringLiteral("/advisor off"));
    QCOMPARE(AcpProtocol::advisorCommandText(true, claude, QStringLiteral("sonnet")),
             QStringLiteral("/advisor opus"));
    QCOMPARE(AcpProtocol::advisorCommandText(false, claude, QStringLiteral("sonnet")),
             QStringLiteral("/advisor off"));
}

void TestAcpProtocolSerialization::parseAdvisorCommand_readsTypedToggle()
{
    QCOMPARE(AcpProtocol::parseAdvisorCommand(QStringLiteral("/advisor opus")),
             std::optional<bool>(true));
    QCOMPARE(AcpProtocol::parseAdvisorCommand(QStringLiteral("  /Advisor   fable ")),
             std::optional<bool>(true));
    QCOMPARE(AcpProtocol::parseAdvisorCommand(QStringLiteral("/advisor off")),
             std::optional<bool>(false));
    QCOMPARE(AcpProtocol::parseAdvisorCommand(QStringLiteral("/advisor OFF")),
             std::optional<bool>(false));
    // Status query, lookalikes and ordinary prompts leave the toggle alone.
    QCOMPARE(AcpProtocol::parseAdvisorCommand(QStringLiteral("/advisor on")),
             std::optional<bool>(true));
    QVERIFY(!AcpProtocol::parseAdvisorCommand(QStringLiteral("/advisor")).has_value());
    // omp's queries and setup do not toggle it.
    QVERIFY(!AcpProtocol::parseAdvisorCommand(QStringLiteral("/advisor status")).has_value());
    QVERIFY(!AcpProtocol::parseAdvisorCommand(QStringLiteral("/advisor dump raw")).has_value());
    QVERIFY(!AcpProtocol::parseAdvisorCommand(QStringLiteral("/advisor configure")).has_value());
    QVERIFY(!AcpProtocol::parseAdvisorCommand(QStringLiteral("/advisors opus")).has_value());
    QVERIFY(!AcpProtocol::parseAdvisorCommand(QStringLiteral("use /advisor off")).has_value());
    QVERIFY(!AcpProtocol::parseAdvisorCommand(QString()).has_value());
}

void TestAcpProtocolSerialization::rawOutputToObject_wrapsNonObjects()
{
    const QJsonObject s = AcpProtocol::rawOutputToObject(
        QJsonValue(QStringLiteral("(Bash completed with no output)")));
    QCOMPARE(s.value(QStringLiteral("output")).toString(),
             QStringLiteral("(Bash completed with no output)"));
    QJsonObject obj;
    obj.insert(QStringLiteral("stdout"), QStringLiteral("x"));
    QCOMPARE(AcpProtocol::rawOutputToObject(QJsonValue(obj)), obj);
    QVERIFY(AcpProtocol::rawOutputToObject(QJsonValue()).isEmpty());
    QVERIFY(AcpProtocol::rawOutputToObject(QJsonValue(QJsonValue::Null)).isEmpty());
}

void TestAcpProtocolSerialization::rpcErrorIsIncompleteServerToolCall_serverToolsOnly()
{
    const auto make = [](const QString &kind, const QString &ids) {
        QJsonObject data;
        data.insert(QStringLiteral("errorKind"), kind);
        QJsonObject err;
        err.insert(QStringLiteral("code"), -32603);
        err.insert(QStringLiteral("message"),
                   QStringLiteral("Internal error: Claude ended the turn without returning "
                                  "results for tool calls: ") + ids);
        err.insert(QStringLiteral("data"), data);
        return err;
    };
    const QString kind = QStringLiteral("incomplete_tool_call");
    QVERIFY(AcpProtocol::rpcErrorIsIncompleteServerToolCall(
        make(kind, QStringLiteral("srvtoolu_01BnrAbnbCsDGgL7ai7UWiUX"))));
    QVERIFY(AcpProtocol::rpcErrorIsIncompleteServerToolCall(
        make(kind, QStringLiteral("srvtoolu_a, srvtoolu_b"))));
    // A real client tool left open is a genuine failure.
    QVERIFY(!AcpProtocol::rpcErrorIsIncompleteServerToolCall(
        make(kind, QStringLiteral("srvtoolu_a, toolu_01XYZ"))));
    QVERIFY(!AcpProtocol::rpcErrorIsIncompleteServerToolCall(
        make(QStringLiteral("other"), QStringLiteral("srvtoolu_a"))));
    QVERIFY(!AcpProtocol::rpcErrorIsIncompleteServerToolCall(QJsonValue()));
    QVERIFY(AcpProtocol::isAdvisorToolName(QStringLiteral("advisor")));
    QVERIFY(!AcpProtocol::isAdvisorToolName(QStringLiteral("Read")));
}

void TestAcpProtocolSerialization::stripPiAcpQueueNotices_dropsStatusKeepsBody()
{
    QCOMPARE(AcpProtocol::stripPiAcpQueueNotices(
                 QStringLiteral("Queued message (position 1).")),
             QString());
    QCOMPARE(AcpProtocol::stripPiAcpQueueNotices(
                 QStringLiteral("Starting queued message. (0 remaining)")),
             QString());
    QCOMPARE(AcpProtocol::stripPiAcpQueueNotices(
                 QStringLiteral("Cleared queued prompts.")),
             QString());
    QCOMPARE(AcpProtocol::stripPiAcpQueueNotices(
                 QStringLiteral("Queued message (position 1). 你好!(ní hǎo) How can I help you "
                                "with the Bevy sandbox-2d project?Starting queued message. (0 remaining)")),
             QStringLiteral("你好!(ní hǎo) How can I help you with the Bevy sandbox-2d project?"));
    QCOMPARE(AcpProtocol::stripPiAcpQueueNotices(QStringLiteral("Hello")),
             QStringLiteral("Hello"));
}


void TestAcpProtocolSerialization::toolCallStatusIsTerminal_matchesFinalStatesOnly()
{
    QVERIFY(AcpProtocol::toolCallStatusIsTerminal(QStringLiteral("completed")));
    QVERIFY(AcpProtocol::toolCallStatusIsTerminal(QStringLiteral("failed")));
    QVERIFY(AcpProtocol::toolCallStatusIsTerminal(QStringLiteral("cancelled")));
    QVERIFY(AcpProtocol::toolCallStatusIsTerminal(QStringLiteral("canceled")));
    QVERIFY(!AcpProtocol::toolCallStatusIsTerminal(QStringLiteral("pending")));
    QVERIFY(!AcpProtocol::toolCallStatusIsTerminal(QStringLiteral("in_progress")));
    QVERIFY(!AcpProtocol::toolCallStatusIsTerminal(QString()));
}

// Wire shapes below were captured from @agentclientprotocol/claude-agent-acp
// 0.89.1 with `_meta.jetbrains.air.capabilities: ["asyncTasks"]` declared.
void TestAcpProtocolSerialization::asyncTaskEvent_spawnedFromWire()
{
    QJsonObject u;
    u.insert(QStringLiteral("asyncTaskId"), QStringLiteral("b9b0j72p2"));
    u.insert(QStringLiteral("name"), QStringLiteral("Sleep then print BGDONE"));
    u.insert(QStringLiteral("taskType"), QStringLiteral("shell"));
    u.insert(QStringLiteral("description"), QStringLiteral("Sleep then print BGDONE"));
    u.insert(QStringLiteral("showInTranscript"), false);
    u.insert(QStringLiteral("canStop"), true);
    u.insert(QStringLiteral("toolCallId"), QStringLiteral("toolu_013"));

    const auto ev = AcpProtocol::asyncTaskEventFromUpdate(QStringLiteral("async_task_spawned"), u);
    QVERIFY(ev.has_value());
    QCOMPARE(ev->kind, AcpProtocol::AcpAsyncTaskEvent::Kind::Spawned);
    QCOMPARE(ev->taskId, QStringLiteral("b9b0j72p2"));
    QCOMPARE(ev->name, QStringLiteral("Sleep then print BGDONE"));
    QCOMPARE(ev->taskType, QStringLiteral("shell"));
    QCOMPARE(ev->toolCallId, QStringLiteral("toolu_013"));
    QVERIFY(ev->canStop);

    // Missing canStop means the agent did not offer a stop for this task.
    u.remove(QStringLiteral("canStop"));
    const auto noStop = AcpProtocol::asyncTaskEventFromUpdate(QStringLiteral("async_task_spawned"), u);
    QVERIFY(noStop.has_value());
    QVERIFY(!noStop->canStop);
}

void TestAcpProtocolSerialization::asyncTaskEvent_progressAndStateFromWire()
{
    QJsonObject progress;
    progress.insert(QStringLiteral("asyncTaskId"), QStringLiteral("t1"));
    progress.insert(QStringLiteral("toolCallId"), QStringLiteral("call-9"));
    progress.insert(QStringLiteral("summary"), QStringLiteral("halfway"));
    const auto p = AcpProtocol::asyncTaskEventFromUpdate(
        QStringLiteral("async_task_progress"), progress);
    QVERIFY(p.has_value());
    QCOMPARE(p->kind, AcpProtocol::AcpAsyncTaskEvent::Kind::Progress);
    QCOMPARE(p->taskId, QStringLiteral("t1"));
    QCOMPARE(p->toolCallId, QStringLiteral("call-9"));
    QCOMPARE(p->summary, QStringLiteral("halfway"));

    QJsonObject state;
    state.insert(QStringLiteral("asyncTaskId"), QStringLiteral("t1"));
    state.insert(QStringLiteral("state"), QStringLiteral("completed"));
    const auto s = AcpProtocol::asyncTaskEventFromUpdate(
        QStringLiteral("async_task_state_update"), state);
    QVERIFY(s.has_value());
    QCOMPARE(s->kind, AcpProtocol::AcpAsyncTaskEvent::Kind::StateChanged);
    QCOMPARE(s->state, QStringLiteral("completed"));
}

void TestAcpProtocolSerialization::asyncTaskEvent_ignoresOtherKinds()
{
    QJsonObject u;
    u.insert(QStringLiteral("asyncTaskId"), QStringLiteral("t1"));
    QVERIFY(!AcpProtocol::asyncTaskEventFromUpdate(QStringLiteral("tool_call"), u).has_value());
    // A task event without an id cannot be tracked.
    QVERIFY(!AcpProtocol::asyncTaskEventFromUpdate(
        QStringLiteral("async_task_spawned"), QJsonObject{}).has_value());
}

void TestAcpProtocolSerialization::asyncTaskStateIsLive_runningAndPausedOnly()
{
    QVERIFY(AcpProtocol::asyncTaskStateIsLive(QStringLiteral("running")));
    QVERIFY(AcpProtocol::asyncTaskStateIsLive(QStringLiteral("paused")));
    QVERIFY(!AcpProtocol::asyncTaskStateIsLive(QStringLiteral("completed")));
    QVERIFY(!AcpProtocol::asyncTaskStateIsLive(QStringLiteral("failed")));
    QVERIFY(!AcpProtocol::asyncTaskStateIsLive(QStringLiteral("stopped")));
}

void TestAcpProtocolSerialization::toolCallUpdateIsBackgrounded_readsAirMarker()
{
    QJsonObject marked;
    marked.insert(QStringLiteral("_meta"), QJsonObject{
        {QStringLiteral("jetbrains"), QJsonObject{
            {QStringLiteral("air"), QJsonObject{
                {QStringLiteral("asyncTasks"), QJsonObject{
                    {QStringLiteral("backgrounded"), true}}}}}}}});
    QVERIFY(AcpProtocol::toolCallUpdateIsBackgrounded(marked));

    // commandTitle-only AIR meta (seen on every Bash call) is not the marker.
    QJsonObject other;
    other.insert(QStringLiteral("_meta"), QJsonObject{
        {QStringLiteral("jetbrains"), QJsonObject{
            {QStringLiteral("air"), QJsonObject{
                {QStringLiteral("commandTitle"), QStringLiteral("Sleep")}}}}}});
    QVERIFY(!AcpProtocol::toolCallUpdateIsBackgrounded(other));
    QVERIFY(!AcpProtocol::toolCallUpdateIsBackgrounded(QJsonObject{}));
}

void TestAcpProtocolSerialization::toolCallUpdateIsAsyncSubagentLaunch_readsToolResponse()
{
    auto withResponse = [](const QJsonObject &toolResponse) {
        QJsonObject update;
        update.insert(QStringLiteral("_meta"), QJsonObject{
            {QStringLiteral("claudeCode"), QJsonObject{
                {QStringLiteral("toolName"), QStringLiteral("Agent")},
                {QStringLiteral("toolResponse"), toolResponse}}}});
        return update;
    };
    QVERIFY(AcpProtocol::toolCallUpdateIsAsyncSubagentLaunch(withResponse(
        {{QStringLiteral("status"), QStringLiteral("async_launched")},
         {QStringLiteral("isAsync"), true}})));
    QVERIFY(AcpProtocol::toolCallUpdateIsAsyncSubagentLaunch(withResponse(
        {{QStringLiteral("status"), QStringLiteral("async_launched")}})));
    QVERIFY(AcpProtocol::toolCallUpdateIsAsyncSubagentLaunch(withResponse(
        {{QStringLiteral("isAsync"), true}})));

    // A foreground Agent finishing, and calls without the marker.
    QVERIFY(!AcpProtocol::toolCallUpdateIsAsyncSubagentLaunch(withResponse(
        {{QStringLiteral("status"), QStringLiteral("completed")},
         {QStringLiteral("isAsync"), false}})));
    QVERIFY(!AcpProtocol::toolCallUpdateIsAsyncSubagentLaunch(withResponse({})));
    QVERIFY(!AcpProtocol::toolCallUpdateIsAsyncSubagentLaunch(QJsonObject{}));
}

void TestAcpProtocolSerialization::clientCapabilitiesMeta_declaresAsyncTasks()
{
    // claude-agent-acp accepts a capability only when version is an integer
    // >= 1 and `capabilities` is an ARRAY containing the exact name.
    const QJsonObject air = AcpProtocol::clientCapabilitiesMeta()
        .value(QStringLiteral("jetbrains")).toObject()
        .value(QStringLiteral("air")).toObject();
    QCOMPARE(air.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(air.value(QStringLiteral("capabilities")).isArray());
    bool found = false;
    for (const auto &v : air.value(QStringLiteral("capabilities")).toArray())
        found = found || v.toString() == QLatin1String("asyncTasks");
    QVERIFY(found);
}


void TestAcpProtocolSerialization::formatWaitDuration_secondsMinutesHours()
{
    QCOMPARE(AcpProtocol::formatWaitDuration(0), QStringLiteral("0.0s"));
    QCOMPARE(AcpProtocol::formatWaitDuration(4200), QStringLiteral("4.2s"));
    QCOMPARE(AcpProtocol::formatWaitDuration(59999), QStringLiteral("59.9s"));
    QCOMPARE(AcpProtocol::formatWaitDuration(60000), QStringLiteral("1m 00s"));
    QCOMPARE(AcpProtocol::formatWaitDuration(125000), QStringLiteral("2m 05s"));
    QCOMPARE(AcpProtocol::formatWaitDuration(3600000), QStringLiteral("1h 00m"));
    QCOMPARE(AcpProtocol::formatWaitDuration(3725000), QStringLiteral("1h 02m"));
    // A negative clock reading (invalid QElapsedTimer) never renders garbage.
    QCOMPARE(AcpProtocol::formatWaitDuration(-5), QStringLiteral("0.0s"));
}

void TestAcpProtocolSerialization::parseAgentCapabilities_specKeysAndLegacyFallback()
{
    // claude-agent-acp 0.91.0 initialize result (trimmed).
    const QJsonObject spec = QJsonDocument::fromJson(R"({
        "protocolVersion": 1,
        "agentCapabilities": {
            "loadSession": true,
            "sessionCapabilities": {"fork": {}, "list": {}, "resume": {}, "close": {}},
            "promptCapabilities": {"image": true}
        }
    })").object();
    const auto caps = AcpProtocol::parseAgentCapabilities(spec);
    QVERIFY(caps.sessionLoad);
    QVERIFY(caps.sessionResume);

    const QJsonObject noResume = QJsonDocument::fromJson(R"({
        "agentCapabilities": {"loadSession": true, "sessionCapabilities": {"resume": null}}
    })").object();
    QVERIFY(!AcpProtocol::parseAgentCapabilities(noResume).sessionResume);

    const QJsonObject legacy = QJsonDocument::fromJson(R"({
        "capabilities": {"fs": {"readTextFile": true}, "terminal": true, "sessionLoad": true}
    })").object();
    const auto old = AcpProtocol::parseAgentCapabilities(legacy);
    QVERIFY(old.fsReadTextFile);
    QVERIFY(old.terminal);
    QVERIFY(old.sessionLoad);
    QVERIFY(!old.sessionResume);
}

QTEST_GUILESS_MAIN(TestAcpProtocolSerialization)
#include "test_acp_protocol_serialization.moc"
