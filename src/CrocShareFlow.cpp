/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#include "CrocShareFlow.h"

#include "CrocTool.h"
#include "FolderZipTransfer.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QVBoxLayout>

CrocShareFlow::CrocShareFlow(QWidget *dialogParent)
    : QObject(dialogParent)
    , m_parent(dialogParent)
    , m_croc(new CrocTool(this))
{
    connect(m_croc, &CrocTool::binaryFailed, this, [this](const QString &msg) {
        showError(msg);
        cleanupTemp();
        deleteLater();
    });
    connect(m_croc, &CrocTool::processFailed, this, [this](const QString &msg) {
        showError(msg);
        cleanupTemp();
        deleteLater();
    });
    connect(m_croc, &CrocTool::statusText, this, [this](const QString &text) {
        if (m_loadDlg)
            m_loadDlg->setLabelText(text);
    });
    connect(m_croc, &CrocTool::codePhraseReady, this, [this](const QString &phrase) {
        showPhrase(phrase);
    });
    connect(m_croc, &CrocTool::sendFinished, this, [this]() {
        if (m_phraseDlg)
            m_phraseDlg->close();
        cleanupTemp();
        deleteLater();
    });
    connect(m_croc, &CrocTool::receiveFinished, this, [this]() {
        QString zip;
        if (m_tmp) {
            const QFileInfoList zips = QDir(m_tmp->path()).entryInfoList(
                {QStringLiteral("*.zip")}, QDir::Files);
            if (!zips.isEmpty())
                zip = zips.first().absoluteFilePath();
            else {
                const QFileInfoList all = QDir(m_tmp->path()).entryInfoList(QDir::Files);
                if (all.size() == 1)
                    zip = all.first().absoluteFilePath();
            }
        }
        if (zip.isEmpty() || !m_zip) {
            showError(tr("No zip file was received."));
            cleanupTemp();
            deleteLater();
            return;
        }
        closeLoading();
        if (m_extractSsh)
            m_zip->uploadRemote(m_extractWorkspaceRoot, zip, m_extractTarget, m_parent);
        else
            m_zip->uploadLocal(m_extractWorkspaceRoot, zip, m_extractTarget, m_parent);
        // Temp dir lives until this flow is destroyed; destroy after extract starts.
        // Extract reads the zip on a worker; keep tmp until zip transfer completes.
        connect(m_zip, &FolderZipTransfer::transferCompleted, this, [this](int) {
            cleanupTemp();
            deleteLater();
        });
        connect(m_zip, &FolderZipTransfer::transferError, this, [this](const QString &) {
            cleanupTemp();
            deleteLater();
        });
        connect(m_zip, &FolderZipTransfer::transferCancelled, this, [this]() {
            cleanupTemp();
            deleteLater();
        });
    });
}

void CrocShareFlow::closeLoading()
{
    if (!m_loadDlg)
        return;
    m_loadDlg->disconnect();
    m_loadDlg->hide();
    m_loadDlg->deleteLater();
    m_loadDlg = nullptr;
}

void CrocShareFlow::showError(const QString &msg)
{
    QWidget *anchor = m_parent;
    if (m_loadDlg) {
        m_loadDlg->disconnect();
        anchor = m_loadDlg;
    } else if (m_phraseDlg) {
        m_phraseDlg->disconnect();
        anchor = m_phraseDlg;
    }
    QMessageBox::warning(anchor, tr("croc"), msg);
    closeLoading();
    if (m_phraseDlg) {
        m_phraseDlg->hide();
        m_phraseDlg->deleteLater();
        m_phraseDlg = nullptr;
    }
}

void CrocShareFlow::cleanupTemp()
{
    if (m_tmp) {
        delete m_tmp;
        m_tmp = nullptr;
    }
}

QProgressDialog *CrocShareFlow::loading(const QString &text)
{
    auto *dlg = new QProgressDialog(text, tr("Cancel"), 0, 0, m_parent);
    m_loadDlg = dlg;
    dlg->setWindowTitle(tr("croc"));
    dlg->setWindowModality(Qt::WindowModal);
    dlg->setMinimumDuration(0);
    dlg->setAutoClose(false);
    dlg->setAutoReset(false);
    dlg->setFixedWidth(420);
    dlg->show();
    connect(dlg, &QProgressDialog::canceled, this, [this]() {
        closeLoading();
        m_croc->disconnect(this);
        m_croc->cancel();
        if (m_zip)
            m_zip->cancel();
        cleanupTemp();
        deleteLater();
    });
    return dlg;
}

void CrocShareFlow::ensureThen(const std::function<void()> &next)
{
    QPointer<QProgressDialog> load = loading(tr("Looking for croc…"));
    connect(m_croc, &CrocTool::binaryReady, this, [this, next, load](const QString &) {
        if (!load)
            return;
        load->disconnect();
        load->hide();
        load->deleteLater();
        if (m_loadDlg == load)
            m_loadDlg = nullptr;
        next();
    }, Qt::SingleShotConnection);
    m_croc->ensureBinary();
}

void CrocShareFlow::shareFolder(FolderZipTransfer *zip, const QString &workspaceRoot,
                                const QString &folderPath, const QString &folderName, bool ssh)
{
    m_zip = zip;
    m_tmp = new QTemporaryDir();
    if (!m_tmp->isValid()) {
        QMessageBox::warning(m_parent, tr("croc"), tr("Could not create a temp folder."));
        deleteLater();
        return;
    }
    m_zipPath = QDir(m_tmp->path()).filePath(folderName + QStringLiteral(".zip"));
    ensureThen([this, workspaceRoot, folderPath, ssh]() {
        FolderZipTransfer *zip = m_zip;
        if (!zip) {
            cleanupTemp();
            deleteLater();
            return;
        }
        connect(zip, &FolderZipTransfer::transferCompleted, this, [this](int) {
            auto *load = loading(tr("Sharing with croc…"));
            Q_UNUSED(load);
            m_croc->sendFile(m_zipPath);
        }, Qt::SingleShotConnection);
        connect(zip, &FolderZipTransfer::transferError, this, [this](const QString &msg) {
            QMessageBox::warning(m_parent, tr("Zip"), msg);
            cleanupTemp();
            deleteLater();
        }, Qt::SingleShotConnection);
        connect(zip, &FolderZipTransfer::transferCancelled, this, [this]() {
            cleanupTemp();
            deleteLater();
        }, Qt::SingleShotConnection);
        if (ssh)
            zip->downloadRemote(workspaceRoot, folderPath, m_zipPath);
        else
            zip->downloadLocal(workspaceRoot, folderPath, m_zipPath);
    });
}

void CrocShareFlow::showPhrase(const QString &phrase)
{
    closeLoading();
    auto *dlg = new QDialog(m_parent);
    m_phraseDlg = dlg;
    dlg->setWindowTitle(tr("croc code-phrase"));
    dlg->setModal(false);
    dlg->setWindowModality(Qt::NonModal);
    dlg->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    auto *lay = new QVBoxLayout(dlg);
    lay->addWidget(new QLabel(tr("Share this code-phrase. Leave this window open until the other side finishes receiving.\nClosing it cancels the send."), dlg));
    auto *edit = new QLineEdit(phrase, dlg);
    edit->setReadOnly(true);
    lay->addWidget(edit);
    auto *copy = new QPushButton(tr("Copy"), dlg);
    copy->setAutoDefault(false);
    copy->setDefault(false);
    connect(copy, &QPushButton::clicked, dlg, [phrase, copy]() {
        QApplication::clipboard()->setText(phrase);
        copy->setText(tr("Copied"));
    });
    auto *cancelBtn = new QPushButton(tr("Cancel send"), dlg);
    cancelBtn->setAutoDefault(false);
    auto *row = new QHBoxLayout();
    row->addStretch(1);
    row->addWidget(copy);
    row->addWidget(cancelBtn);
    lay->addLayout(row);
    connect(cancelBtn, &QPushButton::clicked, dlg, &QDialog::reject);
    connect(dlg, &QDialog::finished, this, [this]() {
        m_croc->cancel();
        cleanupTemp();
        deleteLater();
    });
    dlg->setMinimumWidth(360);
    dlg->setMaximumWidth(480);
    dlg->show();
}

void CrocShareFlow::receiveIntoFolder(FolderZipTransfer *zip, const QString &workspaceRoot,
                                      const QString &folderPath, bool ssh)
{
    m_zip = zip;
    m_extractWorkspaceRoot = workspaceRoot;
    m_extractTarget = folderPath;
    m_extractSsh = ssh;
    bool ok = false;
    const QString phrase = QInputDialog::getText(m_parent, tr("croc code-phrase"),
                                                 tr("Code-phrase:"), QLineEdit::Normal,
                                                 QString(), &ok).trimmed();
    if (!ok || !CrocTool::isValidCodePhrase(phrase)) {
        if (ok)
            QMessageBox::warning(m_parent, tr("croc"), tr("That does not look like a croc code-phrase."));
        deleteLater();
        return;
    }
    ensureThen([this, phrase]() {
        m_tmp = new QTemporaryDir();
        if (!m_tmp->isValid()) {
            QMessageBox::warning(m_parent, tr("croc"), tr("Could not create a temp folder."));
            deleteLater();
            return;
        }
        auto *load = loading(tr("Receiving with croc…"));
        connect(m_croc, &CrocTool::receiveFinished, this, [this]() {
            closeLoading();
        }, Qt::SingleShotConnection);
        m_croc->receiveToDir(phrase, m_tmp->path());
    });
}
