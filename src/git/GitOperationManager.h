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

#ifndef GIT_OPERATION_MANAGER_H
#define GIT_OPERATION_MANAGER_H

#include "ConflictEntry.h"

#include <QObject>
#include <QString>
#include <QHash>

#include <cstdint>
#include <functional>
#include <memory>

class GitController;
class QTcpServer;
class QTcpSocket;

class GitOperationManager : public QObject
{
    Q_OBJECT
public:
    enum class OperationState : std::uint8_t {
        Idle,
        MergeRunning,
        MergeConflicted,
        RebaseRunning,
        RebaseSuspended,
        RebaseSuspendedEdit
    };
    Q_ENUM(OperationState)

    explicit GitOperationManager(QObject *parent = nullptr);
    ~GitOperationManager() override;

    OperationState state(const QString &repoPath) const;
    ConflictEntries conflicts(const QString &repoPath) const;

    void registerController(GitController *controller);
    void unregisterController(GitController *controller);

    void detectInProgressOperations(const QString &repoPath);

    QString editorHelperPath() const;
    int editorServerPort() const;
    QString editorToken() const;

    // Answers git's sequence / message editor without the dialog (the Control
    // API's git.rebaseInteractive): while set, an editor request calls it with
    // the file to edit instead of emitting interactiveRebaseRequested /
    // commitMessageEditRequested, and git gets its result (true: saved).
    using EditorScript = std::function<bool(const QString &filePath, bool isTodoFile)>;
    void setEditorScript(EditorScript script) { m_editorScript = std::move(script); }
    bool hasEditorScript() const { return bool(m_editorScript); }

public slots:
    void startMerge(GitController *controller, const QString &branch,
                    const QStringList &strategyOptions = {});
    void abortMerge(GitController *controller);
    void commitMerge(GitController *controller);

    void startRebase(GitController *controller, const QString &ontoBranch);
    void startInteractiveRebase(GitController *controller, const QString &ontoBranch);
    void continueRebase(GitController *controller);
    void skipRebase(GitController *controller);
    void abortRebase(GitController *controller);

    void resolveFile(GitController *controller, const QString &relPath);
    void acceptOurs(GitController *controller, const QStringList &relPaths);
    void acceptTheirs(GitController *controller, const QStringList &relPaths);

    void replyToEditor(bool accepted);

signals:
    void mergeStarted(const QString &repoPath);
    void mergeConflicted(const QString &repoPath, const ConflictEntries &entries);
    void mergeCompleted(const QString &repoPath);
    void mergeFailed(const QString &repoPath, const QString &error);

    void rebaseStarted(const QString &repoPath);
    void rebaseProgress(const QString &repoPath, int current, int total);
    void rebaseConflicted(const QString &repoPath, const ConflictEntries &entries);
    void rebaseSuspended(const QString &repoPath, const QString &reason);
    void rebaseCompleted(const QString &repoPath);
    void rebaseFailed(const QString &repoPath, const QString &error);
    void rebaseAborted(const QString &repoPath);

    void interactiveRebaseRequested(const QString &todoFilePath);
    void commitMessageEditRequested(const QString &msgFilePath);

    void operationStateChanged(const QString &repoPath, OperationState newState);
    void conflictsUpdated(const QString &repoPath, const ConflictEntries &entries);

private slots:
    void onEditorConnection();
    void onEditorDataReady();
    void onControllerStatusUpdated();

private:
    struct RepoState {
        OperationState opState = OperationState::Idle;
        ConflictEntries conflicts;
        bool stashedBeforeRebase = false;
        QString mergeSourceBranch;
        QString rebaseOntoBranch;
    };

    void setState(const QString &repoPath, OperationState s);
    void refreshConflicts(GitController *controller);
    ConflictEntries parseUnmergedOutput(const QByteArray &output) const;
    bool ensureEditorServer();
    void handleEditorFile(QTcpSocket *conn, const QString &filePath);

    QHash<QString, RepoState> m_repoStates;
    QHash<QString, GitController *> m_controllers;

    QTcpServer *m_editorServer = nullptr;
    QTcpSocket *m_pendingEditorConn = nullptr;
    EditorScript m_editorScript;
    QString m_editorToken;
    int m_editorPort = 0;
};

#endif // GIT_OPERATION_MANAGER_H
