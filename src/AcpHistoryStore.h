/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#ifndef ACP_HISTORY_STORE_H
#define ACP_HISTORY_STORE_H

#include <QHash>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QObject>
#include <QString>

class QTimer;

Q_DECLARE_LOGGING_CATEGORY(lcAcpHistory)

// Worker object that persists per-session ACP transcripts to disk.
//
// Designed to live on its own QThread (owned by AcpAgentManager in Group 4),
// but works on any thread provided callers respect Qt thread-affinity by
// invoking the public slots via QMetaObject::invokeMethod or queued signals.
//
// Writes are debounced per session (500 ms single-shot timer). Calling
// `scheduleWrite` for the same session repeatedly within the debounce window
// REPLACES the pending payload — only the last payload is flushed (this
// matches the model's "rebuild full snapshot on every mutation" approach).
//
// On successful flush the store emits `flushed(sessionId)`. This signal is
// part of the public API — callers (notably tests) use QSignalSpy to wait
// for write completion deterministically.
class AcpHistoryStore : public QObject
{
    Q_OBJECT

public:
    explicit AcpHistoryStore(QObject *parent = nullptr);
    ~AcpHistoryStore() override;

    // Returns the directory where session JSON files are written.
    // Default: <QStandardPaths::AppDataLocation>/acp-history.
    QString historyDir() const;

    // Override the history directory. Intended for tests using a
    // QTemporaryDir. Must be called from the thread that owns the store —
    // tests using a worker-thread store should invoke via QMetaObject
    // (hence Q_INVOKABLE) with a BlockingQueuedConnection.
    Q_INVOKABLE void setHistoryDir(const QString &dir);

    // Builds the on-disk path for a given sessionId — exposed for tests
    // that want to assert file existence/contents.
    QString filePathForSession(const QString &sessionId) const;

public slots:
    // Schedule (or reschedule) a persistence write for `sessionId`. The
    // payload replaces any pending payload for the same session — only the
    // last one wins. Safe to call repeatedly in tight loops.
    void scheduleWrite(const QString &sessionId, const QJsonObject &payload);

    // Delete the on-disk history file for `sessionId` and any pending
    // in-memory payload. Missing-file is treated as success.
    void deleteHistory(const QString &sessionId);

    // Synchronously flush every pending payload to disk. Used at
    // manager-shutdown to drain outstanding writes before the worker
    // thread quits.
    void flushAll();

signals:
    // Emitted after a session payload has been successfully written to
    // disk. Public API (not debug-only): tests use this with QSignalSpy
    // to observe write completion deterministically.
    void flushed(const QString &sessionId);

    // Emitted after deleteHistory completes (whether or not a file existed
    // on disk — missing-file counts as success per the deleteHistory
    // contract). Used by tests to observe queued deletions deterministically.
    void historyDeleted(const QString &sessionId);

private:
    void flushOne(const QString &sessionId);
    QTimer *ensureTimer(const QString &sessionId);

    QString m_historyDir;
    QHash<QString, QTimer *> m_debounceTimers;
    QHash<QString, QJsonObject> m_pendingPayloads;
};

#endif // ACP_HISTORY_STORE_H
