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

#ifndef GOAL_DRAFT_GENERATOR_H
#define GOAL_DRAFT_GENERATOR_H

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>

class AcpAgentManager;
class AcpConnection;
class AcpSessionModel;
class ApplicationSettings;

namespace remote { class ExecutionContext; }

class GoalDraftGenerator : public QObject
{
    Q_OBJECT

public:
    struct Request {
        QString criteria;
        QString agentId;
        QString promptTemplateId;
        QString workingDirectory;
        AcpSessionModel *targetModel = nullptr;
        remote::ExecutionContext *executionContext = nullptr;
    };

    explicit GoalDraftGenerator(AcpAgentManager *manager,
                                ApplicationSettings *settings,
                                QObject *parent = nullptr);
    ~GoalDraftGenerator() override;

    bool start(const Request &request);
    void cancel();
    bool isRunning() const { return m_running; }

    static bool parseDraftResponseForTesting(const QString &response, QString *draft,
                                             bool *complete = nullptr);
    QString renderPromptForTesting(const Request &request) const;
    void setConnectionForTesting(AcpConnection *connection, bool running);
    int teardownCountForTesting() const { return m_teardownCount; }

signals:
    void finished(const QString &draft);
    void errorOccurred(const QString &message);
    void debugLogEntry(const QString &entry);

private slots:
    void onMessageChunk(const QString &chunk);
    void onPromptEnded();
    void onAgentExited(int exitCode, QProcess::ExitStatus exitStatus);

private:
    static bool parseDraftResponse(const QString &response, QString *draft,
                                   bool *complete = nullptr);

    QString renderPrompt(const Request &request) const;
    void finishWithError(const QString &message);
    void finishAndTeardown();

    AcpAgentManager *m_manager = nullptr;
    ApplicationSettings *m_settings = nullptr;
    QPointer<AcpConnection> m_connection;
    QString m_responseBuffer;
    bool m_running = false;
    bool m_teardownDone = true;
    int m_teardownCount = 0;
};

#endif // GOAL_DRAFT_GENERATOR_H
