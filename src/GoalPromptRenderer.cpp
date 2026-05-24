#include "GoalPromptRenderer.h"

#include <QHash>
#include <QRegularExpression>

static QString singlePassReplace(const QString &templateContent,
                                 const QHash<QString, QString> &vars)
{
    static const QRegularExpression re(QStringLiteral(R"(\{\{(\w+)\}\})"));
    QString out;
    out.reserve(templateContent.size());
    qsizetype lastEnd = 0;
    auto it = re.globalMatch(templateContent);
    while (it.hasNext()) {
        const auto match = it.next();
        out.append(templateContent.mid(lastEnd, match.capturedStart() - lastEnd));
        const QString key = match.captured(1);
        auto found = vars.find(key);
        if (found != vars.end()) {
            out.append(found.value());
        } else {
            out.append(match.captured(0));
        }
        lastEnd = match.capturedEnd();
    }
    out.append(templateContent.mid(lastEnd));
    return out;
}

QString GoalPromptRenderer::renderJudgePrompt(const QString &templateContent,
                                              const QString &goal,
                                              const QString &conversation,
                                              int iteration,
                                              int maxIterations,
                                              int criterionIndex,
                                              int totalCriteria)
{
    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("goal"), goal);
    vars.insert(QStringLiteral("conversation"), conversation);
    vars.insert(QStringLiteral("iteration"), QString::number(iteration));
    vars.insert(QStringLiteral("maxIterations"), QString::number(maxIterations));
    vars.insert(QStringLiteral("criterionIndex"), QString::number(criterionIndex));
    vars.insert(QStringLiteral("totalCriteria"), QString::number(totalCriteria));
    return singlePassReplace(templateContent, vars);
}

QString GoalPromptRenderer::renderHandoff(const QString &templateContent,
                                          const QString &verdict,
                                          const QString &nextCriterion,
                                          int criterionIndex,
                                          int totalCriteria)
{
    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("verdict"), verdict);
    vars.insert(QStringLiteral("nextCriterion"), nextCriterion);
    vars.insert(QStringLiteral("criterionIndex"), QString::number(criterionIndex));
    vars.insert(QStringLiteral("totalCriteria"), QString::number(totalCriteria));
    return singlePassReplace(templateContent, vars);
}

QString GoalPromptRenderer::renderHandoffAuthoring(const QString &templateContent,
                                                   const QString &verdict,
                                                   const QString &nextCriterionRaw,
                                                   int criterionIndex,
                                                   int totalCriteria,
                                                   const QString &recentUserMessages)
{
    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("verdict"), verdict);
    vars.insert(QStringLiteral("nextCriterion"), nextCriterionRaw);
    vars.insert(QStringLiteral("criterionIndex"), QString::number(criterionIndex));
    vars.insert(QStringLiteral("totalCriteria"), QString::number(totalCriteria));
    vars.insert(QStringLiteral("recentUserMessages"), recentUserMessages);
    return singlePassReplace(templateContent, vars);
}

QStringList GoalPromptRenderer::requiredPromptPlaceholders()
{
    return {
        QStringLiteral("{{goal}}"),
        QStringLiteral("{{conversation}}"),
        QStringLiteral("{{iteration}}"),
        QStringLiteral("{{maxIterations}}"),
        QStringLiteral("{{criterionIndex}}"),
        QStringLiteral("{{totalCriteria}}"),
    };
}

QStringList GoalPromptRenderer::requiredHandoffPlaceholders()
{
    return {
        QStringLiteral("{{verdict}}"),
        QStringLiteral("{{nextCriterion}}"),
        QStringLiteral("{{criterionIndex}}"),
        QStringLiteral("{{totalCriteria}}"),
    };
}

QStringList GoalPromptRenderer::requiredHandoffAuthoringPlaceholders()
{
    return {
        QStringLiteral("{{verdict}}"),
        QStringLiteral("{{nextCriterion}}"),
        QStringLiteral("{{criterionIndex}}"),
        QStringLiteral("{{totalCriteria}}"),
        QStringLiteral("{{recentUserMessages}}"),
    };
}

QStringList GoalPromptRenderer::missingPlaceholders(const QString &content, const QStringList &required)
{
    QStringList missing;
    for (const auto &ph : required) {
        if (!content.contains(ph))
            missing.append(ph);
    }
    return missing;
}
