#ifndef GOAL_AGENT_SETTINGS_H
#define GOAL_AGENT_SETTINGS_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class ApplicationSettings;

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
    static constexpr int kDefaultMaxIterations = 100;
    static constexpr int kMaxIterationsMin = 1;
    static constexpr int kMaxIterationsMax = 1000;
    static constexpr int kMaxCriteriaRows = 50;
    static constexpr int kMaxCriterionChars = 4000;

    GoalAgentSettings();

    QString agentId;
    QString promptTemplateId;
    int defaultMaxIterations = kDefaultMaxIterations;
    bool autoCompact = false;
    bool useNativeGoal = true;
    QList<GoalPromptTemplate> promptTemplates;
    QString handoffTemplate;
    QString handoffAuthoringTemplate;
    QList<GoalCriteriaPreset> criteriaPresets;
    QString customApiBaseUrl;
    QString customApiModel;

    const GoalPromptTemplate *findTemplate(const QString &id) const;
    const GoalPromptTemplate &defaultTemplate() const;
    QString resolvedPromptTemplateId() const;

    QJsonObject toJson() const;
    static GoalAgentSettings fromJson(const QJsonObject &obj);

    // Id the picker should show. Repairs a dangling stored id to default.
    // Does not write when the key is missing, the blob is corrupt, or settings is null.
    static QString promptTemplateIdForUi(ApplicationSettings *settings);
    // Patches only promptTemplateId. No write if unchanged, empty, null, or the blob is not a JSON object.
    static bool rememberPromptTemplateId(ApplicationSettings *settings, const QString &id);

    static const QString &builtinPromptContent();
    static const QString &builtinHandoffContent();
    static const QString &builtinHandoffAuthoringContent();
};

#endif // GOAL_AGENT_SETTINGS_H
