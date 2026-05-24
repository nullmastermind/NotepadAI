#ifndef GOAL_AGENT_SETTINGS_H
#define GOAL_AGENT_SETTINGS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

struct GoalPromptTemplate
{
    QString id;
    QString name;
    QString content;

    QJsonObject toJson() const;
    static GoalPromptTemplate fromJson(const QJsonObject &obj);
};

struct GoalCriteriaPreset
{
    QString id;
    QString name;
    QStringList criteria;

    QJsonObject toJson() const;
    static GoalCriteriaPreset fromJson(const QJsonObject &obj);
};

class GoalAgentSettings
{
public:
    static constexpr const char *kDefaultTemplateId = "default";
    static constexpr int kDefaultMaxIterations = 10;
    static constexpr int kMaxIterationsMin = 1;
    static constexpr int kMaxIterationsMax = 1000;
    static constexpr int kMaxCriteriaRows = 50;
    static constexpr int kMaxCriterionChars = 4000;

    GoalAgentSettings();

    QString agentId;
    int defaultMaxIterations = kDefaultMaxIterations;
    QList<GoalPromptTemplate> promptTemplates;
    QString handoffTemplate;
    QString handoffAuthoringTemplate;
    QList<GoalCriteriaPreset> criteriaPresets;

    const GoalPromptTemplate *findTemplate(const QString &id) const;
    const GoalPromptTemplate &defaultTemplate() const;

    QJsonObject toJson() const;
    static GoalAgentSettings fromJson(const QJsonObject &obj);

    static const QString &builtinPromptContent();
    static const QString &builtinHandoffContent();
    static const QString &builtinHandoffAuthoringContent();
};

#endif // GOAL_AGENT_SETTINGS_H
