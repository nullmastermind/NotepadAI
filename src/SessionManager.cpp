/*
 * This file is part of Notepad Next.
 * Copyright 2022 Justin Dailey
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


#include "BookMarkDecorator.h"
#include "ProfileScope.h"
#include "ScintillaNext.h"
#include "MainWindow.h"
#include "SessionManager.h"
#include "EditorManager.h"
#include "NotepadNextApplication.h"
#include "DataPaths.h"

#include "remote/ExecutionContext.h"
#include "remote/ExecutionContextRegistry.h"
#include "remote/RemoteExecutionContext.h"
#include "remote/IFileSystemBackend.h"
#include "remote/SshProfile.h"

#include <QDir>
#include <QPointer>
#include <QUuid>


static QString RandomSessionFileName()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// QList<int> cannot be automatically serialized to/from QSettings (i.e. QVariant) so turn it to a QVariantList
static QVariantList QListToQVariantList(const QList<int> intList)
{
    QVariantList vl;
    for (const int i : intList){
        vl.append(i);
    }
    return vl;
}

// Do the opposite of the function above
static QList<int> QVariantListToQList(const QVariantList &variantList) {
    QList<int> intList;
    for (const QVariant &variant : variantList) {
        intList.append(variant.toInt());
    }
    return intList;
}

SessionManager::SessionManager(NotepadNextApplication *app, SessionFileTypes types)
    : app(app)
{
    setSessionFileTypes(types);
}

void SessionManager::setSessionFileTypes(SessionFileTypes types)
{
    fileTypes = types;
}

QDir SessionManager::sessionDirectory() const
{
    QDir d(DataPaths::appDataLocation());

    d.mkpath("session");
    d.cd("session");

    return d;
}

void SessionManager::saveIntoSessionDirectory(ScintillaNext *editor, const QString &sessionFileName) const
{
    editor->saveCopyAs(sessionDirectory().filePath(sessionFileName));
}

SessionManager::SessionFileType SessionManager::determineType(ScintillaNext *editor) const
{
    if (editor->isFile()) {
        if (editor->isSavedToDisk()) {
            return SessionManager::SavedFile;
        }
        else {
            return SessionManager::UnsavedFile;
        }
    }
    else {
        if (!editor->isSavedToDisk()) {
            return SessionManager::TempFile;
        }
        else {
            return SessionManager::None;
        }
    }
}

void SessionManager::clear() const
{
    clearSettings();
    clearDirectory();
}

void SessionManager::clearSettings() const
{
    ApplicationSettings settings;;

    // Clear everything out. There can be left over entries that are no longer needed
    settings.beginGroup("CurrentSession");
    settings.remove("");
}

void SessionManager::clearDirectory() const
{
    QDir d = sessionDirectory();

    for (const QString &f : d.entryList()) {
        d.remove(f);
    }
}

void SessionManager::saveSession(MainWindow *window)
{
    PROFILE_SCOPE("SessionManager::saveSession");
    qInfo(Q_FUNC_INFO);

    clear();

    const ScintillaNext *currentEditor = window->currentEditor();
    int currentEditorIndex = 0;
    ApplicationSettings settings;;

    settings.beginGroup("CurrentSession");

    settings.beginWriteArray("OpenedFiles");

    // SavedFile entries (path + cursor + bookmarks) are written unconditionally,
    // even when the matching flag is off. They are cheap (~200-300 bytes per file,
    // INI-only, no content copy to disk) and persisting them lets the user enable
    // "restore previous session" later without losing the tabs that were open at
    // the time the setting was off. UnsavedFile/TempFile remain gated because
    // they copy editor content into the session directory on every save (60s
    // autosave) and can be expensive.
    int index = 0;
    for (const auto &editor : window->editors()) {
        SessionFileType editorType = determineType(editor);

        const bool shouldStore =
            editorType == SessionManager::SavedFile          // always
            || fileTypes.testFlag(editorType);               // gated for Unsaved/Temp

        if (shouldStore) {
            settings.setArrayIndex(index);

            if (editorType == SessionManager::SavedFile) {
                storeFileDetails(editor, settings);
            }
            else if (editorType == SessionManager::UnsavedFile) {
                storeUnsavedFileDetails(editor, settings);
            }
            else if (editorType == SessionManager::TempFile) {
                storeTempFile(editor, settings);
            }
            else {
                qWarning("Unknown SessionFileType %d", editorType);
            }

            if (currentEditor == editor) {
                currentEditorIndex = index;
            }

            ++index;
        }
    }

    settings.endArray();

    settings.setValue("CurrentEditorIndex", currentEditorIndex);

    settings.endGroup();
}

void SessionManager::loadSession(MainWindow *window)
{
    PROFILE_SCOPE("SessionManager::loadSession");
    qInfo(Q_FUNC_INFO);

    ApplicationSettings settings;;

    settings.beginGroup("CurrentSession");

    ScintillaNext *currentEditor = Q_NULLPTR;
    const int currentEditorIndex = settings.value("CurrentEditorIndex").toInt();
    const int size = settings.beginReadArray("OpenedFiles");

    // NOTE: In theory the fileTypes should determine what is loaded, however if the session fileTypes
    // change from the last time it was saved then it means the settings were manually altered outside of the app,
    // which is non-standard behavior, so just load anything in the file

    for (int index = 0; index < size; ++index) {
        settings.setArrayIndex(index);

        ScintillaNext *editor = Q_NULLPTR;

        if (settings.contains("Type")) {
            const QString type = settings.value("Type").toString();

            if (type == QStringLiteral("File")) {
                editor = loadFileDetails(settings);
            }
            else if (type == QStringLiteral("UnsavedFile")) {
                editor = loadUnsavedFileDetails(settings);
            }
            else if (type == QStringLiteral("Temp")) {
                editor = loadTempFile(settings);
            }
            else {
                qDebug("Unknown session entry type: %s", qUtf8Printable(type));
            }

            if (editor) {
                 if (currentEditorIndex == index) {
                    currentEditor = editor;
                }
            }
        }
        else {
            qDebug("Unknown session entry type for index %d", index);
        }
    }

    settings.endArray();

    settings.endGroup();

    if (currentEditor) {
        window->switchToEditor(currentEditor);
    }
}

bool SessionManager::willFileGetStoredInSession(ScintillaNext *editor) const
{
    SessionFileType editorType = determineType(editor);

    // See if the editor type is in the currently supported file types
    return fileTypes.testFlag(editorType);
}

void SessionManager::storeFileDetails(ScintillaNext *editor, QSettings &settings)
{
    settings.setValue("Type", "File");
    settings.setValue("FilePath", editor->getFilePath());

    storeEditorViewDetails(editor, settings);
}

ScintillaNext* SessionManager::loadFileDetails(QSettings &settings)
{
    qInfo(Q_FUNC_INFO);

    const QString filePath = settings.value("FilePath").toString();

    qDebug("Session file: \"%s\"", qUtf8Printable(filePath));

    ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(filePath);
    if (editor != Q_NULLPTR) {
        qDebug("  file is already open, ignoring");
        return Q_NULLPTR;
    }

    // Remote (ssh://) session entry (D5 branch points 3–4): DROP the local
    // QFileInfo::exists() check (the path is on another host). Only attempt the
    // restore once the workspace's execution context is Connected; otherwise the
    // SFTP backend cannot read yet — defer to the Phase-2 startup reconnect flow,
    // which re-opens the entry once the connection is up.
    if (remote::isSshUri(filePath)) {
        const remote::SshUri uri = remote::parseSshUri(filePath);
        if (!uri.valid) {
            qDebug("  malformed ssh:// session entry, ignoring");
            return Q_NULLPTR;
        }

        remote::ExecutionContextRegistry *registry = app->getExecutionContextRegistry();
        remote::ExecutionContext *ctx =
            registry ? registry->remoteContext(uri.profileId) : nullptr;
        if (!ctx || ctx->state() != remote::ExecutionContext::State::Connected) {
            qDebug("  remote workspace not connected yet, deferring restore");
            return Q_NULLPTR;
        }

        remote::IFileSystemBackend *backend = ctx->fsBackend();
        if (!backend || !backend->isRemote()) {
            qDebug("  no remote backend, ignoring");
            return Q_NULLPTR;
        }

        // Snapshot the view details NOW (the QSettings cursor moves on after this
        // returns); apply them AFTER the content arrives — never onto the empty
        // "Loading…" placeholder.
        const int firstVisibleLine = settings.value("FirstVisibleLine").toInt() - 1;
        const int currentPosition = settings.value("CurrentPosition").toInt();
        QList<int> bookMarkedLines;
        if (settings.contains("BookMarks")) {
            bookMarkedLines = QVariantListToQList(settings.value("BookMarks").toList());
        }

        editor = app->getEditorManager()->createEditorFromRemote(
            backend, filePath, uri.remotePath);
        if (!editor) {
            return Q_NULLPTR;
        }

        QPointer<ScintillaNext> guard(editor);
        QObject::connect(editor, &ScintillaNext::loaded, editor,
                [guard, firstVisibleLine, currentPosition, bookMarkedLines]() {
                    if (!guard) return;
                    ScintillaNext *e = guard.data();
                    e->setFirstVisibleLine(firstVisibleLine);
                    e->setEmptySelection(currentPosition);
                    if (!bookMarkedLines.isEmpty()) {
                        if (auto *decorator = e->findChild<BookMarkDecorator *>(
                                QString(), Qt::FindDirectChildrenOnly)) {
                            QList<int> lines = bookMarkedLines;
                            decorator->setBookMarkedLines(lines);
                        }
                    }
                });

        return editor;
    }

    if (QFileInfo::exists(filePath)) {
        editor = ScintillaNext::fromFile(filePath);

        app->getEditorManager()->manageEditor(editor);

        loadEditorViewDetails(editor, settings);

        return editor;
    }
    else {
        qDebug("  no longer exists on disk, ignoring");
        return Q_NULLPTR;
    }
}

void SessionManager::storeUnsavedFileDetails(ScintillaNext *editor, QSettings &settings)
{
    const QString sessionFileName = RandomSessionFileName();

    settings.setValue("Type", "UnsavedFile");
    settings.setValue("FilePath", editor->getFilePath());
    settings.setValue("SessionFileName", sessionFileName);

    storeEditorViewDetails(editor, settings);

    saveIntoSessionDirectory(editor, sessionFileName);
}

ScintillaNext *SessionManager::loadUnsavedFileDetails(QSettings &settings)
{
    qInfo(Q_FUNC_INFO);

    const QString filePath = settings.value("FilePath").toString();
    const QString sessionFileName = settings.value("SessionFileName").toString();
    const QString sessionFilePath = sessionDirectory().filePath(sessionFileName);

    qDebug("Session file: \"%s\"", qUtf8Printable(filePath));
    qDebug("  temp loc: \"%s\"", qUtf8Printable(sessionFilePath));

    ScintillaNext *editor = app->getEditorManager()->getEditorByFilePath(filePath);
    if (editor != Q_NULLPTR) {
        qDebug("  file is already open, ignoring");
        return Q_NULLPTR;
    }

    if (QFileInfo::exists(filePath) && QFileInfo::exists(sessionFilePath)) {
        ScintillaNext *editor = ScintillaNext::fromFile(sessionFilePath);

        // Since this editor has different file path info, treat this as a temporary buffer
        editor->setFileInfo(filePath);
        editor->setTemporary(true);

        app->getEditorManager()->manageEditor(editor);

        loadEditorViewDetails(editor, settings);

        return editor;
    }
    else {
        // What if just filePath exists?
        qDebug("  no longer exists on disk, ignoring this file for session loading");
        return Q_NULLPTR;
    }
}

void SessionManager::storeTempFile(ScintillaNext *editor, QSettings &settings)
{
    const QString sessionFileName = RandomSessionFileName();

    settings.setValue("Type", "Temp");
    settings.setValue("FileName", editor->getName());
    settings.setValue("SessionFileName", sessionFileName);
    settings.setValue("Language", editor->languageName);

    storeEditorViewDetails(editor, settings);

    saveIntoSessionDirectory(editor, sessionFileName);
}

ScintillaNext *SessionManager::loadTempFile(QSettings &settings)
{
    qInfo(Q_FUNC_INFO);

    const QString fileName = settings.value("FileName").toString();
    const QString sessionFileName = settings.value("SessionFileName").toString();
    const QString languageName = settings.value("Language", QString()).toString();
    const QString fullFilePath = sessionDirectory().filePath(sessionFileName);

    qDebug("Session temp file: \"%s\"", qUtf8Printable(fullFilePath));

    if (QFileInfo::exists(fullFilePath)) {
        ScintillaNext *editor = ScintillaNext::fromFile(fullFilePath, false);

        editor->detachFileInfo(fileName);
        editor->setTemporary(true);

        app->getEditorManager()->manageEditor(editor);

        loadEditorViewDetails(editor, settings);

        if (!languageName.isEmpty()) {
            qDebug("Setting session file language to \"%s\"", qUtf8Printable(languageName));
            app->setEditorLanguage(editor, languageName);
        }

        return editor;
    }
    else {
        qDebug("  no longer exists on disk, ignoring");
        return Q_NULLPTR;
    }
}

void SessionManager::storeEditorViewDetails(ScintillaNext *editor, QSettings &settings)
{
    settings.setValue("FirstVisibleLine", static_cast<int>(editor->firstVisibleLine() + 1)); // Keep it 1-based in the settings just for human-readability
    settings.setValue("CurrentPosition", static_cast<int>(editor->currentPos()));

    BookMarkDecorator *decorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);
    QList<int> bookMarkedLines = decorator->bookMarkedLines();
    if (bookMarkedLines.length() > 0)
        settings.setValue("BookMarks", QListToQVariantList(bookMarkedLines));
}

void SessionManager::loadEditorViewDetails(ScintillaNext *editor, QSettings &settings)
{
    const int firstVisibleLine = settings.value("FirstVisibleLine").toInt() - 1;
    const int currentPosition = settings.value("CurrentPosition").toInt();

    editor->setFirstVisibleLine(firstVisibleLine);
    editor->setEmptySelection(currentPosition);

    if (settings.contains("BookMarks"))
    {
        QList<int> bookMarkedLines = QVariantListToQList(settings.value("BookMarks").toList()); // just using .value<QList<int>>() does not work...possibly a Qt bug?

        BookMarkDecorator *decorator = editor->findChild<BookMarkDecorator*>(QString(), Qt::FindDirectChildrenOnly);
        decorator->setBookMarkedLines(bookMarkedLines);
    }
}
