/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "MiniAppInstance.h"
#include "widgets/WebViewWidget.h"

#include <QDateTime>
#include <QDir>
#include <QMetaEnum>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QHostAddress>
#include <QUrl>

MiniAppInstance::MiniAppInstance(const MiniAppDefinition &def, QObject *parent)
    : QObject(parent)
    , m_def(def)
{
}

MiniAppInstance::~MiniAppInstance()
{
    destroy();
}

QString MiniAppInstance::debugInfo() const
{
    const QMetaEnum me = QMetaEnum::fromType<State>();
    QString info;
    info += QStringLiteral("App ID: %1\n").arg(m_def.id);
    info += QStringLiteral("Name: %1\n").arg(m_def.name);
    info += QStringLiteral("URL: %1\n").arg(m_def.url);
    info += QStringLiteral("Health URL: %1\n").arg(m_def.effectiveHealthUrl());
    info += QStringLiteral("Command: %1\n").arg(m_def.command.isEmpty() ? QStringLiteral("(none)") : m_def.command);
    info += QStringLiteral("CWD: %1\n").arg(m_def.cwd.isEmpty() ? QStringLiteral("(none)") : m_def.cwd);
    info += QStringLiteral("State: %1\n").arg(QString::fromLatin1(me.valueToKey(m_state)));
    info += QStringLiteral("Last Error: %1\n").arg(m_lastError.isEmpty() ? QStringLiteral("(none)") : m_lastError);
    if (m_process) {
        info += QStringLiteral("Process PID: %1\n").arg(m_process->processId());
        info += QStringLiteral("Process State: %1\n").arg(m_process->state() == QProcess::Running ? QStringLiteral("Running") : QStringLiteral("Not Running"));
    } else {
        info += QStringLiteral("Process: (none)\n");
    }
    info += QStringLiteral("WebView: %1\n").arg(m_webView ? QStringLiteral("created") : QStringLiteral("(null)"));
    info += QStringLiteral("Health Timeout: %1 ms\n").arg(m_def.healthTimeoutMs);
    if (m_pollStartTime > 0) {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_pollStartTime;
        info += QStringLiteral("Poll Elapsed: %1 ms\n").arg(elapsed);
    }
    if (m_webView) {
        info += QStringLiteral("\n");
        info += m_webView->debugInfo();
    }
    if (!m_cdpHttpUrl.isEmpty()) {
        info += QStringLiteral("\n--- CDP ---\n");
        info += QStringLiteral("CDP HTTP URL: %1\n").arg(m_cdpHttpUrl);
        info += QStringLiteral("CDP WebSocket URL: %1\n").arg(m_cdpWsUrl);
    }
    return info;
}

void MiniAppInstance::start()
{
    if (m_state != Idle && m_state != Failed && m_state != Crashed)
        return;

    setState(Idle);

    // Pre-launch bind-test for CDP debug port
    if (m_def.debugPort > 0) {
        QTcpSocket sock;
        if (!sock.bind(QHostAddress::LocalHost, static_cast<quint16>(m_def.debugPort))) {
            m_lastError = tr("Debug port %1 is in use").arg(m_def.debugPort);
            setState(Failed);
            return;
        }
        sock.close();
    }

    if (!m_def.command.isEmpty()) {
        spawnProcess();
    } else {
        // No command — go straight to health polling
        startHealthPolling();
    }
}

void MiniAppInstance::retry()
{
    // Clean up previous attempt
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(1000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_webView) {
        m_webView->destroy();
        m_webView->deleteLater();
        m_webView = nullptr;
    }
    m_cdpHttpUrl.clear();
    m_cdpWsUrl.clear();
    start();
}

void MiniAppInstance::destroy()
{
    if (m_pollTimer) {
        m_pollTimer->stop();
        m_pollTimer->deleteLater();
        m_pollTimer = nullptr;
    }
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(3000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_webView) {
        m_webView->destroy();
        m_webView->deleteLater();
        m_webView = nullptr;
    }
    if (m_nam) {
        m_nam->deleteLater();
        m_nam = nullptr;
    }
    m_cdpHttpUrl.clear();
    m_cdpWsUrl.clear();
    emit finished();
}

void MiniAppInstance::setState(State s)
{
    if (m_state == s) return;
    m_state = s;

    // Clear CDP URLs on terminal/idle states per spec
    if (s == Failed || s == Crashed || s == Idle) {
        m_cdpHttpUrl.clear();
        m_cdpWsUrl.clear();
        if (m_webView)
            m_webView->hideCdpUrl();
    }

    emit stateChanged(s);
    emit titleChanged(buildTitle());
}

QString MiniAppInstance::buildTitle() const
{
    switch (m_state) {
    case Spawning:
    case Polling:
        return m_def.name + QStringLiteral(" — Starting...");
    case Initializing:
        return m_def.name + QStringLiteral(" — Loading...");
    case Failed:
        return m_def.name + QStringLiteral(" — Error");
    case Crashed:
        return m_def.name + QStringLiteral(" — Crashed");
    default:
        return m_def.name;
    }
}

void MiniAppInstance::spawnProcess()
{
    setState(Spawning);

    m_process = new QProcess(this);

#ifdef Q_OS_WIN
    const QString shell = qEnvironmentVariable("COMSPEC", QStringLiteral("cmd.exe"));
    const QStringList shellArgs = {QStringLiteral("/c"), m_def.command};
#else
    const QString shell = qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
    const QStringList shellArgs = {QStringLiteral("-c"), m_def.command};
#endif

    if (!m_def.cwd.isEmpty())
        m_process->setWorkingDirectory(m_def.cwd);

    if (!m_def.env.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        const QStringList lines = m_def.env.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
                continue;
            const int eq = trimmed.indexOf(QLatin1Char('='));
            if (eq > 0)
                env.insert(trimmed.left(eq), trimmed.mid(eq + 1));
        }
        m_process->setProcessEnvironment(env);
    }

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_process->readAllStandardOutput();
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_process->readAllStandardError();
    });
    connect(m_process, &QProcess::started, this, &MiniAppInstance::onProcessStarted);
    connect(m_process, &QProcess::errorOccurred, this, &MiniAppInstance::onProcessErrorOccurred);
    connect(m_process, &QProcess::finished, this, &MiniAppInstance::onProcessFinished);

    m_process->start(shell, shellArgs);
}

void MiniAppInstance::onProcessStarted()
{
    startHealthPolling();
}

void MiniAppInstance::onProcessErrorOccurred(QProcess::ProcessError error)
{
    Q_UNUSED(error)
    if (m_state == Spawning || m_state == Polling) {
        m_lastError = tr("Command failed to start: %1").arg(m_process->errorString());
        setState(Failed);
    }
}

void MiniAppInstance::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus)
    if (m_state == Polling || m_state == Spawning) {
        m_lastError = tr("Process exited with code %1 before health check passed").arg(exitCode);
        if (m_pollTimer) m_pollTimer->stop();
        setState(Failed);
    } else if (m_state == Running || m_state == Initializing) {
        m_lastError = tr("Process exited unexpectedly (code %1)").arg(exitCode);
        setState(Crashed);
    }
}

void MiniAppInstance::startHealthPolling()
{
    setState(Polling);
    m_pollStartTime = QDateTime::currentMSecsSinceEpoch();

    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(500);
    connect(m_pollTimer, &QTimer::timeout, this, &MiniAppInstance::onHealthPoll);
    m_pollTimer->start();

    // Fire first poll immediately
    onHealthPoll();
}

void MiniAppInstance::onHealthPoll()
{
    if (m_state != Polling) return;

    // Check timeout
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_pollStartTime;
    if (elapsed > m_def.healthTimeoutMs) {
        m_pollTimer->stop();
        m_lastError = tr("Health check timed out after %1 seconds")
                          .arg(m_def.healthTimeoutMs / 1000);
        setState(Failed);
        return;
    }

    QNetworkRequest req(QUrl(m_def.effectiveHealthUrl()));
    req.setTransferTimeout(2000);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_state != Polling) return;

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 200 && status < 400) {
            m_pollTimer->stop();
            setState(Ready);
            createWebView();
        }
        // else: keep polling on next timer tick
    });
}

void MiniAppInstance::createWebView()
{
    m_webView = WebViewWidget::create(m_def.id, QUrl(m_def.url), m_def.debugPort, nullptr, QString(),
                                      m_def.proxyType, m_def.proxyHost, m_def.proxyPort, m_def.proxyBypassList,
                                      m_allowCrossOrigin);
    if (!m_webView) {
        // Platform doesn't support embedded webview (Linux)
        setState(Running);
        return;
    }

    connect(m_webView, &WebViewWidget::navigationCompleted,
            this, &MiniAppInstance::onWebViewNavigationCompleted);
    connect(m_webView, &WebViewWidget::processFailed,
            this, &MiniAppInstance::onWebViewProcessFailed);
    connect(m_webView, &WebViewWidget::cdpReady,
            this, [this](const QString &httpUrl, const QString &wsUrl) {
                m_cdpHttpUrl = httpUrl;
                m_cdpWsUrl = wsUrl;
                emit cdpUrlChanged(httpUrl);
            });

    setState(Initializing);
}

void MiniAppInstance::onWebViewNavigationCompleted(bool success, const QString &error)
{
    if (m_state == Initializing) {
        if (success) {
            setState(Running);
        } else {
            m_lastError = error;
            setState(Failed);
        }
    }
}

void MiniAppInstance::onWebViewProcessFailed(const QString &description)
{
    m_lastError = description;
    setState(Crashed);
}
