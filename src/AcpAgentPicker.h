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

#ifndef ACP_AGENT_PICKER_H
#define ACP_AGENT_PICKER_H

#include "AcpAgentDefinition.h"
#include "AcpAgentRegistry.h"

#include <QAction>
#include <QFont>
#include <QList>
#include <QMenu>
#include <QObject>
#include <QString>

#include <algorithm>
#include <functional>

inline QList<AcpAgentDefinition> acpAgentsDefaultFirst(AcpAgentRegistry *registry, QString *defaultIdOut)
{
    QList<AcpAgentDefinition> agents = registry ? registry->agents() : QList<AcpAgentDefinition>();
    const QString defaultId = registry ? registry->defaultAgentId() : QString();
    if (defaultIdOut)
        *defaultIdOut = defaultId;
    std::stable_partition(agents.begin(), agents.end(),
        [&defaultId](const AcpAgentDefinition &a) { return a.id == defaultId; });
    return agents;
}

inline void fillAcpAgentPickerMenu(QMenu *menu,
                                   const QList<AcpAgentDefinition> &agents,
                                   const QString &defaultId,
                                   bool enabled,
                                   QObject *receiver,
                                   const std::function<void(const QString &)> &onPicked)
{
    if (!menu)
        return;
    menu->clear();
    const bool hasAgents = !agents.isEmpty();
    menu->setEnabled(enabled && hasAgents);
    if (!enabled || !hasAgents || !onPicked)
        return;
    for (const AcpAgentDefinition &agent : agents) {
        QAction *action = menu->addAction(agent.name);
        if (agent.id == defaultId) {
            QFont f = action->font();
            f.setBold(true);
            action->setFont(f);
        }
        const QString agentId = agent.id;
        QObject::connect(action, &QAction::triggered, receiver, [onPicked, agentId]() {
            if (!agentId.isEmpty())
                onPicked(agentId);
        });
    }
}

#endif // ACP_AGENT_PICKER_H
