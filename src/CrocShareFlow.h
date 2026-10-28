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
    void receiveIntoFolder(FolderZipTransfer *zip, const QString &workspaceRoot,
                           const QString &folderPath, bool ssh);
    // The same with the code-phrase given (the Control API) instead of asked.
    void receiveIntoFolder(FolderZipTransfer *zip, const QString &workspaceRoot,
                           const QString &folderPath, bool ssh, const QString &phrase);
    // Errors reach the caller through failed() only, without a message box.
    void setQuiet(bool quiet) { m_quiet = quiet; }

signals:
    // Sharing: the code-phrase to give the other side (also shown to the user).
    void phraseReady(const QString &phrase);
    // Sharing: the other side received it. Receiving: the files are extracted.
    void finished();
    void failed(const QString &message);

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
    QString m_extractWorkspaceRoot;
    bool m_extractSsh = false;
    bool m_quiet = false;
};

#endif // CROCSHAREFLOW_H
