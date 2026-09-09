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


#include "ScintillaNext.h"
#include "Finder.h"
#include "ProfileScope.h"
#include "ScintillaCommenter.h"

#include "remote/IFileSystemBackend.h"
#include "remote/RemoteFsBackend.h"

#include "ByteArrayUtils.h"
#include "uchardet.h"
#include <cinttypes>

#include <QDir>
#include <QMouseEvent>
#include <QPointer>
#include <QSaveFile>
#include <QTextCodec>
#include <QTimer>

#include <memory>
#include <utility>


const int CHUNK_SIZE = 1024 * 1024 * 4; // Not sure what is best

// SFTP session init on a cold connection can exceed 30s on slow servers, so
// give the remote load 60s before declaring failure (matches kTimeoutStatus).
static constexpr int kRemoteLoadTimeoutMs = 60'000;

constexpr char kBomUtf8[] = "\xEF\xBB\xBF";
constexpr char kBomUtf16LE[] = "\xFF\xFE";
constexpr char kBomUtf16BE[] = "\xFE\xFF";

ScintillaNext::BomType detectBom(const QByteArray& data)
{
    if (data.startsWith(kBomUtf8))    return ScintillaNext::BomType::Utf8;
    if (data.startsWith(kBomUtf16LE)) return ScintillaNext::BomType::Utf16LE;
    if (data.startsWith(kBomUtf16BE)) return ScintillaNext::BomType::Utf16BE;

    return ScintillaNext::BomType::None;
}

QByteArray bomData(ScintillaNext::BomType bom)
{
    switch (bom) {
    case ScintillaNext::BomType::Utf8:    return QByteArray(kBomUtf8, 3);
    case ScintillaNext::BomType::Utf16LE: return QByteArray(kBomUtf16LE, 2);
    case ScintillaNext::BomType::Utf16BE: return QByteArray(kBomUtf16BE, 2);
    case ScintillaNext::BomType::None:    return QByteArray();
    }
    return QByteArray();
}

int bomLength(ScintillaNext::BomType bom)
{
    if (bom == ScintillaNext::BomType::Utf8) return 3;
    else if (bom == ScintillaNext::BomType::Utf16LE) return 2;
    else if (bom == ScintillaNext::BomType::Utf16BE) return 2;

    return 0;
}

static QFileDevice::FileError writeToDisk(const QByteArray &data, const QString &path, ScintillaNext::BomType bom)
{
    qInfo(Q_FUNC_INFO);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("writeToDisk() failed to open file %s: %s", qPrintable(path), qPrintable(file.errorString()));
        return file.error();
    }

    // Write BOM
    const QByteArray bomBytes = bomData(bom);
    if (!bomBytes.isEmpty() && file.write(bomBytes) == -1) {
        qWarning("writeToDisk() failed writing BOM: %s", qPrintable(file.errorString()));
        return file.error();
    }

    // Write actual data
    if (file.write(data) == -1) {
        qWarning("writeToDisk() failed writing data: %s", qPrintable(file.errorString()));
        return file.error();
    }

    return file.error();
}

static bool isNewlineCharacter(char c)
{
    return c == '\n' || c == '\r';
}

ScintillaNext::ScintillaNext(const QString &name, QWidget *parent) :
    ScintillaEdit(parent),
    name(name),
    indicatorResources(INDICATOR_MAX + 1)
{
    // Per the scintilla documentation, some parts of the range are not generally available
    indicatorResources.disableRange(0, 7);
    indicatorResources.disableRange(INDICATOR_IME, INDICATOR_IME_MAX);
    indicatorResources.disableRange(INDICATOR_HISTORY_REVERTED_TO_ORIGIN_INSERTION, INDICATOR_HISTORY_REVERTED_TO_MODIFIED_DELETION);
}

ScintillaNext::~ScintillaNext()
{
}

ScintillaNext *ScintillaNext::fromFile(const QString &filePath, bool tryToCreate)
{
    PROFILE_SCOPE("ScintillaNext::fromFile");
    QFile file(filePath);
    ScintillaNext *editor = new ScintillaNext(file.fileName());

    if(tryToCreate && !file.exists()) {
        qInfo("Trying to create %s", qUtf8Printable(filePath));
        QDir d;
        d.mkpath(QFileInfo(file).path());

        QFile f(filePath);
        f.open(QIODevice::WriteOnly);
        f.close();
    }

    bool readSuccessful = editor->readFromDisk(file);

    if (!readSuccessful) {
        delete editor;
        return Q_NULLPTR;
    }

    editor->setFileInfo(filePath);

    return editor;
}

ScintillaNext *ScintillaNext::createShell(const QString &name,
                                          remote::IFileSystemBackend *backend,
                                          const QString &uri,
                                          const QString &remotePath)
{
    PROFILE_SCOPE("ScintillaNext::createShell");

    // Tab title = basename of the remote path (fall back to the supplied name).
    QString tabName = name;
    if (tabName.isEmpty()) {
        const int slash = remotePath.lastIndexOf(QLatin1Char('/'));
        tabName = (slash >= 0 && slash + 1 < remotePath.size())
                      ? remotePath.mid(slash + 1)
                      : remotePath;
    }

    ScintillaNext *editor = new ScintillaNext(tabName);

    editor->fsBackend = backend;
    editor->remoteUriString = uri;
    editor->remoteFilePath = remotePath;

    // CRITICAL (D4): stamp File identity BEFORE any bytes arrive. fileInfo is set
    // from the remote path WITHOUT touching the local disk (setFileInfo() would
    // Q_ASSERT(exists()) against the local fs, which is wrong for a remote path).
    // bufferType = File makes DockedEditor::initialEditor() reject this editor, so
    // a still-loading shell is never mistaken for a pristine "New X" scratch tab.
    editor->fileInfo.setFile(remotePath);
    editor->bufferType = ScintillaNext::File;

    // Read-only "Loading…" placeholder until loadInto() fills the buffer.
    editor->enterLoadingState();

    return editor;
}

void ScintillaNext::enterLoadingState()
{
    loadStatus = LoadState::Loading;

    const QByteArray placeholder = tr("Loading…").toUtf8();

    const QSignalBlocker blocker(this);
    setUndoCollection(false);
    emptyUndoBuffer();
    setReadOnly(false);
    setText("");
    appendText(placeholder.size(), placeholder.constData());
    setReadOnly(true);
    setSavePoint();
    // Undo stays OFF while loading so canUndo()/canRedo() are false — another
    // belt for initialEditor()'s reject test (though isFile() already wins).
}

void ScintillaNext::loadInto(LoadCallback cb)
{
    if (!fsBackend) {
        // No backend → nothing to load asynchronously. Treat as loaded so the
        // caller's completion contract still fires.
        loadStatus = LoadState::Loaded;
        if (cb) cb(true, QString());
        emit loaded();
        return;
    }

    loadStatus = LoadState::Loading;

    if (!loadTimeoutTimer) {
        loadTimeoutTimer = new QTimer(this);
        loadTimeoutTimer->setSingleShot(true);
    } else {
        // Disconnect any previous timeout slot (e.g. from a prior retryLoad call)
        // so we never accumulate duplicate connections on the same timer.
        QObject::disconnect(loadTimeoutTimer, &QTimer::timeout, this, nullptr);
        loadTimeoutTimer->stop();
    }

    QPointer<ScintillaNext> guard(this);
    QPointer<remote::IFileSystemBackend> backendGuard(fsBackend);

    // Wrap cb so it fires at most once — both the SFTP callback and the timeout
    // timer capture it. In the late-arrival recovery scenario the timer fires first
    // (with ok=false) and then the SFTP bytes arrive (ok=true); without this guard
    // the caller's callback would be invoked twice.
    auto sharedCb = std::make_shared<LoadCallback>(std::move(cb));
    auto fireCb = [sharedCb](bool ok, const QString &err) {
        if (sharedCb && *sharedCb) {
            LoadCallback f = std::move(*sharedCb);
            *sharedCb = {};
            f(ok, err);
        }
    };

    // Use the tracked variant for remote backends so we can cancel on timeout.
    // For local backends (no cancel needed) the regular path is fine.
    auto *remoteFsBackend = qobject_cast<remote::RemoteFsBackend *>(fsBackend);
    loadReqId = 0;
    auto readCb = [this, guard, fireCb](bool ok, const QByteArray &data,
                                                               const QString &error) {
        if (!guard) return; // editor closed mid-load

        // If the timeout already fired AND the read also failed, we are done: the
        // error placeholder is already in the editor from the timer path. If the
        // read succeeded despite a late arrival (ok==true), fall through and recover
        // — overwrite the placeholder with the real content so the user is not stuck
        // staring at "Load timed out" when the bytes are available.
        const bool timedOut = (loadStatus == LoadState::Error);
        if (timedOut && !ok) return; // timer won, read failed → nothing to recover

        if (loadTimeoutTimer) loadTimeoutTimer->stop();
        loadReqId = 0;

        if (!ok) {
            loadStatus = LoadState::Error;
            // Show the error in the (still read-only) placeholder.
            {
                const QSignalBlocker blocker(this);
                setUndoCollection(false);
                setReadOnly(false);
                setText("");
                const QByteArray msg = (tr("Failed to load: ") + error).toUtf8();
                appendText(msg.size(), msg.constData());
                setReadOnly(true);
                setSavePoint();
            }
            fireCb(false, error);
            emit loadFailed(error);
            return;
        }

        // Fill from the received bytes using the exact same detection + fill as
        // the local readFromDisk path. This path also covers the late-arrival
        // recovery case (timedOut==true): overwrite the error placeholder with
        // the real content.
        setReadOnly(false);
        {
            const QSignalBlocker blocker(this);
            setText("");
            emptyUndoBuffer();
        }
        const bool filled = fillFromBytes(data);

        updateTimestamp();
        setSavePoint();
        loadStatus = filled ? LoadState::Loaded : LoadState::Error;

        if (!filled) {
            const QString err = tr("Document error while loading");
            fireCb(false, err);
            emit loadFailed(err);
            return;
        }

        fireCb(true, QString());
        emit loaded();
    };

    if (remoteFsBackend) {
        loadReqId = remoteFsBackend->readFileAsyncTracked(remoteFilePath, std::move(readCb));
    } else {
        fsBackend->readFileAsync(remoteFilePath, std::move(readCb));
    }

    // 30-second timeout: if the read callback never fires, transition to error.
    connect(loadTimeoutTimer, &QTimer::timeout, this, [this, guard, backendGuard, fireCb]() {
        if (!guard) return;
        if (loadStatus != LoadState::Loading) return;
        loadStatus = LoadState::Error;
        if (backendGuard) {
            backendGuard->logEvent(QStringLiteral("load-timeout: path=%1").arg(remoteFilePath));
            // Cancel the stuck SFTP op so it is dequeued from the bulk lane,
            // unblocking all subsequent file opens on this SSH session.
            if (loadReqId != 0) {
                backendGuard->cancelReadAsync(loadReqId);
                loadReqId = 0;
            }
        }
        {
            const QSignalBlocker blocker(this);
            setUndoCollection(false);
            setReadOnly(false);
            setText("");
            const QByteArray msg = tr("Load timed out — the remote server did not respond").toUtf8();
            appendText(msg.size(), msg.constData());
            setReadOnly(true);
            setSavePoint();
        }
        const QString err = tr("Load timed out");
        fireCb(false, err);
        emit loadFailed(err);
    }, Qt::SingleShotConnection);
    loadTimeoutTimer->start(kRemoteLoadTimeoutMs);
}

void ScintillaNext::retryLoad(LoadCallback cb)
{
    // Re-arm the placeholder, then re-issue the async read.
    enterLoadingState();
    loadInto(std::move(cb));
}

bool ScintillaNext::isRemote() const
{
    return fsBackend && fsBackend->isRemote();
}

void ScintillaNext::setRemoteIdentity(const QString &newRemotePath, const QString &newUri)
{
    remoteFilePath = newRemotePath;
    remoteUriString = newUri;
    const int slash = newRemotePath.lastIndexOf(QLatin1Char('/'));
    name = slash >= 0 ? newRemotePath.mid(slash + 1) : newRemotePath;
    emit renamed();
}

bool ScintillaNext::fillFromBytes(const QByteArray &data)
{
    // Mirror readFromDisk's fill: preallocate, BOM-detect on the head, strip a
    // UTF-8 BOM, then append the bytes verbatim with undo OFF + signals blocked.
    // This is byte-for-byte the same document the chunked local path produces;
    // only the source of the bytes differs (network vs QFile).
    allocate(data.size());

    setUndoCollection(false);
    blockSignals(true);

    bomType = detectBom(data);

    const char *bytes = data.constData();
    int size = data.size();
    if (bomType == BomType::Utf8) {
        const int n = bomLength(bomType);
        bytes += n;
        size -= n;
    }
    // UTF-16 BOMs are intentionally left in place (matches readFromDisk).

    appendText(size, bytes);

    this->blockSignals(false);
    setUndoCollection(true);

    if (status() != SC_STATUS_OK) {
        qWarning("fillFromBytes(): document error %ld", status());
        return false;
    }
    return true;
}

QString ScintillaNext::eolModeToString(int eolMode)
{
    if (eolMode == SC_EOL_CRLF)
        return QStringLiteral("crlf");
    else if (eolMode == SC_EOL_CR)
        return QStringLiteral("cr");
    else if (eolMode == SC_EOL_LF)
        return QStringLiteral("lf");
    else
        return QString(); // unknown
}

int ScintillaNext::stringToEolMode(const QString &eolMode)
{
    if (eolMode == QStringLiteral("crlf"))
        return SC_EOL_CRLF;
    else if (eolMode == QStringLiteral("cr"))
        return SC_EOL_CR;
    else if (eolMode == QStringLiteral("lf"))
        return SC_EOL_LF;
    else
        return -1;
}

int ScintillaNext::allocateIndicator(const QString &name)
{
    return indicatorResources.requestResource(name);
}

void ScintillaNext::goToRange(const Sci_CharacterRange &range)
{
    qInfo(Q_FUNC_INFO);

    if (isRangeValid(range)) {
        // Lines can be folded so make sure they are visible
        ensureVisible(lineFromPosition(range.cpMin));
        ensureVisible(lineFromPosition(range.cpMax));

        setSelection(range.cpMax, range.cpMin);
        scrollRange(range.cpMax, range.cpMin);
    }
}

QByteArray ScintillaNext::eolString() const
{
    const int eol = eOLMode();

    if (eol == SC_EOL_LF) return QByteArrayLiteral("\n");
    else if (eol == SC_EOL_CRLF) return QByteArrayLiteral("\r\n");
    else return QByteArrayLiteral("\r");
}

bool ScintillaNext::lineIsEmpty(int line)
{
    return (lineEndPosition(line) - positionFromLine(line)) == 0;
}

void ScintillaNext::deleteLine(int line)
{
    deleteRange(positionFromLine(line), lineLength(line));
}

void ScintillaNext::cutAllowLine()
{
    if (selectionEmpty()) {
        copyAllowLine();
        lineDelete();
    }
    else {
        cut();
    }
}

void ScintillaNext::modifyFoldLevels(int level, int action)
{
    const int totalLines = lineCount();

    int line = 0;
    while (line < totalLines) {
        int foldFlags = foldLevel(line); // Even though its called fold level it contains several other flags
        bool isHeader = foldFlags & SC_FOLDLEVELHEADERFLAG;
        int actualLevel = (foldFlags & SC_FOLDLEVELNUMBERMASK) - SC_FOLDLEVELBASE;

        if (isHeader && actualLevel == level) {
            foldLine(line, action);
            line = lastChild(line, -1) + 1;
        }
        else {
            ++line;
        }
    }
}

void ScintillaNext::foldAllLevels(int level)
{
    modifyFoldLevels(level, SC_FOLDACTION_CONTRACT);
}

void ScintillaNext::unFoldAllLevels(int level)
{
    modifyFoldLevels(level, SC_FOLDACTION_EXPAND);
}

void ScintillaNext::deleteLeadingEmptyLines()
{
    while (lineCount() > 1 && lineIsEmpty(0)) {
        deleteLine(0);
    }
}

void ScintillaNext::deleteTrailingEmptyLines()
{
    const int docLength = length();
    int position = docLength;

    while (position > 0 && isNewlineCharacter(charAt(position - 1))) {
        position--;
    }

    deleteRange(position, docLength - position);
}

bool ScintillaNext::isSavedToDisk() const
{
    return !canSaveToDisk();
}

bool ScintillaNext::canSaveToDisk() const
{
    // The buffer can be saved if:
    // - It is marked as a temporary since as soon as it gets saved it is no longer a temporary buffer
    // - A modified file
    // - A missing file since as soon as it is saved it is no longer missing.
    return temporary ||
           (bufferType == ScintillaNext::New && modify()) ||
           (bufferType == ScintillaNext::File && modify()) ||
            (bufferType == ScintillaNext::FileMissing);
}

void ScintillaNext::setName(const QString &name)
{
    this->name = name;

    emit renamed();
}

bool ScintillaNext::isFile() const
{
    return bufferType == ScintillaNext::File || bufferType == ScintillaNext::FileMissing;
}

QFileInfo ScintillaNext::getFileInfo() const
{
    Q_ASSERT(isFile());

    return fileInfo;
}

QString ScintillaNext::getPath() const
{
    Q_ASSERT(isFile());

    if (isRemote()) {
        // POSIX parent of the remote path (no local canonicalization).
        const int slash = remoteFilePath.lastIndexOf(QLatin1Char('/'));
        return slash > 0 ? remoteFilePath.left(slash) : QStringLiteral("/");
    }

    return QDir::toNativeSeparators(fileInfo.canonicalPath());
}

QString ScintillaNext::getFilePath() const
{
    Q_ASSERT(isFile());

    if (isRemote()) {
        // Identity is the ssh:// URI (a local canonicalFilePath() is meaningless
        // for a remote path). EditorManager::getEditorByFilePath compares this.
        return remoteUriString;
    }

    return QDir::toNativeSeparators(fileInfo.canonicalFilePath());
}

void ScintillaNext::setFoldMarkers(const QString &type)
{
    QMap<QString, QList<int>> map{
        {"simple", {SC_MARK_MINUS, SC_MARK_PLUS, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY}},
        {"arrow",  {SC_MARK_ARROWDOWN, SC_MARK_ARROW, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY, SC_MARK_EMPTY}},
        {"circle", {SC_MARK_CIRCLEMINUS, SC_MARK_CIRCLEPLUS, SC_MARK_VLINE, SC_MARK_LCORNERCURVE, SC_MARK_CIRCLEPLUSCONNECTED, SC_MARK_CIRCLEMINUSCONNECTED, SC_MARK_TCORNERCURVE }},
        {"box",    {SC_MARK_BOXMINUS, SC_MARK_BOXPLUS, SC_MARK_VLINE, SC_MARK_LCORNER, SC_MARK_BOXPLUSCONNECTED, SC_MARK_BOXMINUSCONNECTED, SC_MARK_TCORNER }},
    };

    if (!map.contains(type))
        return;

    const auto types = map[type];
    markerDefine(SC_MARKNUM_FOLDEROPEN, types[0]);
    markerDefine(SC_MARKNUM_FOLDER, types[1]);
    markerDefine(SC_MARKNUM_FOLDERSUB, types[2]);
    markerDefine(SC_MARKNUM_FOLDERTAIL, types[3]);
    markerDefine(SC_MARKNUM_FOLDEREND, types[4]);
    markerDefine(SC_MARKNUM_FOLDEROPENMID, types[5]);
    markerDefine(SC_MARKNUM_FOLDERMIDTAIL, types[6]);
}

void ScintillaNext::close()
{
    emit closed();

    deleteLater();
}

QFileDevice::FileError ScintillaNext::save()
{
    qInfo(Q_FUNC_INFO);

    Q_ASSERT(isFile());

    if (isRemote()) {
        return saveRemote();
    }

    emit aboutToSave();

    const QByteArray data = QByteArray::fromRawData((char*)characterPointer(), textLength());
    const QString path = fileInfo.filePath();
    QFileDevice::FileError writeSuccessful = writeToDisk(data, path, bomType);

    if (writeSuccessful == QFileDevice::NoError) {
        updateTimestamp();
        setSavePoint();

        // If this was a temporary file, make sure it is not any more
        setTemporary(false);

        emit saved();
    }

    return writeSuccessful;
}

QFileDevice::FileError ScintillaNext::saveRemote()
{
    if (!fsBackend) {
        emit saveFailed(tr("Remote connection lost"));
        return QFileDevice::ResourceError;
    }

    // Async remote save (D4). Q_ASSERT(isFile()) already holds (remote buffers
    // are File). The buffer is shown "Saving…" by the UI on aboutToSave(); on
    // success setSavePoint()/saved(); on failure it STAYS dirty (no setSavePoint)
    // and saveFailed() reports the reason — no content loss.
    emit aboutToSave();

    // Snapshot the bytes into an OWNED buffer (prepend BOM as writeToDisk does):
    // the document's characterPointer() must not be referenced from the async
    // callback, which fires after this returns.
    QByteArray payload = bomData(bomType);
    payload.append((const char *)characterPointer(), textLength());

    QPointer<ScintillaNext> guard(this);
    fsBackend->writeFileAsync(remoteFilePath, payload, [this, guard](bool ok, const QString &error) {
        if (!guard) return;
        if (ok) {
            updateTimestamp();
            setSavePoint();
            setTemporary(false);
            emit saved();
        } else {
            emit saveFailed(error);
        }
    });

    // The write is in flight; report "no synchronous error". The real outcome
    // arrives via saved()/saveFailed().
    return QFileDevice::NoError;
}

void ScintillaNext::reload()
{
    Q_ASSERT(isFile());

    if (isRemote()) {
        const int line = firstVisibleLine();
        const int caret = selectionNCaret(mainSelection());
        const int anchor = selectionNAnchor(mainSelection());
        QPointer<ScintillaNext> guard(this);
        retryLoad([guard, line, caret, anchor](bool ok, const QString &) {
            if (!guard || !ok) return;
            guard->scrollVertical(line, 0);
            guard->setSelection(caret, anchor);
            emit guard->reloaded();
        });
        return;
    }

    // Ensure the file still exists.
    if (!QFile::exists(fileInfo.canonicalFilePath())) {
        return;
    }

    const int line = firstVisibleLine();
    const int caret = selectionNCaret(mainSelection());
    const int anchor = selectionNAnchor(mainSelection());

    // Remove all the text
    {
        const QSignalBlocker blocker(this);
        setUndoCollection(false);
        emptyUndoBuffer();
        setText("");
        setUndoCollection(true);
    }

    // NOTE: if the read fails then the buffer will be completely empty...which probably
    // isn't a good thing, but this should be a rare occurrence.
    QFile f(fileInfo.canonicalFilePath());
    bool readSuccessful = readFromDisk(f);

    if (!readSuccessful) {
        return;
    }

    updateTimestamp();
    setSavePoint();

    // If this was a temporary file, make sure it is not any more
    if (isTemporary())
        setTemporary(false);

    scrollVertical(line, 0);
    setSelection(caret, anchor);

    emit reloaded();
}

void ScintillaNext::omitModifications()
{
    // If file modifications will be omitted just update file timestamp
    // so pop-up will be displayed only once per file modifications.
    updateTimestamp();
    setTemporary(true);
}

QFileDevice::FileError ScintillaNext::saveAs(const QString &newFilePath)
{
    bool isRenamed = bufferType == ScintillaNext::New || fileInfo.canonicalFilePath() != newFilePath;

    emit aboutToSave();

    const QByteArray data = QByteArray::fromRawData((char*)characterPointer(), textLength());
    QFileDevice::FileError saveSuccessful = writeToDisk(data, newFilePath, bomType);

    if (saveSuccessful == QFileDevice::NoError) {
        setFileInfo(newFilePath);
        setSavePoint();

        // If this was a temporary file, make sure it is not any more
        setTemporary(false);

        emit saved();

        if (isRenamed) {
            emit renamed();
        }
    }

    return saveSuccessful;
}

QFileDevice::FileError ScintillaNext::saveCopyAs(const QString &filePath)
{
    const QByteArray data = QByteArray::fromRawData((char*)characterPointer(), textLength());
    return writeToDisk(data, filePath, bomType);
}

bool ScintillaNext::rename(const QString &newFilePath)
{
    emit aboutToSave();

    // Write out the buffer to the new path
    if (saveCopyAs(newFilePath) == QFileDevice::NoError) {
        // Remove the old file
        const QString oldPath = fileInfo.canonicalFilePath();
        QFile::remove(oldPath);

        // Everything worked fine, so update the buffer's info
        setFileInfo(newFilePath);
        setSavePoint();

        // If this was a temporary file, make sure it is not any more
        setTemporary(false);

        emit saved();

        emit renamed();

        return true;
    }

    return false;
}

ScintillaNext::FileStateChange ScintillaNext::checkFileForStateChange()
{
    // Remote path doesn't exist locally — QFileInfo would wrongly flip bufferType to FileMissing.
    if (isRemote()) return FileStateChange::NoChange;

    if (bufferType == BufferType::New) {
        return FileStateChange::NoChange;
    }
    else if (bufferType == BufferType::File) {
        // refresh else exists() fails to notice missing file
        fileInfo.refresh();

        if (!fileInfo.exists()) {
            bufferType = BufferType::FileMissing;

            emit savePointChanged(false);

            return FileStateChange::Deleted;
        }

        // See if the timestamp changed
        if (modifiedTime != fileTimestamp()) {
            return FileStateChange::Modified;
        }
        else {
            return FileStateChange::NoChange;
        }
    }
    else if (bufferType == BufferType::FileMissing) {
        // See if it reappeared
        fileInfo.refresh();

        if (fileInfo.exists()) {
            bufferType = BufferType::File;

            return FileStateChange::Restored;
        }
        else {
            return FileStateChange::NoChange;
        }
    }

    qInfo("type() = %d", bufferType);
    Q_ASSERT(false);

    return FileStateChange::NoChange;
}

bool ScintillaNext::moveToTrash()
{
    if (QFile::exists(fileInfo.canonicalFilePath())) {
        QFile f(fileInfo.canonicalFilePath());

        return f.moveToTrash();
    }

    return false;
}

void ScintillaNext::toggleCommentSelection()
{
    if (languageSingleLineComment.isEmpty())
        return;

    const int sel = mainSelection();
    const bool hasSelection = selectionNStart(sel) != selectionNEnd(sel);

    {
        ScintillaCommenter sc(this);
        sc.toggleSelection();
    }

    if (!hasSelection) {
        const auto col = column(selectionNCaret(mainSelection()));
        const int caretLine = lineFromPosition(selectionNCaret(mainSelection()));
        const int nextLine = caretLine + 1;
        if (nextLine < lineCount()) {
            const auto pos = findColumn(nextLine, col);
            setSelection(pos, pos);
        }
    }
}

void ScintillaNext::commentLineSelection()
{
    ScintillaCommenter sc(this);
    sc.commentSelection();
}

void ScintillaNext::uncommentLineSelection()
{
    ScintillaCommenter sc(this);
    sc.uncommentSelection();
}

void ScintillaNext::removeDuplicateLines()
{
    QByteArray data = QByteArray::fromRawData((char*) characterPointer(), textLength());
    const QByteArray delim = eolString();

    auto lines = ByteArrayUtils::split(data, delim);
    int originalLineCount = lines.length();
    ByteArrayUtils::removeDuplicates(lines);

    if (originalLineCount == lines.length()){
        return; // No lines were removed
    }

    QByteArray result = ByteArrayUtils::join(lines, delim);

    const UndoAction ua(this);
    setTargetRange(0, textLength());
    replaceTarget(result.length(), result.constData());
}

void ScintillaNext::removeConsecutiveDuplicateLines()
{
    QByteArray data = QByteArray::fromRawData((char*) characterPointer(), textLength());
    const QByteArray delim = eolString();

    auto lines = ByteArrayUtils::split(data, delim);
    int originalLineCount = lines.length();
    ByteArrayUtils::removeConsecutiveDuplicates(lines);
    QByteArray result = ByteArrayUtils::join(lines, delim);

    if (originalLineCount == lines.length()){
        return; // No lines were removed
    }

    const UndoAction ua(this);
    setTargetRange(0, textLength());
    replaceTarget(result.length(), result.constData());
}

void ScintillaNext::dragEnterEvent(QDragEnterEvent *event)
{
    // Ignore all drag and drop events with urls and let the main application handle it
    if (event->mimeData()->hasUrls()) {
        return;
    }

    ScintillaEdit::dragEnterEvent(event);
}

void ScintillaNext::dropEvent(QDropEvent *event)
{
    // Ignore all drag and drop events with urls and let the main application handle it
    if (event->mimeData()->hasUrls()) {
        return;
    }

    ScintillaEdit::dropEvent(event);
}

bool ScintillaNext::readFromDisk(QFile &file)
{
    PROFILE_SCOPE("ScintillaNext::readFromDisk");
    if (!file.exists()) {
        qWarning("Cannot read \"%s\": doesn't exist", qUtf8Printable(file.fileName()));
        return false;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("QFile::open() failed when opening \"%s\" - error code %d: %s", qUtf8Printable(file.fileName()), file.error(), qUtf8Printable(file.errorString()));
        return false;
    }

    // TODO: figure out what to do if "size" is too big
    allocate(file.size());

    // Turn off undo collection and block signals during loading
    setUndoCollection(false);
    blockSignals(true);
    // TODO disable notifications
    // modEventMask(SC_MOD_NONE)?

    QByteArray chunk;
    qint64 bytesRead;

    bool first_read = true;
    do {
        // Try to read as much as possible
        chunk.resize(CHUNK_SIZE);
        bytesRead = file.read(chunk.data(), CHUNK_SIZE);
        chunk.resize(bytesRead);

        qDebug("Read %lld bytes", bytesRead);

        // TODO: this needs moved out of here. Would make much more sense to have a class (or classes)
        // responsible for handling low level situations like this to do things like:
        // - determine encoding
        // - determine space vs tabs
        // - determine indentation size

        if (first_read) {
            first_read = false;

            bomType = detectBom(chunk);

            if (bomType != BomType::None) {
                qDebug("BOM found");
            }

            if (bomType == BomType::Utf8) {
                chunk.remove(0, bomLength(bomType));
            }

            if (bomType == BomType::Utf16BE || bomType == BomType::Utf16LE) {
                // Um...ignore this for now?
            }
        }

        appendText(chunk.size(), chunk.constData());
    } while (!file.atEnd() && status() == SC_STATUS_OK);

    file.close();

    // Restore it back
    this->blockSignals(false);
    setUndoCollection(true);
    // modEventMask(SC_MODEVENTMASKALL)?

    if (status() != SC_STATUS_OK) {
        qWarning("something bad happened in document->add_data() %ld", status());
        return false;
    }

    if (bytesRead == -1) {
        qWarning("Something bad happened when reading disk %d %s", file.error(), qUtf8Printable(file.errorString()));
        return false;
    }

    if (!QFileInfo(file).isWritable()) {
        qInfo("Setting file as read-only");
        setReadOnly(true);
    }

    return true;
}

QDateTime ScintillaNext::fileTimestamp()
{
    Q_ASSERT(bufferType != ScintillaNext::New);

    fileInfo.refresh();
    qInfo("%s last modified %s", qUtf8Printable(fileInfo.fileName()), qUtf8Printable(fileInfo.lastModified().toString()));
    return fileInfo.lastModified();
}

void ScintillaNext::updateTimestamp()
{
    // A remote buffer has no local QFileInfo to stat; skip the local timestamp
    // refresh (remote freshness is handled by the poll-watcher, not here).
    if (isRemote()) {
        modifiedTime = QDateTime();
        return;
    }
    modifiedTime = fileTimestamp();
}

void ScintillaNext::setFileInfo(const QString &filePath)
{
    fileInfo.setFile(filePath);
    fileInfo.makeAbsolute();

    Q_ASSERT(fileInfo.exists());

    name = fileInfo.fileName();
    bufferType = ScintillaNext::File;

    updateTimestamp();
}

void ScintillaNext::updatePathAfterMove(const QString &newFilePath)
{
    // Metadata-only move: reuse setFileInfo for the full internal state update
    // (fileInfo, name, bufferType=File, updateTimestamp) then emit renamed().
    // Deliberately NOT setSavePoint() — disk content is untouched, so a dirty
    // buffer stays dirty. Emit only renamed() (no aboutToSave/saved): those are
    // save signals and nothing was written here.
    setFileInfo(newFilePath);

    emit renamed();
}

void ScintillaNext::detachFileInfo(const QString &newName)
{
    setName(newName);

    bufferType = ScintillaNext::New;
}

void ScintillaNext::setTemporary(bool temp)
{
    temporary = temp;

    // Fake this signal
    emit savePointChanged(temporary);
}
