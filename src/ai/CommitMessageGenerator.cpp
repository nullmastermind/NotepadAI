/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "CommitMessageGenerator.h"

#include "ApplicationSettings.h"
#include "CredentialStore.h"
#include "DiffCompressor.h"
#include "LlmHttpClient.h"
#include "PromptAssembler.h"
#include "RulesLocator.h"

#include "git/CommitComposer.h"

#include <QPlainTextEdit>
#include <QTextCursor>

namespace ai {

CommitMessageGenerator::CommitMessageGenerator(ApplicationSettings *settings,
                                               CredentialStore *credStore,
                                               QObject *parent)
    : QObject(parent),
      m_settings(settings),
      m_credStore(credStore),
      m_http(new LlmHttpClient(this))
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(0);
    m_flushTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_flushTimer, &QTimer::timeout, this, &CommitMessageGenerator::flushPending);
    m_pendingTokens.reserve(4096);

    connect(m_http, &ILlmHttpClient::firstByteReceived, this, &CommitMessageGenerator::onFirstByte);
    connect(m_http, &ILlmHttpClient::tokenReceived,     this, &CommitMessageGenerator::onToken);
    connect(m_http, &ILlmHttpClient::streamEnded,       this, &CommitMessageGenerator::onStreamEnded);
    connect(m_http, &ILlmHttpClient::errorOccurred,     this, &CommitMessageGenerator::onStreamError);
}

CommitMessageGenerator::~CommitMessageGenerator() = default;

void CommitMessageGenerator::setHttpClientForTesting(ILlmHttpClient *client)
{
    Q_ASSERT(client);
    if (m_http) {
        m_http->disconnect(this);
        m_http->deleteLater();
    }
    m_http = client;
    m_http->setParent(this);
    connect(m_http, &ILlmHttpClient::firstByteReceived, this, &CommitMessageGenerator::onFirstByte);
    connect(m_http, &ILlmHttpClient::tokenReceived,     this, &CommitMessageGenerator::onToken);
    connect(m_http, &ILlmHttpClient::streamEnded,       this, &CommitMessageGenerator::onStreamEnded);
    connect(m_http, &ILlmHttpClient::errorOccurred,     this, &CommitMessageGenerator::onStreamError);
}

bool CommitMessageGenerator::canFireGenerate(const QString &workspaceRoot,
                                             CommitComposer *composer,
                                             QString *whyNot) const
{
    auto fail = [whyNot](const QString &msg) {
        if (whyNot) *whyNot = msg;
        return false;
    };
    if (!composer)        return fail(QObject::tr("No commit message editor"));
    if (workspaceRoot.isEmpty()) return fail(QObject::tr("No workspace selected"));
    if (!m_settings)      return fail(QObject::tr("Settings unavailable"));

    if (m_state != State::Idle && m_state != State::Error) {
        return fail(QObject::tr("A generation is already in progress"));
    }
    if (m_settings->commitMessageProviderUrl().trimmed().isEmpty()) {
        return fail(QObject::tr("Configure the AI provider URL in Preferences → AI"));
    }
    if (m_settings->commitMessageModel().trimmed().isEmpty()) {
        return fail(QObject::tr("Configure the AI model in Preferences → AI"));
    }
    if (m_credStore && !m_credStore->isApiKeyAvailable()) {
        return fail(QObject::tr("Configure the AI API key in Preferences → AI"));
    }
    return true;
}

void CommitMessageGenerator::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

void CommitMessageGenerator::trigger(const QString &workspaceRoot,
                                     const QString &submoduleRoot,
                                     CommitComposer *composer,
                                     const QString &subjectHint,
                                     const QByteArray &compressedDiff)
{
    QString why;
    if (!canFireGenerate(workspaceRoot, composer, &why)) {
        emit errorOccurred(why);
        return;
    }
    Q_ASSERT(m_settings);

    QString apiKey;
    if (m_credStore) {
        QString err;
        apiKey = m_credStore->retrieveApiKey(&err);
        if (apiKey.isEmpty()) {
            emit errorOccurred(err.isEmpty() ? tr("AI API key not available") : err);
            return;
        }
    }

    const QString rules = RulesLocator::locate(submoduleRoot, workspaceRoot,
                                               m_settings->commitMessageRulesByteBudget());
    const QByteArray diff = DiffCompressor::compress(
        compressedDiff, m_settings->commitMessageDiffByteBudget());
    const QString prompt = PromptAssembler::assemble(
        m_settings->commitMessagePromptTemplate(), rules, subjectHint, diff);

    m_target = composer;
    m_currentRepoKey = workspaceRoot;
    m_hadSubjectHint = !subjectHint.trimmed().isEmpty();
    m_firstChunkSeen = false;
    m_pendingTokens.clear();

    ILlmHttpClient::Request req;
    req.url = QUrl(m_settings->commitMessageProviderUrl());
    req.model = m_settings->commitMessageModel();
    req.apiKey = apiKey;
    req.prompt = prompt;
    req.idleTimeoutSec = m_settings->commitMessageStreamIdleTimeoutSec();
    req.apiFormat = LlmHttpClient::apiFormatFromStored(m_settings->commitMessageApiFormat());

    setState(State::Authenticating);
    m_http->openStream(req);
}

void CommitMessageGenerator::cancel()
{
    if (m_state == State::Idle || m_state == State::Cancelling) return;
    setState(State::Cancelling);
    m_http->cancel();
    // Flush any buffered tokens so user sees them; then go Idle. Tokens
    // arriving after this point are ignored via state guard in appendChunk.
    flushPending();
    setState(State::Idle);
    m_target.clear();
    m_currentRepoKey.clear();
}

void CommitMessageGenerator::cancelIfTarget(CommitComposer *composer)
{
    if (m_state == State::Idle) return;
    if (m_target.data() == composer) cancel();
}

void CommitMessageGenerator::onFirstByte()
{
    if (m_state == State::Cancelling || m_state == State::Idle) return;
    setState(State::Streaming);
}

void CommitMessageGenerator::onToken(const QString &chunk)
{
    if (m_state != State::Authenticating && m_state != State::Streaming) return;
    if (m_state == State::Authenticating) setState(State::Streaming);
    appendChunk(chunk);
}

void CommitMessageGenerator::appendChunk(const QString &chunk)
{
    if (!m_firstChunkSeen) {
        m_firstChunkSeen = true;
        // Prepend a newline so generated body sits below the user's subject.
        if (m_hadSubjectHint) m_pendingTokens.append(QChar::LineFeed);
    }
    m_pendingTokens.append(chunk);
    if (!m_flushTimer.isActive()) m_flushTimer.start();
}

void CommitMessageGenerator::flushPending()
{
    if (m_pendingTokens.isEmpty()) return;
    if (!m_target || (m_state != State::Streaming
                      && m_state != State::Authenticating
                      && m_state != State::Cancelling)) {
        m_pendingTokens.clear();
        return;
    }
    QPlainTextEdit *edit = m_target->edit();
    if (!edit) {
        m_pendingTokens.clear();
        return;
    }
    QTextCursor cursor(edit->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(m_pendingTokens);
    m_pendingTokens.clear();
}

void CommitMessageGenerator::onStreamEnded()
{
    flushPending();
    setState(State::Idle);
    m_target.clear();
    m_currentRepoKey.clear();
}

void CommitMessageGenerator::onStreamError(int httpStatus, const QString &message)
{
    Q_UNUSED(httpStatus);
    flushPending();              // preserve any partial text in the composer
    setState(State::Error);
    emit errorOccurred(message);
    teardownAfterError();
}

void CommitMessageGenerator::teardownAfterError()
{
    setState(State::Idle);
    m_target.clear();
    m_currentRepoKey.clear();
}

} // namespace ai
