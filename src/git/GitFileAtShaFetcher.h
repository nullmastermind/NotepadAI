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

#ifndef GIT_FILE_AT_SHA_FETCHER_H
#define GIT_FILE_AT_SHA_FETCHER_H

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

class GitProcessRunner;

// Fetches a single file's content at a specific commit revision via
//   git -c color.ui=never show <sha>:<relPath>
//
// Used by the "click filename in commit detail diff → open file at that
// revision" flow. Output is bounded by a 5MB cap; binary detection is the
// caller's responsibility (use GitCommitDetail::FileStat::isBinary). The
// fetcher will still return whatever bytes git produces for a binary file —
// the host UI must check the flag and decide whether to render.
class GitFileAtShaFetcher : public QObject
{
    Q_OBJECT
public:
    explicit GitFileAtShaFetcher(QObject *parent = nullptr);
    ~GitFileAtShaFetcher() override;

    using Callback = std::function<void(const QByteArray &bytes,
                                        bool truncated,
                                        const QString &errorMessage)>;

    void setRepoRoot(const QString &repoToplevel);
    QString repoRoot() const { return m_repoRoot; }

    // Request file content. Cancels any in-flight fetch. On failure (commit
    // or path not found, git missing) errorMessage is non-empty.
    void request(const QByteArray &sha, const QString &relPath, Callback cb);

    void cancel();

private:
    QString          m_repoRoot;
    GitProcessRunner *m_runner = nullptr;
    quint64          m_generation = 0;

    static constexpr qint64 kCapBytes = 5 * 1024 * 1024;
    static constexpr int    kTimeoutMs = 30000;
};

#endif // GIT_FILE_AT_SHA_FETCHER_H
