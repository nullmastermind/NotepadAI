/*
 * This file is part of Notepad Next.
 * Copyright 2021 Justin Dailey
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


#ifndef EDITORMANAGER_H
#define EDITORMANAGER_H

#include <QObject>
#include <QPointer>
#include <QSet>


class ApplicationSettings;
class ScintillaNext;

namespace remote { class IFileSystemBackend; }

class EditorManager : public QObject
{
    Q_OBJECT

public:
    explicit EditorManager(ApplicationSettings *settings, QObject *parent = nullptr);

    ScintillaNext *createEditor(const QString &name);
    ScintillaNext *createEditorFromFile(const QString &filePath, bool tryToCreate=false);

    // Async remote open (D4): creates the editor SHELL synchronously (File + ssh://
    // URI identity stamped immediately) and registers it via manageEditor →
    // editorCreated → addEditor (so the tab + totalTabCount are settled on return,
    // exactly like a local open). Content streams in afterwards; editorLoaded /
    // editorLoadFailed fire when the async read resolves. `backend` must be remote.
    ScintillaNext *createEditorFromRemote(remote::IFileSystemBackend *backend,
                                          const QString &uri,
                                          const QString &remotePath);

    ScintillaNext *getEditorByFilePath(const QString &filePath);

    void manageEditor(ScintillaNext *editor);

    QList<QPointer<ScintillaNext>> getEditors();

    // Updates the cached dark/light flag and re-applies editor colors to every
    // managed editor. Called by NotepadNextApplication when the effective theme
    // changes (and once during startup).
    void applyTheme(bool dark);

    // Diff-view registry. ScintillaNext instances tagged here are treated as
    // read-only diff previews — find/replace UI suppresses Replace, save
    // dialogs skip them, etc. Auto-cleared when the editor is destroyed.
    void registerAsDiffView(ScintillaNext *editor);
    bool isDiffView(const ScintillaNext *editor) const;

signals:
    void editorCreated(ScintillaNext *editor);
    void editorClosed(ScintillaNext *editor);
    void blameCommitClicked(const QByteArray &sha);

    // Async remote load resolution (D4) — Batch H wires these to clear the
    // "Loading…" state / show an error+Retry. Local opens never emit them
    // (they complete synchronously inside createEditorFromFile).
    void editorLoaded(ScintillaNext *editor);
    void editorLoadFailed(ScintillaNext *editor, const QString &error);

private:
    void setupEditor(ScintillaNext *editor);
    void purgeOldEditorPointers();
    int detectEOLMode(ScintillaNext *editor) const;

    // initialSetup=true does the destructive styleClearAll pass (only safe on a
    // freshly-created editor); false preserves per-style foregrounds set by the
    // active lexer and only overrides backgrounds + chrome colors.
    void applyThemeToEditor(ScintillaNext *editor, bool dark, bool initialSetup);

    // Task runner gutter: create or destroy based on filename match.
    void evaluateTaskRunner(ScintillaNext *editor);

    QList<QPointer<ScintillaNext>> editors;
    QSet<const ScintillaNext *> m_diffViews;
    ApplicationSettings *settings;
    bool darkTheme = false;
};

#endif // EDITORMANAGER_H
