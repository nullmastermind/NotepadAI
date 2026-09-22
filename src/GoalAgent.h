#ifndef GOAL_AGENT_H
#define GOAL_AGENT_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>

#include "GoalActionParser.h"
#include "GoalAgentSettings.h"

class AcpAgentManager;
class AcpConnection;
class AcpSessionModel;
class ApplicationSettings;
class GoalHttpJudgeSession;

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
        int maxIterations = GoalAgentSettings::kDefaultMaxIterations;
        QString promptTemplateId;
        bool autoCompact = false;
        QString originalUserMessage;
        // True when Goal is attached to a session that already has a turn
        // (user clicked Send, then Goal). Includes existing messages in the
        // first evaluation and evaluates immediately if the target is idle.
        bool attachToExistingConversation = false;
    };

    bool start(const StartRequest &req);
    void stop();
    void setTargetSession(AcpConnection *conn, AcpSessionModel *model);

    // Composer + session state → whether Goal sends a new prompt, attaches to
    // the existing turn, or refuses. Processing never sends (no stacked prompt).
    enum class LaunchAction : std::uint8_t { NeedComposer, Attach, Send };
    static LaunchAction launchAction(bool hasComposer, bool sessionHasHistory, bool processing);
    static QStringList nativeGoalCommands(const QStringList &criteria);
    static QString nativeGoalWorktreeInstruction()
    {
        return QStringLiteral(
            "Create a new git worktree for this task. When finished, merge the result "
            "into the current branch and remove the worktree to free disk space.");
    }
    // Agent echoes the wire suffix inside "Goal set:". Hide that sentence in the
    // bubble only — session/prompt still sends the full text.
    static QString nativeGoalDisplayText(const QString &text)
    {
        if (!text.trimmed().startsWith(QLatin1String("Goal set:")))
            return text;
        const QString instruction = nativeGoalWorktreeInstruction();
        auto chopTrailingLine = [](const QString &src, int lineStart) {
            QString out = src.left(lineStart);
            while (!out.isEmpty() && out.back().isSpace())
                out.chop(1);
            return out;
        };
        const int at = text.lastIndexOf(instruction);
        if (at >= 0 && text.mid(at + instruction.size()).trimmed().isEmpty()) {
            int lineStart = at;
            while (lineStart > 0 && text.at(lineStart - 1).isSpace()
                   && text.at(lineStart - 1) != QLatin1Char('\n')
                   && text.at(lineStart - 1) != QLatin1Char('\r'))
                --lineStart;
            if (lineStart > 0) {
                const QChar prev = text.at(lineStart - 1);
                if (prev == QLatin1Char('\n') || prev == QLatin1Char('\r'))
                    return chopTrailingLine(text, lineStart);
            }
        }
        // Streaming may deliver only a prefix of the injected sentence.
        const int nl = text.lastIndexOf(QLatin1Char('\n'));
        if (nl > 0) {
            const QString last = text.mid(nl + 1).remove(QLatin1Char('\r'));
            if (last.size() >= 16 && instruction.startsWith(last))
                return chopTrailingLine(text, nl);
        }
        return text;
    }
    static QString nativeGoalWireText(const QString &goalCommand, bool injectWorktree);
    static bool isNativeGoalSlash(const QString &text);
    static void sendAutoCompactTo(AcpConnection *conn, AcpSessionModel *model);

    struct RestartedSession {
        QString sessionId;
        AcpConnection *connection = nullptr;
        AcpSessionModel *model = nullptr;
    };
    void setSessionRestarter(std::function<RestartedSession(const QString &oldSessionId)> fn);

    // Optional transform applied to prompts forwarded to the target session
    // (continue / restart / handoff). Transcript stays on the display text.
    // `/compact` is never decorated — it is a system command.
    void setTargetPromptDecorator(std::function<QString(const QString &)> fn);

signals:
    void statusChanged(GoalAgent::Status status);
    void criterionAdvanced(int newIndex);
    void actionEmitted(const QString &type, const QString &text);
    void iterationChanged(int criterionIndex, int iteration);
    void debugLogEntry(const QString &entry);
    void httpJudgeBusyChanged(bool busy);

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
    void finalizeHandoff(const QString &verdict, const QString &authoredText,
                         bool authoringSucceeded);
    void markTerminal(Status s, const QString &reason);
    void maybeSendAutoCompact();
    void destroyJudgeConnection();
    void spawnJudgeForCriterion(int index);
    void evaluateViaHttp();
    void ensureHttpJudge();
    void applyJudgeAction(const GoalAction &action);
    void restartWatchedSession(const QString &prompt);
    void sendPromptToTarget(const QString &displayText);
    QString wireTextForTarget(const QString &displayText) const;
    void onHttpVerdict(const GoalAction &action);
    void onHttpAssumedAchieved(const QString &reason);
    void onHttpFailed(const QString &message);
    QString buildConversationSummary();
    QString collectRecentUserMessages(int take, int perEntryCharCap);

    friend class TestGoalAgent;

    AcpAgentManager *m_manager;
    ApplicationSettings *m_appSettings;

    Status m_status = Idle;
    QString m_targetSessionId;
    QList<Criterion> m_criteria;
    int m_currentCriterionIndex = 0;
    int m_maxIterations = GoalAgentSettings::kDefaultMaxIterations;
    QString m_agentId;
    QString m_promptTemplateId;
    bool m_autoCompact = false;
    QString m_originalUserMessage;
    QString m_lastActionText;

    QPointer<AcpConnection> m_targetConnection;
    QPointer<AcpSessionModel> m_targetModel;
    QPointer<AcpConnection> m_judgeConnection;

    GoalHttpJudgeSession *m_httpSession = nullptr;

    QString m_judgeResponseBuffer;
    bool m_awaitingJudgeResponse = false;
    bool m_correctionAttempted = false;
    int m_lastSeenTargetMessageCount = 0;
    bool m_restartingTarget = false;
    bool m_restartedSinceLastEval = false;
    std::function<RestartedSession(const QString &)> m_sessionRestarter;
    std::function<QString(const QString &)> m_targetPromptDecorator;

    bool m_awaitingAuthoring = false;
    QString m_authoringBuffer;
    QString m_authoringVerdict;
};

#endif // GOAL_AGENT_H
