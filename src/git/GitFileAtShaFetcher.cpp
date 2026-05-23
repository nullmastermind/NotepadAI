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

#include "GitFileAtShaFetcher.h"

#include "GitProcessRunner.h"

#include <QStringList>

GitFileAtShaFetcher::GitFileAtShaFetcher(QObject *parent)
    : QObject(parent), m_runner(new GitProcessRunner(this))
{
    m_runner->setMaxOutputBytes(kCapBytes);
}

GitFileAtShaFetcher::~GitFileAtShaFetcher() = default;

void GitFileAtShaFetcher::setRepoRoot(const QString &repoToplevel)
{
    if (repoToplevel == m_repoRoot) return;
    cancel();
    m_repoRoot = repoToplevel;
}

void GitFileAtShaFetcher::request(const QByteArray &sha,
                                  const QString &relPath, Callback cb)
{
    if (m_repoRoot.isEmpty() || sha.isEmpty() || relPath.isEmpty()) {
        if (cb) cb({}, false, QStringLiteral("invalid request"));
        return;
    }
    cancel();
    ++m_generation;
    const quint64 myGen = m_generation;

    // Argument: <sha>:<path>. Forward slashes are required even on Windows
    // (git's object spec uses internal POSIX paths).
    const QByteArray spec = sha + ':' + relPath.toUtf8();
    const QStringList argv = {
        QStringLiteral("-c"), QStringLiteral("color.ui=never"),
        QStringLiteral("show"),
        QString::fromUtf8(spec)
    };
    m_runner->run(m_repoRoot, argv, QByteArray(), kTimeoutMs, false,
                  [this, myGen, cb = std::move(cb)](int exit,
                                                    const QByteArray &out,
                                                    const QByteArray &err) {
        if (myGen != m_generation) return;
        const bool truncated = (exit == GitProcessRunner::kExitTruncated);
        if (exit != 0 && !truncated) {
            const QString msg = QString::fromUtf8(err).trimmed();
            if (cb) cb({}, false, msg.isEmpty()
                                  ? QStringLiteral("git show failed") : msg);
            return;
        }
        if (cb) cb(out, truncated, QString());
    });
}

void GitFileAtShaFetcher::cancel()
{
    if (m_runner && m_runner->isRunning()) m_runner->cancelAsync();
    ++m_generation;
}
