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

#include "GitCommitView.h"

#include "CrashContext.h"
#include "EditorManager.h"
#include "GitCommitFetcher.h"
#include "GitDiffPainter.h"
#include "GitDiffPalette.h"
#include "GitFileAtShaFetcher.h"
#include "GitWatcher.h"
#include "LineNumbers.h"
#include "ScintillaNext.h"

#include "Scintilla.h"
#include "ScintillaTypes.h"

#include <QFileInfo>

#include <algorithm>

namespace {

QString tabTitleForCommit(const GitCommitDetail &detail)
{
    QString shortSha = QString::fromLatin1(detail.sha.left(7));
    QString subj = QString::fromUtf8(detail.subject);
    if (subj.size() > 60) subj = subj.left(60) + QStringLiteral("…");
    if (shortSha.isEmpty()) return subj;
    if (subj.isEmpty()) return shortSha;
    return shortSha + QStringLiteral(" — ") + subj;
}

QString tabTitleForFileAtSha(const QByteArray &sha, const QString &relPath)
{
    return QString::fromLatin1(sha.left(7)) + QLatin1Char(':') + relPath;
}

QByteArray makeFileAtShaKey(const QByteArray &sha, const QString &relPath)
{
    QByteArray k = sha;
    k += ':';
    k += relPath.toUtf8();
    return k;
}

} // namespace

GitCommitView::GitCommitView(const QString &repoRoot,
                              EditorManager *editorManager,
                              QObject *parent)
    : QObject(parent),
      m_repoRoot(repoRoot),
      m_editorManager(editorManager),
      m_fetcher(new GitCommitFetcher(this)),
      m_fileFetcher(new GitFileAtShaFetcher(this))
{
    m_fetcher->setRepoRoot(repoRoot);
    m_fileFetcher->setRepoRoot(repoRoot);
}

GitCommitView::~GitCommitView() = default;

void GitCommitView::setRepoRoot(const QString &repoRoot)
{
    if (repoRoot == m_repoRoot) return;
    m_repoRoot = repoRoot;
    m_fetcher->setRepoRoot(repoRoot);
    m_fileFetcher->setRepoRoot(repoRoot);
    // Cache is per-repo; setRepoRoot already invalidates.
}

void GitCommitView::setDarkPalette(bool dark)
{
    if (m_dark == dark) return;
    m_dark = dark;
    // Re-apply palette to the shared editors. Scintilla style lookup tables
    // refresh on configureEditor; the buffer's style bytes don't need rewriting.
    const GitDiffPalette &pal = GitDiffPalette::current(m_dark);
    if (auto *e = m_detailEditor.data()) {
        GitDiffPainter::configureEditor(e, pal);
    }
    if (auto *e = m_fileAtShaEditor.data()) {
        GitDiffPainter::configureEditor(e, pal);
    }
}

void GitCommitView::setWatcher(GitWatcher *watcher)
{
    if (!watcher) return;
    m_fetcher->connectWatcher(watcher);
}

void GitCommitView::openForSha(const QByteArray &sha)
{
    if (sha.isEmpty() || !m_editorManager) return;
    CrashContext::setLastAction(QStringLiteral("openCommitDetail:")
                                 + QString::fromLatin1(sha.left(7)));

    // Same SHA already showing in the shared tab → just focus, no refetch.
    if (m_detailEditor && sha == m_currentSha) {
        emit focusExistingCommitEditor(m_detailEditor.data());
        return;
    }

    const bool needNewEditor = !m_detailEditor;
    ScintillaNext *editor = m_detailEditor.data();

    if (needNewEditor) {
        editor = m_editorManager->createEditor(
            QStringLiteral("[Commit ") + QString::fromLatin1(sha.left(7))
            + QStringLiteral("]"));
        m_editorManager->registerAsDiffView(editor);
        if (auto *ln = editor->findChild<LineNumbers *>(QString(),
                                                         Qt::FindDirectChildrenOnly)) {
            ln->setEnabled(false);
        }
        const GitDiffPalette &pal = GitDiffPalette::current(m_dark);
        GitDiffPainter::configureEditor(editor, pal);
        m_detailEditor = editor;

        // Indicator-click hook (file paths in diff section). Pattern mirrors
        // URLFinder decorator — there's no dedicated indicatorClick signal on
        // ScintillaEditBase.
        connect(editor, &ScintillaEditBase::notify, this,
                [this, editor](Scintilla::NotificationData *scn) {
            if (!scn) return;
            if (scn->nmhdr.code != Scintilla::Notification::IndicatorClick) return;
            onIndicatorClicked(editor, static_cast<int>(scn->position));
        });
        // User closed the tab → drop our pointer + state so next click
        // creates a fresh one.
        connect(editor, &QObject::destroyed, this, [this](QObject *) {
            m_currentSha.clear();
            m_currentFileLinks.clear();
        });

        emit newCommitEditorCreated(editor);
    } else {
        // Reusing the existing tab — show a placeholder title while the new
        // content is being fetched so the user gets immediate feedback.
        editor->setName(QStringLiteral("[Commit ") + QString::fromLatin1(sha.left(7))
                        + QStringLiteral("]"));
    }

    m_currentSha = sha;
    m_currentFileLinks.clear();

    m_fetcher->requestDetail(sha,
        [this, sha](const std::shared_ptr<const GitCommitDetail> &detail) {
        // Stale guard — user may have clicked another SHA before this fetch
        // returned. m_currentSha tracks the latest click.
        if (sha != m_currentSha) return;
        ScintillaNext *e = m_detailEditor.data();
        if (!e) return;
        if (!detail || detail->isEmpty()) {
            emit fetchFailed(sha, tr("Could not load commit %1")
                                    .arg(QString::fromLatin1(sha.left(7))));
            return;
        }
        e->setName(tabTitleForCommit(*detail));
        QVector<GitCommitRenderer::FileLink> links;
        renderInto(e, *detail, links);
        m_currentFileLinks = std::move(links);
        emit commitDetailRendered(e);
    });
}

void GitCommitView::renderInto(ScintillaNext *editor,
                                const GitCommitDetail &detail,
                                QVector<GitCommitRenderer::FileLink> &outLinks)
{
    if (!editor) return;
    auto res = GitCommitRenderer::render(editor, detail,
                                          GitDiffPalette::current(m_dark));
    outLinks = std::move(res.fileLinks);
    // Pre-sort by start for binary search in onIndicatorClicked.
    std::sort(outLinks.begin(), outLinks.end(),
              [](const GitCommitRenderer::FileLink &a,
                 const GitCommitRenderer::FileLink &b) {
                  return a.start < b.start;
              });
}

void GitCommitView::onIndicatorClicked(ScintillaNext *editor, int position)
{
    if (!editor || editor != m_detailEditor.data()) return;
    if (m_currentSha.isEmpty() || m_currentFileLinks.isEmpty()) return;

    const auto &links = m_currentFileLinks;
    int lo = 0, hi = links.size() - 1;
    int hit = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const auto &fl = links[mid];
        if (position < fl.start) {
            hi = mid - 1;
        } else if (position >= fl.start + fl.length) {
            lo = mid + 1;
        } else {
            hit = mid;
            break;
        }
    }
    if (hit < 0) return;
    const QString relPath = QString::fromUtf8(links[hit].path);
    openFileAtSha(m_currentSha, relPath);
}

void GitCommitView::openFileAtSha(const QByteArray &sha, const QString &relPath)
{
    if (sha.isEmpty() || relPath.isEmpty() || !m_editorManager) return;
    const QByteArray key = makeFileAtShaKey(sha, relPath);

    // Same (sha, relPath) already showing → just focus.
    if (m_fileAtShaEditor && key == m_currentFileAtShaKey) {
        emit focusExistingFileAtShaEditor(m_fileAtShaEditor.data());
        return;
    }

    const bool needNewEditor = !m_fileAtShaEditor;
    ScintillaNext *editor = m_fileAtShaEditor.data();

    if (needNewEditor) {
        editor = m_editorManager->createEditor(tabTitleForFileAtSha(sha, relPath));
        m_editorManager->registerAsDiffView(editor);
        m_fileAtShaEditor = editor;
        connect(editor, &QObject::destroyed, this, [this](QObject *) {
            m_currentFileAtShaKey.clear();
        });
        emit newFileAtShaEditorCreated(editor);
    } else {
        editor->setName(tabTitleForFileAtSha(sha, relPath));
    }

    m_currentFileAtShaKey = key;

    m_fileFetcher->request(sha, relPath,
        [this, key](const QByteArray &bytes, bool truncated, const QString &err) {
        if (key != m_currentFileAtShaKey) return;     // stale
        ScintillaNext *e = m_fileAtShaEditor.data();
        if (!e) return;
        e->send(SCI_SETREADONLY, 0, 0);
        e->send(SCI_CLEARALL, 0, 0);
        if (!err.isEmpty()) {
            const QByteArray msg = tr("Could not load file: %1").arg(err).toUtf8();
            e->send(SCI_APPENDTEXT, msg.size(),
                    reinterpret_cast<sptr_t>(msg.constData()));
        } else {
            e->send(SCI_APPENDTEXT, bytes.size(),
                    reinterpret_cast<sptr_t>(bytes.constData()));
            if (truncated) {
                const QByteArray foot =
                    QByteArrayLiteral("\n... file truncated (too large) ...\n");
                e->send(SCI_APPENDTEXT, foot.size(),
                        reinterpret_cast<sptr_t>(foot.constData()));
            }
        }
        e->send(SCI_SETREADONLY, 1, 0);
    });
}
