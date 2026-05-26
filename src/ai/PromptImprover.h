#ifndef AI_PROMPT_IMPROVER_H
#define AI_PROMPT_IMPROVER_H

#include <QObject>
#include <QPair>
#include <QString>
#include <QList>
#include <QVector>

#include <cstdint>

#include "AcpProtocol.h"

class ApplicationSettings;

namespace ai {

class CredentialStore;
class ILlmHttpClient;

class PromptImprover : public QObject
{
    Q_OBJECT
public:
    enum class State : std::uint8_t { Idle, Streaming, Error };
    Q_ENUM(State)

    explicit PromptImprover(ApplicationSettings *settings,
                            CredentialStore *credStore,
                            QObject *parent = nullptr);
    ~PromptImprover() override;

    State state() const { return m_state; }

    bool canImprove(QString *whyNot = nullptr) const;

    void trigger(const QString &userDraft,
                 const QString &workingDirectory,
                 const QList<AcpProtocol::AcpCommandInfo> &commands,
                 const QString &chatHistory = {},
                 const QVector<QPair<QByteArray, QString>> &images = {});

    void cancel();

signals:
    void stateChanged(ai::PromptImprover::State state);
    void finished(const QString &improvedText);
    void errorOccurred(const QString &message);
    void imagesBudgeted(int sent, int total);

private slots:
    void onFirstByte();
    void onToken(const QString &chunk);
    void onStreamEnded();
    void onStreamError(int httpStatus, const QString &message);

private:
    void setState(State s);
    QString buildSystemPrompt(const QString &rules,
                              const QList<AcpProtocol::AcpCommandInfo> &commands,
                              int imageCount) const;
    QString loadRules(const QString &workingDirectory) const;
    QString serializeCommands(const QList<AcpProtocol::AcpCommandInfo> &commands) const;
    QString parseImprovedPrompt(const QString &response) const;

    ApplicationSettings *m_settings = nullptr;
    CredentialStore     *m_credStore = nullptr;
    ILlmHttpClient      *m_http = nullptr;

    State   m_state = State::Idle;
    QString m_responseBuffer;
};

} // namespace ai

#endif // AI_PROMPT_IMPROVER_H
