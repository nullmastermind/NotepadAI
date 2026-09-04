#ifndef SEND_WITH_GOAL_DIALOG_H
#define SEND_WITH_GOAL_DIALOG_H

#include <QDialog>
#include <QStringList>

class AcpAgentRegistry;
class ApplicationSettings;
class GoalConfigWidget;
class QLabel;
class QPushButton;
struct GoalConfigResult;

struct SendWithGoalResult
{
    QStringList successCriteriaList;
    QString agentId;
    int maxIterations = 100;
    QString promptTemplateId;
};

class SendWithGoalDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SendWithGoalDialog(AcpAgentRegistry *registry,
                                ApplicationSettings *settings,
                                QWidget *parent = nullptr);
    ~SendWithGoalDialog() override;

    // Named goalResult() rather than result() to avoid shadowing
    // QDialog::result() (which returns the int accept/reject code).
    SendWithGoalResult goalResult() const;

private slots:
    void onStart();

private:
    bool validate(const GoalConfigResult &result);

    ApplicationSettings *m_settings = nullptr;
    GoalConfigWidget *m_goalConfig = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_startBtn = nullptr;
};

#endif // SEND_WITH_GOAL_DIALOG_H
