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

#include "SubmoduleStatusFetcher.h"

#include "GitProcessRunner.h"
#include "GitStatusParser.h"

#include <QPointer>
#include <QProcess>
#include <QTimer>

namespace {
constexpr int kPerSubmoduleTimeoutMs = 30000;
} // namespace

SubmoduleStatusFetcher::SubmoduleStatusFetcher(QObject *parent) : QObject(parent)
{
    m_timeoutMs = kPerSubmoduleTimeoutMs;
}

SubmoduleStatusFetcher::~SubmoduleStatusFetcher()
{
    cancelAll();
}

void SubmoduleStatusFetcher::cancelAll()
{
    // Bump generation so any late-finishing process is ignored. Detach and
    // kill in-flight processes. Lambdas hold QPointer<Task>, so a queued
    // finished/readyRead after this is a no-op rather than a UAF. Unparent
    // each QProcess before deleteLater so ~SubmoduleStatusFetcher cannot
    // destroy a still-running child (QProcess warns/crashes in that case).
    ++m_generation;
    for (Task *t : m_tasks) {
        if (t->proc) {
            t->proc->disconnect(this);
            t->proc->setParent(nullptr);
            if (t->proc->state() != QProcess::NotRunning)
                t->proc->kill();
            t->proc->deleteLater();
            t->proc = nullptr;
        }
        t->deleteLater();
    }
    m_tasks.clear();
    m_pending.clear();
    m_inflight = 0;
}

void SubmoduleStatusFetcher::fetch(const QVector<Submodule> &submodules)
{
    cancelAll();

    const bool haveSpawnTarget =
        !m_overrideProgram.isEmpty() || !GitProcessRunner::gitExecutable().isEmpty();
    if (submodules.isEmpty() || !haveSpawnTarget) {
        // Single-shot empty emission on the next tick so callers see a
        // deterministic signal regardless of whether anything spawned.
        QTimer::singleShot(0, this, [this]() {
            emit entriesReady({});
        });
        return;
    }

    const int gen = m_generation;
    m_inflight = submodules.size();
    m_tasks.reserve(submodules.size());
    for (const Submodule &s : submodules) {
        startOne(s, gen);
    }
}

void SubmoduleStatusFetcher::startOne(const Submodule &sub, int generation)
{
    auto *t = new Task(this);
    t->relFromRoot = sub.relFromRoot;
    t->generation = generation;
    t->proc = new QProcess(this);

    QString program;
    QStringList argv;
    if (!m_overrideProgram.isEmpty()) {
        program = m_overrideProgram;
        argv = m_overrideArgs;
    } else {
        program = GitProcessRunner::gitExecutable();
        argv = {
            QStringLiteral("-c"), QStringLiteral("core.quotepath=false"),
            QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
            QStringLiteral("-z"),
            QStringLiteral("--untracked-files=all"),
            QStringLiteral("--renames"),
        };
    }
    t->proc->setProgram(program);
    t->proc->setArguments(argv);
    t->proc->setWorkingDirectory(sub.absPath);
    t->proc->setProcessEnvironment(GitProcessRunner::baseEnv());
    t->proc->setProcessChannelMode(QProcess::SeparateChannels);

    connect(t->proc, &QProcess::readyReadStandardOutput, this, [t = QPointer<Task>(t)]() {
        if (!t || !t->proc) return;
        t->stdoutBuf.append(t->proc->readAllStandardOutput());
    });
    connect(t->proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, t = QPointer<Task>(t)](int code, QProcess::ExitStatus status) {
        // A killed/crashed process emits BOTH finished and errorOccurred for
        // the same QProcess. onTaskFinished disconnects this task on first
        // entry, so the partner signal can never re-enter; the generation +
        // null + QPointer guards here catch a stale/late delivery after cancelAll.
        if (!t || t->generation != m_generation || !t->proc) return;
        t->stdoutBuf.append(t->proc->readAllStandardOutput());
        const int effectiveExit = (status == QProcess::CrashExit) ? -1 : code;
        onTaskFinished(t, effectiveExit);
    });
    connect(t->proc, &QProcess::errorOccurred, this, [this, t = QPointer<Task>(t)](QProcess::ProcessError) {
        if (!t || t->generation != m_generation || !t->proc) return;
        t->stdoutBuf.append(t->proc->readAllStandardOutput());
        onTaskFinished(t, -1);
    });

    // Per-task timeout: kill the process; its finished()/errorOccurred() will
    // fire and flow through the (idempotent) onTaskFinished, so m_inflight is
    // still decremented and entriesReady is never stranded. Guard the process
    // with a QPointer and bind the timer to `this` (which outlives every Task)
    // so a fire after the task was freed/cancelled is a harmless no-op rather
    // than a use-after-free.
    QPointer<QProcess> procGuard(t->proc);
    QTimer::singleShot(m_timeoutMs, this, [procGuard]() {
        if (procGuard && procGuard->state() != QProcess::NotRunning) {
            procGuard->kill();
        }
    });

    m_tasks.append(t);
    t->proc->start();
    // Invalid cwd (rm -rf of the submodule) fails to start. errorOccurred may
    // already have run onTaskFinished; the idempotent !t->proc guard makes a
    // second call here a no-op. If Qt left the process NotRunning without
    // emitting, this is what un-strands m_inflight / entriesReady.
    if (t->proc && t->proc->state() == QProcess::NotRunning)
        onTaskFinished(t, -1);
}

void SubmoduleStatusFetcher::onTaskFinished(Task *t, int exitCode)
{
    // IDEMPOTENT GUARD. A killed/crashed QProcess emits BOTH finished and
    // errorOccurred, so this can be reached twice for one Task. The first entry
    // nulls t->proc; a second entry bails HERE — before --m_inflight — so each
    // Task decrements the counter exactly once (a double decrement would drive
    // m_inflight negative or fire entriesReady while siblings are still live).
    if (!t->proc) return;

    // Sever this task from its QProcess up front so the partner signal of the
    // pair cannot re-enter this slot at all (mirrors GitProcessRunner teardown).
    t->proc->disconnect(this);

    if (t->generation != m_generation) {
        // Stale completion from a cancelled round — discard silently.
        t->proc->setParent(nullptr);
        t->proc->deleteLater();
        t->proc = nullptr;
        return;
    }

    if (exitCode == 0 && !t->stdoutBuf.isEmpty()) {
        GitStatusEntries inner = GitStatusParser::parsePorcelainV2(t->stdoutBuf, nullptr);
        // Rewrite each entry's relPath to be relative to the parent workspace
        // root by prefixing the submodule's relative path. origRelPath (for
        // renames) gets the same treatment.
        const QString prefix = t->relFromRoot + QLatin1Char('/');
        m_pending.reserve(m_pending.size() + inner.size());
        for (GitStatusEntry &e : inner) {
            if (!e.relPath.isEmpty()) e.relPath = prefix + e.relPath;
            if (!e.origRelPath.isEmpty()) e.origRelPath = prefix + e.origRelPath;
            m_pending.append(std::move(e));
        }
    }
    // Note: exitCode != 0 (e.g. submodule directory is empty / not a repo, or
    // the process was killed by the per-task timeout) is silently treated as
    // "no entries". This matches the user-visible outcome of no decorations for
    // that subtree, which is preferable to a hard error.

    t->proc->setParent(nullptr);
    t->proc->deleteLater();
    t->proc = nullptr;

    --m_inflight;
    if (m_inflight <= 0) {
        const GitStatusEntries out = std::move(m_pending);
        m_pending.clear();
        // Free the task storage before emitting so subscribers calling
        // fetch() again from the slot see a clean state.
        for (Task *task : m_tasks) task->deleteLater();
        m_tasks.clear();
        emit entriesReady(out);
    }
}
