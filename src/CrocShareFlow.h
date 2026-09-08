/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#ifndef CROCSHAREFLOW_H
#define CROCSHAREFLOW_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>

#include <functional>

class CrocTool;
class FolderZipTransfer;
class QDialog;
class QProgressDialog;
class QWidget;

class CrocShareFlow : public QObject
{
    Q_OBJECT

public:
    explicit CrocShareFlow(QWidget *dialogParent);

    void shareFolder(FolderZipTransfer *zip, const QString &workspaceRoot,
                     const QString &folderPath, const QString &folderName, bool ssh);
    void receiveIntoFolder(FolderZipTransfer *zip, const QString &folderPath, bool ssh);

private:
    void ensureThen(const std::function<void()> &next);
    void showPhrase(const QString &phrase);
    void cleanupTemp();
    void closeLoading();
    void showError(const QString &msg);
    QProgressDialog *loading(const QString &text);

    QWidget *m_parent = nullptr;
    CrocTool *m_croc = nullptr;
    QTemporaryDir *m_tmp = nullptr;
    QString m_zipPath;
    QPointer<QDialog> m_phraseDlg;
    QPointer<QProgressDialog> m_loadDlg;
    QPointer<FolderZipTransfer> m_zip;
    QString m_extractTarget;
    bool m_extractSsh = false;
};

#endif // CROCSHAREFLOW_H
