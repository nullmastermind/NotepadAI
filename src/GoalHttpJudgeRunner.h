#ifndef GOAL_HTTP_JUDGE_RUNNER_H
#define GOAL_HTTP_JUDGE_RUNNER_H

#include "GoalActionParser.h"
#include "ai/IAnthropicMessagesClient.h"

#include <QObject>
#include <QString>
#include <QUrl>

class GoalHttpJudgeRunner : public QObject
{
    Q_OBJECT
public:
    explicit GoalHttpJudgeRunner(ai::IAnthropicMessagesClient *client,
                                 QObject *parent = nullptr);

    void evaluate(const QUrl &url,
                  const QString &apiKey,
                  const QString &model,
                  const QString &userPrompt);
    void cancel();

signals:
    void verdict(GoalAction action);
    void assumedAchieved(const QString &reason);
    void failed(const QString &message);

private slots:
    void onFinished(const QByteArray &body);
    void onError(int httpStatus, const QString &message);

private:
    void postPrompt(const QString &userPrompt);

    ai::IAnthropicMessagesClient *m_client = nullptr;
    QUrl m_url;
    QString m_apiKey;
    QString m_model;
    QString m_originalPrompt;
    bool m_busy = false;
    bool m_retried = false;
};

#endif // GOAL_HTTP_JUDGE_RUNNER_H
