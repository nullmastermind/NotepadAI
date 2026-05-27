/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "MiniAppRegistry.h"
#include "ApplicationSettings.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

MiniAppRegistry::MiniAppRegistry(ApplicationSettings *settings)
    : m_settings(settings)
{
}

QString MiniAppRegistry::normalizeWorkspacePath(const QString &path)
{
#ifdef Q_OS_WIN
    return QDir::cleanPath(path).toLower();
#else
    return QDir::cleanPath(path);
#endif
}

QList<MiniAppDefinition> MiniAppRegistry::globalApps() const
{
    if (!m_settings) return {};
    return parseJson(m_settings->miniAppsGlobalJson());
}

QList<MiniAppDefinition> MiniAppRegistry::workspaceApps(const QString &workspacePath) const
{
    if (!m_settings || workspacePath.isEmpty()) return {};

    const QString raw = m_settings->miniAppsWorkspaceJson();
    if (raw.isEmpty()) return {};

    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if (!doc.isObject()) return {};

    const QString key = normalizeWorkspacePath(workspacePath);
    const QJsonArray arr = doc.object().value(key).toArray();

    QList<MiniAppDefinition> result;
    result.reserve(arr.size());
    for (const auto &v : arr) {
        const QJsonObject obj = v.toObject();
        MiniAppDefinition def;
        def.id = obj.value(QStringLiteral("id")).toString();
        def.name = obj.value(QStringLiteral("name")).toString();
        def.url = obj.value(QStringLiteral("url")).toString();
        def.command = obj.value(QStringLiteral("command")).toString();
        def.env = obj.value(QStringLiteral("env")).toString();
        def.cwd = obj.value(QStringLiteral("cwd")).toString();
        def.icon = obj.value(QStringLiteral("icon")).toString();
        def.healthCheckUrl = obj.value(QStringLiteral("healthCheckUrl")).toString();
        def.healthTimeoutMs = obj.value(QStringLiteral("healthTimeoutMs")).toInt(30000);
        def.debugPort = obj.value(QStringLiteral("debugPort")).toInt(0);
        def.autoKillOnClose = obj.value(QStringLiteral("autoKillOnClose")).toBool(true);
        const QString pt = obj.value(QStringLiteral("proxyType")).toString();
        if (pt == QLatin1String("http")) def.proxyType = 1;
        else if (pt == QLatin1String("https")) def.proxyType = 2;
        else if (pt == QLatin1String("socks4")) def.proxyType = 3;
        else if (pt == QLatin1String("socks5")) def.proxyType = 4;
        else def.proxyType = 0;
        def.proxyHost = obj.value(QStringLiteral("proxyHost")).toString();
        def.proxyPort = obj.value(QStringLiteral("proxyPort")).toInt(0);
        def.proxyBypassList = obj.value(QStringLiteral("proxyBypassList")).toString();
        if (!def.name.isEmpty())
            result.append(def);
    }
    return result;
}

void MiniAppRegistry::setGlobalApps(const QList<MiniAppDefinition> &apps)
{
    if (!m_settings) return;
    m_settings->setMiniAppsGlobalJson(toJson(apps));
}

void MiniAppRegistry::setWorkspaceApps(const QString &workspacePath, const QList<MiniAppDefinition> &apps)
{
    if (!m_settings || workspacePath.isEmpty()) return;

    const QString raw = m_settings->miniAppsWorkspaceJson();
    QJsonObject root;
    if (!raw.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
        if (doc.isObject())
            root = doc.object();
    }

    const QString key = normalizeWorkspacePath(workspacePath);
    QJsonArray arr;
    for (const MiniAppDefinition &def : apps) {
        if (def.name.isEmpty()) continue;
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), def.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : def.id);
        obj.insert(QStringLiteral("name"), def.name);
        obj.insert(QStringLiteral("url"), def.url);
        if (!def.command.isEmpty()) obj.insert(QStringLiteral("command"), def.command);
        if (!def.env.isEmpty()) obj.insert(QStringLiteral("env"), def.env);
        if (!def.cwd.isEmpty()) obj.insert(QStringLiteral("cwd"), def.cwd);
        if (!def.icon.isEmpty()) obj.insert(QStringLiteral("icon"), def.icon);
        if (!def.healthCheckUrl.isEmpty()) obj.insert(QStringLiteral("healthCheckUrl"), def.healthCheckUrl);
        if (def.healthTimeoutMs != 30000) obj.insert(QStringLiteral("healthTimeoutMs"), def.healthTimeoutMs);
        if (def.debugPort > 0) obj.insert(QStringLiteral("debugPort"), def.debugPort);
        if (!def.autoKillOnClose) obj.insert(QStringLiteral("autoKillOnClose"), false);
        if (def.proxyType > 0 && !def.proxyHost.isEmpty()) {
            static const char *typeStrings[] = {"none", "http", "https", "socks4", "socks5"};
            obj.insert(QStringLiteral("proxyType"), QLatin1String(typeStrings[qBound(0, def.proxyType, 4)]));
            obj.insert(QStringLiteral("proxyHost"), def.proxyHost);
            if (def.proxyPort > 0) obj.insert(QStringLiteral("proxyPort"), def.proxyPort);
            if (!def.proxyBypassList.isEmpty()) obj.insert(QStringLiteral("proxyBypassList"), def.proxyBypassList);
        }
        arr.append(obj);
    }

    if (arr.isEmpty())
        root.remove(key);
    else
        root.insert(key, arr);

    m_settings->setMiniAppsWorkspaceJson(
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

QList<MiniAppDefinition> MiniAppRegistry::mergedApps(const QString &workspacePath) const
{
    QList<MiniAppDefinition> result = globalApps();
    if (workspacePath.isEmpty()) return result;

    const QList<MiniAppDefinition> wsApps = workspaceApps(workspacePath);
    for (const MiniAppDefinition &wsDef : wsApps) {
        // Workspace overrides global with same id
        bool replaced = false;
        for (int i = 0; i < result.size(); ++i) {
            if (result[i].id == wsDef.id) {
                result[i] = wsDef;
                replaced = true;
                break;
            }
        }
        if (!replaced)
            result.append(wsDef);
    }
    return result;
}

QList<MiniAppDefinition> MiniAppRegistry::parseJson(const QString &json)
{
    if (json.isEmpty()) return {};

    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) return {};

    const QJsonArray arr = doc.array();
    QList<MiniAppDefinition> result;
    result.reserve(arr.size());
    for (const auto &v : arr) {
        const QJsonObject obj = v.toObject();
        MiniAppDefinition def;
        def.id = obj.value(QStringLiteral("id")).toString();
        def.name = obj.value(QStringLiteral("name")).toString();
        def.url = obj.value(QStringLiteral("url")).toString();
        def.command = obj.value(QStringLiteral("command")).toString();
        def.env = obj.value(QStringLiteral("env")).toString();
        def.cwd = obj.value(QStringLiteral("cwd")).toString();
        def.icon = obj.value(QStringLiteral("icon")).toString();
        def.healthCheckUrl = obj.value(QStringLiteral("healthCheckUrl")).toString();
        def.healthTimeoutMs = obj.value(QStringLiteral("healthTimeoutMs")).toInt(30000);
        def.debugPort = obj.value(QStringLiteral("debugPort")).toInt(0);
        def.autoKillOnClose = obj.value(QStringLiteral("autoKillOnClose")).toBool(true);
        const QString pt2 = obj.value(QStringLiteral("proxyType")).toString();
        if (pt2 == QLatin1String("http")) def.proxyType = 1;
        else if (pt2 == QLatin1String("https")) def.proxyType = 2;
        else if (pt2 == QLatin1String("socks4")) def.proxyType = 3;
        else if (pt2 == QLatin1String("socks5")) def.proxyType = 4;
        else def.proxyType = 0;
        def.proxyHost = obj.value(QStringLiteral("proxyHost")).toString();
        def.proxyPort = obj.value(QStringLiteral("proxyPort")).toInt(0);
        def.proxyBypassList = obj.value(QStringLiteral("proxyBypassList")).toString();
        if (!def.name.isEmpty())
            result.append(def);
    }
    return result;
}

QString MiniAppRegistry::toJson(const QList<MiniAppDefinition> &apps)
{
    QJsonArray arr;
    for (const MiniAppDefinition &def : apps) {
        if (def.name.isEmpty()) continue;
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), def.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : def.id);
        obj.insert(QStringLiteral("name"), def.name);
        obj.insert(QStringLiteral("url"), def.url);
        if (!def.command.isEmpty()) obj.insert(QStringLiteral("command"), def.command);
        if (!def.env.isEmpty()) obj.insert(QStringLiteral("env"), def.env);
        if (!def.cwd.isEmpty()) obj.insert(QStringLiteral("cwd"), def.cwd);
        if (!def.icon.isEmpty()) obj.insert(QStringLiteral("icon"), def.icon);
        if (!def.healthCheckUrl.isEmpty()) obj.insert(QStringLiteral("healthCheckUrl"), def.healthCheckUrl);
        if (def.healthTimeoutMs != 30000) obj.insert(QStringLiteral("healthTimeoutMs"), def.healthTimeoutMs);
        if (def.debugPort > 0) obj.insert(QStringLiteral("debugPort"), def.debugPort);
        if (!def.autoKillOnClose) obj.insert(QStringLiteral("autoKillOnClose"), false);
        if (def.proxyType > 0 && !def.proxyHost.isEmpty()) {
            static const char *typeStrings[] = {"none", "http", "https", "socks4", "socks5"};
            obj.insert(QStringLiteral("proxyType"), QLatin1String(typeStrings[qBound(0, def.proxyType, 4)]));
            obj.insert(QStringLiteral("proxyHost"), def.proxyHost);
            if (def.proxyPort > 0) obj.insert(QStringLiteral("proxyPort"), def.proxyPort);
            if (!def.proxyBypassList.isEmpty()) obj.insert(QStringLiteral("proxyBypassList"), def.proxyBypassList);
        }
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
