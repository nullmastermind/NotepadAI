/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QString>

// Builds Chromium command-line arguments for proxy configuration and CDP debug port.
// proxyType: 0=None, 1=HTTP, 2=HTTPS, 3=SOCKS4, 4=SOCKS5.
// Returns empty string if proxy is disabled or host is empty.
// debugPort > 0 appends --remote-debugging-port.
QString buildBrowserArgs(int debugPort, int proxyType = 0, const QString &proxyHost = QString(),
                         int proxyPort = 0, const QString &proxyBypassList = QString());
