#ifndef AI_ANTHROPIC_MESSAGES_CLIENT_H
#define AI_ANTHROPIC_MESSAGES_CLIENT_H

#include "IAnthropicMessagesClient.h"

#include <QPointer>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

namespace ai {

class AnthropicMessagesClient : public IAnthropicMessagesClient
{
    Q_OBJECT
public:
    explicit AnthropicMessagesClient(QObject *parent = nullptr);
    ~AnthropicMessagesClient() override;

    void post(const Request &req) override;
    void cancel() override;

private slots:
    void onFinished();
    void onIdleTimeout();

private:
    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    QTimer m_idleTimer;
    int m_idleTimeoutSec = 10 * 60;
};

} // namespace ai

#endif // AI_ANTHROPIC_MESSAGES_CLIENT_H
