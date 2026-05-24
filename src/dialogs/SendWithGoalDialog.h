#ifndef SEND_WITH_GOAL_DIALOG_H
#define SEND_WITH_GOAL_DIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QStringList>

class AcpAgentRegistry;
class ApplicationSettings;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QVBoxLayout;

struct SendWithGoalResult
{
    QStringList successCriteriaList;
    QString agentId;
    int maxIterations = 10;
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

    SendWithGoalResult result() const;

private slots:
    void onAddCriterion();
    void onRemoveCriterion();
    void onLoadPreset();
    void onSavePreset();
    void onDeletePreset();
    void onStart();

private:
    void buildUi();
    void populateAgents();
    void populateTemplates();
    void populatePresets();
    void updateRowCount();
    bool validate();
    QPlainTextEdit *createCriterionEdit(const QString &text = QString());

    AcpAgentRegistry *m_registry;
    ApplicationSettings *m_settings;

    QScrollArea *m_criteriaScroll = nullptr;
    QVBoxLayout *m_criteriaLayout = nullptr;
    QList<QPlainTextEdit *> m_criteriaEdits;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QLabel *m_rowCountLabel = nullptr;
    QComboBox *m_agentCombo = nullptr;
    QComboBox *m_templateCombo = nullptr;
    QSpinBox *m_maxIterSpin = nullptr;
    QPushButton *m_loadPresetBtn = nullptr;
    QPushButton *m_savePresetBtn = nullptr;
    QMenu *m_presetMenu = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QLabel *m_errorLabel = nullptr;
};

#endif // SEND_WITH_GOAL_DIALOG_H
