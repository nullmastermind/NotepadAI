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

#ifndef TESTS_FAKESSHTRANSPORT_H
#define TESTS_FAKESSHTRANSPORT_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include "remote/ISshTransport.h"

// Scriptable ISshTransport fake shared across every offline SSH test (the
// SshSessionWorker state machine and all the Phase-2..4 remote subsystems —
// RemoteFsBackend, RemoteFileSystemModel, RemoteGitProcessRunner, the ACP exec
// transport — drive against this so CI needs no real sshd). Every knob is
// public so each test arranges exactly the scenario it needs. socketFd()
// returns -1 so the worker creates no real QSocketNotifier and pumpForTest()
// drives the pump deterministically.
//
// Two scriptable layers sit on top of the Phase-1 channel scripting:
//   * exec replay  — execOrShell captures the command string per channel;
//     stdout is scripted via readScript, stderr via readStderrScript, exit via
//     exitStatus (mirrors how a real exec channel surfaces those streams).
//   * SFTP         — readdir/stat/read/write are scripted per path with full
//     partial-read / EAGAIN / error injection (see the SFTP block below).
class FakeSshTransport : public remote::ISshTransport
{
public:
    // ---- connect / auth scripting (Phase 1) --------------------------------
    Step connectStep = Step::Ok;
    Step handshakeStep = Step::Ok;
    Step authStep = Step::Ok;
    QByteArray fakeHostKey = QByteArrayLiteral("FAKE-HOST-KEY-BLOB");

    // ---- connect-in-progress scripting (FIX-4) -----------------------------
    // While connectInProgress is true, connectSocket() returns Step::Again on
    // every call (the non-blocking ::connect is still waiting for writable), so
    // the worker stays in ConnectingSocket and a queued requestDisconnect can
    // preempt. Flip it false (and optionally set connectStep = Error) to let the
    // next advanceConnect proceed / fail. connectSocketCalls counts every call.
    bool connectInProgress = false;
    int  connectSocketCalls = 0;

    // ---- keepalive scripting (FIX-3) ---------------------------------------
    // sendKeepalive() returns keepaliveReturn (default 15 = secs-to-next on
    // success); set it < 0 to simulate a fatal socket error during the probe.
    // keepaliveCalls tracks how many probes the worker actually sent.
    int keepaliveReturn = 15;
    int keepaliveCalls = 0;

    // recorded auth
    int authPasswordCalls = 0;
    int authPublicKeyCalls = 0;
    int authAgentCalls = 0;
    QString lastAuthUser;
    QString lastAuthPassword;

    // ---- channel-open scripting (Phase 1) ----------------------------------
    Step openStep = Step::Ok; // Again to simulate EAGAIN on open
    QList<int> openedIds;     // transport ids assigned, in order
    int nextId = 1;
    int closedCount = 0;
    QList<int> closedIds;

    // ---- ChannelBusy scripting (FIX-1) -------------------------------------
    // openChannel() returns Step::ChannelBusy for the first openBusyCount calls
    // (server MaxSessions back-pressure), then resumes its normal Ok/openStep
    // behavior. Lets the backoff test assert the open is re-queued (never
    // dropped) and eventually succeeds. openChannelCalls counts every call so
    // the test can verify the retry cadence.
    int openBusyCount = 0;
    int openChannelCalls = 0;

    // per-transportId stdout read script (popped front; empty -> Again)
    QHash<int, QList<ReadResult>> readScript;
    QHash<int, int> readCalls;

    // per-transportId write script: number of EAGAINs before it accepts in full
    QHash<int, int> writeEagainsRemaining;
    int writeCalls = 0;
    // per-transportId captured channel-write bytes (stdin feed assertion for the
    // exec/git path — the worker writes stdinPayload here before draining).
    QHash<int, QByteArray> chWritten;
    QList<int> eofSentIds;
    QHash<int, QList<Step>> eofScript;

    QHash<int, int> exitStatus;

    // ---- exec replay (D6/D8) -----------------------------------------------
    // execOrShell return value (default Ok keeps the Phase-1 worker tests
    // happy — they only need a channel to come up). Command strings are
    // captured so the git/ACP exec tests can assert the exact `cd .. && ..`.
    Step execStep = Step::Ok;
    QHash<int, QString> execCommands; // last command seen per transport id
    QList<QString> execCommandLog;    // every command, in call order
    QString lastExecCommand;

    // per-transportId stderr read script (popped front; empty -> Again)
    QHash<int, QList<ReadResult>> readStderrScript;
    QHash<int, int> readStderrCalls;

    // ---- SFTP scripting (D1/D1a/D11) -----------------------------------------
    // The fake models the SFTP session layer. D1a splits into two independent
    // lanes (bulk + metadata), each calling sftpInit(lane) once — so
    // sftpInitCalls counts how many sessions were established (expected: 2 after
    // the split, one per lane). The fake tracks a per-lane established flag plus
    // a live-session count so sftpShutdown can free both.
    Step sftpInitStep = Step::Ok;
    int sftpInitCalls = 0;   // times a NEW session was established (any lane)
    int sftpSessionCount = 0; // currently-live sessions (0, 1, or 2)
    int sftpShutdownCalls = 0;
    // Per-lane "session established" flags (D1a). The worker calls sftpInit(lane)
    // once per lane and reuses it; the fake returns Ok without re-incrementing
    // sftpInitCalls for an already-established lane.
    bool sftpBulkInited = false;
    bool sftpMetaInited = false;
    // ---- transient-init injection (D12 read-only retry) --------------------
    // sftpInitFailsRemaining > 0 makes sftpInit(lane) return Error that many
    // times before succeeding — simulating a transient "Could not open SFTP
    // session" (the worker surfaces exactly that reason, which RemoteFsBackend
    // classifies as transient and retries for read-only ops). Decremented on each
    // failing call. Lets a test drive "fail N times then succeed" deterministically
    // without a real connection drop.
    int sftpInitFailsRemaining = 0;

    // File-open scripting, keyed by path. sftpOpenStep lets a test force
    // Again/Error on the next open of any path; per-path overrides win.
    Step sftpOpenStep = Step::Ok;
    QHash<QString, Step> sftpOpenStepByPath;

    // Read scripting: queued chunks per PATH, bound to the handle at open time
    // (so a test scripts content by path, not by the opaque handle id). Empty
    // queue -> Again, letting tests inject partial reads then an explicit EOF.
    QHash<QString, QList<ReadResult>> sftpReadScript;

    // Write scripting: per-path EAGAIN budget + captured bytes. After a write
    // handle is closed, sftpWrittenByPath holds everything written to it, so a
    // round-trip test can assert the payload.
    QHash<QString, int> sftpWriteEagainsRemaining;
    QHash<QString, QByteArray> sftpWrittenByPath;

    // Readdir scripting: queued entries per dir PATH (Done is synthesized when
    // the queue drains, but a test can also enqueue an explicit Again/Error to
    // exercise the retry / fatal paths mid-enumeration).
    QHash<QString, QList<SftpDirEntry>> sftpReaddirScript;

    // Stat scripting: result per path (default: Error == "no such path").
    QHash<QString, SftpStatResult> sftpStatByPath;

    // Recorded activity for assertions.
    QList<QString> sftpOpenedPaths;
    QList<QString> sftpOpendirPaths;
    QList<QString> sftpStatPaths;
    int sftpCloseCalls = 0;

    // Per-handle runtime state (private to the fake; tests script by path).
    struct SftpHandleState
    {
        QString path;
        bool forWrite = false;
        bool isDir = false;
        QList<ReadResult> reads;     // copied from sftpReadScript at open
        QList<SftpDirEntry> entries; // copied from sftpReaddirScript at opendir
        QByteArray written;          // accumulated by sftpWrite
    };
    QHash<int, SftpHandleState> sftpHandles;
    int nextSftpHandle = 1;

    Step connectSocket(const QString &, int) override
    {
        ++connectSocketCalls;
        if (connectInProgress) return Step::Again;
        return connectStep;
    }
    Step handshake() override { return handshakeStep; }
    QByteArray hostKey() const override { return fakeHostKey; }

    Step authPassword(const QString &username, const QString &password) override
    {
        ++authPasswordCalls;
        lastAuthUser = username;
        lastAuthPassword = password;
        return authStep;
    }
    Step authPublicKey(const QString &username, const QString &, const QString &) override
    {
        ++authPublicKeyCalls;
        lastAuthUser = username;
        return authStep;
    }
    Step authAgent(const QString &username) override
    {
        ++authAgentCalls;
        lastAuthUser = username;
        return authStep;
    }

    OpenResult openChannel() override
    {
        ++openChannelCalls;
        OpenResult r;
        // FIX-1: ChannelBusy scripting — return ChannelBusy N times first.
        if (openBusyCount > 0) {
            --openBusyCount;
            r.step = Step::ChannelBusy;
            return r;
        }
        if (openStep != Step::Ok) {
            r.step = openStep;
            return r;
        }
        r.step = Step::Ok;
        r.channelId = nextId++;
        openedIds.append(r.channelId);
        return r;
    }
    Step requestPty(int, const QByteArray &, int, int) override { return Step::Ok; }
    Step resizePty(int, int, int) override { return Step::Ok; }
    Step execOrShell(int channelId, const QString &command) override
    {
        // Capture the command (empty == interactive shell) so the exec-channel
        // tests can replay/assert it; default return keeps Phase-1 tests green.
        execCommands[channelId] = command;
        execCommandLog.append(command);
        lastExecCommand = command;
        return execStep;
    }

    qint64 chWrite(int channelId, const QByteArray &bytes) override
    {
        ++writeCalls;
        int &eagains = writeEagainsRemaining[channelId];
        if (eagains > 0) {
            --eagains;
            return kWriteAgain;
        }
        chWritten[channelId].append(bytes); // capture stdin feed
        return bytes.size(); // accept in full
    }

    Step chSendEof(int channelId) override
    {
        QList<Step> &q = eofScript[channelId];
        Step result = q.isEmpty() ? Step::Ok : q.takeFirst();
        if (result == Step::Ok) eofSentIds.append(channelId);
        return result;
    }

    ReadResult chRead(int channelId) override
    {
        ++readCalls[channelId];
        QList<ReadResult> &q = readScript[channelId];
        if (q.isEmpty()) {
            ReadResult r;
            r.again = true;
            return r;
        }
        return q.takeFirst();
    }

    ReadResult chReadStderr(int channelId) override
    {
        ++readStderrCalls[channelId];
        QList<ReadResult> &q = readStderrScript[channelId];
        if (q.isEmpty()) {
            ReadResult r;
            r.again = true;
            return r;
        }
        return q.takeFirst();
    }

    int chExitStatus(int channelId) override { return exitStatus.value(channelId, 0); }
    void closeChannel(int channelId) override { ++closedCount; closedIds.append(channelId); }
    qintptr socketFd() const override { return -1; } // no real QSocketNotifier in tests
    void disconnect() override {}

    // FIX-3: scriptable keepalive probe. Default 15 (secs-to-next on success);
    // set keepaliveReturn < 0 to simulate a fatal socket error during the send.
    int sendKeepalive() override
    {
        ++keepaliveCalls;
        return keepaliveReturn;
    }

    // PLACEHOLDER_SFTP_METHODS
    SftpOpenResult sftpInit(SftpLane lane) override
    {
        SftpOpenResult r;
        // D12: transient-failure injection — fail the next N init attempts so a
        // read-only op surfaces "Could not open SFTP session" and RemoteFsBackend
        // retries. Applies to whichever lane init is attempted.
        if (sftpInitFailsRemaining > 0) {
            --sftpInitFailsRemaining;
            r.step = Step::Error;
            return r;
        }
        if (sftpInitStep != Step::Ok) {
            r.step = sftpInitStep;
            return r;
        }
        // D1a: one session per lane; already-established lane reuses (no re-count).
        bool &inited = (lane == SftpLane::Bulk) ? sftpBulkInited : sftpMetaInited;
        if (!inited) {
            inited = true;
            ++sftpSessionCount;
            ++sftpInitCalls; // counts every new session (D1a: expected 2, one/lane)
        }
        r.step = Step::Ok;
        return r;
    }

    void sftpShutdown() override
    {
        ++sftpShutdownCalls;
        // D1a: free BOTH lane sessions.
        if (sftpBulkInited) {
            sftpBulkInited = false;
            if (sftpSessionCount > 0) --sftpSessionCount;
        }
        if (sftpMetaInited) {
            sftpMetaInited = false;
            if (sftpSessionCount > 0) --sftpSessionCount;
        }
        sftpHandles.clear();
    }

    SftpOpenResult sftpOpen(SftpLane, const QString &path, bool forWrite) override
    {
        SftpOpenResult r;
        sftpOpenedPaths.append(path);
        const Step step = sftpOpenStepByPath.value(path, sftpOpenStep);
        if (step != Step::Ok) {
            r.step = step;
            return r;
        }
        SftpHandleState st;
        st.path = path;
        st.forWrite = forWrite;
        st.isDir = false;
        if (!forWrite) {
            st.reads = sftpReadScript.value(path); // bind content at open
        }
        const int id = nextSftpHandle++;
        sftpHandles.insert(id, st);
        r.step = Step::Ok;
        r.handleId = id;
        return r;
    }

    ReadResult sftpRead(SftpLane, int handleId) override
    {
        ReadResult out;
        auto it = sftpHandles.find(handleId);
        if (it == sftpHandles.end()) {
            out.error = true;
            return out;
        }
        QList<ReadResult> &q = it->reads;
        if (q.isEmpty()) {
            out.again = true; // tests enqueue an explicit eof to finish
            return out;
        }
        return q.takeFirst();
    }

    qint64 sftpWrite(SftpLane, int handleId, const QByteArray &bytes) override
    {
        auto it = sftpHandles.find(handleId);
        if (it == sftpHandles.end()) {
            return kWriteError;
        }
        int &eagains = sftpWriteEagainsRemaining[it->path];
        if (eagains > 0) {
            --eagains;
            return kWriteAgain;
        }
        it->written.append(bytes);
        return bytes.size(); // accept in full
    }

    void sftpClose(SftpLane, int handleId) override
    {
        ++sftpCloseCalls;
        auto it = sftpHandles.find(handleId);
        if (it == sftpHandles.end()) {
            return;
        }
        if (it->forWrite) {
            sftpWrittenByPath[it->path] = it->written; // expose for assertions
        }
        sftpHandles.erase(it);
    }

    SftpOpenResult sftpOpendir(SftpLane, const QString &path) override
    {
        SftpOpenResult r;
        sftpOpendirPaths.append(path);
        const Step step = sftpOpenStepByPath.value(path, sftpOpenStep);
        if (step != Step::Ok) {
            r.step = step;
            return r;
        }
        SftpHandleState st;
        st.path = path;
        st.isDir = true;
        st.entries = sftpReaddirScript.value(path); // bind listing at opendir
        const int id = nextSftpHandle++;
        sftpHandles.insert(id, st);
        r.step = Step::Ok;
        r.handleId = id;
        return r;
    }

    SftpDirEntry sftpReaddir(SftpLane, int handleId) override
    {
        SftpDirEntry out;
        auto it = sftpHandles.find(handleId);
        if (it == sftpHandles.end()) {
            out.kind = SftpDirEntry::Kind::Error;
            return out;
        }
        QList<SftpDirEntry> &q = it->entries;
        if (q.isEmpty()) {
            out.kind = SftpDirEntry::Kind::Done; // drained -> end of stream
            return out;
        }
        return q.takeFirst();
    }

    void sftpClosedir(SftpLane lane, int handleId) override { sftpClose(lane, handleId); }

    SftpStatResult sftpStat(SftpLane, const QString &path) override
    {
        sftpStatPaths.append(path);
        SftpStatResult def;
        def.step = Step::Error; // default: path absent / not scripted
        return sftpStatByPath.value(path, def);
    }

    // --- Rename / Mkdir / Unlink scripting ------------------------------------
    // Default step: Ok. Override per-path for error injection.
    QHash<QString, Step> sftpRenameStepBySrc;
    QList<QPair<QString, QString>> sftpRenameCalls;

    QHash<QString, Step> sftpMkdirStepByPath;
    QList<QString> sftpMkdirPaths;

    QHash<QString, Step> sftpUnlinkStepByPath;
    QList<QString> sftpUnlinkPaths;

    Step sftpRename(SftpLane, const QString &src, const QString &dst) override
    {
        sftpRenameCalls.append({src, dst});
        return sftpRenameStepBySrc.value(src, Step::Ok);
    }

    Step sftpMkdir(SftpLane, const QString &path) override
    {
        sftpMkdirPaths.append(path);
        return sftpMkdirStepByPath.value(path, Step::Ok);
    }

    Step sftpUnlink(SftpLane, const QString &path) override
    {
        sftpUnlinkPaths.append(path);
        return sftpUnlinkStepByPath.value(path, Step::Ok);
    }
};

#endif // TESTS_FAKESSHTRANSPORT_H
