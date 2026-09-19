/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "MiniAppDefinition.h"

#include <QList>
#include <QString>

class ApplicationSettings;

// Persistence layer for mini app definitions. Stores global apps and
// per-workspace apps as JSON in ApplicationSettings (same pattern as
// TerminalTaskRegistry). No UI, no process lifecycle.
class MiniAppRegistry
{
public:
    explicit MiniAppRegistry(ApplicationSettings *settings);

    QList<MiniAppDefinition> globalApps() const;
    QList<MiniAppDefinition> workspaceApps(const QString &workspacePath) const;

    void setGlobalApps(const QList<MiniAppDefinition> &apps);
    void setWorkspaceApps(const QString &workspacePath, const QList<MiniAppDefinition> &apps);

    // Returns merged list: global first, then workspace (workspace overrides
    // global entries with the same id).
    QList<MiniAppDefinition> mergedApps(const QString &workspacePath) const;

    // Global first, then every workspace list. Empty id if not found.
    MiniAppDefinition findById(const QString &id) const;

    static QString normalizeWorkspacePath(const QString &path);

private:
    static QList<MiniAppDefinition> parseJson(const QString &json);
    static QString toJson(const QList<MiniAppDefinition> &apps);

    ApplicationSettings *m_settings;
};
