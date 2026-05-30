#ifndef FILEWATCHER_H
#define FILEWATCHER_H

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class QFileSystemWatcher;
class QTimer;
class ScintillaNext;

class FileWatcher : public QObject
{
    Q_OBJECT

public:
    explicit FileWatcher(QObject *parent = nullptr);

    void watchEditor(ScintillaNext *editor);
    void unwatchEditor(ScintillaNext *editor);

signals:
    void fileModifiedExternally(ScintillaNext *editor);
    void fileDeletedExternally(ScintillaNext *editor);
    void fileRestoredExternally(ScintillaNext *editor);

private slots:
    void onFileChanged(const QString &path);
    void onDebounceTimeout();
    // Re-arm the QFileSystemWatcher after the editor saves / is renamed. These
    // MUST be member-function slots, not lambdas: they are connected with
    // Qt::UniqueConnection, and QObject::connect silently refuses to create a
    // unique connection whose slot is a lambda/functor (it requires a pointer
    // to member function). The originating editor is recovered via sender().
    void onEditorSaved();
    void onEditorRenamed();

private:
    static constexpr int kDebounceMs = 150;
    static constexpr qint64 kMaxWaitMs = 1000;

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QElapsedTimer m_firstEventTimer;
    bool m_firstEventTimerRunning = false;

    QHash<QString, ScintillaNext *> m_pathToEditor;
    QSet<QString> m_pendingPaths;
};

#endif // FILEWATCHER_H
