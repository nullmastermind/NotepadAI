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

#include "AcpHistoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

Q_LOGGING_CATEGORY(lcAcpHistory, "notepadnext.acp.history")

namespace {
constexpr int kDebounceMs = 500;
} // namespace

AcpHistoryStore::AcpHistoryStore(QObject *parent)
    : QObject(parent)
    , m_historyDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                   + QStringLiteral("/acp-history"))
{
}

AcpHistoryStore::~AcpHistoryStore() = default;

QString AcpHistoryStore::historyDir() const
{
    return m_historyDir;
}

void AcpHistoryStore::setHistoryDir(const QString &dir)
{
    m_historyDir = dir;
}

QString AcpHistoryStore::filePathForSession(const QString &sessionId) const
{
    return m_historyDir + QStringLiteral("/") + sessionId + QStringLiteral(".json");
}

QTimer *AcpHistoryStore::ensureTimer(const QString &sessionId)
{
    auto it = m_debounceTimers.find(sessionId);
    if (it != m_debounceTimers.end()) {
        return it.value();
    }
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(kDebounceMs);
    QObject::connect(timer, &QTimer::timeout, this, [this, sessionId]() {
        flushOne(sessionId);
    });
    m_debounceTimers.insert(sessionId, timer);
    return timer;
}

void AcpHistoryStore::scheduleWrite(const QString &sessionId, const QJsonObject &payload)
{
    if (sessionId.isEmpty()) {
        qCWarning(lcAcpHistory) << "scheduleWrite called with empty sessionId — ignoring";
        return;
    }
    m_pendingPayloads.insert(sessionId, payload);
    QTimer *timer = ensureTimer(sessionId);
    // start() on an already-running single-shot timer resets the interval.
    timer->start(kDebounceMs);
}

void AcpHistoryStore::deleteHistory(const QString &sessionId)
{
    if (sessionId.isEmpty()) {
        return;
    }
    auto it = m_debounceTimers.find(sessionId);
    if (it != m_debounceTimers.end()) {
        it.value()->stop();
        it.value()->deleteLater();
        m_debounceTimers.erase(it);
    }
    m_pendingPayloads.remove(sessionId);
    const QString path = filePathForSession(sessionId);
    QFile f(path);
    if (f.exists()) {
        if (!f.remove()) {
            qCWarning(lcAcpHistory) << "failed to delete history file:" << path
                                    << f.errorString();
        }
    }
    emit historyDeleted(sessionId);
}

void AcpHistoryStore::flushAll()
{
    // Snapshot keys — flushOne mutates m_pendingPayloads.
    const QStringList sessions = m_pendingPayloads.keys();
    for (const QString &sid : sessions) {
        auto it = m_debounceTimers.find(sid);
        if (it != m_debounceTimers.end()) {
            it.value()->stop();
        }
        flushOne(sid);
    }
}

void AcpHistoryStore::flushOne(const QString &sessionId)
{
    auto it = m_pendingPayloads.find(sessionId);
    if (it == m_pendingPayloads.end()) {
        return;
    }
    const QJsonObject payload = it.value();
    m_pendingPayloads.erase(it);

    if (m_historyDir.isEmpty()) {
        qCWarning(lcAcpHistory) << "historyDir is empty — skipping flush for" << sessionId;
        return;
    }

    QDir().mkpath(m_historyDir);

    const QString target = filePathForSession(sessionId);
    QSaveFile saveFile(target);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcAcpHistory) << "[acp-history] open failed:" << target
                                << saveFile.errorString();
        return;
    }
    const QByteArray bytes = QJsonDocument(payload).toJson(QJsonDocument::Indented);
    const qint64 written = saveFile.write(bytes);
    if (written != bytes.size()) {
        qCWarning(lcAcpHistory) << "[acp-history] short write:" << target
                                << written << "/" << bytes.size();
        // Fall through to commit which will fail; let it report.
    }
    if (!saveFile.commit()) {
        qCWarning(lcAcpHistory) << "[acp-history] write failed:" << target
                                << saveFile.errorString();
        return;
    }
    emit flushed(sessionId);
}
