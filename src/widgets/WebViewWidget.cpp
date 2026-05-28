/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "WebViewWidget.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPalette>
#include <QPushButton>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>

namespace {

QIcon tintedSvgIcon(const QString &svgPath, const QColor &color)
{
    QIcon source(svgPath);
    if (source.isNull()) return source;
    QIcon dst;
    for (int sz : {14, 16, 20, 22, 24, 28, 32, 48}) {
        QPixmap pm = source.pixmap(sz, sz);
        if (pm.isNull()) continue;
        QPainter p(&pm);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(pm.rect(), color);
        p.end();
        dst.addPixmap(pm);
    }
    return dst;
}

} // namespace

void WebViewWidget::copilotLog(const QString &msg)
{
    if (!m_copilotLogDlg) {
        m_copilotLogDlg = new QDialog(this);
        m_copilotLogDlg->setWindowTitle(QStringLiteral("AI Copilot Log"));
        m_copilotLogDlg->resize(600, 400);
        auto *layout = new QVBoxLayout(m_copilotLogDlg);
        m_copilotLogText = new QPlainTextEdit(m_copilotLogDlg);
        m_copilotLogText->setReadOnly(true);
        m_copilotLogText->setFont(QFont(QStringLiteral("Consolas"), 9));
        layout->addWidget(m_copilotLogText);
    }
    m_copilotLogText->appendPlainText(msg);
}

WebViewWidget::WebViewWidget(const QString &appId, const QUrl &url, QWidget *parent)
    : QWidget(parent)
    , m_appId(appId)
    , m_url(url)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    setupToolbar();

    // Cross-page retry: when page navigates during execution, re-send command on new page.
    // Uses a single-shot member timer so rapid successive navigations only produce ONE retry.
    m_copilotRetryTimer = new QTimer(this);
    m_copilotRetryTimer->setSingleShot(true);
    m_copilotRetryTimer->setInterval(1500);

    connect(m_copilotRetryTimer, &QTimer::timeout, this, [this]() {
        if (!m_copilotExecuting || m_copilotLastCmd.isEmpty()) return;
        m_copilotExecuting = false;
        executeCopilotCommand(m_copilotLastCmd, m_copilotProviderUrl, m_copilotModel, m_copilotApiKey);
    });

    connect(this, &WebViewWidget::navigationCompleted, this, [this](bool success, const QString &) {
        if (!m_copilotExecuting || !success) return;
        if (m_copilotLastCmd.isEmpty()) return;

        ++m_copilotNavRetries;
        if (m_copilotNavRetries > kMaxNavRetries) {
            copilotLog(QStringLiteral("[navigation] max retries (%1) reached").arg(kMaxNavRetries));
            m_copilotExecuting = false;
            m_copilotNavRetries = 0;
            m_copilotRetryTimer->stop();
            if (m_aiStopBtn) m_aiStopBtn->hide();
            showCopilotResultDialog(false, tr("Stopped after %1 page navigations").arg(kMaxNavRetries));
            return;
        }

        copilotLog(QStringLiteral("[navigation] page navigated, re-sending command (retry %1/%2)")
                       .arg(m_copilotNavRetries).arg(kMaxNavRetries));

        m_copilotRetryTimer->start();
    });
}

void WebViewWidget::rebuildToolbarIcons()
{
    const QColor color = palette().color(QPalette::WindowText);
    if (m_backBtn)
        m_backBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/arrow-left.svg"), color));
    if (m_forwardBtn)
        m_forwardBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/arrow-right.svg"), color));
    if (m_reloadBtn)
        m_reloadBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/refresh-cw.svg"), color));
    if (m_goBtn)
        m_goBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/send.svg"), color));
    if (m_stopBtn)
        m_stopBtn->setIcon(tintedSvgIcon(QStringLiteral(":/icons/circle-stop.svg"), color));
}

void WebViewWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::ApplicationPaletteChange:
        rebuildToolbarIcons();
        break;
    default:
        break;
    }
}

void WebViewWidget::setupToolbar()
{
    auto *toolbarWidget = new QWidget(this);
    toolbarWidget->setFixedHeight(24);
    m_toolbarLayout = new QHBoxLayout(toolbarWidget);
    m_toolbarLayout->setContentsMargins(4, 0, 4, 0);
    m_toolbarLayout->setSpacing(4);

    m_backBtn = new QToolButton(toolbarWidget);
    m_backBtn->setAutoRaise(true);
    m_backBtn->setToolTip(tr("Back"));
    m_backBtn->setIconSize(QSize(14, 14));
    connect(m_backBtn, &QToolButton::clicked, this, &WebViewWidget::goBack);
    m_toolbarLayout->addWidget(m_backBtn);

    m_forwardBtn = new QToolButton(toolbarWidget);
    m_forwardBtn->setAutoRaise(true);
    m_forwardBtn->setToolTip(tr("Forward"));
    m_forwardBtn->setIconSize(QSize(14, 14));
    connect(m_forwardBtn, &QToolButton::clicked, this, &WebViewWidget::goForward);
    m_toolbarLayout->addWidget(m_forwardBtn);

    m_reloadBtn = new QToolButton(toolbarWidget);
    m_reloadBtn->setAutoRaise(true);
    m_reloadBtn->setToolTip(tr("Reload"));
    m_reloadBtn->setIconSize(QSize(14, 14));
    connect(m_reloadBtn, &QToolButton::clicked, this, &WebViewWidget::reload);
    m_toolbarLayout->addWidget(m_reloadBtn);

    // AI button — right after reload, with blink animation
    m_aiBtn = new QToolButton(toolbarWidget);
    m_aiBtn->setAutoRaise(true);
    m_aiBtn->setText(QStringLiteral("AI"));
    m_aiBtn->setToolTip(tr("AI Copilot — control this page with natural language"));
    QFont aiFont = m_aiBtn->font();
    aiFont.setBold(true);
    m_aiBtn->setFont(aiFont);
    connect(m_aiBtn, &QToolButton::clicked, this, &WebViewWidget::showCopilotInputDialog);
    m_toolbarLayout->addWidget(m_aiBtn);

    // Stop AI button — red label, hidden by default, shown during execution
    m_aiStopBtn = new QToolButton(toolbarWidget);
    m_aiStopBtn->setAutoRaise(true);
    m_aiStopBtn->setText(QStringLiteral("Stop AI"));
    m_aiStopBtn->setToolTip(tr("Stop the running AI Copilot"));
    m_aiStopBtn->setStyleSheet(QStringLiteral("QToolButton { color: #e03030; font-weight: bold; }"));
    m_aiStopBtn->hide();
    connect(m_aiStopBtn, &QToolButton::clicked, this, [this]() {
        if (!m_copilotExecuting) return;
        copilotLog(QStringLiteral("[stop] user requested stop"));
        executeScript(QStringLiteral("if(window.__nai_pa)window.__nai_pa.stop()"), nullptr);
        m_copilotExecuting = false;
        m_copilotNavRetries = 0;
        m_copilotLastCmd.clear();
        m_copilotRetryTimer->stop();
        m_aiStopBtn->hide();
        if (m_aiBtn) m_aiBtn->setStyleSheet(QString());
        if (m_aiBlinkTimer) {
            m_aiBlinkTimer->start();
            QTimer::singleShot(5000, this, [this]() {
                if (m_aiBlinkTimer) m_aiBlinkTimer->stop();
                if (m_aiBtn) m_aiBtn->setStyleSheet(QString());
            });
        }
        emit copilotResult(false, tr("Stopped by user"));
    });
    m_toolbarLayout->addWidget(m_aiStopBtn);

    // Blink animation for AI button — short attention pulse, not permanent
    m_aiBlinkTimer = new QTimer(this);
    m_aiBlinkTimer->setInterval(400);
    bool *blinkState = new bool(false);
    connect(m_aiBlinkTimer, &QTimer::timeout, this, [this, blinkState]() {
        *blinkState = !*blinkState;
        if (*blinkState)
            m_aiBtn->setStyleSheet(QStringLiteral("QToolButton { color: palette(highlight); font-weight: bold; }"));
        else
            m_aiBtn->setStyleSheet(QString());
    });
    connect(m_aiBtn, &QObject::destroyed, this, [blinkState]() { delete blinkState; });
    m_aiBlinkTimer->start();
    QTimer::singleShot(5000, this, [this]() {
        if (m_aiBlinkTimer) m_aiBlinkTimer->stop();
        if (m_aiBtn) m_aiBtn->setStyleSheet(QString());
    });

    m_urlEdit = new QLineEdit(toolbarWidget);
    m_urlEdit->setText(m_url.toString());
    QFont editFont = m_urlEdit->font();
    editFont.setPointSize(editFont.pointSize() - 1);
    m_urlEdit->setFont(editFont);
    m_urlEdit->setMinimumWidth(0);
    m_urlEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_urlEdit->setPlaceholderText(tr("Enter URL and press Enter"));
    auto navigateFromBar = [this]() {
        QString text = m_urlEdit->text().trimmed();
        if (text.isEmpty()) return;
        if (!text.contains(QStringLiteral("://")) && !text.startsWith(QStringLiteral("//")))
            text.prepend(QStringLiteral("https://"));
        navigate(QUrl(text));
    };
    connect(m_urlEdit, &QLineEdit::returnPressed, this, navigateFromBar);
    m_toolbarLayout->addWidget(m_urlEdit, 1);

    m_goBtn = new QToolButton(toolbarWidget);
    m_goBtn->setAutoRaise(true);
    m_goBtn->setToolTip(tr("Go"));
    m_goBtn->setIconSize(QSize(14, 14));
    connect(m_goBtn, &QToolButton::clicked, this, navigateFromBar);
    m_toolbarLayout->addWidget(m_goBtn);

    m_stopBtn = new QToolButton(toolbarWidget);
    m_stopBtn->setAutoRaise(true);
    m_stopBtn->setToolTip(tr("Stop"));
    m_stopBtn->setIconSize(QSize(14, 14));
    m_stopBtn->hide();
    connect(m_stopBtn, &QToolButton::clicked, this, &WebViewWidget::stop);
    m_toolbarLayout->addWidget(m_stopBtn);

    m_cdpBtn = new QToolButton(toolbarWidget);
    m_cdpBtn->setAutoRaise(true);
    m_cdpBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_cdpBtn->setToolTip(tr("Click to copy CDP URL to clipboard"));
    QFont cdpFont = m_cdpBtn->font();
    cdpFont.setPointSize(cdpFont.pointSize() - 1);
    m_cdpBtn->setFont(cdpFont);
    m_cdpBtn->hide();
    connect(m_cdpBtn, &QToolButton::clicked, this, [this]() {
        if (m_cdpHttpUrl.isEmpty()) return;
        QApplication::clipboard()->setText(m_cdpHttpUrl);
        const QString original = m_cdpBtn->text();
        m_cdpBtn->setText(tr("Copied!"));
        QTimer::singleShot(1500, this, [this, original]() {
            if (m_cdpBtn)
                m_cdpBtn->setText(original);
        });
    });
    m_toolbarLayout->addWidget(m_cdpBtn);

    m_mainLayout->addWidget(toolbarWidget);
    rebuildToolbarIcons();
}

void WebViewWidget::setLoading(bool loading)
{
    if (m_stopBtn)
        m_stopBtn->setVisible(loading);
    emit loadingStateChanged(loading);
}

void WebViewWidget::showCdpUrl(const QString &httpUrl)
{
    m_cdpHttpUrl = httpUrl;
    QUrl u(httpUrl);
    m_cdpDisplayText = QStringLiteral("CDP: %1:%2").arg(u.host()).arg(u.port());
    if (m_cdpBtn) {
        m_cdpBtn->setText(m_cdpDisplayText);
        m_cdpBtn->show();
    }
}

void WebViewWidget::hideCdpUrl()
{
    m_cdpHttpUrl.clear();
    m_cdpDisplayText.clear();
    if (m_cdpBtn)
        m_cdpBtn->hide();
}

void WebViewWidget::updateUrlBar(const QString &url)
{
    if (m_urlEdit && !m_urlEdit->hasFocus())
        m_urlEdit->setText(url);
    emit urlChanged(url);
}

void WebViewWidget::showCopilotInputDialog()
{
    if (m_copilotExecuting) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("AI Copilot"));
    dlg.resize(450, 160);
    auto *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *label = new QLabel(tr("Describe what you want to do on this page:"), &dlg);
    layout->addWidget(label);

    auto *input = new QPlainTextEdit(&dlg);
    input->setPlaceholderText(tr("e.g. \"click Login button\"\n\"fill email with test@abc.com, then click Submit\""));
    input->setMaximumHeight(80);
    layout->addWidget(input);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Send"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    input->setFocus();
    if (dlg.exec() != QDialog::Accepted) return;

    const QString cmd = input->toPlainText().trimmed();
    if (cmd.isEmpty()) return;

    m_copilotNavRetries = 0;
    copilotLog(QStringLiteral("[showCopilotInputDialog] cmd='%1'").arg(cmd));
    emit copilotCommandRequested(cmd);
}

void WebViewWidget::showCopilotResultDialog(bool success, const QString &data)
{
    QDialog dlg(this);
    dlg.setWindowTitle(success ? tr("AI Copilot — Done") : tr("AI Copilot — Error"));
    dlg.resize(500, 350);
    auto *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *browser = new QTextBrowser(&dlg);
    browser->setOpenExternalLinks(true);
    if (success)
        browser->setMarkdown(data);
    else
        browser->setPlainText(data);
    layout->addWidget(browser);

    auto *nextLabel = new QLabel(tr("Next action (leave empty to close):"), &dlg);
    layout->addWidget(nextLabel);

    auto *nextInput = new QPlainTextEdit(&dlg);
    nextInput->setPlaceholderText(tr("e.g. \"now click Submit\", \"fill the password field\""));
    nextInput->setMaximumHeight(60);
    layout->addWidget(nextInput);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Send"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Close"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    nextInput->setFocus();
    if (dlg.exec() != QDialog::Accepted) return;

    const QString nextCmd = nextInput->toPlainText().trimmed();
    if (nextCmd.isEmpty()) return;

    // Prepend previous result as context for the next command
    const QString fullCmd = QStringLiteral("Previous result: %1\n\nUser's next request: %2").arg(data, nextCmd);

    m_copilotNavRetries = 0;
    copilotLog(QStringLiteral("[showCopilotResultDialog] next cmd='%1'").arg(nextCmd));
    emit copilotCommandRequested(fullCmd);
}

void WebViewWidget::executeCopilotCommand(const QString &command, const QString &providerUrl,
                                          const QString &model, const QString &apiKey)
{
    if (m_copilotExecuting) return;
    m_copilotExecuting = true;
    if (m_aiStopBtn) m_aiStopBtn->show();

    ensureCspBypassed();

    // Stop blink while executing
    if (m_aiBlinkTimer) m_aiBlinkTimer->stop();
    if (m_aiBtn) m_aiBtn->setStyleSheet(QStringLiteral("QToolButton { color: palette(highlight); font-weight: bold; }"));

    m_copilotLastCmd = command;
    m_copilotProviderUrl = providerUrl;
    m_copilotModel = model;
    m_copilotApiKey = apiKey;

    copilotLog(QStringLiteral("[executeCopilotCommand] command='%1' model='%2' baseURL='%3' retry=%4")
                   .arg(command, model, providerUrl).arg(m_copilotNavRetries));

    static QString bundleCache;
    if (bundleCache.isEmpty()) {
        QFile f(QStringLiteral(":/scripts/page-agent-1.8.2.js"));
        if (f.open(QIODevice::ReadOnly))
            bundleCache = QString::fromUtf8(f.readAll());
        copilotLog(QStringLiteral("[executeCopilotCommand] bundle loaded, size=%1").arg(bundleCache.size()));
    }

    const QString postMsg = nativePostMessage();
    const QString escapedCmd = QString(command).replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
                                   .replace(QLatin1Char('\''), QStringLiteral("\\'"))
                                   .replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    const QString escapedUrl = QString(providerUrl).replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
                                   .replace(QLatin1Char('\''), QStringLiteral("\\'"));
    const QString escapedModel = QString(model).replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
                                     .replace(QLatin1Char('\''), QStringLiteral("\\'"));
    const QString escapedKey = QString(apiKey).replace(QLatin1Char('\\'), QStringLiteral("\\\\"))
                                   .replace(QLatin1Char('\''), QStringLiteral("\\'"));

    QString js;
    js += QStringLiteral("(async function() {\n"
                         "  var POST_MSG = ");
    js += postMsg;
    js += QStringLiteral(";\n"
                         "  if (!window.__nai_fetch_cbs) window.__nai_fetch_cbs = {};\n"
                         "  var nativeFetch = function(url, opts) {\n"
                         "    opts = opts || {};\n"
                         "    var id = Math.random().toString(36).slice(2) + Date.now().toString(36);\n"
                         "    var headers = {};\n"
                         "    if (opts.headers) {\n"
                         "      if (opts.headers instanceof Headers) {\n"
                         "        opts.headers.forEach(function(v,k){ headers[k]=v; });\n"
                         "      } else if (typeof opts.headers === 'object') {\n"
                         "        headers = opts.headers;\n"
                         "      }\n"
                         "    }\n"
                         "    var body = '';\n"
                         "    if (opts.body) body = typeof opts.body === 'string' ? opts.body : JSON.stringify(opts.body);\n"
                         "    return new Promise(function(resolve, reject) {\n"
                         "      window.__nai_fetch_cbs[id] = function(status, text) {\n"
                         "        resolve({ ok: status>=200&&status<300, status:status, statusText:'',\n"
                         "          text: function(){return Promise.resolve(text);},\n"
                         "          json: function(){return Promise.resolve(JSON.parse(text));},\n"
                         "          headers: new Headers() });\n"
                         "      };\n"
                         "      POST_MSG(JSON.stringify({type:'nai-fetch',id:id,url:url,method:opts.method||'GET',headers:headers,body:body}));\n"
                         "      setTimeout(function(){ if(window.__nai_fetch_cbs[id]){delete window.__nai_fetch_cbs[id]; reject(new Error('Native fetch timeout'));} }, 300000);\n"
                         "    });\n"
                         "  };\n"
                         "  try {\n"
                         "    if (!window.__pa_injected) {\n"
                         "      (function() {\n"
                         "        if (!window.trustedTypes) return;\n"
                         "        if (!window.__nai_tt_policy) {\n"
                         "          var policy;\n"
                         "          try { policy = window.trustedTypes.createPolicy('default', {\n"
                         "            createHTML: function(s){return s;}, createScript: function(s){return s;}, createScriptURL: function(s){return s;}\n"
                         "          }); } catch(e) {}\n"
                         "          if (!policy) try { policy = window.trustedTypes.createPolicy('nai-page-agent', {\n"
                         "            createHTML: function(s){return s;}, createScript: function(s){return s;}, createScriptURL: function(s){return s;}\n"
                         "          }); } catch(e) {}\n"
                         "          if (policy) window.__nai_tt_policy = policy;\n"
                         "        }\n"
                         "        if (!window.__nai_tt_policy) return;\n"
                         "        function patchSetter(proto, prop) {\n"
                         "          var desc = Object.getOwnPropertyDescriptor(proto, prop);\n"
                         "          if (!desc || !desc.set) return;\n"
                         "          var origSet = desc.set;\n"
                         "          Object.defineProperty(proto, prop, {\n"
                         "            set: function(v) {\n"
                         "              origSet.call(this, window.__nai_tt_policy.createHTML(String(v)));\n"
                         "            },\n"
                         "            get: desc.get, configurable: true, enumerable: true\n"
                         "          });\n"
                         "        }\n"
                         "        patchSetter(Element.prototype, 'innerHTML');\n"
                         "        if (Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'innerHTML'))\n"
                         "          patchSetter(HTMLElement.prototype, 'innerHTML');\n"
                         "      })();\n"
                         "      // Fake currentScript to disable autoInit in the bundle\n"
                         "      var _fakeScript = {src:'blob:nai?autoInit=false'};\n"
                         "      Object.defineProperty(document, 'currentScript', {value:_fakeScript, configurable:true});\n");
    js += bundleCache;
    js += QStringLiteral("\n      Object.defineProperty(document, 'currentScript', {value:null, configurable:true});\n"
                         "      window.__pa_injected = true;\n"
                         "    }\n"
                         "    var _paBaseURL = '");
    js += escapedUrl;
    js += QStringLiteral("';\n"
                         "    var _origFetch = window.fetch;\n"
                         "    window.fetch = new Proxy(_origFetch, {\n"
                         "      apply: function(target, thisArg, args) {\n"
                         "        var url = typeof args[0] === 'string' ? args[0] : (args[0] && args[0].url ? args[0].url : '');\n"
                         "        if (url && url.indexOf(_paBaseURL) === 0) {\n"
                         "          var opts = args[1] || {};\n"
                         "          if (typeof args[0] !== 'string' && args[0]) {\n"
                         "            opts.method = opts.method || args[0].method;\n"
                         "            opts.headers = opts.headers || args[0].headers;\n"
                         "            opts.body = opts.body !== undefined ? opts.body : args[0].body;\n"
                         "          }\n"
                         "          return nativeFetch(url, opts);\n"
                         "        }\n"
                         "        return Reflect.apply(target, thisArg, args);\n"
                         "      }\n"
                         "    });\n"
                         "    try {\n"
                         "      var pa = new window.PageAgent({\n"
                         "        model: '");
    js += escapedModel;
    js += QStringLiteral("',\n        baseURL: '");
    js += escapedUrl;
    js += QStringLiteral("',\n        apiKey: '");
    js += escapedKey;
    js += QStringLiteral("',\n        language: 'en-US',\n"
                         "        maxSteps: 15\n"
                         "      });\n"
                         "      if (pa.panel) { pa.panel.dispose(); pa.panel = null; }\n"
                         "      window.__nai_pa = pa;\n"
                         "      var _paObserver = new MutationObserver(function(muts){\n"
                         "        for(var m of muts) for(var n of m.addedNodes){\n"
                         "          if(n.nodeType===1 && n.style && parseInt(n.style.zIndex)>2147483600) n.remove();\n"
                         "        }\n"
                         "      });\n"
                         "      _paObserver.observe(document.body, {childList:true});\n"
                         "      var result;\n"
                         "      try {\n"
                         "        result = await Promise.race([\n"
                         "          pa.execute('");
    js += escapedCmd;
    js += QStringLiteral("'),\n"
                         "          new Promise(function(_, rej) { setTimeout(function() { rej(new Error('Timeout: 10min exceeded')); }, 600000); })\n"
                         "        ]);\n"
                         "      } finally { _paObserver.disconnect(); }\n"
                         "      POST_MSG(JSON.stringify({type:'pa-result',success:result.success,data:result.data}));\n"
                         "    } catch(e) {\n"
                         "      POST_MSG(JSON.stringify({type:'pa-result',success:false,data:e.message||String(e)}));\n"
                         "    } finally {\n"
                         "      window.fetch = _origFetch;\n"
                         "    }\n"
                         "  } catch(e) {\n"
                         "    POST_MSG(JSON.stringify({type:'pa-result',success:false,data:e.message||String(e)}));\n"
                         "  }\n"
                         "})();\n");

    copilotLog(QStringLiteral("[executeCopilotCommand] JS size=%1, calling executeScript...").arg(js.size()));
    executeScript(js, nullptr);
}

void WebViewWidget::handleCopilotMessage(const QString &json)
{
    copilotLog(QStringLiteral("[handleCopilotMessage] received: %1").arg(json.left(200)));
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("nai-fetch")) {
        handleNativeFetch(json);
        return;
    }
    if (type != QStringLiteral("pa-result")) return;

    if (!m_copilotExecuting) return;

    m_copilotExecuting = false;
    m_copilotNavRetries = 0;
    m_copilotLastCmd.clear();
    m_copilotRetryTimer->stop();
    if (m_aiStopBtn) m_aiStopBtn->hide();

    // Restore blink briefly
    if (m_aiBtn) m_aiBtn->setStyleSheet(QString());
    if (m_aiBlinkTimer) {
        m_aiBlinkTimer->start();
        QTimer::singleShot(5000, this, [this]() {
            if (m_aiBlinkTimer) m_aiBlinkTimer->stop();
            if (m_aiBtn) m_aiBtn->setStyleSheet(QString());
        });
    }

    const bool success = obj.value(QStringLiteral("success")).toBool();
    const QString data = obj.value(QStringLiteral("data")).toString();

    showCopilotResultDialog(success, data);
    emit copilotResult(success, data);
}

void WebViewWidget::handleNativeFetch(const QString &json)
{
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    QJsonObject obj = doc.object();
    const QString id = obj.value(QStringLiteral("id")).toString();
    const QString url = obj.value(QStringLiteral("url")).toString();
    const QString method = obj.value(QStringLiteral("method")).toString(QStringLiteral("GET"));
    const QJsonObject headers = obj.value(QStringLiteral("headers")).toObject();
    const QString body = obj.value(QStringLiteral("body")).toString();

    copilotLog(QStringLiteral("[nativeFetch] id=%1 %2 %3").arg(id, method, url));

    if (!m_fetchNam)
        m_fetchNam = new QNetworkAccessManager(this);

    QNetworkRequest req{QUrl(url)};
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        req.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
    }
    req.setTransferTimeout(300000);

    QNetworkReply *reply = nullptr;
    if (method == QStringLiteral("POST"))
        reply = m_fetchNam->post(req, body.toUtf8());
    else if (method == QStringLiteral("PUT"))
        reply = m_fetchNam->put(req, body.toUtf8());
    else if (method == QStringLiteral("DELETE"))
        reply = m_fetchNam->deleteResource(req);
    else
        reply = m_fetchNam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray responseBody = reply->readAll();
        reply->deleteLater();

        copilotLog(QStringLiteral("[nativeFetch] id=%1 done, status=%2 bodyLen=%3")
                       .arg(id).arg(status).arg(responseBody.size()));

        // Use Uint8Array + TextDecoder for proper UTF-8 handling
        const QString b64 = QString::fromLatin1(responseBody.toBase64());
        const QString js = QStringLiteral(
            "if(window.__nai_fetch_cbs && window.__nai_fetch_cbs['%1']){"
            "  var _b=atob('%3');"
            "  var _u=new Uint8Array(_b.length);"
            "  for(var _i=0;_i<_b.length;_i++) _u[_i]=_b.charCodeAt(_i);"
            "  var _t=new TextDecoder('utf-8').decode(_u);"
            "  window.__nai_fetch_cbs['%1'](%2, _t);"
            "  delete window.__nai_fetch_cbs['%1'];"
            "}").arg(id).arg(status).arg(b64);
        executeScript(js, nullptr);
    });
}

// Factory implementation — returns nullptr on unsupported platforms.
// Platform-specific create() is defined in WebViewWidget_win.cpp / _mac.mm.
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
WebViewWidget *WebViewWidget::create(const QString & /*appId*/, const QUrl & /*url*/, int /*debugPort*/,
                                     QWidget * /*parent*/, const QString & /*userDataFolder*/,
                                     int /*proxyType*/, const QString & /*proxyHost*/,
                                     int /*proxyPort*/, const QString & /*proxyBypassList*/,
                                     bool /*allowCrossOrigin*/)
{
    return nullptr; // Linux: no embedded webview, use xdg-open
}
#endif
