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

#ifndef SUBMODULE_STATUS_FETCHER_H
#define SUBMODULE_STATUS_FETCHER_H

#include "GitStatusEntry.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

class QProcess;

// Runs `git status --porcelain=v2 -z --untracked-files=all --renames` inside
// every submodule of the active workspace concurrently, parses each output,
// and emits an aggregated GitStatusEntries vector whose relPaths are rewritten
// to be relative to the parent workspace root.
//
// Necessary because the parent repo's `git status` reports a submodule as a
// single aggregate entry (isSubmodule=true) and never enumerates the files
// inside it. Without a per-submodule status fetch the file tree cannot show
// accurate colours for paths under a modified submodule.
//
// Concurrency model: each fetch() cancels all in-flight processes and starts
// a fresh round, bumping a generation counter so any late completions from
// the previous round are silently dropped. Result emission happens once,
// when every spawned QProcess has finished (or errored / been cancelled).
class SubmoduleStatusFetcher : public QObject
{
    Q_OBJECT
public:
    struct Submodule {
        QString absPath;      // absolute cleanPath of the submodule's working tree
        QString relFromRoot;  // path of the submodule relative to the workspace root
    };

    explicit SubmoduleStatusFetcher(QObject *parent = nullptr);
    ~SubmoduleStatusFetcher() override;

    // Cancel any in-flight requests, then start one `git status` per submodule
    // in parallel. Emits entriesReady exactly once when all complete.
    // Empty input emits entriesReady({}) synchronously on the next event-loop
    // tick so callers can rely on a deterministic signal.
    void fetch(const QVector<Submodule> &submodules);

    bool isRunning() const { return m_inflight > 0; }

    // --- Test seam (offline, deterministic) ----------------------------------
    // Override the spawned program/args so a unit test can drive the
    // killed-process double-signal path (errorOccurred + finished for one
    // QProcess) without a real submodule or a git executable on PATH. When set,
    // fetch() bypasses the git-availability guard. Not used in production.
    void setSpawnOverrideForTesting(const QString &program, const QStringList &args)
    {
        m_overrideProgram = program;
        m_overrideArgs = args;
    }
    // Shorten the per-task kill timeout so a test can force the kill() path fast.
    void setTimeoutMsForTesting(int ms) { m_timeoutMs = ms; }

signals:
    // Aggregated entries from every submodule's status output. relPath fields
    // are already prefixed with each submodule's path so they are relative to
    // the parent workspace root and can be appended to the parent's entries
    // before calling PathStatusIndex::rebuild.
    void entriesReady(const GitStatusEntries &entries);

private:
    // QObject so lambdas can hold QPointer<Task> and cancelAll can deleteLater
    // instead of `delete t` while a queued finished/readyRead still captures t.
    struct Task : QObject {
        using QObject::QObject;
        QProcess *proc = nullptr;
        QString  relFromRoot;
        QByteArray stdoutBuf;
        int generation = 0;
    };

    void cancelAll();
    void startOne(const Submodule &sub, int generation);
    void onTaskFinished(Task *t, int exitCode);

    QVector<Task *> m_tasks;
    GitStatusEntries m_pending;
    int m_inflight   = 0;
    int m_generation = 0;

    // Test-only spawn override (see setSpawnOverrideForTesting). Empty in
    // production, in which case the real git executable + status argv are used.
    QString     m_overrideProgram;
    QStringList m_overrideArgs;
    int         m_timeoutMs = 30000; // kPerSubmoduleTimeoutMs; overridable in tests
};

#endif // SUBMODULE_STATUS_FETCHER_H
