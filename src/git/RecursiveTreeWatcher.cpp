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

#include "RecursiveTreeWatcher.h"

#ifdef Q_OS_WIN

#include "RecursiveTreeWatcherLogic.h"

#include <QMetaObject>
#include <QStringView>

#include <qt_windows.h>

#include <chrono>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
constexpr DWORD kFilter = FILE_NOTIFY_CHANGE_FILE_NAME
                        | FILE_NOTIFY_CHANGE_DIR_NAME
                        | FILE_NOTIFY_CHANGE_LAST_WRITE;
// Per-watch buffer. 64 KiB is the largest ReadDirectoryChangesW accepts on a
// network path. With ignored top-level dirs no longer watched, overflow of an
// individual watched dir's buffer is now rare (only a hard burst inside a
// watched, non-ignored subtree can do it).
constexpr DWORD kBufSize = 64 * 1024;
// Floor on the gap between two blind (buffer-overflow) refresh notifications.
// Overflow means "something changed, contents unknown" so we cannot ignore-
// filter it. With the selective-watch model this should essentially never fire
// for top-level ignored dirs (they aren't watched); it remains as a residual
// bound for a nested-ignore burst that overflows a watched parent's buffer.
constexpr auto kOverflowMinInterval = std::chrono::milliseconds(750);

// Completion keys for control messages posted to the IOCP. Real per-watch
// completions carry a Watch* as their key, which is never one of these (a heap
// pointer is never 1/2/3), so dispatch is unambiguous.
constexpr ULONG_PTR kKeyStop = 1;
constexpr ULONG_PTR kKeyReconfigure = 2;

// Pure decision logic lives in RecursiveTreeWatcherLogic.h (Win32-free, unit-
// tested). WCHAR == wchar_t on Windows so these bind directly to the raw
// FILE_NOTIFY_INFORMATION::FileName buffer with no copy/conversion.
using rtwlogic::deriveWatchedTopLevelDirs;
using rtwlogic::isUnderIgnored;

// One watched directory: its handle, the in-flight OVERLAPPED, its read buffer,
// the top-level entry name it represents ("" for the root watch), whether the
// watch is recursive, whether it is being torn down (CancelIoEx issued), and
// whether a ReadDirectoryChangesW completion is still owed to us.
//
// ioInFlight is the load-bearing field for leak-free teardown: it is true from
// the moment a ReadDirectoryChangesW succeeds until we dequeue that read's
// completion. While true, EXACTLY ONE completion is still owed (the pending
// read's, OR — if the read already completed — its packet still sitting
// undequeued in the IOCP queue). While false, NO completion is coming. The
// drain frees a watch on its owed completion when ioInFlight is true, and
// SYNCHRONOUSLY (via the reap pass) when it is false — so termination never
// depends on CancelIoEx finding an in-flight read to cancel.
struct Watch {
    HANDLE hDir = INVALID_HANDLE_VALUE;
    OVERLAPPED ov{};
    std::unique_ptr<char[]> buf;
    std::wstring relName; // lowercased top-level dir name; empty for root
    bool recursive = false;
    bool closing = false;
    bool ioInFlight = false;
};
} // namespace

// Forward decl: the worker body, defined after start(). Free function (not a
// member) so the header carries no Win32-typed signature; it only needs the
// public `changed()` signal, reached via QMetaObject::invokeMethod.
static void runWatcherWorker(
    RecursiveTreeWatcher *self,
    const QString &rootPath,
    std::atomic<bool> *stopFlag,
    std::atomic<bool> *pendingFlag,
    std::atomic<std::shared_ptr<const std::vector<std::wstring>>> *ignoredSlot,
    std::atomic<void *> *portSlot);

RecursiveTreeWatcher::RecursiveTreeWatcher(QObject *parent)
    : QObject(parent)
{
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

RecursiveTreeWatcher::~RecursiveTreeWatcher()
{
    stop();
    if (m_stopEvent)
        CloseHandle(static_cast<HANDLE>(m_stopEvent));
}

void RecursiveTreeWatcher::start(const QString &path)
{
    stop();

    // Reset run state BEFORE spawning the worker. m_stop is reset SEQ_CST so it
    // lives in the same total order as the Dekker handshake (worker publishes
    // port seq_cst → loads m_stop seq_cst; stop() stores m_stop seq_cst → loads
    // port seq_cst). Without resetting it, a prior stop() left m_stop == true,
    // and the freshly-spawned worker's post-publish load would read that stale
    // true and immediately self-exit — the watcher would silently die on the
    // second repo ever opened. The store is sequenced-before m_thread->start()
    // below, and thread-spawn synchronizes-with the worker's begin, so the
    // worker is guaranteed to observe false.
    m_stop.store(false, std::memory_order_seq_cst);
    m_pendingNotify.store(false, std::memory_order_relaxed);
    // m_port is already null here: every worker exit path nulls it and the
    // preceding stop() joined the old worker, so there is no stale port to clear.
    ResetEvent(static_cast<HANDLE>(m_stopEvent));

    std::atomic<bool> *stopFlag = &m_stop;
    std::atomic<bool> *pendingFlag = &m_pendingNotify;
    std::atomic<std::shared_ptr<const std::vector<std::wstring>>> *ignoredSlot = &m_ignored;
    std::atomic<void *> *portSlot = &m_port;
    RecursiveTreeWatcher *self = this;

    m_thread = QThread::create([self, path, stopFlag, pendingFlag, ignoredSlot, portSlot]() {
        runWatcherWorker(self, path, stopFlag, pendingFlag, ignoredSlot, portSlot);
    });

    m_thread->start();
}

// The entire watch set lives on this one worker thread: the IOCP, the Watch
// map, every HANDLE/OVERLAPPED/buffer. The GUI thread only ever publishes the
// ignored snapshot and PostQueuedCompletionStatus-es control keys — it never
// touches a handle. That is what makes the map lock-free.
static void runWatcherWorker(
    RecursiveTreeWatcher *self,
    const QString &rootPath,
    std::atomic<bool> *stopFlag,
    std::atomic<bool> *pendingFlag,
    std::atomic<std::shared_ptr<const std::vector<std::wstring>>> *ignoredSlot,
    std::atomic<void *> *portSlot)
{
    const std::wstring wroot = rootPath.toStdWString();

    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (!port)
        return;
    // Publish the port so setIgnoredPrefixes()/stop() on the GUI thread can
    // post control messages. Cleared again before we close it on the way out.
    // SEQ_CST store paired with the SEQ_CST load of stopFlag below forms a
    // Dekker handshake with stop(): stop() does store(m_stop,true) THEN
    // load(m_port). If stop() ran in the gap before this publish, exactly one
    // of {stop sees the port, we see m_stop} is guaranteed by the total order,
    // so a stop requested during startup is never lost (which would otherwise
    // deadlock the INFINITE wait below against m_thread->wait()).
    portSlot->store(port, std::memory_order_seq_cst);
    if (stopFlag->load(std::memory_order_seq_cst)) {
        portSlot->store(nullptr, std::memory_order_release);
        CloseHandle(port);
        return;
    }

    std::unordered_map<Watch *, std::unique_ptr<Watch>> watches;
    auto lastOverflow = std::chrono::steady_clock::time_point::min();

    // Notify the GUI thread at most once until it ack's (coalescing handshake).
    auto notifyOnce = [self, pendingFlag]() {
        if (!pendingFlag->exchange(true, std::memory_order_acq_rel))
            QMetaObject::invokeMethod(self, &RecursiveTreeWatcher::changed,
                                      Qt::QueuedConnection);
    };

    // Open `dir`, associate it to the port keyed by its own Watch*, and arm the
    // first ReadDirectoryChangesW. Returns the raw Watch* on success (owned by
    // `watches`), nullptr on failure (nothing leaked — the handle is closed and
    // no Watch is inserted, so no completion is ever owed). `rel` is the
    // lowercased top-level name, empty for the root.
    auto armWatch = [&](const std::wstring &fullPath, const std::wstring &rel,
                        bool recursive) -> Watch * {
        HANDLE hDir = CreateFileW(
            fullPath.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (hDir == INVALID_HANDLE_VALUE)
            return nullptr;

        auto w = std::make_unique<Watch>();
        w->hDir = hDir;
        w->buf = std::make_unique<char[]>(kBufSize);
        w->relName = rel;
        w->recursive = recursive;
        Watch *key = w.get();

        if (!CreateIoCompletionPort(hDir, port, reinterpret_cast<ULONG_PTR>(key), 0)) {
            CloseHandle(hDir); // never inserted → no completion owed
            return nullptr;
        }
        if (!ReadDirectoryChangesW(hDir, w->buf.get(), kBufSize, recursive,
                                   kFilter, nullptr, &w->ov, nullptr)) {
            CloseHandle(hDir); // arm failed → no read in flight → no completion owed
            return nullptr;
        }
        w->ioInFlight = true; // a completion is now owed for this read
        watches.emplace(key, std::move(w));
        return key;
    };

    // Re-arm a live watch's read. On success a completion is owed again; on
    // failure (dir vanished, handle bad) NO completion is owed, so the caller
    // must free synchronously. Returns true if re-armed.
    auto rearm = [](Watch *w) -> bool {
        if (ReadDirectoryChangesW(w->hDir, w->buf.get(), kBufSize, w->recursive,
                                  kFilter, nullptr, &w->ov, nullptr)) {
            w->ioInFlight = true;
            return true;
        }
        w->ioInFlight = false;
        return false;
    };

    // Free a watch right now: close its handle (exactly once) and erase it,
    // which frees its OVERLAPPED + buffer via unique_ptr. ONLY call this when no
    // completion is owed (w->ioInFlight == false) — otherwise the owed
    // completion would later dequeue a freed Watch*. Returns the next iterator
    // so callers erasing mid-iteration stay valid.
    using WatchIt = std::unordered_map<Watch *, std::unique_ptr<Watch>>::iterator;
    auto freeWatchNow = [&](const WatchIt &it) {
        CloseHandle(it->second->hDir);
        return watches.erase(it);
    };

    // Begin teardown of a watch. If a read is in flight, ask the kernel to abort
    // it (queues the final completion) and mark closing — the completion handler
    // frees it. If NO read is in flight (a watch that completed but wasn't re-
    // armed), CancelIoEx would queue nothing and the watch would leak forever
    // waiting for a completion that never comes — so free it synchronously here.
    // This is what makes drain termination independent of "is a read in flight".
    // Returns the iterator to continue iteration from (the watch's successor if
    // reaped synchronously, else the watch itself advanced by the caller).
    auto beginDrop = [&](const WatchIt &it) -> WatchIt {
        Watch *w = it->second.get();
        if (w->closing)
            return std::next(it);
        w->closing = true;
        if (w->ioInFlight) {
            CancelIoEx(w->hDir, &w->ov); // → final ERROR_OPERATION_ABORTED completion
            return std::next(it);        // watch stays (closing); freed on completion
        }
        return freeWatchNow(it);         // no completion owed → reap now
    };

    // Recompute the desired watch set from the current top-level listing + the
    // ignored snapshot, and apply the diff. Runs on start and on every
    // reconfigure post. O(top-level entries); persistent watches are left
    // untouched so steady-state churn triggers no work here.
    auto reconfigure = [&]() {
        const std::shared_ptr<const std::vector<std::wstring>> ignored =
            ignoredSlot->load(std::memory_order_acquire);
        static const std::vector<std::wstring> kEmpty;
        const std::vector<std::wstring> &prefixes = ignored ? *ignored : kEmpty;

        // Enumerate immediate child directories of the root.
        std::vector<std::wstring> dirNames;
        WIN32_FIND_DATAW fd;
        const std::wstring pattern = wroot + L"\\*";
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                const std::wstring name = fd.cFileName;
                if (name == L"." || name == L"..")
                    continue;
                dirNames.push_back(name);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }

        const std::vector<std::wstring> desired =
            deriveWatchedTopLevelDirs(dirNames, prefixes);

        // Build a quick membership set of desired names.
        std::unordered_map<std::wstring, bool> desiredSet;
        desiredSet.reserve(desired.size());
        for (const std::wstring &n : desired)
            desiredSet.emplace(n, true);

        // Which top-level names are currently watched (non-closing, recursive)?
        std::unordered_map<std::wstring, bool> watchedSet;
        // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order) — order-insensitive: only collects relName strings into a set; map is Watch*-keyed for O(1) IOCP completion dispatch.
        for (auto &kv : watches) {
            Watch *w = kv.second.get();
            if (w->recursive && !w->closing && !w->relName.empty())
                watchedSet.emplace(w->relName, true);
        }

        // Drop watches no longer desired (became ignored, or dir removed).
        // beginDrop returns the iterator to continue from (it may free-and-erase
        // synchronously when no read is in flight), so never advance manually
        // past a beginDrop.
        for (auto it = watches.begin(); it != watches.end();) {
            Watch *w = it->second.get();
            if (!w->recursive || w->closing || w->relName.empty()) {
                ++it;
                continue;
            }
            if (desiredSet.find(w->relName) == desiredSet.end())
                it = beginDrop(it);
            else
                ++it;
        }

        // Arm watches for newly-desired dirs. A dir that appears here is new
        // (or newly un-ignored): arm it AND notify once, so the guaranteed
        // git-status rescan observes anything written into it before the watch
        // was armed (closes the new-dir race — the watcher is an invalidation
        // signal, not a source of truth).
        for (const std::wstring &n : desired) {
            if (watchedSet.find(n) != watchedSet.end())
                continue;
            std::wstring full;
            full.reserve(wroot.size() + 1 + n.size());
            full = wroot;
            full += L'\\';
            full += n;
            if (armWatch(full, n, /*recursive=*/true))
                notifyOnce();
        }
    };

    // Arm the non-recursive root watch (structural + top-level files). If even
    // this fails the tree is unusable — clean up and bail.
    if (!armWatch(wroot, std::wstring(), /*recursive=*/false)) {
        portSlot->store(nullptr, std::memory_order_release);
        CloseHandle(port);
        return;
    }
    // Arm recursive watches for the current non-ignored top-level dirs.
    reconfigure();

    // Service loop. Every completion is either a control key (stop/reconfigure)
    // or a Watch* whose read completed (or whose cancellation completed).
    bool stopping = false;
    while (true) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED *ovl = nullptr;
        // Block indefinitely in steady state; once tearing down, cap the wait so
        // a pathological never-delivered abort completion can't hang stop()'s
        // thread->wait() forever — the defensive sweep after the loop closes any
        // remaining handle.
        const DWORD waitMs = stopping ? 2000 : INFINITE;
        const BOOL ok = GetQueuedCompletionStatus(port, &bytes, &key, &ovl, waitMs);
        const DWORD gle = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && ovl == nullptr) {
            // No packet dequeued: a timeout (only possible while stopping) or a
            // port error. Either way, stop draining and fall through to sweep.
            if (stopping || gle == WAIT_TIMEOUT)
                break;
            // Spurious failure with the port still valid in steady state: retry.
            continue;
        }

        if (key == kKeyStop) {
            stopping = true;
            // Tear down every watch. beginDrop reaps quiesced watches (no read
            // in flight) synchronously and CancelIoEx's the rest; their final
            // abort completions drain through the handler below. Iterate via the
            // returned iterator since beginDrop may erase.
            for (auto it = watches.begin(); it != watches.end();)
                it = beginDrop(it);
            if (watches.empty())
                break; // everything was quiesced — nothing left to drain
            continue;
        }
        if (key == kKeyReconfigure) {
            if (!stopping)
                reconfigure();
            continue;
        }

        // Per-watch completion. The key IS the Watch*; the map still owns it
        // (we only compare the pointer value in find() — never dereferenced
        // before the membership check, so a stale key is safe).
        Watch *w = reinterpret_cast<Watch *>(key);
        auto it = watches.find(w);
        if (it == watches.end())
            continue; // already freed; stray/duplicate completion — ignore.

        // This completion discharges the one we owed for this watch's read.
        w->ioInFlight = false;

        const bool aborted = (!ok && gle == ERROR_OPERATION_ABORTED);
        if (w->closing || aborted) {
            // Final completion for a cancelled (or closing) read: free now.
            // No further completion is owed (ioInFlight just cleared, no re-arm).
            freeWatchNow(it);
            if (stopping && watches.empty())
                break;
            continue;
        }

        if (!ok) {
            // A non-abort failure on a live watch (dir vanished, handle went
            // bad). ioInFlight was just cleared and we do not re-arm, so no
            // completion is owed — free synchronously.
            freeWatchNow(it);
            continue;
        }

        // A successful read of zero bytes means this watch's buffer overflowed
        // and the OS discarded the batch. Unfilterable — rate-limited notify.
        if (bytes == 0) {
            const auto now = std::chrono::steady_clock::now();
            if (now - lastOverflow >= kOverflowMinInterval) {
                lastOverflow = now;
                notifyOnce();
            }
        } else {
            // Scan the batch. Drop .git/ internals and nested-ignore events;
            // anything else is a real working-tree change.
            const std::shared_ptr<const std::vector<std::wstring>> ignored =
                ignoredSlot->load(std::memory_order_acquire);
            static const std::vector<std::wstring> kEmpty;
            const std::vector<std::wstring> &prefixes = ignored ? *ignored : kEmpty;

            bool relevant = false;
            // The root (non-recursive) watch reports top-level structural
            // churn; recompute the watch set so new/removed top-level dirs are
            // (un)watched. The reconfigure itself notifies for new dirs.
            bool rootStructural = false;

            auto *info = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(w->buf.get());
            for (;;) {
                const int nameLen = static_cast<int>(info->FileNameLength / sizeof(WCHAR));
                const QStringView rel(info->FileName, nameLen);
                const bool isDotGit = rel.startsWith(u".git/") || rel.startsWith(u".git\\")
                                      || rel == u".git";
                if (!isDotGit && !isUnderIgnored(info->FileName, nameLen, prefixes)) {
                    relevant = true;
                    if (!w->recursive
                        && (info->Action == FILE_ACTION_ADDED
                            || info->Action == FILE_ACTION_REMOVED
                            || info->Action == FILE_ACTION_RENAMED_OLD_NAME
                            || info->Action == FILE_ACTION_RENAMED_NEW_NAME)) {
                        rootStructural = true;
                    }
                    // Nothing more to learn from a recursive child watch once a
                    // single relevant record is seen (it can't set rootStructural);
                    // and the root watch can stop early once it has both flags.
                    if (!w->recursive || rootStructural)
                        break;
                }
                if (info->NextEntryOffset == 0)
                    break;
                info = reinterpret_cast<FILE_NOTIFY_INFORMATION *>(
                    reinterpret_cast<char *>(info) + info->NextEntryOffset);
            }

            if (relevant)
                notifyOnce();
            // Re-arm BEFORE reconfiguring so this watch keeps observing; the
            // reconfigure may add/remove OTHER watches but never the root one.
            // rearm() sets ioInFlight on success; on failure (dir vanished) it
            // clears ioInFlight and we free synchronously (no completion owed).
            if (!rearm(w)) {
                freeWatchNow(it);
                continue;
            }
            if (rootStructural)
                reconfigure();
            continue;
        }

        // Re-arm a live watch after an overflow (no records consumed). Same
        // failure handling: a dead handle is dropped synchronously, not leaked.
        if (!rearm(w))
            freeWatchNow(it);
    }

    // Teardown is complete (watches empty) or we are bailing via the drain
    // timeout. Defensive sweep for any watch whose abort completion was never
    // delivered within the 2s drain (a dead network handle, a kernel that
    // dropped the cancel completion). We must NOT block here: this sweep runs
    // ON the drain timeout, and m_thread->wait() is blocked on us — any blocking
    // wait (e.g. GetOverlappedResult(TRUE)) reintroduces the exact deadlock the
    // timeout exists to prevent. So:
    //   * No read in flight → free normally (close handle; unique_ptr frees buf).
    //   * Read still in flight → CloseHandle (non-blocking; forces the kernel to
    //     abandon the pending read) but INTENTIONALLY LEAK the OVERLAPPED+buffer
    //     by releasing it from the unique_ptr. We cannot prove the kernel is done
    //     with that buffer without blocking, so releasing it is the only memory-
    //     safe non-blocking choice. Bounded to one 64 KiB buffer per genuinely-
    //     stuck handle, only on pathological teardown — never in normal operation
    //     (normal stop drains every completion and reaches here with watches
    //     already empty).
    // The port slot is cleared first so a late GUI-thread post sees null and
    // no-ops; a post that already captured the handle races us and fails
    // harmlessly against the closed handle.
    portSlot->store(nullptr, std::memory_order_release);
    // NOLINTNEXTLINE(bugprone-nondeterministic-pointer-iteration-order) — order-insensitive: closes every handle once; map is Watch*-keyed for O(1) IOCP completion dispatch.
    for (auto &kv : watches) {
        Watch *w = kv.second.get();
        if (w->ioInFlight) {
            // Can't wait, can't safely free the buffer the kernel may still
            // write. Abandon the buffer to the kernel; close the handle (no block).
            (void)w->buf.release(); // deliberate leak — see rationale above
            CloseHandle(w->hDir);
        } else {
            CloseHandle(w->hDir); // quiesced — unique_ptr frees the buffer on clear()
        }
    }
    watches.clear();
    CloseHandle(port);
}

void RecursiveTreeWatcher::stop()
{
    if (!m_thread)
        return;
    // The worker may have self-exited (root unmount, port-creation or
    // root-watch arm failure), leaving isRunning() false but m_thread still
    // pointing at a finished QThread. Always delete it here so a later start()
    // doesn't overwrite (and leak) the stale handle.
    if (m_thread->isRunning()) {
        // SEQ_CST store paired with the worker's SEQ_CST load after it publishes
        // the port: this is the Dekker handshake that makes a stop requested
        // during worker startup impossible to lose. Set the flag BEFORE reading
        // the port.
        m_stop.store(true, std::memory_order_seq_cst);
        // Wake the IOCP service loop with the stop sentinel. It cancels every
        // live read and drains their abort completions (freeing each handle/
        // buffer on its final completion) before returning — no leak, no UAF.
        // If the port isn't published yet, the worker's post-publish stopFlag
        // check (seq_cst) catches the stop and self-exits, so wait() returns.
        if (HANDLE port = static_cast<HANDLE>(m_port.load(std::memory_order_seq_cst)))
            PostQueuedCompletionStatus(port, 0, kKeyStop, nullptr);
        SetEvent(static_cast<HANDLE>(m_stopEvent));
        m_thread->wait();
    }
    delete m_thread;
    m_thread = nullptr;
}

void RecursiveTreeWatcher::setIgnoredPrefixes(const QStringList &prefixes)
{
    auto norm = std::make_shared<std::vector<std::wstring>>();
    norm->reserve(static_cast<std::size_t>(prefixes.size()));
    for (const QString &raw : prefixes) {
        // Normalise to the same shape the worker compares against: lowercase,
        // '/'-separated, no leading/trailing slash. git emits repo-relative,
        // '/'-separated paths with a trailing slash on directories.
        QString p = raw;
        p.replace(QLatin1Char('\\'), QLatin1Char('/'));
        while (p.startsWith(QLatin1Char('/'))) p.remove(0, 1);
        while (p.endsWith(QLatin1Char('/'))) p.chop(1);
        if (p.isEmpty()) continue;
        norm->push_back(p.toLower().toStdWString());
    }
    // Publish a const snapshot; the worker swaps it in atomically. Empty list
    // is a valid "no filter" state.
    m_ignored.store(std::shared_ptr<const std::vector<std::wstring>>(std::move(norm)),
                    std::memory_order_release);
    // Wake the worker to recompute its watch set against the new prefixes:
    // dirs that became ignored get their watches torn down, dirs that became
    // un-ignored get fresh recursive watches. No-op if the worker isn't up yet
    // (it runs an initial reconfigure on start, picking up whatever is current).
    if (HANDLE port = static_cast<HANDLE>(m_port.load(std::memory_order_acquire)))
        PostQueuedCompletionStatus(port, 0, kKeyReconfigure, nullptr);
}

#endif // Q_OS_WIN
