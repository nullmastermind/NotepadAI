/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadADE contributors
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

#ifndef ACP_AGENT_REGISTRY_H
#define ACP_AGENT_REGISTRY_H

#include <QList>
#include <QObject>
#include <QString>

#include "AcpAgentDefinition.h"

class ApplicationSettings;

class AcpAgentRegistry : public QObject
{
    Q_OBJECT

public:
    explicit AcpAgentRegistry(ApplicationSettings *settings, QObject *parent = nullptr);

    static QString builtinClaudeCodeId();
    static AcpAgentDefinition builtinClaudeCodeDefinition();

    QList<AcpAgentDefinition> agents() const;
    bool contains(const QString &id) const;
    AcpAgentDefinition agent(const QString &id) const;

    bool addAgent(const AcpAgentDefinition &def);
    bool updateAgent(const AcpAgentDefinition &def);
    bool removeAgent(const QString &id);

    QString defaultAgentId() const;
    void setDefaultAgentId(const QString &id);

    QString autoApprovePolicy() const;
    void setAutoApprovePolicy(const QString &policy);

signals:
    void changed();
    void defaultAgentIdChanged(const QString &id);
    void autoApprovePolicyChanged(const QString &policy);

private:
    void load();
    void persistUserAgents();

    ApplicationSettings *m_settings;
    QList<AcpAgentDefinition> m_agents;
};

#endif // ACP_AGENT_REGISTRY_H
