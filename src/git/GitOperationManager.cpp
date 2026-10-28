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

#include "GitOperationManager.h"
#include "GitController.h"
#include "GitProcessRunner.h"
#include "GitRunnerFactory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>

GitOperationManager::GitOperationManager(QObject *parent)
    : QObject(parent)
    , m_editorToken(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

GitOperationManager::~GitOperationManager()
{
    delete m_editorServer;
}

GitOperationManager::OperationState GitOperationManager::state(const QString &repoPath) const
{
    auto it = m_repoStates.constFind(repoPath);
    return it != m_repoStates.constEnd() ? it->opState : OperationState::Idle;
}

ConflictEntries GitOperationManager::conflicts(const QString &repoPath) const
{
    auto it = m_repoStates.constFind(repoPath);
    return it != m_repoStates.constEnd() ? it->conflicts : ConflictEntries{};
}

void GitOperationManager::registerController(GitController *controller)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) {
        connect(controller, &GitController::statusUpdated, this, [this, controller]() {
            const QString r = controller->currentRepo();
            if (!r.isEmpty() && !m_controllers.contains(r)) {
                m_controllers.insert(r, controller);
                detectInProgressOperations(r);
            }
        }, Qt::SingleShotConnection);
        return;
    }
    m_controllers.insert(repo, controller);
    // Every refresh: resolving markers in a conflicted file leaves its status unchanged.
    connect(controller, &GitController::statusRefreshed,
            this, &GitOperationManager::onControllerStatusUpdated);
    detectInProgressOperations(repo);
}

void GitOperationManager::unregisterController(GitController *controller)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (!repo.isEmpty())
        m_controllers.remove(repo);
    disconnect(controller, nullptr, this, nullptr);
}

void GitOperationManager::detectInProgressOperations(const QString &repoPath)
{
    QString gitDir = repoPath + QStringLiteral("/.git");
    if (!QDir(gitDir).exists()) {
        QFile gitFile(repoPath + QStringLiteral("/.git"));
        if (gitFile.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(gitFile.readAll()).trimmed();
            if (content.startsWith(QStringLiteral("gitdir: ")))
                gitDir = QDir::cleanPath(repoPath + QLatin1Char('/') + content.mid(8));
        }
    }

    bool hasMerge = QFile::exists(gitDir + QStringLiteral("/MERGE_HEAD"));
    bool hasRebaseMerge = QDir(gitDir + QStringLiteral("/rebase-merge")).exists();
    bool hasRebaseApply = QDir(gitDir + QStringLiteral("/rebase-apply")).exists();

    if (hasMerge) {
        setState(repoPath, OperationState::MergeConflicted);
        if (auto *ctrl = m_controllers.value(repoPath))
            refreshConflicts(ctrl);
    } else if (hasRebaseMerge || hasRebaseApply) {
        setState(repoPath, OperationState::RebaseSuspended);
        if (auto *ctrl = m_controllers.value(repoPath))
            refreshConflicts(ctrl);
    }
}

QString GitOperationManager::editorHelperPath() const
{
    QString path = QCoreApplication::applicationDirPath()
                   + QStringLiteral("/notepadai-editor");
#ifdef Q_OS_WIN
    path += QStringLiteral(".exe");
#endif
    return path;
}

int GitOperationManager::editorServerPort() const
{
    return m_editorPort;
}

QString GitOperationManager::editorToken() const
{
    return m_editorToken;
}

bool GitOperationManager::ensureEditorServer()
{
    if (m_editorServer && m_editorServer->isListening())
        return true;

    if (!m_editorServer) {
        m_editorServer = new QTcpServer(this);
        connect(m_editorServer, &QTcpServer::newConnection,
                this, &GitOperationManager::onEditorConnection);
    }

    if (!m_editorServer->listen(QHostAddress::LocalHost, 0))
        return false;

    m_editorPort = m_editorServer->serverPort();
    return true;
}

void GitOperationManager::startMerge(GitController *controller, const QString &branch,
                                     const QStringList &strategyOptions)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;

    auto &rs = m_repoStates[repo];
    if (rs.opState != OperationState::Idle) return;

    rs.mergeSourceBranch = branch;
    setState(repo, OperationState::MergeRunning);
    emit mergeStarted(repo);

    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("merge"), branch};
    for (const QString &opt : strategyOptions)
        argv.append(opt);

    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 120000, false,
        [this, repo, runner](int exit, const QByteArray &out, const QByteArray &err) {
            runner->asQObject()->deleteLater();
            if (exit == 0) {
                setState(repo, OperationState::Idle);
                m_repoStates[repo].mergeSourceBranch.clear();
                emit mergeCompleted(repo);
            } else {
                QString combined = QString::fromUtf8(out) + QString::fromUtf8(err);
                if (combined.contains(QStringLiteral("Merge conflict")) ||
                    combined.contains(QStringLiteral("CONFLICT"))) {
                    setState(repo, OperationState::MergeConflicted);
                    if (auto *ctrl = m_controllers.value(repo))
                        refreshConflicts(ctrl);
                } else {
                    setState(repo, OperationState::Idle);
                    m_repoStates[repo].mergeSourceBranch.clear();
                    emit mergeFailed(repo, combined.trimmed());
                }
            }
            if (auto *ctrl = m_controllers.value(repo))
                ctrl->refresh();
        });
}

void GitOperationManager::abortMerge(GitController *controller)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;

    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("merge"), QStringLiteral("--abort")};
    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 30000, false,
        [this, repo, runner](int exit, const QByteArray &, const QByteArray &err) {
            runner->asQObject()->deleteLater();
            if (exit == 0) {
                m_repoStates[repo].conflicts.clear();
                m_repoStates[repo].mergeSourceBranch.clear();
                setState(repo, OperationState::Idle);
            } else {
                emit mergeFailed(repo, QString::fromUtf8(err).trimmed());
            }
            if (auto *ctrl = m_controllers.value(repo))
                ctrl->refresh();
        });
}

void GitOperationManager::commitMerge(GitController *controller)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;

    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("commit"), QStringLiteral("--no-edit")};
    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 30000, false,
        [this, repo, runner](int exit, const QByteArray &, const QByteArray &err) {
            runner->asQObject()->deleteLater();
            if (exit == 0) {
                m_repoStates[repo].conflicts.clear();
                m_repoStates[repo].mergeSourceBranch.clear();
                setState(repo, OperationState::Idle);
                emit mergeCompleted(repo);
            } else {
                emit mergeFailed(repo, QString::fromUtf8(err).trimmed());
            }
            if (auto *ctrl = m_controllers.value(repo))
                ctrl->refresh();
        });
}

void GitOperationManager::startRebase(GitController *controller, const QString &ontoBranch)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;

    auto &rs = m_repoStates[repo];
    if (rs.opState != OperationState::Idle) return;

    rs.rebaseOntoBranch = ontoBranch;
    setState(repo, OperationState::RebaseRunning);
    emit rebaseStarted(repo);

    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("rebase"), ontoBranch};
    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 300000, true,
        [this, repo, runner](int exit, const QByteArray &out, const QByteArray &err) {
            runner->asQObject()->deleteLater();
            QString combined = QString::fromUtf8(out) + QString::fromUtf8(err);
            if (exit == 0) {
                m_repoStates[repo].rebaseOntoBranch.clear();
                setState(repo, OperationState::Idle);
                emit rebaseCompleted(repo);
            } else if (combined.contains(QStringLiteral("CONFLICT")) ||
                       combined.contains(QStringLiteral("could not apply"))) {
                setState(repo, OperationState::RebaseSuspended);
                if (auto *ctrl = m_controllers.value(repo))
                    refreshConflicts(ctrl);
            } else {
                setState(repo, OperationState::Idle);
                m_repoStates[repo].rebaseOntoBranch.clear();
                emit rebaseFailed(repo, combined.trimmed());
            }
            if (auto *ctrl = m_controllers.value(repo))
                ctrl->refresh();
        });
}

void GitOperationManager::startInteractiveRebase(GitController *controller, const QString &ontoBranch)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;
    if (!ensureEditorServer()) {
        emit rebaseFailed(repo, tr("Cannot start local editor server."));
        return;
    }

    auto &rs = m_repoStates[repo];
    if (rs.opState != OperationState::Idle) return;

    rs.rebaseOntoBranch = ontoBranch;
    setState(repo, OperationState::RebaseRunning);
    emit rebaseStarted(repo);

    QString helper = editorHelperPath();
    QString editorCmd = helper + QStringLiteral(" --port=")
                        + QString::number(m_editorPort)
                        + QStringLiteral(" --token=") + m_editorToken;

    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("rebase"),
                     QStringLiteral("-i"), ontoBranch};

    // Interactive rebase is LOCAL-ONLY (D7): the notepadai-editor helper set as
    // GIT_SEQUENCE_EDITOR must IPC back to this local MainWindow, which a remote
    // host has no channel to. The action is gated off for remote workspaces
    // (GitTabWidget + MainWindow guard), so this path is only ever reached for a
    // local repo — use the concrete local runner directly, never the factory.
    auto *runner = new GitProcessRunner(this);
    auto env = GitProcessRunner::baseEnv();
    env.insert(QStringLiteral("GIT_SEQUENCE_EDITOR"), editorCmd);
    env.insert(QStringLiteral("GIT_EDITOR"), editorCmd);

    runner->run(repo, argv, {}, 600000, true,
        [this, repo, runner](int exit, const QByteArray &out, const QByteArray &err) {
            runner->asQObject()->deleteLater();
            QString combined = QString::fromUtf8(out) + QString::fromUtf8(err);
            if (exit == 0) {
                m_repoStates[repo].rebaseOntoBranch.clear();
                setState(repo, OperationState::Idle);
                emit rebaseCompleted(repo);
            } else if (combined.contains(QStringLiteral("CONFLICT")) ||
                       combined.contains(QStringLiteral("could not apply"))) {
                setState(repo, OperationState::RebaseSuspended);
                if (auto *ctrl = m_controllers.value(repo))
                    refreshConflicts(ctrl);
            } else if (combined.contains(QStringLiteral("You can amend the commit now"))) {
                setState(repo, OperationState::RebaseSuspendedEdit);
                emit rebaseSuspended(repo, tr("Stopped for editing"));
            } else {
                setState(repo, OperationState::Idle);
                m_repoStates[repo].rebaseOntoBranch.clear();
                emit rebaseFailed(repo, combined.trimmed());
            }
            if (auto *ctrl = m_controllers.value(repo))
                ctrl->refresh();
        });
}

void GitOperationManager::continueRebase(GitController *controller)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;

    setState(repo, OperationState::RebaseRunning);
    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("rebase"), QStringLiteral("--continue")};
    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 300000, true,
        [this, repo, runner, controller](int exit, const QByteArray &out, const QByteArray &err) {
            runner->asQObject()->deleteLater();
            QString combined = QString::fromUtf8(out) + QString::fromUtf8(err);
            if (exit == 0) {
                m_repoStates[repo].conflicts.clear();
                m_repoStates[repo].rebaseOntoBranch.clear();
                setState(repo, OperationState::Idle);
                emit rebaseCompleted(repo);
            } else if (combined.contains(QStringLiteral("No changes"))) {
                skipRebase(controller);
                return;
            } else if (combined.contains(QStringLiteral("CONFLICT")) ||
                       combined.contains(QStringLiteral("could not apply"))) {
                setState(repo, OperationState::RebaseSuspended);
                if (auto *ctrl = m_controllers.value(repo))
                    refreshConflicts(ctrl);
            } else {
                setState(repo, OperationState::RebaseSuspended);
                emit rebaseFailed(repo, combined.trimmed());
            }
            if (auto *ctrl = m_controllers.value(repo))
                ctrl->refresh();
        });
}

void GitOperationManager::skipRebase(GitController *controller)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;

    setState(repo, OperationState::RebaseRunning);
    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("rebase"), QStringLiteral("--skip")};
    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 300000, true,
        [this, repo, runner](int exit, const QByteArray &out, const QByteArray &err) {
            runner->asQObject()->deleteLater();
            QString combined = QString::fromUtf8(out) + QString::fromUtf8(err);
            if (exit == 0) {
                m_repoStates[repo].conflicts.clear();
                m_repoStates[repo].rebaseOntoBranch.clear();
                setState(repo, OperationState::Idle);
                emit rebaseCompleted(repo);
            } else if (combined.contains(QStringLiteral("CONFLICT"))) {
                setState(repo, OperationState::RebaseSuspended);
                if (auto *ctrl = m_controllers.value(repo))
                    refreshConflicts(ctrl);
            } else {
                setState(repo, OperationState::RebaseSuspended);
                emit rebaseFailed(repo, combined.trimmed());
            }
            if (auto *ctrl = m_controllers.value(repo))
                ctrl->refresh();
        });
}

void GitOperationManager::abortRebase(GitController *controller)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;

    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("rebase"), QStringLiteral("--abort")};
    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 30000, false,
        [this, repo, runner](int exit, const QByteArray &, const QByteArray &err) {
            runner->asQObject()->deleteLater();
            if (exit == 0) {
                m_repoStates[repo].conflicts.clear();
                m_repoStates[repo].rebaseOntoBranch.clear();
                setState(repo, OperationState::Idle);
                emit rebaseAborted(repo);
            } else {
                emit rebaseFailed(repo, QString::fromUtf8(err).trimmed());
            }
            if (auto *ctrl = m_controllers.value(repo))
                ctrl->refresh();
        });
}

void GitOperationManager::resolveFile(GitController *controller, const QString &relPath)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;

    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("add"), QStringLiteral("--"), relPath};
    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 10000, false,
        [this, repo, runner](int, const QByteArray &, const QByteArray &) {
            runner->asQObject()->deleteLater();
            if (auto *ctrl = m_controllers.value(repo))
                refreshConflicts(ctrl);
        });
}

void GitOperationManager::acceptOurs(GitController *controller, const QStringList &relPaths)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty() || relPaths.isEmpty()) return;

    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("checkout"),
                     QStringLiteral("--ours"), QStringLiteral("--")};
    argv.append(relPaths);

    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 10000, false,
        [this, repo, relPaths, runner, scope](int exit, const QByteArray &, const QByteArray &) {
            runner->asQObject()->deleteLater();
            if (exit != 0) return;
            QStringList addArgv{QStringLiteral("-C"), repo, QStringLiteral("add"), QStringLiteral("--")};
            addArgv.append(relPaths);
            auto *addRunner = GitRunnerFactory::createForRepo(scope, this);
            addRunner->run(repo, addArgv, {}, 10000, false,
                [this, repo, addRunner](int, const QByteArray &, const QByteArray &) {
                    addRunner->asQObject()->deleteLater();
                    if (auto *ctrl = m_controllers.value(repo))
                        refreshConflicts(ctrl);
                });
        });
}

void GitOperationManager::acceptTheirs(GitController *controller, const QStringList &relPaths)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty() || relPaths.isEmpty()) return;

    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("checkout"),
                     QStringLiteral("--theirs"), QStringLiteral("--")};
    argv.append(relPaths);

    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 10000, false,
        [this, repo, relPaths, runner, scope](int exit, const QByteArray &, const QByteArray &) {
            runner->asQObject()->deleteLater();
            if (exit != 0) return;
            QStringList addArgv{QStringLiteral("-C"), repo, QStringLiteral("add"), QStringLiteral("--")};
            addArgv.append(relPaths);
            auto *addRunner = GitRunnerFactory::createForRepo(scope, this);
            addRunner->run(repo, addArgv, {}, 10000, false,
                [this, repo, addRunner](int, const QByteArray &, const QByteArray &) {
                    addRunner->asQObject()->deleteLater();
                    if (auto *ctrl = m_controllers.value(repo))
                        refreshConflicts(ctrl);
                });
        });
}

void GitOperationManager::setState(const QString &repoPath, OperationState s)
{
    m_repoStates[repoPath].opState = s;
    emit operationStateChanged(repoPath, s);
}

void GitOperationManager::refreshConflicts(GitController *controller)
{
    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;

    QStringList argv{QStringLiteral("-C"), repo, QStringLiteral("ls-files"),
                     QStringLiteral("--unmerged"), QStringLiteral("-z")};
    auto *runner = GitRunnerFactory::createForRepo(scope, this);
    runner->run(repo, argv, {}, 10000, false,
        [this, repo, runner](int exit, const QByteArray &out, const QByteArray &) {
            runner->asQObject()->deleteLater();
            if (exit != 0) return;

            ConflictEntries entries = parseUnmergedOutput(out);
            auto &rs = m_repoStates[repo];
            rs.conflicts = entries;
            emit conflictsUpdated(repo, entries);

            if (entries.isEmpty()) {
                if (rs.opState == OperationState::MergeConflicted)
                    emit mergeConflicted(repo, entries);
                else if (rs.opState == OperationState::RebaseSuspended)
                    emit rebaseConflicted(repo, entries);
            } else {
                if (rs.opState == OperationState::MergeConflicted ||
                    rs.opState == OperationState::MergeRunning) {
                    setState(repo, OperationState::MergeConflicted);
                    emit mergeConflicted(repo, entries);
                } else {
                    setState(repo, OperationState::RebaseSuspended);
                    emit rebaseConflicted(repo, entries);
                }
            }
        });
}

ConflictEntries GitOperationManager::parseUnmergedOutput(const QByteArray &output) const
{
    QHash<QString, ConflictEntry> byPath;

    const auto parts = output.split('\0');
    for (const QByteArray &part : parts) {
        if (part.isEmpty()) continue;
        // Format: <mode> <sha> <stage>\t<path>
        int tabIdx = part.indexOf('\t');
        if (tabIdx < 0) continue;

        QString path = QString::fromUtf8(part.mid(tabIdx + 1));
        QByteArray meta = part.left(tabIdx);
        auto fields = meta.split(' ');
        if (fields.size() < 3) continue;

        int stage = fields[2].toInt();
        QString sha = QString::fromUtf8(fields[1]);

        auto &entry = byPath[path];
        entry.relPath = path;
        switch (stage) {
        case 1: entry.baseSha = sha; break;
        case 2: entry.oursSha = sha; break;
        case 3: entry.theirsSha = sha; break;
        default: break;
        }
    }

    ConflictEntries result;
    result.reserve(byPath.size());
    for (auto it = byPath.begin(); it != byPath.end(); ++it) {
        auto &e = it.value();
        if (!e.oursSha.isEmpty() && !e.theirsSha.isEmpty()) {
            e.type = e.baseSha.isEmpty() ? ConflictEntry::BothAdded : ConflictEntry::BothModified;
        } else if (e.oursSha.isEmpty()) {
            e.type = ConflictEntry::DeletedModified;
        } else {
            e.type = ConflictEntry::ModifiedDeleted;
        }
        result.append(std::move(e));
    }
    return result;
}

void GitOperationManager::onEditorConnection()
{
    QTcpSocket *conn = m_editorServer->nextPendingConnection();
    if (!conn) return;

    m_pendingEditorConn = conn;
    connect(conn, &QTcpSocket::readyRead, this, &GitOperationManager::onEditorDataReady);
    connect(conn, &QTcpSocket::disconnected, conn, &QObject::deleteLater);
}

void GitOperationManager::onEditorDataReady()
{
    auto *conn = qobject_cast<QTcpSocket *>(sender());
    if (!conn) return;
    if (!conn->canReadLine()) return;

    QByteArray tokenLine = conn->readLine().trimmed();
    if (QString::fromUtf8(tokenLine) != m_editorToken) {
        conn->write("1\n");
        conn->flush();
        conn->disconnectFromHost();
        return;
    }

    if (!conn->canReadLine()) {
        connect(conn, &QTcpSocket::readyRead, this, [this, conn]() {
            if (!conn->canReadLine()) return;
            disconnect(conn, &QTcpSocket::readyRead, this, nullptr);
            QByteArray fileLine = conn->readLine().trimmed();
            QString filePath = QString::fromUtf8(fileLine);
            handleEditorFile(conn, filePath);
        });
        return;
    }

    QByteArray fileLine = conn->readLine().trimmed();
    QString filePath = QString::fromUtf8(fileLine);
    handleEditorFile(conn, filePath);
}

void GitOperationManager::onControllerStatusUpdated()
{
    auto *controller = qobject_cast<GitController *>(sender());
    if (!controller) return;

    const QString repo = controller->currentRepo();
    const QString scope = controller->runnerScope();
    if (repo.isEmpty()) return;

    auto &rs = m_repoStates[repo];
    if (rs.opState == OperationState::MergeConflicted ||
        rs.opState == OperationState::RebaseSuspended) {
        refreshConflicts(controller);
    }
}

void GitOperationManager::handleEditorFile(QTcpSocket *conn, const QString &filePath)
{
    // Determine if this is a rebase todo file or a commit message file.
    // Todo files contain lines like "pick <sha> <subject>".
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        conn->write("1\n");
        conn->flush();
        conn->disconnectFromHost();
        return;
    }

    QByteArray content = f.readAll();
    f.close();

    bool isTodoFile = content.contains("pick ") || content.contains("# Rebase ");

    if (m_editorScript) {
        const bool saved = m_editorScript(filePath, isTodoFile);
        conn->write(saved ? "0\n" : "1\n");
        conn->flush();
        conn->disconnectFromHost();
        return;
    }

    if (isTodoFile) {
        emit interactiveRebaseRequested(filePath);
    } else {
        emit commitMessageEditRequested(filePath);
    }

    // Store the connection — the dialog will call back to reply.
    m_pendingEditorConn = conn;
}

void GitOperationManager::replyToEditor(bool accepted)
{
    if (!m_pendingEditorConn) return;
    m_pendingEditorConn->write(accepted ? "0\n" : "1\n");
    m_pendingEditorConn->flush();
    m_pendingEditorConn->disconnectFromHost();
    m_pendingEditorConn = nullptr;
}
