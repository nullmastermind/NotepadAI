#ifndef GOAL_PROMPT_RENDERER_H
#define GOAL_PROMPT_RENDERER_H

#include <QString>
#include <QStringList>

class GoalPromptRenderer
{
public:
    static QString renderJudgePrompt(const QString &templateContent,
                                     const QString &goal,
                                     const QString &conversation,
                                     int iteration,
                                     int maxIterations,
                                     int criterionIndex,
                                     int totalCriteria);

    static QString renderHandoff(const QString &templateContent,
                                 const QString &verdict,
                                 const QString &nextCriterion,
                                 int criterionIndex,
                                 int totalCriteria);

    static QString renderHandoffAuthoring(const QString &templateContent,
                                          const QString &verdict,
                                          const QString &nextCriterionRaw,
                                          int criterionIndex,
                                          int totalCriteria,
                                          const QString &recentUserMessages);

    static QStringList requiredPromptPlaceholders();
    static QStringList requiredHandoffPlaceholders();
    static QStringList requiredHandoffAuthoringPlaceholders();
    static QStringList missingPlaceholders(const QString &content, const QStringList &required);
};

#endif // GOAL_PROMPT_RENDERER_H
