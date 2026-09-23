/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QString>

struct MiniAppDefinition
{
    QString id;               // UUID string
    QString name;             // Display name (required)
    QString url;              // http/https URL (required)
    QString command;          // Shell command to spawn (optional)
    QString env;              // KEY=VALUE lines (optional)
    QString cwd;              // Working directory (optional)
    QString icon;             // Reserved for future custom icon path
    QString healthCheckUrl;   // Health poll URL (defaults to url if empty)
    int healthTimeoutMs = 60000; // Timeout in ms (range 5000-300000)
    // Off remembers healthCheckUrl/healthTimeoutMs but launch uses the defaults.
    bool advancedEnabled = false;
    int debugPort = 0;           // CDP debug port (0 = none, 1-65535 = a port)
    // Off remembers debugPort but launch does not open a debug server.
    bool debugEnabled = false;
    bool autoKillOnClose = true; // Kill process on tab close
    int proxyType = 0;            // 0=None, 1=HTTP, 2=HTTPS, 3=SOCKS4, 4=SOCKS5
    QString proxyHost;
    int proxyPort = 0;            // 0 = use scheme default
    QString proxyBypassList;
    // Off remembers proxy fields but launch does not apply them.
    bool proxyEnabled = false;
    bool allowCrossOrigin = false;

    bool isValid() const { return !name.isEmpty() && !url.isEmpty(); }

    QString effectiveHealthUrl() const
    {
        if (!advancedEnabled || healthCheckUrl.isEmpty())
            return url;
        return healthCheckUrl;
    }

    int effectiveHealthTimeoutMs() const
    {
        return advancedEnabled ? healthTimeoutMs : 60000;
    }

    int effectiveDebugPort() const
    {
        return debugEnabled ? debugPort : 0;
    }

    int effectiveProxyType() const
    {
        return proxyEnabled ? proxyType : 0;
    }
};
