/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
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


#ifndef SCINTILLANEXT_H
#define SCINTILLANEXT_H

#include "RangeAllocator.h"
#include "ScintillaEdit.h"

#include <cstdint>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QPointer>

#include <functional>

class QTimer;
namespace remote { class IFileSystemBackend; }




class ScintillaNext : public ScintillaEdit
{
    Q_OBJECT

public:
    static bool isRangeValid(const Sci_CharacterRange &range)
    {
        return range.cpMin != INVALID_POSITION && range.cpMax != INVALID_POSITION;
    }

    explicit ScintillaNext(const QString &name, QWidget *parent = Q_NULLPTR);
    ~ScintillaNext() override;

    static ScintillaNext *fromFile(const QString &filePath, bool tryToCreate=false);

    // --- Async (shell-then-load) open path (D4) ------------------------------
    //
    // createShell() is SYNCHRONOUS: it constructs the editor, stamps it with the
    // ssh:// URI identity and BufferType::File IMMEDIATELY (before any bytes), and
    // (for remote) puts it in a read-only "Loading…" state. It returns the editor
    // NOW so the tab can be registered synchronously — DockedEditor::initialEditor()
    // rejects isFile(), so a still-loading shell can never be reused/closed as a
    // pristine scratch tab. `backend` must be a remote (isRemote()==true) backend;
    // `uri` is the ssh://<profileId><remotePath> identity; `remotePath` is the
    // POSIX path passed to the backend's read/write.
    static ScintillaNext *createShell(const QString &name,
                                      remote::IFileSystemBackend *backend,
                                      const QString &uri,
                                      const QString &remotePath);

    // Callback invoked once on the UI thread when an async load resolves.
    using LoadCallback = std::function<void(bool ok, const QString &error)>;

    // loadInto() is ASYNC for remote (backend->readFileAsync) — it never blocks
    // the UI thread on the network. On success the received bytes run through the
    // SAME local detection (detectBom) + fill (undo off + signals blocked) the
    // local readFromDisk path uses, then editing is re-enabled and the save point
    // set. On failure the editor enters an error state and `cb`/loadFailed report
    // it so a caller can offer Retry (re-invoke loadInto).
    void loadInto(LoadCallback cb = {});

    // Re-invoke the load after a read failure (Retry affordance).
    void retryLoad(LoadCallback cb = {});

    // Remote identity / state -------------------------------------------------
    bool isRemote() const;
    QString remoteUri() const { return remoteUriString; }
    QString remotePath() const { return remoteFilePath; }
    void setRemoteIdentity(const QString &newRemotePath, const QString &newUri);

    enum class LoadState : std::uint8_t {
        Idle,    // local buffer or fully-loaded remote buffer
        Loading, // remote bytes in flight (read-only placeholder shown)
        Loaded,  // remote content arrived and filled
        Error,   // remote read failed (placeholder shows the error)
    };
    LoadState loadState() const { return loadStatus; }

    static QString eolModeToString(int eolMode);
    static int stringToEolMode(const QString &eolMode);

    int allocateIndicator(const QString &name);

    template<typename Func>
    void forEachMatch(const QString &text, Func callback) { forEachMatch(text.toUtf8(), callback); }

    template<typename Func>
    void forEachMatch(const QByteArray &byteArray, Func callback) { forEachMatchInRange(byteArray, callback, {0, (Sci_PositionCR)length()}); }

    template<typename Func>
    void forEachMatchInRange(const QByteArray &byteArray, Func callback, Sci_CharacterRange range);

    template<typename Func>
    void forEachLineInSelection(int selection, Func callback);

    void goToRange(const Sci_CharacterRange &range);

    QByteArray eolString() const;

    bool lineIsEmpty(int line);

    void deleteLine(int line);

    // Intentional wrap of non-virtual ScintillaEdit::cutAllowLine with empty-selection cut-line behavior.
    void cutAllowLine(); // NOLINT(bugprone-derived-method-shadowing-base-method)

    void modifyFoldLevels(int level, int action);
    void foldAllLevels(int level);
    void unFoldAllLevels(int level);

    void deleteLeadingEmptyLines();
    void deleteTrailingEmptyLines();

    bool isFile() const;
    QFileInfo getFileInfo() const;

    bool isSavedToDisk() const;
    bool canSaveToDisk() const;

    QString getName() const { return name; }
    void setName(const QString &name);
    QString getPath() const;
    QString getFilePath() const;

    // NOTE: this is dangerous and should only be used in very rare situations
    void setFileInfo(const QString &filePath);

    // Re-point an open buffer at a file that was moved/renamed on disk by an
    // external actor (e.g. the workspace file tree's Rename action) WITHOUT
    // rewriting the buffer. The on-disk bytes are unchanged, so unsaved edits
    // are preserved (no setSavePoint). setFileInfo() refreshes the timestamp so
    // checkFileForStateChange() does not false-positive "Modified", and the
    // renamed() signal drives the exact same consumer set as rename()/saveAs()
    // (tab title, FileListDock, FileWatcher remap, PreviewTabManager, task
    // gutter, language re-detect). The caller MUST have completed the disk
    // rename successfully first, so newFilePath exists.
    void updatePathAfterMove(const QString &newFilePath);

    void detachFileInfo(const QString &newName);

    enum FileStateChange : std::uint8_t {
        NoChange,
        Modified,
        Deleted,
        Restored,
    };

    enum BufferType : std::uint8_t {
        New, // A temporary buffer, e.g. "New 1"
        File, // Buffer tied to a file on the file system
        FileMissing, // Buffer with a missing file on the file system
    };

    enum class BomType : std::uint8_t {
        None,
        Utf8,
        Utf16LE,
        Utf16BE
    };

    BomType bom() const { return bomType; }

    bool isTemporary() const { return temporary; }
    void setTemporary(bool temp);

    void setFoldMarkers(const QString &type);

    QString languageName;
    QByteArray languageSingleLineComment;

    #include "ScintillaEnums.h"


public slots:
    // Buffer-close slot; QWidget::close() is bool and non-virtual, so this cannot override.
    void close(); // NOLINT(bugprone-derived-method-shadowing-base-method)
    QFileDevice::FileError save();
    void reload();
    void omitModifications();
    QFileDevice::FileError saveAs(const QString &newFilePath);
    QFileDevice::FileError saveCopyAs(const QString &filePath);
    bool rename(const QString &newFilePath);
    ScintillaNext::FileStateChange checkFileForStateChange();
    bool moveToTrash();

    void toggleCommentSelection();
    void commentLineSelection();
    void uncommentLineSelection();

    void removeDuplicateLines();
    void removeConsecutiveDuplicateLines();

signals:
    void aboutToSave();
    void saved();
    void closed();
    void renamed();

    void lexerChanged();
    void reloaded();

    // Async open/save outcome (D4). loaded(): remote content arrived + filled.
    // loadFailed(): remote read failed (error tab + Retry). saveFailed(): remote
    // write failed — the buffer stays dirty (no content loss) and the UI shows
    // the reason. Local open/save never emits these (they complete inline).
    void loaded();
    void loadFailed(const QString &error);
    void saveFailed(const QString &error);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    QString name;
    BufferType bufferType = BufferType::New;
    BomType bomType = BomType::None;
    QFileInfo fileInfo;
    QDateTime modifiedTime;
    RangeAllocator indicatorResources;

    bool temporary = false; // Temporary file loaded from a session. It can either be a 'New' file or actual 'File'

    // Remote (ssh://) backing. nullptr for a local buffer. Not owned — the
    // ExecutionContext owns the backend; QPointer auto-nulls on destruction.
    QPointer<remote::IFileSystemBackend> fsBackend;
    QString remoteUriString;   // ssh://<profileId><remotePath> identity
    QString remoteFilePath;    // POSIX path handed to the backend
    quint64 loadReqId = 0;     // reqId of the in-flight SFTP read (0 = none)
    LoadState loadStatus = LoadState::Idle;
    QTimer *loadTimeoutTimer = nullptr;

    bool readFromDisk(QFile &file);
    // Fill the buffer from already-read bytes, running the SAME BOM detection +
    // undo-off/signals-blocked fill as readFromDisk's chunk loop. Shared by the
    // remote async load path. Returns false if Scintilla reported an error.
    bool fillFromBytes(const QByteArray &data);
    // Enter the read-only "Loading…" placeholder state (remote shell pre-bytes).
    void enterLoadingState();
    // Async remote save (D4): writeFileAsync; OK → setSavePoint/saved, fail →
    // stay dirty + saveFailed (no content loss).
    QFileDevice::FileError saveRemote();
    QDateTime fileTimestamp();
    void updateTimestamp();

};

template<typename Func>
void ScintillaNext::forEachLineInSelection(int selection, Func callback)
{
    int lineStart = lineFromPosition(selectionNStart(selection));
    int lineEnd = lineFromPosition(selectionNEnd(selection));

    for (int curLine = lineStart; curLine <= lineEnd; ++curLine) {
        callback(curLine);
    }
}

// Stick this in the header file...because C++, that's why
template<typename Func>
void ScintillaNext::forEachMatchInRange(const QByteArray &text, Func callback, Sci_CharacterRange range)
{
    Sci_TextToFind ttf {range, text.constData(), {-1, -1}};
    int flags = searchFlags();

    while (send(SCI_FINDTEXT, flags, reinterpret_cast<sptr_t>(&ttf)) != -1) {
        if(ttf.chrgText.cpMin == ttf.chrgText.cpMax)
            break;
        ttf.chrg.cpMin = callback(ttf.chrgText.cpMin, ttf.chrgText.cpMax);
    }
}

#endif // SCINTILLANEXT_H
