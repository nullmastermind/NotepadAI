#include "PromptImprover.h"

#include "ApplicationSettings.h"
#include "CredentialStore.h"
#include "LlmHttpClient.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace ai {

namespace {

const QString &kSystemTemplate()
{
    static const QString tpl = QStringLiteral(
    "You are a prompt improvement assistant for a coding agent chat interface.\n"
    "\n"
    "Your task: rewrite the user's draft prompt to be clearer, more specific, "
    "and more actionable for the target coding agent — while preserving the "
    "user's original intent exactly.\n"
    "\n"
    "Rules:\n"
    "- If the prompt starts with a slash command (e.g. /chore, /feat, /fix, "
    "/refactor), you MUST keep that command at the beginning. Never remove, "
    "rename, or reorder it.\n"
    "- Always output the improved prompt in English, regardless of the input "
    "language.\n"
    "- Do not add information the user did not mention. You may restructure, "
    "clarify ambiguity, add specificity where the intent is obvious, and "
    "improve phrasing.\n"
    "- Do not add greetings, sign-offs, or meta-commentary.\n"
    "- Output ONLY the improved prompt wrapped in "
    "<improved_prompt></improved_prompt> tags. No explanation, no preamble, "
    "no alternatives.\n");
    return tpl;
}

} // namespace

PromptImprover::PromptImprover(ApplicationSettings *settings,
                               CredentialStore *credStore,
                               QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_credStore(credStore)
    , m_http(new LlmHttpClient(this))
{
    connect(m_http, &ILlmHttpClient::firstByteReceived, this, &PromptImprover::onFirstByte);
    connect(m_http, &ILlmHttpClient::tokenReceived,     this, &PromptImprover::onToken);
    connect(m_http, &ILlmHttpClient::streamEnded,       this, &PromptImprover::onStreamEnded);
    connect(m_http, &ILlmHttpClient::errorOccurred,     this, &PromptImprover::onStreamError);
}

PromptImprover::~PromptImprover() { cancel(); }

void PromptImprover::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

bool PromptImprover::canImprove(QString *whyNot) const
{
    auto fail = [whyNot](const QString &msg) {
        if (whyNot) *whyNot = msg;
        return false;
    };
    if (!m_settings) return fail(tr("Settings unavailable"));
    if (m_state == State::Streaming)
        return fail(tr("Improvement already in progress"));
    if (m_settings->commitMessageProviderUrl().trimmed().isEmpty())
        return fail(tr("AI provider not configured. Set the endpoint in Preferences → AI Provider."));
    if (m_settings->commitMessageModel().trimmed().isEmpty())
        return fail(tr("AI model not configured. Set the model in Preferences → AI Provider."));
    if (m_credStore && !m_credStore->isApiKeyAvailable())
        return fail(tr("AI API key not configured. Save your key in Preferences → AI Provider."));
    return true;
}

void PromptImprover::trigger(const QString &userDraft,
                             const QString &workingDirectory,
                             const QList<AcpProtocol::AcpCommandInfo> &commands,
                             const QString &chatHistory,
                             const QVector<QPair<QByteArray, QString>> &images)
{
    QString why;
    if (!canImprove(&why)) {
        emit errorOccurred(why);
        return;
    }

    QString apiKey;
    if (m_credStore) {
        QString err;
        apiKey = m_credStore->retrieveApiKey(&err);
        if (apiKey.isEmpty()) {
            emit errorOccurred(err.isEmpty() ? tr("AI API key not available") : err);
            return;
        }
    }

    const QString rules = loadRules(workingDirectory);

    constexpr qint64 kImageBudget = 10LL * 1024 * 1024;
    QVector<QPair<QByteArray, QString>> budgetedImages;
    qint64 totalBytes = 0;
    for (const auto &img : images) {
        if (totalBytes + img.first.size() > kImageBudget) break;
        budgetedImages.append(img);
        totalBytes += img.first.size();
    }
    if (budgetedImages.size() < images.size()) {
        emit imagesBudgeted(budgetedImages.size(), images.size());
    }

    const QString systemPrompt = buildSystemPrompt(rules, commands, budgetedImages.size());

    m_responseBuffer.clear();

    QString userMessage;
    if (!chatHistory.isEmpty()) {
        userMessage += QStringLiteral("<chat_history>\n%1\n</chat_history>\n\n").arg(chatHistory);
    }
    userMessage += QStringLiteral("<user_draft>\n%1\n</user_draft>").arg(userDraft);

    ILlmHttpClient::Request req;
    req.url = QUrl(m_settings->commitMessageProviderUrl());
    req.model = m_settings->commitMessageModel();
    req.apiKey = apiKey;
    req.systemPrompt = systemPrompt;
    req.prompt = userMessage;
    req.maxTokens = 4096;
    req.idleTimeoutSec = m_settings->commitMessageStreamIdleTimeoutSec();
    req.images = budgetedImages;

    setState(State::Streaming);
    m_http->openStream(req);
}

void PromptImprover::cancel()
{
    if (m_state != State::Streaming) return;
    m_http->cancel();
    m_responseBuffer.clear();
    setState(State::Idle);
}

void PromptImprover::onFirstByte()
{
    // Nothing special — streaming state already set in trigger().
}

void PromptImprover::onToken(const QString &chunk)
{
    if (m_state != State::Streaming) return;
    m_responseBuffer.append(chunk);
}

void PromptImprover::onStreamEnded()
{
    if (m_state != State::Streaming) return;

    const QString improved = parseImprovedPrompt(m_responseBuffer);
    m_responseBuffer.clear();

    if (improved.isEmpty()) {
        setState(State::Error);
        emit errorOccurred(tr("Could not parse AI response"));
        setState(State::Idle);
        return;
    }

    setState(State::Idle);
    emit finished(improved);
}

void PromptImprover::onStreamError(int httpStatus, const QString &message)
{
    Q_UNUSED(httpStatus);
    m_responseBuffer.clear();
    setState(State::Error);
    emit errorOccurred(message);
    setState(State::Idle);
}

QString PromptImprover::buildSystemPrompt(
    const QString &rules,
    const QList<AcpProtocol::AcpCommandInfo> &commands,
    int imageCount) const
{
    QString prompt = kSystemTemplate();

    if (imageCount > 0) {
        prompt += QStringLiteral(
            "- The user has attached %1 image(s) alongside their draft. Examine the "
            "visual content and incorporate what you see into the improved prompt — "
            "reference specific UI elements, layouts, errors, or details visible in "
            "the images to make the prompt more concrete. Do not narrate what you see "
            "in the images; weave their context into the rewritten prompt naturally.\n")
            .arg(imageCount);
    }

    if (!rules.isEmpty()) {
        prompt += QStringLiteral("\n<project_rules>\n%1\n</project_rules>\n").arg(rules);
    }

    const QString json = serializeCommands(commands);
    if (!json.isEmpty()) {
        prompt += QStringLiteral("\n<available_commands>\n%1\n</available_commands>\n").arg(json);
    }

    return prompt;
}

QString PromptImprover::loadRules(const QString &workingDirectory) const
{
    if (workingDirectory.isEmpty()) return {};

    QDir dir(workingDirectory);
    const QString claudePath = dir.filePath(QStringLiteral("CLAUDE.md"));
    const QString agentsPath = dir.filePath(QStringLiteral("AGENTS.md"));

    auto readFile = [](const QString &path) -> QByteArray {
        QFile f(path);
        if (!f.exists() || !f.open(QIODevice::ReadOnly)) return {};
        return f.readAll();
    };

    const QByteArray claudeContent = readFile(claudePath);
    const QByteArray agentsContent = readFile(agentsPath);

    if (claudeContent.isEmpty() && agentsContent.isEmpty())
        return {};

    if (claudeContent.isEmpty())
        return QString::fromUtf8(agentsContent).trimmed();

    if (agentsContent.isEmpty())
        return QString::fromUtf8(claudeContent).trimmed();

    // De-dup: if identical content, send only one copy.
    if (claudeContent == agentsContent)
        return QString::fromUtf8(claudeContent).trimmed();

    return QString::fromUtf8(claudeContent).trimmed()
           + QStringLiteral("\n\n")
           + QString::fromUtf8(agentsContent).trimmed();
}

QString PromptImprover::serializeCommands(
    const QList<AcpProtocol::AcpCommandInfo> &commands) const
{
    if (commands.isEmpty()) return {};

    QJsonArray arr;
    for (const auto &cmd : commands) {
        QJsonObject obj;
        obj.insert(QLatin1String("name"), cmd.name);
        if (!cmd.description.isEmpty())
            obj.insert(QLatin1String("description"), cmd.description);
        if (!cmd.inputHint.isEmpty())
            obj.insert(QLatin1String("inputHint"), cmd.inputHint);
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QString PromptImprover::parseImprovedPrompt(const QString &response) const
{
    static const QRegularExpression re(
        QStringLiteral("<improved_prompt>([\\s\\S]*?)</improved_prompt>"));
    const auto match = re.match(response);
    if (!match.hasMatch()) return {};
    const QString content = match.captured(1).trimmed();
    return content.isEmpty() ? QString() : content;
}

} // namespace ai
