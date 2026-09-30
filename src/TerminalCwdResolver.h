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

#ifndef TERMINALCWDRESOLVER_H
#define TERMINALCWDRESOLVER_H

#include <QList>
#include <QString>
#include <QStringList>

namespace remote { class ExecutionContext; }

class TerminalCwdResolver
{
public:
    static bool canOpenInWorkspace(const QString &workspaceRoot);
    static bool canOpenInFolder(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot);

    static QString resolveWorkspace(const QString &workspaceRoot);
    static QString resolveFolder(const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot);

    // Context-aware menu gating/resolution. Local/null contexts preserve the
    // legacy local QFileInfo policy. Remote contexts accept only ssh:// workspace
    // or file URIs, extract their POSIX remote path, and normalize through
    // resolveForContext().
    static bool canOpenInWorkspaceForContext(remote::ExecutionContext *ctx, const QString &workspaceRoot);
    static bool canOpenInFolderForContext(remote::ExecutionContext *ctx, const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot);
    static QString resolveWorkspaceForContext(remote::ExecutionContext *ctx, const QString &workspaceRoot);
    static QString resolveFolderForContext(remote::ExecutionContext *ctx, const QString &activeFilePath, bool activeBufferIsFile, const QString &workspaceRoot);

    // Context-aware resolution (design D11). When `ctx` is remote: NO local
    // QFileInfo check (the path is on another machine) — require the context to
    // be Connected and `requested` non-empty, then POSIX-normalize. When `ctx`
    // is local or null: delegates to resolveWorkspace() so the existing local
    // behavior is byte-for-byte preserved. Returns empty on a disabled case.
    static QString resolveForContext(remote::ExecutionContext *ctx, const QString &requested);

    enum class CwdSpace : quint8 { Local, Remote };

    // True iff captured terminal cwd is the workspace root or a descendant.
    // Local space matches only local (non-ssh) roots. Remote space matches only
    // ssh:// roots, comparing POSIX cwd (or an ssh:// cwd's remotePath) to the
    // URI's remotePath.
    static bool cwdBelongsToWorkspace(const QString &cwd, CwdSpace space, const QString &workspaceRoot);

    // Open workspace root that owns `cwd` (longest matching prefix), or empty.
    static QString matchingWorkspace(const QString &cwd, CwdSpace space, const QStringList &workspaceRoots);

    // Tab-strip visibility. Filter to the active workspace unless any terminal
    // matches no open workspace — then every index is visible (no filter).
    // Empty active root or empty open-roots → all visible.
    static QList<int> visibleTerminalIndices(const QStringList &cwds,
                                             const QList<CwdSpace> &spaces,
                                             const QStringList &workspaceRoots,
                                             const QString &activeWorkspaceRoot);
    static bool workspaceRootsEqual(const QString &a, const QString &b);

    // Per-tab want after the unmatched scan. `anyUnmatched` true → show every
    // tab (no workspace filter). Otherwise only the active workspace's owner.
    static bool terminalTabWanted(const QString &cwd, CwdSpace space,
                                  const QStringList &openWorkspaceRoots,
                                  const QString &activeWorkspaceRoot,
                                  bool anyUnmatched);


    // Terminals owned by `closingWorkspaceRoot` (longest matching prefix among
    // open roots plus the closing root). Empty closing root → none.
    static QList<int> closeTerminalIndices(const QStringList &cwds,
                                           const QList<CwdSpace> &spaces,
                                           const QStringList &openWorkspaceRoots,
                                           const QString &closingWorkspaceRoot);

};

#endif
