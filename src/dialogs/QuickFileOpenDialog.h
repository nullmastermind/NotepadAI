#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QListView>
#include <QStringList>
#include <QFutureWatcher>
#include <QVector>

class QStandardItemModel;

class QuickFileOpenDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QuickFileOpenDialog(const QString &rootPath, QWidget *parent = nullptr);
    ~QuickFileOpenDialog() override;

    QString selectedFilePath() const;

    static constexpr int MatchPositionsRole = Qt::UserRole + 1;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private slots:
    void onTextChanged(const QString &text);
    void onItemActivated(const QModelIndex &index);
    void onIndexReady();

private:
    static QStringList buildFileIndex(const QString &rootPath);
    void applyFilter(const QString &pattern);
    static int fuzzyScore(const QString &pattern, const QString &candidate);
    static int fuzzyMatch(const QString &pattern, const QString &candidate, QVector<int> &positions);

    QString m_rootPath;
    QLineEdit *m_lineEdit = nullptr;
    QListView *m_listView = nullptr;
    QStandardItemModel *m_model = nullptr;
    QFutureWatcher<QStringList> *m_watcher = nullptr;
    QStringList m_allFiles;
    QStringList m_filteredFiles;
    QString m_selectedFile;
    bool m_indexReady = false;
};
