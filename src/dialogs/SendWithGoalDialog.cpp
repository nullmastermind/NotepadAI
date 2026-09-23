#include "SendWithGoalDialog.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ApplicationSettings.h"
#include "GoalAgentSettings.h"
#include "GoalConfigWidget.h"

SendWithGoalDialog::SendWithGoalDialog(AcpAgentRegistry *registry,
                                       ApplicationSettings *settings,
                                       QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("Send with Goal"));
    setMinimumWidth(440);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    m_goalConfig = new GoalConfigWidget(registry, settings, this);
    m_goalConfig->setRememberPromptTemplate(true);
    mainLayout->addWidget(m_goalConfig);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(QStringLiteral("color: red; font-size: 12px;"));
    m_errorLabel->hide();
    mainLayout->addWidget(m_errorLabel);

    auto *footerLayout = new QHBoxLayout;
    m_autoCompactCheck = new QCheckBox(tr("Auto compact"), this);
    m_autoCompactCheck->setObjectName(QStringLiteral("autoCompactCheck"));
    m_autoCompactCheck->setToolTip(
        tr("After the goal is achieved, send /compact to the target agent."));
    if (m_settings) {
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            const GoalAgentSettings goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
            m_autoCompactCheck->setChecked(goalSettings.autoCompact);
        }
    }
    connect(m_autoCompactCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_settings)
            return;
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        GoalAgentSettings goalSettings;
        if (!settingsJson.isEmpty()) {
            goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
        }
        if (goalSettings.autoCompact == checked)
            return;
        goalSettings.autoCompact = checked;
        m_settings->setValue(
            QStringLiteral("Ai/GoalAgentSettings"),
            QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    });
    footerLayout->addWidget(m_autoCompactCheck);
    m_useNativeGoalCheck = new QCheckBox(tr("Use Native Goal"), this);
    m_useNativeGoalCheck->setObjectName(QStringLiteral("useNativeGoalCheck"));
    m_useNativeGoalCheck->setToolTip(
        tr("Send /goal to the ACP agent after your prompt. No goal-agent UI."));
    m_useNativeGoalCheck->setChecked(true);
    if (m_settings) {
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            const GoalAgentSettings goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
            m_useNativeGoalCheck->setChecked(goalSettings.useNativeGoal);
        }
    }
    connect(m_useNativeGoalCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_settings)
            return;
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        GoalAgentSettings goalSettings;
        if (!settingsJson.isEmpty()) {
            goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
        }
        if (goalSettings.useNativeGoal == checked)
            return;
        goalSettings.useNativeGoal = checked;
        m_settings->setValue(
            QStringLiteral("Ai/GoalAgentSettings"),
            QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    });
    footerLayout->addWidget(m_useNativeGoalCheck);
    m_prefixGoalCheck = new QCheckBox(tr("Prefix /goal"), this);
    m_prefixGoalCheck->setObjectName(QStringLiteral("prefixGoalCheck"));
    m_prefixGoalCheck->setToolTip(
        tr("Prepend /goal to messages the goal-agent sends to the target."));
    if (m_settings) {
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        if (!settingsJson.isEmpty()) {
            const GoalAgentSettings goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
            m_prefixGoalCheck->setChecked(goalSettings.prefixGoal);
        }
    }
    connect(m_prefixGoalCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (!m_settings)
            return;
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        GoalAgentSettings goalSettings;
        if (!settingsJson.isEmpty()) {
            goalSettings = GoalAgentSettings::fromJson(
                QJsonDocument::fromJson(settingsJson.toUtf8()).object());
        }
        if (goalSettings.prefixGoal == checked)
            return;
        goalSettings.prefixGoal = checked;
        m_settings->setValue(
            QStringLiteral("Ai/GoalAgentSettings"),
            QString::fromUtf8(QJsonDocument(goalSettings.toJson()).toJson(QJsonDocument::Compact)));
    });
    footerLayout->addWidget(m_prefixGoalCheck);
    footerLayout->addStretch();
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    footerLayout->addWidget(cancelBtn);

    m_startBtn = new QPushButton(tr("Start Goal"), this);
    m_startBtn->setDefault(true);
    m_startBtn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    connect(m_startBtn, &QPushButton::clicked, this, &SendWithGoalDialog::onStart);
    footerLayout->addWidget(m_startBtn);
    mainLayout->addLayout(footerLayout);
}

SendWithGoalDialog::~SendWithGoalDialog() = default;

bool SendWithGoalDialog::validate(const GoalConfigResult &result)
{
    if (result.criteriaList.isEmpty()) {
        m_errorLabel->setText(tr("At least one criterion is required."));
        m_errorLabel->show();
        return false;
    }
    if (result.agentId.isEmpty()) {
        m_errorLabel->setText(tr("Select a goal-agent."));
        m_errorLabel->show();
        return false;
    }
    const QString customErr = m_goalConfig->customApiValidationError();
    if (!customErr.isEmpty()) {
        m_errorLabel->setText(customErr);
        m_errorLabel->show();
        return false;
    }
    m_errorLabel->hide();
    return true;
}

void SendWithGoalDialog::onStart()
{
    m_goalConfig->persistPendingCustomApi();
    const GoalConfigResult result = m_goalConfig->result();
    if (!validate(result))
        return;

    if (m_settings) {
        const QString settingsJson = m_settings->get("Ai/GoalAgentSettings", QString());
        const QJsonDocument doc = QJsonDocument::fromJson(settingsJson.toUtf8());
        const bool readable = settingsJson.isEmpty() || doc.isObject();
        if (readable) {
            QJsonObject settingsObject = doc.object();
            if (settingsObject.value(QStringLiteral("agentId")).toString() != result.agentId) {
                settingsObject.insert(QStringLiteral("agentId"), result.agentId);
                m_settings->setValue(
                    QStringLiteral("Ai/GoalAgentSettings"),
                    QString::fromUtf8(QJsonDocument(settingsObject).toJson(QJsonDocument::Compact)));
            }
        }
    }

    GoalAgentSettings::rememberPromptTemplateId(m_settings, result.promptTemplateId);

    accept();
}

SendWithGoalResult SendWithGoalDialog::goalResult() const
{
    const GoalConfigResult gcr = m_goalConfig->result();
    SendWithGoalResult r;
    r.successCriteriaList = gcr.criteriaList;
    r.agentId = gcr.agentId;
    r.maxIterations = gcr.maxIterations;
    r.promptTemplateId = gcr.promptTemplateId;
    r.autoCompact = m_autoCompactCheck && m_autoCompactCheck->isChecked();
    r.useNativeGoal = !m_useNativeGoalCheck || m_useNativeGoalCheck->isChecked();
    r.prefixGoal = m_prefixGoalCheck && m_prefixGoalCheck->isChecked();
    return r;
}
