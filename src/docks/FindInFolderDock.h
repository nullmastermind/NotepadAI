#ifndef FINDINFOLDEROCK_H
#define FINDINFOLDEROCK_H

#include <QDockWidget>
#include <QVector>

#include <memory>

class FolderSearchEngine;
struct FolderSearchSink;
struct FolderSearchFileBatch;
class QTimer;
class QTreeWidgetItem;

namespace Ui {
class FindInFolderDock;
}

class FindInFolderDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit FindInFolderDock(QWidget *parent = nullptr);
    ~FindInFolderDock() override;

    void setFolder(const QString &folderPath, const QString &workspaceRoot);

signals:
    void resultActivated(const QString &filePath, int lineNumber, int matchStart, int matchEnd);
    void resultClicked(const QString &filePath, int lineNumber, int matchStart, int matchEnd);

private slots:
    void performSearch();
    void drainResults();
    void onItemActivated(QTreeWidgetItem *item, int column);
    void onItemClicked(QTreeWidgetItem *item, int column);

private:
    Ui::FindInFolderDock *ui;
    FolderSearchEngine *m_engine;
    QTimer *m_drainTimer;
    QString m_folderPath;
    QString m_workspaceRoot;
    int m_totalFiles = 0;
    int m_totalMatches = 0;
};

#endif // FINDINFOLDEROCK_H
