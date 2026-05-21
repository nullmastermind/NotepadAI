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

#include "AcpAgentRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ApplicationSettings.h"


namespace {
constexpr const char kBuiltinClaudeCodeId[] = "builtin:claude-code";
constexpr const char kPolicyManual[]        = "manual";
constexpr const char kPolicyAllowAll[]      = "allowAll";
}

QString AcpAgentRegistry::builtinClaudeCodeId()
{
    return QString::fromLatin1(kBuiltinClaudeCodeId);
}

AcpAgentDefinition AcpAgentRegistry::builtinClaudeCodeDefinition()
{
    AcpAgentDefinition def;
    def.id = builtinClaudeCodeId();
    def.name = QStringLiteral("Claude Code");
    def.command = QStringLiteral("npx");
    def.args = QStringList{
        QStringLiteral("-y"),
        QStringLiteral("@agentclientprotocol/claude-agent-acp@latest"),
    };
    def.icon = QString();
    def.builtin = true;
    return def;
}

AcpAgentRegistry::AcpAgentRegistry(ApplicationSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    load();
}

void AcpAgentRegistry::load()
{
    m_agents.clear();
    m_agents.append(builtinClaudeCodeDefinition());

    if (!m_settings) {
        return;
    }

    const QString json = m_settings->aiAgentsJson();
    if (json.isEmpty()) {
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return;
    }

    const QJsonArray arr = doc.array();
    for (const auto &v : arr) {
        if (!v.isObject()) {
            continue;
        }
        AcpAgentDefinition def = acpAgentDefinitionFromJson(v.toObject());
        // User-saved entries are never built-ins, even if a stale file claims otherwise.
        def.builtin = false;
        if (def.id.isEmpty()) {
            continue;
        }
        // Skip any duplicate of the seeded built-in id.
        bool duplicate = false;
        for (const AcpAgentDefinition &existing : m_agents) {
            if (existing.id == def.id) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        m_agents.append(def);
    }
}

void AcpAgentRegistry::persistUserAgents()
{
    if (!m_settings) {
        return;
    }

    QJsonArray arr;
    for (const AcpAgentDefinition &def : m_agents) {
        if (def.builtin) {
            continue;
        }
        arr.append(acpAgentDefinitionToJson(def));
    }

    const QJsonDocument doc(arr);
    const QString compact = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
    m_settings->setAiAgentsJson(compact);
}

QList<AcpAgentDefinition> AcpAgentRegistry::agents() const
{
    return m_agents;
}

bool AcpAgentRegistry::contains(const QString &id) const
{
    for (const AcpAgentDefinition &def : m_agents) {
        if (def.id == id) {
            return true;
        }
    }
    return false;
}

AcpAgentDefinition AcpAgentRegistry::agent(const QString &id) const
{
    for (const AcpAgentDefinition &def : m_agents) {
        if (def.id == id) {
            return def;
        }
    }
    return AcpAgentDefinition{};
}

bool AcpAgentRegistry::addAgent(const AcpAgentDefinition &def)
{
    if (def.id.isEmpty()) {
        return false;
    }
    if (def.builtin) {
        // Callers cannot add a new built-in; built-ins are seeded only.
        return false;
    }
    if (contains(def.id)) {
        return false;
    }
    AcpAgentDefinition copy = def;
    copy.builtin = false;
    m_agents.append(copy);
    persistUserAgents();
    emit changed();
    return true;
}

bool AcpAgentRegistry::updateAgent(const AcpAgentDefinition &def)
{
    if (def.id.isEmpty()) {
        return false;
    }
    for (int i = 0; i < m_agents.size(); ++i) {
        if (m_agents[i].id != def.id) {
            continue;
        }
        if (m_agents[i].builtin) {
            // Built-ins are immutable.
            return false;
        }
        AcpAgentDefinition copy = def;
        copy.builtin = false;
        m_agents[i] = copy;
        persistUserAgents();
        emit changed();
        return true;
    }
    return false;
}

bool AcpAgentRegistry::removeAgent(const QString &id)
{
    for (int i = 0; i < m_agents.size(); ++i) {
        if (m_agents[i].id != id) {
            continue;
        }
        if (m_agents[i].builtin) {
            return false;
        }
        m_agents.removeAt(i);
        persistUserAgents();
        emit changed();
        return true;
    }
    return false;
}

QString AcpAgentRegistry::defaultAgentId() const
{
    if (!m_settings) {
        return builtinClaudeCodeId();
    }
    QString id = m_settings->defaultAiAgentId();
    if (id.isEmpty() || !contains(id)) {
        return builtinClaudeCodeId();
    }
    return id;
}

void AcpAgentRegistry::setDefaultAgentId(const QString &id)
{
    if (!m_settings) {
        return;
    }
    if (m_settings->defaultAiAgentId() == id) {
        return;
    }
    m_settings->setDefaultAiAgentId(id);
    emit defaultAgentIdChanged(id);
}

QString AcpAgentRegistry::autoApprovePolicy() const
{
    if (!m_settings) {
        return QString::fromLatin1(kPolicyManual);
    }
    QString p = m_settings->aiAutoApprovePolicy();
    if (p == QLatin1String(kPolicyAllowAll)) {
        return p;
    }
    return QString::fromLatin1(kPolicyManual);
}

void AcpAgentRegistry::setAutoApprovePolicy(const QString &policy)
{
    if (!m_settings) {
        return;
    }
    QString normalized = (policy == QLatin1String(kPolicyAllowAll))
        ? QString::fromLatin1(kPolicyAllowAll)
        : QString::fromLatin1(kPolicyManual);

    if (m_settings->aiAutoApprovePolicy() == normalized) {
        return;
    }
    m_settings->setAiAutoApprovePolicy(normalized);
    emit autoApprovePolicyChanged(normalized);
}
