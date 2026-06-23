#include <QtTest/QtTest>

#include <QByteArray>
#include <QCoreApplication>
#include <QEvent>
#include <QJsonDocument>
#include <QPair>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QVector>

#include "AcpConnection.h"
#include "AcpSessionModel.h"
#include "ApplicationSettings.h"
#include "GoalAgentSettings.h"
#include "GoalConversationSummary.h"
#include "GoalDraftGenerator.h"

class TestGoalDraftGenerator : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void parse_validActionBody_returnsDraft();
    void parse_completeAction_returnsNoDraft();
    void parse_malformedOrEmptyAction_rejects();
    void renderPrompt_usesFullTargetHistoryForEnhance();
    void conversationSummary_startIndexKeepsIncrementalWindow();
    void cancel_isIdempotentAndDeletesConnection();

private:
    QTemporaryDir m_settingsDir;
};

void TestGoalDraftGenerator::initTestCase()
{
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("NotepadNextTest"));
    QCoreApplication::setApplicationName(QStringLiteral("NotepadNextTest_GoalDraftGenerator"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void TestGoalDraftGenerator::init()
{
    ApplicationSettings settings;
    settings.clear();
    settings.sync();
}

void TestGoalDraftGenerator::parse_validActionBody_returnsDraft()
{
    QString draft;
    bool complete = true;
    QVERIFY(GoalDraftGenerator::parseDraftResponseForTesting(
        QStringLiteral("<action type=\"continue\">  Run the focused tests.  </action>"),
        &draft, &complete));
    QCOMPARE(draft, QStringLiteral("Run the focused tests."));
    QVERIFY(!complete);
}

void TestGoalDraftGenerator::parse_completeAction_returnsNoDraft()
{
    QString draft = QStringLiteral("unchanged");
    bool complete = false;

    QVERIFY(GoalDraftGenerator::parseDraftResponseForTesting(
        QStringLiteral("<action type=\"complete\">Already satisfied.</action>"),
        &draft, &complete));

    QVERIFY(complete);
    QVERIFY(draft.isEmpty());
}

void TestGoalDraftGenerator::parse_malformedOrEmptyAction_rejects()
{
    QString draft = QStringLiteral("unchanged");
    QVERIFY(!GoalDraftGenerator::parseDraftResponseForTesting(
        QStringLiteral("plain text without xml"), &draft));
    QVERIFY(draft.isEmpty());

    draft = QStringLiteral("unchanged");
    QVERIFY(!GoalDraftGenerator::parseDraftResponseForTesting(
        QStringLiteral("<action type=\"continue\">   </action>"), &draft));
    QVERIFY(draft.isEmpty());
}

void TestGoalDraftGenerator::renderPrompt_usesFullTargetHistoryForEnhance()
{
    GoalAgentSettings goalSettings;
    goalSettings.defaultMaxIterations = 7;
    GoalPromptTemplate tpl;
    tpl.id = QStringLiteral("test-template");
    tpl.name = QStringLiteral("Test template");
    tpl.content = QStringLiteral(
        "goal={{goal}}\n"
        "conversation={{conversation}}\n"
        "iteration={{iteration}}/{{maxIterations}}\n"
        "criterion={{criterionIndex}}/{{totalCriteria}}\n"
        "original={{originalUserMessage}}");
    goalSettings.promptTemplates.append(tpl);

    ApplicationSettings settings;
    settings.setValue(QStringLiteral("Ai/GoalAgentSettings"),
                      QString::fromUtf8(QJsonDocument(goalSettings.toJson())
                                            .toJson(QJsonDocument::Compact)));

    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("draft-history"),
                          QStringLiteral("proj"),
                          historyDir.path());
    model.appendUserMessage(QStringLiteral("Please inspect <xml>"),
                            QVector<QPair<QByteArray, QString>>{});
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("I checked A & B"));
    model.onPromptEnded();
    model.appendSystemMessage(QStringLiteral("system text ignored"));
    model.onThoughtChunk(QStringLiteral("thought text ignored"));

    GoalDraftGenerator generator(nullptr, &settings);
    GoalDraftGenerator::Request req;
    req.criteria = QStringLiteral("first criterion\nsecond criterion");
    req.promptTemplateId = tpl.id;
    req.targetModel = &model;

    const QString prompt = generator.renderPromptForTesting(req);

    QVERIFY(prompt.contains(QStringLiteral("goal=first criterion")));
    QVERIFY(prompt.contains(
        QStringLiteral("<message role=\"user\">Please inspect &lt;xml&gt;</message>")));
    QVERIFY(prompt.contains(
        QStringLiteral("<message role=\"assistant\">I checked A &amp; B</message>")));
    QVERIFY(!prompt.contains(QStringLiteral("system text ignored")));
    QVERIFY(!prompt.contains(QStringLiteral("thought text ignored")));
    QVERIFY(prompt.contains(QStringLiteral("iteration=1/7")));
    QVERIFY(prompt.contains(QStringLiteral("criterion=1/2")));
    QVERIFY(prompt.contains(QStringLiteral("original=first criterion\nsecond criterion")));
}

void TestGoalDraftGenerator::conversationSummary_startIndexKeepsIncrementalWindow()
{
    QTemporaryDir historyDir;
    QVERIFY(historyDir.isValid());
    AcpSessionModel model(QStringLiteral("incremental-history"),
                          QStringLiteral("proj"),
                          historyDir.path());
    model.appendUserMessage(QStringLiteral("old user"),
                            QVector<QPair<QByteArray, QString>>{});
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("old assistant"));
    model.onPromptEnded();

    const int lastSeen = model.messages().size();

    model.appendUserMessage(QStringLiteral("new user"),
                            QVector<QPair<QByteArray, QString>>{});
    model.onPromptStarted();
    model.onMessageChunk(QStringLiteral("new assistant"));
    model.onPromptEnded();

    const QString incremental = GoalConversationSummary::fromModel(&model, lastSeen);

    QVERIFY(!incremental.contains(QStringLiteral("old user")));
    QVERIFY(!incremental.contains(QStringLiteral("old assistant")));
    QVERIFY(incremental.contains(QStringLiteral("new user")));
    QVERIFY(incremental.contains(QStringLiteral("new assistant")));
    QCOMPARE(model.messages().size(), lastSeen + 2);
}

void TestGoalDraftGenerator::cancel_isIdempotentAndDeletesConnection()
{
    GoalDraftGenerator generator(nullptr, nullptr);
    auto *connection = new AcpConnection;
    QPointer<AcpConnection> connectionPtr(connection);

    generator.setConnectionForTesting(connection, true);
    generator.cancel();
    generator.cancel();

    QCOMPARE(generator.teardownCountForTesting(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(connectionPtr.isNull());

    generator.cancel();
    QCOMPARE(generator.teardownCountForTesting(), 1);
}

QTEST_MAIN(TestGoalDraftGenerator)

#include "test_goal_draft_generator.moc"
