#ifndef GOAL_AGENT_H
#define GOAL_AGENT_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <cstdint>

#include "GoalAgentSettings.h"

class AcpAgentManager;
class AcpConnection;
class AcpSessionModel;
class ApplicationSettings;

class GoalAgent : public QObject
{
    Q_OBJECT

public:
    enum Status { Idle, Active, Achieved, Cancelled, Failed }; // NOLINT(performance-enum-size) Q_ENUM requires int
    Q_ENUM(Status)

    enum CriterionStatus : std::uint8_t { Pending, CriterionActive, Archived };

    struct Criterion {
        QString text;
        CriterionStatus status = Pending;
        QString verdict;
        int iteration = 0;
    };

    explicit GoalAgent(AcpAgentManager *manager,
                       ApplicationSettings *settings,
                       QObject *parent = nullptr);
    ~GoalAgent() override;

    Status status() const { return m_status; }
    QString targetSessionId() const { return m_targetSessionId; }
    int currentCriterionIndex() const { return m_currentCriterionIndex; }
    const QList<Criterion> &criteria() const { return m_criteria; }
    int maxIterations() const { return m_maxIterations; }
    QString lastActionText() const { return m_lastActionText; }

    struct StartRequest {
        QString targetSessionId;
        QStringList successCriteriaList;
        QString agentId;
        int maxIterations = 10;
        QString promptTemplateId;
    };

    bool start(const StartRequest &req);
    void stop();
    void setTargetSession(AcpConnection *conn, AcpSessionModel *model);

signals:
    void statusChanged(GoalAgent::Status status);
    void criterionAdvanced(int newIndex);
    void actionEmitted(const QString &type, const QString &text);
    void iterationChanged(int criterionIndex, int iteration);
    void debugLogEntry(const QString &entry);

private slots:
    void onTargetPromptEnded();
    void onTargetDestroyed();
    void onJudgeMessageChunk(const QString &chunk);
    void onJudgePromptEnded();
    void onJudgeExited(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void setStatus(Status s);
    void logDebug(const QString &msg);
    void evaluateCurrentCriterion();
    void processJudgeResponse();
    void advanceToNextCriterion(const QString &verdict);
    void beginAuthoringStep(const QString &verdict);
    void onAuthoringChunk(const QString &chunk);
    void onAuthoringPromptEnded();
    void finalizeHandoff(const QString &verdict, const QString &authoredText);
    void markTerminal(Status s, const QString &reason);
    void destroyJudgeConnection();
    void spawnJudgeForCriterion(int index);
    QString buildConversationSummary();
    QString collectRecentUserMessages(int take, int perEntryCharCap);

    AcpAgentManager *m_manager;
    ApplicationSettings *m_appSettings;

    Status m_status = Idle;
    QString m_targetSessionId;
    QList<Criterion> m_criteria;
    int m_currentCriterionIndex = 0;
    int m_maxIterations = 10;
    QString m_agentId;
    QString m_promptTemplateId;
    QString m_lastActionText;

    QPointer<AcpConnection> m_targetConnection;
    QPointer<AcpSessionModel> m_targetModel;
    QPointer<AcpConnection> m_judgeConnection;

    QString m_judgeResponseBuffer;
    bool m_awaitingJudgeResponse = false;
    bool m_correctionAttempted = false;
    int m_lastSeenTargetMessageCount = 0;

    bool m_awaitingAuthoring = false;
    QString m_authoringBuffer;
    QString m_authoringVerdict;
};

#endif // GOAL_AGENT_H
