#ifndef GOAL_HTTP_JUDGE_SESSION_H
#define GOAL_HTTP_JUDGE_SESSION_H

#include "GoalActionParser.h"

#include <QObject>
#include <QString>
#include <QUrl>

class ApplicationSettings;
class GoalHttpJudgeRunner;

namespace ai {
class IAnthropicMessagesClient;
}

class GoalHttpJudgeSession : public QObject
{
    Q_OBJECT
public:
    explicit GoalHttpJudgeSession(QObject *parent = nullptr);

    void evaluate(ApplicationSettings *settings, const QString &prompt);
    void cancel();

signals:
    void verdict(GoalAction action);
    void assumedAchieved(const QString &reason);
    void failed(const QString &message);
    void busyChanged(bool busy);

private:
    void finishBusy();
    void fail(const QString &reason);

    ai::IAnthropicMessagesClient *m_client = nullptr;
    GoalHttpJudgeRunner *m_runner = nullptr;
    QUrl m_url;
    QString m_model;
    bool m_busy = false;
};

#endif // GOAL_HTTP_JUDGE_SESSION_H
