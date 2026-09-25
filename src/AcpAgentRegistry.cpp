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

#include "AcpAgentRegistry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ApplicationSettings.h"


namespace {
constexpr const char kBuiltinClaudeCodeId[] = "builtin:claude-code";
constexpr const char kBuiltinCodexId[]      = "builtin:codex";
constexpr const char kPolicyManual[]        = "manual";
constexpr const char kPolicyAllowAll[]      = "allowAll";
constexpr int kAgentsJsonVersion = 2;

void seedFactoryAgents(QList<AcpAgentDefinition> *agents)
{
    agents->append(AcpAgentRegistry::builtinClaudeCodeDefinition());
    agents->append(AcpAgentRegistry::builtinCodexDefinition());
}
}

QString AcpAgentRegistry::builtinClaudeCodeId()
{
    return QString::fromLatin1(kBuiltinClaudeCodeId);
}

QString AcpAgentRegistry::builtinCodexId()
{
    return QString::fromLatin1(kBuiltinCodexId);
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

AcpAgentDefinition AcpAgentRegistry::builtinCodexDefinition()
{
    AcpAgentDefinition def;
    def.id = builtinCodexId();
    def.name = QStringLiteral("Codex");
    def.command = QStringLiteral("npx");
    def.args = QStringList{
        QStringLiteral("-y"),
        QStringLiteral("@zed-industries/codex-acp"),
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

    if (!m_settings) {
        seedFactoryAgents(&m_agents);
        return;
    }

    const QString json = m_settings->aiAgentsJson();
    if (json.isEmpty()) {
        seedFactoryAgents(&m_agents);
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        seedFactoryAgents(&m_agents);
        return;
    }

    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        if (obj.value(QStringLiteral("version")).toInt() >= kAgentsJsonVersion) {
            const QJsonArray arr = obj.value(QStringLiteral("agents")).toArray();
            for (const auto &v : arr) {
                if (!v.isObject()) {
                    continue;
                }
                AcpAgentDefinition def = acpAgentDefinitionFromJson(v.toObject());
                if (def.id.isEmpty()) {
                    continue;
                }
                m_agents.append(def);
            }
            return;
        }
    }

    seedFactoryAgents(&m_agents);
    if (!doc.isArray()) {
        return;
    }

    const QJsonArray arr = doc.array();
    for (const auto &v : arr) {
        if (!v.isObject()) {
            continue;
        }
        AcpAgentDefinition def = acpAgentDefinitionFromJson(v.toObject());
        def.builtin = false;
        if (def.id.isEmpty()) {
            continue;
        }
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
        arr.append(acpAgentDefinitionToJson(def));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), kAgentsJsonVersion);
    root.insert(QStringLiteral("agents"), arr);
    const QJsonDocument doc(root);
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
        m_agents.removeAt(i);
        persistUserAgents();
        emit changed();
        return true;
    }
    return false;
}

QString AcpAgentRegistry::defaultAgentId() const
{
    auto fallback = [this]() -> QString {
        if (m_agents.isEmpty()) {
            return QString();
        }
        return m_agents.first().id;
    };
    if (!m_settings) {
        return fallback();
    }
    QString id = m_settings->defaultAiAgentId();
    if (id.isEmpty() || !contains(id)) {
        return fallback();
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

QString AcpAgentRegistry::agentPreference(const QString &agentId, const QString &key) const
{
    if (!m_settings || agentId.isEmpty() || key.isEmpty()) {
        return QString();
    }
    const QString raw = m_settings->aiAgentPreferencesJson();
    if (raw.isEmpty()) {
        return QString();
    }
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject()) {
        return QString();
    }
    const QJsonObject root = doc.object();
    const QJsonValue agentEntry = root.value(agentId);
    if (!agentEntry.isObject()) {
        return QString();
    }
    return agentEntry.toObject().value(key).toString();
}

void AcpAgentRegistry::setAgentPreference(const QString &agentId, const QString &key, const QString &value)
{
    if (!m_settings || agentId.isEmpty() || key.isEmpty()) {
        return;
    }
    QJsonObject root;
    const QString raw = m_settings->aiAgentPreferencesJson();
    if (!raw.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        if (doc.isObject()) {
            root = doc.object();
        }
    }
    QJsonObject agentEntry = root.value(agentId).toObject();
    if (agentEntry.value(key).toString() == value) {
        return;
    }
    if (value.isEmpty()) {
        agentEntry.remove(key);
    } else {
        agentEntry.insert(key, value);
    }
    if (agentEntry.isEmpty()) {
        root.remove(agentId);
    } else {
        root.insert(agentId, agentEntry);
    }
    m_settings->setAiAgentPreferencesJson(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}
