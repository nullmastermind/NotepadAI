/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 */

#include "CrocTool.h"

#include "DataPaths.h"
#include "ZipArchive.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>
#include <QTemporaryDir>
#include <QUrl>

namespace {

QString stripAnsi(QString s)
{
    static const QRegularExpression ansi(QStringLiteral("\x1B\\[[0-9;]*[A-Za-z]"));
    s.remove(ansi);
    return s;
}

QString toolsDir()
{
    return QDir(DataPaths::appDataLocation()).filePath(QStringLiteral("tools/croc"));
}

QString lastStatusLine(const QString &acc)
{
    QString t = stripAnsi(acc);
    t.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = t.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString s = lines.at(i).trimmed();
        if (!s.isEmpty())
            return s;
    }
    return {};
}

bool looksLikeTransfer(const QString &line)
{
    return line.contains(QLatin1String("authenticating"))
        || line.contains(QLatin1String("opening transfer"))
        || line.contains(QLatin1String("waiting for file list"))
        || line.contains(QLatin1String("Receiving '"))
        || line.contains(QLatin1String("Sending ("))
        || line.contains(QLatin1Char('%'));
}

} // namespace

CrocTool::CrocTool(QObject *parent)
    : QObject(parent)
{
    m_handshakeTimer = new QTimer(this);
    m_handshakeTimer->setSingleShot(true);
    m_handshakeTimer->setInterval(60'000);
    connect(m_handshakeTimer, &QTimer::timeout, this, &CrocTool::onHandshakeTimeout);

    m_stallTimer = new QTimer(this);
    m_stallTimer->setSingleShot(true);
    m_stallTimer->setInterval(90'000);
    connect(m_stallTimer, &QTimer::timeout, this, &CrocTool::onStallTimeout);
}

CrocTool::~CrocTool()
{
    cancel();
}

QString CrocTool::parseCodePhrase(const QString &text)
{
    const QString clean = stripAnsi(text);
    static const QRegularExpression reCodeIs(QStringLiteral("Code is:\\s*(\\S+)"),
                                             QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = reCodeIs.match(clean);
    if (m.hasMatch())
        return m.captured(1).trimmed();
    static const QRegularExpression reUrl(QStringLiteral("[?&]code=([^\\s&]+)"));
    m = reUrl.match(clean);
    if (m.hasMatch())
        return m.captured(1).trimmed();
    static const QRegularExpression reRun(QStringLiteral("\\bcroc\\s+(\\S+\\-\\S+)"));
    m = reRun.match(clean);
    if (m.hasMatch())
        return m.captured(1).trimmed();
    return {};
}

QString CrocTool::generateSendCode()
{
    const QString id = QUuid::createUuid().toString(QUuid::Id128).left(10);
    return QStringLiteral("nai-") + id;
}

bool CrocTool::isValidCodePhrase(const QString &phrase)
{
    const QString p = phrase.trimmed();
    if (p.size() < 6 || p.size() > 128)
        return false;
    if (p.contains(QLatin1Char(' ')) || p.contains(QLatin1Char('\n')) || p.contains(QLatin1Char('\t')))
        return false;
    return p.contains(QLatin1Char('-'));
}

QString CrocTool::releaseAssetHint()
{
    const QString arch = QSysInfo::currentCpuArchitecture().toLower();
    const bool arm = arch.contains(QLatin1String("arm")) || arch.contains(QLatin1String("aarch"));
#if defined(Q_OS_WIN)
    Q_UNUSED(arm);
    return QStringLiteral("Windows-64bit.zip");
#elif defined(Q_OS_MACOS)
    return arm ? QStringLiteral("macOS-ARM64.tar.gz") : QStringLiteral("macOS-64bit.tar.gz");
#else
    return arm ? QStringLiteral("Linux-ARM64.tar.gz") : QStringLiteral("Linux-64bit.tar.gz");
#endif
}

QString CrocTool::bundledBinaryPath()
{
#ifdef Q_OS_WIN
    return QDir(toolsDir()).filePath(QStringLiteral("croc.exe"));
#else
    return QDir(toolsDir()).filePath(QStringLiteral("croc"));
#endif
}

QString CrocTool::fallbackRelayAddress()
{
    return QStringLiteral("142.132.189.179:9009");
}

QString CrocTool::pickRelayIPv4(const QList<QHostAddress> &addresses)
{
    for (const QHostAddress &addr : addresses) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()
            && !addr.isNull())
            return addr.toString() + QLatin1String(":9009");
    }
    return fallbackRelayAddress();
}

QString CrocTool::relayAddress()
{
    const QHostInfo hi = QHostInfo::fromName(QStringLiteral("getcroc.com"));
    if (hi.error() == QHostInfo::NoError)
        return pickRelayIPv4(hi.addresses());
    return fallbackRelayAddress();
}

QString CrocTool::formatProcessFailure(int exitCode, const QString &output, const QString &secret)
{
    QString detail = stripAnsi(output);
    if (!secret.isEmpty())
        detail.replace(secret, QStringLiteral("***"));
    detail.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    detail = detail.trimmed();
    if (detail.size() > 600)
        detail = detail.right(600).trimmed();
    if (detail.isEmpty())
        return tr("croc failed (exit %1) with no output.").arg(exitCode);
    return tr("croc failed (exit %1).\n%2").arg(exitCode).arg(detail);
}

void CrocTool::cancel()
{
    m_userCancelled = true;
    m_timedOut = false;
    if (m_handshakeTimer)
        m_handshakeTimer->stop();
    if (m_stallTimer)
        m_stallTimer->stop();
    if (m_reply) {
        m_reply->abort();
        m_reply = nullptr;
    }
    if (m_proc) {
        m_proc->disconnect();
        m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_busy = false;
}

void CrocTool::ensureBinary()
{
    if (m_busy)
        return;
    tryPathThenBundled();
}

void CrocTool::tryPathThenBundled()
{
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("croc"));
    if (!onPath.isEmpty() && QFileInfo::exists(onPath)) {
        m_binary = onPath;
        emit binaryReady(m_binary);
        return;
    }
    const QString bundled = bundledBinaryPath();
    if (QFileInfo::exists(bundled) && QFileInfo(bundled).isExecutable()) {
        m_binary = bundled;
        emit binaryReady(m_binary);
        return;
    }
#ifdef Q_OS_WIN
    if (QFileInfo::exists(bundled)) {
        m_binary = bundled;
        emit binaryReady(m_binary);
        return;
    }
#endif
    tryPackageManager();
}

void CrocTool::tryPackageManager()
{
    QString prog;
    QStringList args;
#ifdef Q_OS_WIN
    if (!QStandardPaths::findExecutable(QStringLiteral("winget")).isEmpty()) {
        prog = QStringLiteral("winget");
        args = QStringList{QStringLiteral("install"), QStringLiteral("-e"),
                           QStringLiteral("--id"), QStringLiteral("schollz.croc"),
                           QStringLiteral("--accept-package-agreements"),
                           QStringLiteral("--accept-source-agreements")};
    } else if (!QStandardPaths::findExecutable(QStringLiteral("scoop")).isEmpty()) {
        prog = QStringLiteral("scoop");
        args = QStringList{QStringLiteral("install"), QStringLiteral("croc")};
    } else if (!QStandardPaths::findExecutable(QStringLiteral("choco")).isEmpty()) {
        prog = QStringLiteral("choco");
        args = QStringList{QStringLiteral("install"), QStringLiteral("croc"), QStringLiteral("-y")};
    }
#elif defined(Q_OS_MACOS)
    if (!QStandardPaths::findExecutable(QStringLiteral("brew")).isEmpty()) {
        prog = QStringLiteral("brew");
        args = QStringList{QStringLiteral("install"), QStringLiteral("croc")};
    }
#endif
    if (prog.isEmpty()) {
        tryGitHub();
        return;
    }
    m_busy = true;
    if (m_proc) {
        m_proc->disconnect();
        m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_userCancelled = false;
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (m_proc) {
            m_proc->deleteLater();
            m_proc = nullptr;
        }
        m_busy = false;
        if (m_userCancelled)
            return;
        const QString onPath = QStandardPaths::findExecutable(QStringLiteral("croc"));
        if (code == 0 && !onPath.isEmpty()) {
            m_binary = onPath;
            emit binaryReady(m_binary);
            return;
        }
        tryGitHub();
    });
    m_proc->start(prog, args);
    if (!m_proc->waitForStarted(5000)) {
        if (!m_proc) {
            m_busy = false;
            return;
        }
        m_proc->disconnect();
        m_proc->deleteLater();
        m_proc = nullptr;
        m_busy = false;
        if (m_userCancelled)
            return;
        tryGitHub();
    }
}

void CrocTool::tryGitHub()
{
    m_busy = true;
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/schollz/croc/releases/latest")));
    req.setRawHeader("User-Agent", "NotepadAI");
    req.setRawHeader("Accept", "application/vnd.github+json");
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this, &CrocTool::onGithubLatestFinished);
}

void CrocTool::onGithubLatestFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply) {
        m_busy = false;
        emit binaryFailed(tr("Could not reach GitHub releases.")
                          + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
        return;
    }
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        m_busy = false;
        emit binaryFailed(tr("Could not fetch croc release: %1").arg(reply->errorString())
                          + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
        return;
    }
    const QByteArray body = reply->readAll();
    const QString hint = releaseAssetHint();
    const int idx = body.indexOf(hint.toUtf8());
    if (idx < 0) {
        m_busy = false;
        emit binaryFailed(tr("No croc build for this platform (%1).")
                              .arg(hint)
                          + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
        return;
    }
    const int urlKey = body.indexOf("browser_download_url", idx);
    if (urlKey < 0) {
        m_busy = false;
        emit binaryFailed(tr("Could not parse croc release JSON.")
                          + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
        return;
    }
    const int q1 = body.indexOf('"', urlKey + 22);
    const int q2 = body.indexOf('"', q1 + 1);
    if (q1 < 0 || q2 < 0) {
        m_busy = false;
        emit binaryFailed(tr("Could not parse croc download URL.")
                          + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
        return;
    }
    const QUrl url = QUrl(QString::fromUtf8(body.mid(q1 + 1, q2 - q1 - 1)));
    if (url.scheme() != QLatin1String("https")
        || url.host() != QLatin1String("github.com")
        || !url.path().startsWith(QLatin1String("/schollz/croc/releases/download/"))) {
        m_busy = false;
        emit binaryFailed(tr("Refusing to download croc from an unexpected URL.")
                          + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
        return;
    }
    m_expectedSha256.clear();
    const int digestPos = body.indexOf("sha256:", idx);
    if (digestPos > 0 && digestPos < idx + 2500) {
        const int hexStart = digestPos + 7;
        int hexEnd = hexStart;
        while (hexEnd < body.size()) {
            const char c = body.at(hexEnd);
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))
                ++hexEnd;
            else
                break;
        }
        if (hexEnd - hexStart == 64)
            m_expectedSha256 = QByteArray::fromHex(body.mid(hexStart, 64));
    }
    QDir().mkpath(toolsDir());
    m_downloadPath = QDir(toolsDir()).filePath(QFileInfo(url.path()).fileName());
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "NotepadAI");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam->get(req);
    connect(m_reply, &QNetworkReply::finished, this, &CrocTool::onGithubAssetFinished);
}

void CrocTool::onGithubAssetFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply) {
        m_busy = false;
        emit binaryFailed(tr("croc download failed.")
                          + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
        return;
    }
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        m_busy = false;
        emit binaryFailed(tr("croc download failed: %1").arg(reply->errorString())
                          + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
        return;
    }
    QFile f(m_downloadPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_busy = false;
        emit binaryFailed(tr("Could not write %1").arg(m_downloadPath));
        return;
    }
    f.write(reply->readAll());
    f.close();
    if (!m_expectedSha256.isEmpty()) {
        QFile in(m_downloadPath);
        if (!in.open(QIODevice::ReadOnly)
            || QCryptographicHash::hash(in.readAll(), QCryptographicHash::Sha256) != m_expectedSha256) {
            in.close();
            QFile::remove(m_downloadPath);
            m_busy = false;
            emit binaryFailed(tr("croc download checksum mismatch.")
                              + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
            return;
        }
    }
    unpackToBundle(m_downloadPath);
}

void CrocTool::unpackToBundle(const QString &archivePath)
{
    QDir().mkpath(toolsDir());
    const QString dest = bundledBinaryPath();
    if (archivePath.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
        ZipReader reader;
        if (!reader.open(archivePath)) {
            m_busy = false;
            emit binaryFailed(reader.errorString());
            return;
        }
        const QStringList names = reader.fileEntries();
        QString exeEntry;
        for (const QString &n : names) {
            if (n.endsWith(QLatin1String("croc.exe"), Qt::CaseInsensitive)
                || n.section(QLatin1Char('/'), -1) == QLatin1String("croc")) {
                exeEntry = n;
                break;
            }
        }
        if (exeEntry.isEmpty() || !reader.extractToFile(exeEntry, dest)) {
            m_busy = false;
            emit binaryFailed(tr("croc binary missing from archive.")
                              + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
            return;
        }
    } else {
        QProcess tar;
        tar.setWorkingDirectory(toolsDir());
        tar.start(QStringLiteral("tar"),
                  {QStringLiteral("-xzf"), archivePath, QStringLiteral("-C"), toolsDir()});
        if (!tar.waitForFinished(60000) || tar.exitCode() != 0) {
            m_busy = false;
            emit binaryFailed(tr("Could not unpack croc archive.")
                              + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
            return;
        }
    }
    QFile::remove(archivePath);
#ifndef Q_OS_WIN
    QFile::setPermissions(dest, QFile::permissions(dest) | QFile::ExeUser | QFile::ExeGroup
                                                    | QFile::ExeOther);
#endif
    if (!QFileInfo::exists(dest)) {
        m_busy = false;
        emit binaryFailed(tr("croc did not unpack to %1").arg(dest)
                          + QStringLiteral("\nhttps://github.com/schollz/croc/releases"));
        return;
    }
    m_binary = dest;
    m_busy = false;
    emit binaryReady(m_binary);
}

void CrocTool::sendFile(const QString &zipPath)
{
    if (m_binary.isEmpty()) {
        emit processFailed(tr("croc is not installed."));
        return;
    }
    const QString code = generateSendCode();
    m_sendCode = code;
    QStringList args{QStringLiteral("--yes"), QStringLiteral("--ignore-stdin"),
                     QStringLiteral("--disable-clipboard"), QStringLiteral("--relay"),
                     relayAddress(), QStringLiteral("send"), QStringLiteral("--no-local"),
                     QStringLiteral("--code"), code, zipPath};
    startProcess(args, QFileInfo(zipPath).absolutePath(), true);
}

void CrocTool::receiveToDir(const QString &phrase, const QString &destDir)
{
    if (m_binary.isEmpty()) {
        emit processFailed(tr("croc is not installed."));
        return;
    }
    QStringList args{QStringLiteral("--yes"), QStringLiteral("--ignore-stdin"),
                     QStringLiteral("--overwrite"), QStringLiteral("--relay"), relayAddress(),
                     phrase};
    startProcess(args, destDir, false, phrase);
}

void CrocTool::startProcess(const QStringList &args, const QString &workingDir, bool sendMode,
                           const QString &secret)
{
    if (m_handshakeTimer)
        m_handshakeTimer->stop();
    if (m_stallTimer)
        m_stallTimer->stop();
    if (m_proc) {
        m_proc->disconnect();
        m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_userCancelled = false;
    m_timedOut = false;
    m_transferSeen = false;
    m_busy = true;
    m_sendMode = sendMode;
    m_phraseEmitted = false;
    m_stdoutAcc.clear();
    m_lastStatus.clear();
    m_timeoutMsg.clear();
    m_activeSecret = secret;
    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(workingDir);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyRead, this, &CrocTool::onProcessOutput);
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        onProcessDone(st == QProcess::CrashExit ? -1 : code);
    });
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (secret.isEmpty())
        env.remove(QStringLiteral("CROC_SECRET"));
    else
        env.insert(QStringLiteral("CROC_SECRET"), secret);
    m_proc->setProcessEnvironment(env);
    m_proc->start(m_binary, args);
    if (!m_proc->waitForStarted(8000)) {
        if (!m_proc) {
            m_busy = false;
            return;
        }
        const QString err = m_proc->errorString();
        m_proc->deleteLater();
        m_proc = nullptr;
        m_busy = false;
        if (!m_userCancelled)
            emit processFailed(tr("Could not start croc: %1").arg(err));
        return;
    }
    if (!m_proc)
        return;
    m_proc->closeWriteChannel();
    if (!m_sendMode)
        m_handshakeTimer->start();
}

void CrocTool::onProcessOutput()
{
    if (!m_proc)
        return;
    m_stdoutAcc += QString::fromUtf8(m_proc->readAll());
    const QString line = lastStatusLine(m_stdoutAcc);
    if (!line.isEmpty() && line != m_lastStatus) {
        m_lastStatus = line;
        emit statusText(line);
        if (looksLikeTransfer(line)) {
            m_transferSeen = true;
            m_handshakeTimer->stop();
            m_stallTimer->start();
        } else if (m_transferSeen) {
            m_stallTimer->start();
        }
    }
    if (m_sendMode && !m_phraseEmitted && !m_sendCode.isEmpty()) {
        const QString acc = stripAnsi(m_stdoutAcc);
        if (acc.contains(QLatin1String("On the other computer"))
            || acc.contains(QLatin1String("getcroc.com/?code="))
            || acc.contains(QLatin1String("Sending '"))) {
            m_phraseEmitted = true;
            emit codePhraseReady(m_sendCode);
        }
    }
}

void CrocTool::onHandshakeTimeout()
{
    if (m_transferSeen)
        return;
    QString msg = tr("Timed out waiting for the other side. Keep the sender window open and check the code-phrase.");
    const QString tail = lastStatusLine(m_stdoutAcc);
    if (!tail.isEmpty())
        msg += QLatin1Char('\n') + tail;
    killForTimeout(msg);
}

void CrocTool::onStallTimeout()
{
    QString msg = tr("croc transfer stalled (no progress).");
    const QString tail = lastStatusLine(m_stdoutAcc);
    if (!tail.isEmpty())
        msg += QLatin1Char('\n') + tail;
    killForTimeout(msg);
}

void CrocTool::killForTimeout(const QString &message)
{
    m_timeoutMsg = message;
    m_timedOut = true;
    m_handshakeTimer->stop();
    m_stallTimer->stop();
    if (!m_proc) {
        m_busy = false;
        emit processFailed(message);
        return;
    }
    m_proc->kill();
    if (m_proc->state() != QProcess::NotRunning)
        m_proc->waitForFinished(2000);
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->disconnect();
        m_proc->deleteLater();
        m_proc = nullptr;
        m_busy = false;
        emit processFailed(message);
    }
}

void CrocTool::onProcessDone(int exitCode)
{
    m_busy = false;
    if (m_handshakeTimer)
        m_handshakeTimer->stop();
    if (m_stallTimer)
        m_stallTimer->stop();
    if (m_proc) {
        m_stdoutAcc += QString::fromUtf8(m_proc->readAll());
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    if (m_userCancelled) {
        m_userCancelled = false;
        return;
    }
    if (m_timedOut) {
        m_timedOut = false;
        emit processFailed(m_timeoutMsg);
        return;
    }
    if (exitCode != 0) {
        emit processFailed(formatProcessFailure(exitCode, m_stdoutAcc, m_activeSecret));
        return;
    }
    if (m_sendMode)
        emit sendFinished();
    else
        emit receiveFinished();
}
