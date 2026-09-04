#include "AnthropicMessagesClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QtGlobal>

namespace ai {

AnthropicMessagesClient::AnthropicMessagesClient(QObject *parent)
    : IAnthropicMessagesClient(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_idleTimer.setSingleShot(true);
    connect(&m_idleTimer, &QTimer::timeout, this, &AnthropicMessagesClient::onIdleTimeout);
}

AnthropicMessagesClient::~AnthropicMessagesClient()
{
    cancel();
}

void AnthropicMessagesClient::post(const Request &req)
{
    cancel();

    QNetworkRequest httpReq(req.url);
    httpReq.setHeader(QNetworkRequest::ContentTypeHeader, QLatin1String("application/json"));
    httpReq.setRawHeader("x-api-key", req.apiKey.toUtf8());
    httpReq.setRawHeader("anthropic-version", "2023-06-01");
    httpReq.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    m_reply = m_nam->post(httpReq, req.body);
    connect(m_reply.data(), &QNetworkReply::finished, this, &AnthropicMessagesClient::onFinished);

    m_idleTimer.start(qMax(5, m_idleTimeoutSec) * 1000);
}

void AnthropicMessagesClient::cancel()
{
    m_idleTimer.stop();
    if (m_reply) {
        QNetworkReply *r = m_reply.data();
        m_reply.clear();
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
}

void AnthropicMessagesClient::onFinished()
{
    m_idleTimer.stop();
    if (!m_reply)
        return;

    QNetworkReply *r = m_reply.data();
    m_reply.clear();

    const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = r->readAll();
    Q_UNUSED(body)
    const QNetworkReply::NetworkError netErr = r->error();
    const QString errStr = r->errorString();
    r->deleteLater();

    if (status >= 400 || netErr != QNetworkReply::NoError) {
        QString msg;
        if (status == 401 || status == 403)
            msg = QStringLiteral("Authentication failed. Check the API key.");
        else if (status == 0)
            msg = errStr.isEmpty() ? QStringLiteral("Network error") : errStr;
        else
            msg = QStringLiteral("HTTP %1").arg(status);
        emit errorOccurred(status, msg);
        qWarning("notepadai.goal.http: %s", qUtf8Printable(msg));
        return;
    }
    emit finished(body);
}

void AnthropicMessagesClient::onIdleTimeout()
{
    qWarning("notepadai.goal.http: Idle timeout");
    emit errorOccurred(0, QStringLiteral("Idle timeout"));
    cancel();
}

} // namespace ai
