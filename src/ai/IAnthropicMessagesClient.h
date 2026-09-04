#ifndef AI_I_ANTHROPIC_MESSAGES_CLIENT_H
#define AI_I_ANTHROPIC_MESSAGES_CLIENT_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

namespace ai {

class IAnthropicMessagesClient : public QObject
{
    Q_OBJECT
public:
    struct Request {
        QUrl url;
        QString apiKey;
        QByteArray body;
    };

    explicit IAnthropicMessagesClient(QObject *parent = nullptr) : QObject(parent) {}
    ~IAnthropicMessagesClient() override = default;

    virtual void post(const Request &req) = 0;
    virtual void cancel() = 0;

signals:
    void finished(const QByteArray &body);
    void errorOccurred(int httpStatus, const QString &message);
};

} // namespace ai

#endif // AI_I_ANTHROPIC_MESSAGES_CLIENT_H
