#ifndef FOLDER_SEARCH_ENGINE_H
#define FOLDER_SEARCH_ENGINE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMutex>
#include <QRegularExpression>

#include <atomic>
#include <memory>

class GitProcessRunner;
class QTimer;

struct FolderSearchMatch {
    int lineNumber;
    int matchStart;
    int matchEnd;
    QString lineText;
};

struct FolderSearchFileBatch {
    QString filePath;
    QVector<FolderSearchMatch> matches;
};

struct FolderSearchSink {
    std::atomic<bool> cancelled{false};
    std::atomic<int> generation{0};
    QMutex mutex;
    QVector<FolderSearchFileBatch> pending;
    std::atomic<bool> enumerationDone{false};
    std::atomic<bool> workersLaunched{false};
    std::atomic<int> workersRemaining{0};
    QStringList enumeratedFiles; // guarded by mutex, set by DFS fallback
};

class FolderSearchEngine : public QObject
{
    Q_OBJECT
public:
    explicit FolderSearchEngine(QObject *parent = nullptr);
    ~FolderSearchEngine() override;

    void startSearch(const QString &folderPath,
                     const QString &workspaceRoot,
                     const QString &query,
                     bool caseSensitive,
                     bool wholeWord,
                     bool regex);
    void cancel();
    bool isRunning() const;
    void pollEnumeration();

    std::shared_ptr<FolderSearchSink> sink() const { return m_sink; }

signals:
    void searchComplete(int totalFiles, int totalMatches);

private:
    void onFilesEnumerated(const QStringList &files, int generation);
    void launchScanWorkers(const QStringList &absolutePaths, int generation);

    static QVector<FolderSearchMatch> scanFile(const QString &filePath,
                                               const QRegularExpression &re,
                                               const std::shared_ptr<FolderSearchSink> &sink,
                                               int generation);

    // Non-git fallback
    static QStringList walkDfsFiltered(const QString &folder,
                                       const std::shared_ptr<std::atomic<bool>> &cancel);

    GitProcessRunner *m_gitRunner = nullptr;
    std::shared_ptr<FolderSearchSink> m_sink;
    QString m_folderPath;
    QString m_workspaceRoot;
    QRegularExpression m_regex;
    std::atomic<bool> m_running{false};
    bool m_enumerationConsumed = false;
};

#endif // FOLDER_SEARCH_ENGINE_H
