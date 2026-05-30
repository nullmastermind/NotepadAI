#include "FileWatcher.h"
#include "ScintillaNext.h"

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

FileWatcher::FileWatcher(QObject *parent)
    : QObject(parent)
{
    m_watcher = new QFileSystemWatcher(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(kDebounceMs);

    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &FileWatcher::onFileChanged);
    connect(m_debounce, &QTimer::timeout,
            this, &FileWatcher::onDebounceTimeout);
}

void FileWatcher::watchEditor(ScintillaNext *editor)
{
    if (!editor || !editor->isFile()) return;

    const QString path = editor->getFileInfo().canonicalFilePath();
    if (path.isEmpty()) return;

    m_pathToEditor.insert(path, editor);
    m_watcher->addPath(path);

    connect(editor, &ScintillaNext::saved,
            this, &FileWatcher::onEditorSaved, Qt::UniqueConnection);
    connect(editor, &ScintillaNext::renamed,
            this, &FileWatcher::onEditorRenamed, Qt::UniqueConnection);
}

void FileWatcher::onEditorSaved()
{
    auto *editor = qobject_cast<ScintillaNext *>(sender());
    if (!editor || !editor->isFile()) return;
    const QString p = editor->getFileInfo().canonicalFilePath();
    if (p.isEmpty()) return;
    if (!m_watcher->files().contains(p))
        m_watcher->addPath(p);
}

void FileWatcher::onEditorRenamed()
{
    auto *editor = qobject_cast<ScintillaNext *>(sender());
    if (!editor) return;
    const auto keys = m_pathToEditor.keys(editor);
    for (const QString &oldPath : keys) {
        m_pathToEditor.remove(oldPath);
        m_pendingPaths.remove(oldPath);
        if (m_watcher->files().contains(oldPath))
            m_watcher->removePath(oldPath);
    }
    if (editor->isFile()) {
        const QString newPath = editor->getFileInfo().canonicalFilePath();
        if (!newPath.isEmpty()) {
            m_pathToEditor.insert(newPath, editor);
            m_watcher->addPath(newPath);
        }
    }
}

void FileWatcher::unwatchEditor(ScintillaNext *editor)
{
    const auto keys = m_pathToEditor.keys(editor);
    for (const QString &path : keys) {
        m_pathToEditor.remove(path);
        m_pendingPaths.remove(path);
        if (m_watcher->files().contains(path))
            m_watcher->removePath(path);
    }
}

void FileWatcher::onFileChanged(const QString &path)
{
    if (!m_pathToEditor.contains(path)) return;

    m_pendingPaths.insert(path);

    if (!m_watcher->files().contains(path) && QFileInfo::exists(path))
        m_watcher->addPath(path);

    if (!m_firstEventTimerRunning) {
        m_firstEventTimer.start();
        m_firstEventTimerRunning = true;
    }

    if (m_firstEventTimer.elapsed() >= kMaxWaitMs) {
        m_debounce->stop();
        onDebounceTimeout();
    } else {
        m_debounce->start();
    }
}

void FileWatcher::onDebounceTimeout()
{
    m_firstEventTimerRunning = false;

    const QSet<QString> paths = m_pendingPaths;
    m_pendingPaths.clear();

    for (const QString &path : paths) {
        ScintillaNext *editor = m_pathToEditor.value(path, nullptr);
        if (!editor) continue;

        auto state = editor->checkFileForStateChange();
        switch (state) {
        case ScintillaNext::Modified:
            emit fileModifiedExternally(editor);
            break;
        case ScintillaNext::Deleted:
            emit fileDeletedExternally(editor);
            break;
        case ScintillaNext::Restored:
            emit fileRestoredExternally(editor);
            break;
        case ScintillaNext::NoChange:
            break;
        }
    }
}
