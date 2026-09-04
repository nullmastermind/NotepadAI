#ifndef GOAL_CONFIG_WIDGET_H
#define GOAL_CONFIG_WIDGET_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

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
class QWidget;

struct GoalConfigResult
{
    QStringList criteriaList;
    QString agentId;
    int maxIterations = 10;
    QString promptTemplateId;
};

class GoalConfigWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GoalConfigWidget(AcpAgentRegistry *registry,
                              ApplicationSettings *settings,
                              QWidget *parent = nullptr);

    GoalConfigResult result() const;
    void persistPendingCustomApi();
    QString customApiValidationError() const;
    void setJudgeLoading(bool loading);

    void setCriteria(const QStringList &criteria);
    void setAgentId(const QString &agentId);
    void setMaxIterations(int value);
    void setPromptTemplateId(const QString &id);

private slots:
    void onAddCriterion();
    void onRemoveCriterion();
    void onSavePreset();
    void onTemplateNew();
    void onTemplateRename();
    void onTemplateEdit();
    void onTemplateDelete();

private:
    void buildUi();
    void populateAgents();
    void populateTemplates();
    void populatePresets();
    void updateRowCount();
    void updateTemplateButtons();
    QPlainTextEdit *createCriterionEdit(const QString &text = QString());

    void persistCustomApiConfig();
    void persistCustomApiKey();
    void updateCustomApiVisibility();
    void updateCustomApiStatus();

    AcpAgentRegistry *m_registry;
    ApplicationSettings *m_settings;

    QScrollArea *m_criteriaScroll = nullptr;
    QVBoxLayout *m_criteriaLayout = nullptr;
    QList<QPlainTextEdit *> m_criteriaEdits;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QLabel *m_rowCountLabel = nullptr;
    QPushButton *m_loadPresetBtn = nullptr;
    QPushButton *m_savePresetBtn = nullptr;
    QMenu *m_presetMenu = nullptr;
    QComboBox *m_agentCombo = nullptr;
    QWidget *m_customApiFields = nullptr;
    QLineEdit *m_baseUrlEdit = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QLabel *m_customApiStatus = nullptr;
    bool m_keyStored = false;
    bool m_judgeLoading = false;
    QComboBox *m_templateCombo = nullptr;
    QPushButton *m_tplRenameBtn = nullptr;
    QPushButton *m_tplEditBtn = nullptr;
    QPushButton *m_tplDeleteBtn = nullptr;
    QSpinBox *m_maxIterSpin = nullptr;
};

#endif // GOAL_CONFIG_WIDGET_H
