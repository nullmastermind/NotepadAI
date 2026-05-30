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

#ifndef RECURSIVE_TREE_WATCHER_H
#define RECURSIVE_TREE_WATCHER_H

#include <QtGlobal>

#ifdef Q_OS_WIN

#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Watches a repository working tree on Windows and emits changed() (delivered
// on the GUI thread) when any file that git cares about is created, deleted,
// renamed, or has its last-write time modified.
//
// Watch model — SELECTIVE, not a single recursive root watch. The worker keeps:
//   * one NON-recursive watch on the repo root (structural: detects top-level
//     entries appearing/disappearing, and changes to top-level files), plus
//   * one RECURSIVE watch per top-level child directory that is NOT gitignored.
// A top-level ignored directory (node_modules/, build/, .git/, …) gets NO
// watch, so the OS generates ZERO change events for it. This is the whole
// point: a prior design watched the root recursively and filtered ignored
// events after the fact, but a heavy burst (npm install) overflowed the
// notification buffer — at which point the batch is unfilterable and the panel
// refreshed ~1.3x/second for the whole install, pegging the CPU. Not watching
// the ignored dirs at all eliminates the events at the source.
//
// All directory handles are serviced through a single I/O Completion Port on
// one worker thread (NOT WaitForMultipleObjects, which caps at 64 handles — a
// monorepo root can exceed that). The port also carries control messages
// (stop, reconfigure) via PostQueuedCompletionStatus, so the worker owns every
// HANDLE/OVERLAPPED/buffer and the watch map with no locks. See the design note
// openspec/changes/optimize-git-tree-watcher-ignored/design.md for the
// handle-lifecycle (deferred teardown on cancellation completion) and the
// new-top-level-dir race resolution (arm-time notify + git-status rescan).
//
// Nested ignores (an ignored dir inside a watched top-level dir, e.g.
// src/generated/) still reach a watched buffer and are dropped by the retained
// per-event isUnderIgnored() filter; the 750ms overflow throttle bounds the
// rare nested-burst-overflow case.
class RecursiveTreeWatcher : public QObject
{
    Q_OBJECT
public:
    explicit RecursiveTreeWatcher(QObject *parent = nullptr);
    ~RecursiveTreeWatcher() override;

    void start(const QString &path);
    void stop();
    void ackNotify() { m_pendingNotify.store(false, std::memory_order_release); }
    bool isRunning() const { return m_thread != nullptr && m_thread->isRunning(); }

    // Set the repo-relative paths git considers ignored (from
    // `git ls-files --others --ignored --directory`; dirs carry a trailing
    // slash). Publishes a new normalised snapshot AND posts a reconfigure to
    // the worker so the live watch set is recomputed: directories that became
    // ignored have their watches torn down, directories that became un-ignored
    // get fresh recursive watches. Safe to call from the GUI thread at any
    // time; the worker applies the change on its own thread (no HANDLE is
    // touched here). Passing an empty list clears the filter (everything
    // top-level becomes watched again). Paths are normalised internally
    // (lowercased, '/'-separated, trailing slash stripped) so matching is case-
    // and separator-insensitive, matching Windows filesystem semantics.
    void setIgnoredPrefixes(const QStringList &prefixes);

signals:
    void changed();

private:
    QThread *m_thread = nullptr;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_pendingNotify{false};
    void *m_stopEvent = nullptr; // HANDLE — legacy stop event, retained for ABI

    // I/O Completion Port that services every directory watch and carries the
    // stop/reconfigure control messages. Created on the worker in start(),
    // published here (seq_cst) so the GUI thread can PostQueuedCompletionStatus
    // to it. A null value means no worker is up; a stop requested in the publish
    // window is caught by the worker's paired seq_cst load of m_stop (Dekker
    // handshake in runWatcherWorker / stop()).
    std::atomic<void *> m_port{nullptr}; // HANDLE

    // Immutable snapshot of normalised ignored prefixes, swapped atomically on
    // update and loaded by the worker when it (re)derives the watch set or
    // filters a nested-ignore event. shared_ptr keeps a snapshot alive for the
    // duration of a worker read even if the GUI thread publishes a newer one.
    std::atomic<std::shared_ptr<const std::vector<std::wstring>>> m_ignored;
};

#endif // Q_OS_WIN
#endif // RECURSIVE_TREE_WATCHER_H
