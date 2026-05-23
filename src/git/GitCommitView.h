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

#ifndef GIT_COMMIT_VIEW_H
#define GIT_COMMIT_VIEW_H

#include "GitCommitDetail.h"
#include "GitCommitRenderer.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <memory>

class EditorManager;
class GitCommitFetcher;
class GitFileAtShaFetcher;
class GitWatcher;
class ScintillaNext;

// Host-facing controller for the "click a commit → open editor tab"
// experience. Mirrors GitDiffViewController shape: a SINGLE shared
// commit-detail tab whose contents are replaced on each click, plus a
// SINGLE shared file-at-sha tab for file-link clicks inside that view.
// This matches Zed's preview-tab UX (and the user's screenshots) — one
// commit tab open at a time, repeated clicks re-populate it rather
// than fanning out into N tabs.
class GitCommitView : public QObject
{
    Q_OBJECT
public:
    GitCommitView(const QString &repoRoot,
                  EditorManager *editorManager,
                  QObject *parent = nullptr);
    ~GitCommitView() override;

    void setRepoRoot(const QString &repoRoot);
    QString repoRoot() const { return m_repoRoot; }

    void setDarkPalette(bool dark);

    // Hook GitWatcher → invalidate the commit fetcher's cache on refs/HEAD
    // changes (force-push can re-write SHAs but leave GC'd objects).
    void setWatcher(GitWatcher *watcher);

public slots:
    // Show commit detail in the shared preview tab. Creates the tab if it
    // doesn't exist yet; otherwise reuses it and re-fetches/re-renders.
    void openForSha(const QByteArray &sha);

signals:
    // The shared commit-detail editor was just created and needs to be
    // raised (first ever click). Subsequent clicks reuse the same editor
    // so they only emit commitDetailRendered / focusExistingCommitEditor.
    void newCommitEditorCreated(ScintillaNext *editor);

    // Content for the requested SHA finished rendering — host raises it.
    void commitDetailRendered(ScintillaNext *editor);

    // User clicked the same SHA that's already showing → focus, no refetch.
    void focusExistingCommitEditor(ScintillaNext *editor);

    // Same pattern for the shared file-at-sha tab.
    void newFileAtShaEditorCreated(ScintillaNext *editor);
    void focusExistingFileAtShaEditor(ScintillaNext *editor);

    // Surface fetch failures so the host can show a status banner.
    void fetchFailed(const QByteArray &sha, const QString &message);

private:
    void renderInto(ScintillaNext *editor, const GitCommitDetail &detail,
                    QVector<GitCommitRenderer::FileLink> &outLinks);
    void onIndicatorClicked(ScintillaNext *editor, int position);

    // Show file content at a SHA in the shared file-at-sha tab.
    void openFileAtSha(const QByteArray &sha, const QString &relPath);

    QString          m_repoRoot;
    EditorManager   *m_editorManager;
    GitCommitFetcher *m_fetcher;
    GitFileAtShaFetcher *m_fileFetcher;
    bool             m_dark = false;

    // Shared commit-detail tab. QPointer auto-nulls when the user closes it.
    QPointer<ScintillaNext> m_detailEditor;
    QByteArray              m_currentSha;
    QVector<GitCommitRenderer::FileLink> m_currentFileLinks;

    // Shared file-at-sha tab.
    QPointer<ScintillaNext> m_fileAtShaEditor;
    QByteArray              m_currentFileAtShaKey;
};

#endif // GIT_COMMIT_VIEW_H
