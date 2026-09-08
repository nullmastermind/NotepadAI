/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#ifndef CROCTOOL_H
#define CROCTOOL_H

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTimer;

class CrocTool : public QObject
{
    Q_OBJECT

public:
    explicit CrocTool(QObject *parent = nullptr);
    ~CrocTool() override;

    static QString parseCodePhrase(const QString &text);
    static QString generateSendCode();
    static QString releaseAssetHint();
    static QString bundledBinaryPath();
    static bool isValidCodePhrase(const QString &phrase);
    static QString fallbackRelayAddress();
    static QString pickRelayIPv4(const QList<QHostAddress> &addresses);
    static QString relayAddress();
    static QString formatProcessFailure(int exitCode, const QString &output,
                                        const QString &secret = QString());

    bool isBusy() const { return m_busy; }
    QString binaryPath() const { return m_binary; }

    void ensureBinary();
    void sendFile(const QString &zipPath);
    void receiveToDir(const QString &phrase, const QString &destDir);
    void cancel();

signals:
    void binaryReady(const QString &path);
    void binaryFailed(const QString &message);
    void codePhraseReady(const QString &phrase);
    void sendFinished();
    void receiveFinished();
    void processFailed(const QString &message);
    void statusText(const QString &text);

private:
    void tryPathThenBundled();
    void tryPackageManager();
    void tryGitHub();
    void onGithubLatestFinished();
    void onGithubAssetFinished();
    void unpackToBundle(const QString &archivePath);
    void startProcess(const QStringList &args, const QString &workingDir, bool sendMode,
                     const QString &secret = QString());
    void onProcessOutput();
    void onProcessDone(int exitCode);
    void onHandshakeTimeout();
    void onStallTimeout();
    void killForTimeout(const QString &message);

    QString m_binary;
    bool m_busy = false;
    bool m_sendMode = false;
    bool m_phraseEmitted = false;
    bool m_userCancelled = false;
    bool m_timedOut = false;
    bool m_transferSeen = false;
    QString m_stdoutAcc;
    QString m_activeSecret;
    QString m_sendCode;
    QString m_timeoutMsg;
    QString m_lastStatus;
    QProcess *m_proc = nullptr;
    QTimer *m_handshakeTimer = nullptr;
    QTimer *m_stallTimer = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    QString m_downloadPath;
    QByteArray m_expectedSha256;
};

#endif // CROCTOOL_H
