/*
 * This file is part of Notepad Next.
 * Copyright 2024 Justin Dailey
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

#include "ApplicationSettings.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>

#define CREATE_SETTING(group, name, lname, type, default) \
ApplicationSetting<type> name{#group "/" #name, default};\
    type ApplicationSettings::lname() const\
{\
        return get(name);\
}\
    void ApplicationSettings::set##name(type lname)\
{\
        set(name, lname);\
        emit lname##Changed(lname);\
}



ApplicationSettings::ApplicationSettings(QObject *parent)
    : QSettings{parent}
{
}

CREATE_SETTING(Gui, ShowMenuBar, showMenuBar, bool, true)
CREATE_SETTING(Gui, ShowToolBar, showToolBar, bool, true)
CREATE_SETTING(Gui, ShowTabBar, showTabBar, bool, true)
CREATE_SETTING(Gui, ShowStatusBar, showStatusBar, bool, true)
CREATE_SETTING(Gui, CenterSearchDialog, centerSearchDialog, bool, true)

CREATE_SETTING(Gui, TabsClosable, tabsClosable, bool, true)
CREATE_SETTING(Gui, ExitOnLastTabClosed, exitOnLastTabClosed, bool, false)

CREATE_SETTING(Gui, CombineSearchResults, combineSearchResults, bool, false)

// Default true — matches VSCode/Sublime/JetBrains norm. SavedFile entries are
// also saved unconditionally inside SessionManager so users who toggle this
// later still have data to load on the next restart.
CREATE_SETTING(App, RestorePreviousSession, restorePreviousSession, bool, true)
CREATE_SETTING(App, RestoreUnsavedFiles, restoreUnsavedFiles, bool, false)
CREATE_SETTING(App, RestoreTempFiles, restoreTempFiles, bool, false)

CREATE_SETTING(App, DataDir, dataDir, QString, QString())

CREATE_SETTING(App, DefaultDirectoryBehavior, defaultDirectoryBehavior, ApplicationSettings::DefaultDirectoryBehaviorEnum, ApplicationSettings::FollowCurrentDocument)
CREATE_SETTING(App, DefaultDirectory, defaultDirectory, QString, QString())

CREATE_SETTING(App, Translation, translation, QString, QStringLiteral(""))

CREATE_SETTING(App, Theme, theme, ApplicationSettings::ThemeEnum, ApplicationSettings::System)

CREATE_SETTING(Editor, ShowWhitespace, showWhitespace, bool, false);
CREATE_SETTING(Editor, ShowEndOfLine, showEndOfLine, bool, false);
CREATE_SETTING(Editor, ShowWrapSymbol, showWrapSymbol, bool, false);
CREATE_SETTING(Editor, ShowIndentGuide, showIndentGuide, bool, true);
CREATE_SETTING(Editor, WordWrap, wordWrap, bool, false)
CREATE_SETTING(Editor, FontName, fontName, QString, QStringLiteral("Courier New"))
CREATE_SETTING(Editor, FontSize, fontSize, int, []() { return qApp->font().pointSize() + 2; })
CREATE_SETTING(Editor, AdditionalWordChars, additionalWordChars, QString, QStringLiteral(""));
CREATE_SETTING(Editor, DefaultEOLMode, defaultEOLMode, QString, QStringLiteral(""))
CREATE_SETTING(Editor, URLHighlighting, urlHighlighting, bool, true)
CREATE_SETTING(Editor, ShowLineNumbers, showLineNumbers, bool, true)

static QString defaultShellCommand()
{
#ifdef Q_OS_WIN
    return qEnvironmentVariable("COMSPEC", QStringLiteral("cmd.exe"));
#else
    return qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
#endif
}

CREATE_SETTING(Terminal, ShellCommand, shellCommand, QString, []() { return defaultShellCommand(); })
CREATE_SETTING(Terminal, TerminalFont, terminalFont, QString, []() {
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).toString();
})

// --- Quick Browser last-used proxy settings ------------------------------------

CREATE_SETTING(QuickBrowser, LastProxyType, lastProxyType, int, 0)
CREATE_SETTING(QuickBrowser, LastProxyHost, lastProxyHost, QString, QStringLiteral(""))
CREATE_SETTING(QuickBrowser, LastProxyPort, lastProxyPort, int, 0)
CREATE_SETTING(QuickBrowser, LastProxyBypassList, lastProxyBypassList, QString, QStringLiteral(""))

CREATE_SETTING(Git, SyntaxHighlightDiffEnabled, syntaxHighlightDiffEnabled, bool, true)

// Files-tab decoration master toggle — see file-tree-git-decorations spec.
// Default ON so users see the feature out of the box. No PreferencesDialog
// UI in this proposal; users who want it off edit the INI directly.
CREATE_SETTING(Editor, FileTreeGitColors, fileTreeGitColors, bool, true)
CREATE_SETTING(Editor, GitGutterEnabled, gitGutterEnabled, bool, true)
CREATE_SETTING(Editor, InlineBlameEnabled, inlineBlameEnabled, bool, true)
CREATE_SETTING(Editor, MinimapEnabled, minimapEnabled, bool, false)

#ifndef NDEBUG
// Debug-only diagnostics: emit shutdown_report.txt on clean exit. See
// ShutdownDiagnostics.{h,cpp} and "Help -> Debug -> Shutdown Diagnostics" toggle.
CREATE_SETTING(Debug, ShutdownDiagnosticsEnabled, shutdownDiagnosticsEnabled, bool, true)
#endif

// --- Commit-message AI generation settings ------------------------------------
//
// Reads/writes go through the standard CREATE_SETTING macro. The API key value
// itself never lives in QSettings — only a flag tracking whether the OS
// keychain holds one. See ai/CredentialStore.{h,cpp}.

static QString defaultCommitMessagePromptTemplate()
{
    return QStringLiteral(
        "You are an expert at writing git commit messages in the Conventional Commits format.\n"
        "\n"
        "Generate a commit message for the following changes. Follow these rules strictly:\n"
        "- First line: <type>(<scope>): <subject>, no more than 50 characters, imperative mood, no trailing period.\n"
        "- Blank line.\n"
        "- Body: explain WHY, not WHAT. Wrap at 72 characters. Use bullet points if multiple concerns.\n"
        "- No trailing whitespace.\n"
        "- Output ONLY the commit message text — no markdown fences, no preamble, no explanation.\n"
        "\n"
        "{{rules}}\n"
        "{{subject_hint}}\n"
        "Diff:\n"
        "```\n"
        "{{diff}}\n"
        "```\n");
}

// CREATE_SETTING expands to a by-value QString setter, matching the existing
// project idiom used by every other QString setting above. Suppressed here so
// we don't drift from the established macro pattern (changing the macro is
// out of scope and would noisily diff every preexisting QString setting).
// NOLINTBEGIN(performance-unnecessary-value-param)
CREATE_SETTING(Ai, CommitMessageProviderUrl, commitMessageProviderUrl, QString, QStringLiteral(""))
CREATE_SETTING(Ai, CommitMessageModel, commitMessageModel, QString, QStringLiteral(""))
CREATE_SETTING(Ai, CommitMessageApiKeyConfigured, commitMessageApiKeyConfigured, bool, false)
CREATE_SETTING(Ai, CommitMessagePromptTemplate, commitMessagePromptTemplate, QString, []() {
    return defaultCommitMessagePromptTemplate();
})
CREATE_SETTING(Ai, CommitMessageDiffByteBudget, commitMessageDiffByteBudget, int, 20000)
CREATE_SETTING(Ai, CommitMessageRulesByteBudget, commitMessageRulesByteBudget, int, 4000)
CREATE_SETTING(Ai, CommitMessageStreamIdleTimeoutSec, commitMessageStreamIdleTimeoutSec, int, 60)
CREATE_SETTING(Ai, CommitMessageGenerateShortcut, commitMessageGenerateShortcut, QString, QStringLiteral("Ctrl+Alt+G"))
// NOLINTEND(performance-unnecessary-value-param)

CREATE_SETTING(Ai, SyncWorkspaceOnAiSwitch, syncWorkspaceOnAiSwitch, bool, true)

// --- AI / ACP agent settings ---------------------------------------------------
//
// Declared by hand (not via CREATE_SETTING) because the auto-approve setter
// emits an extra `autoApprovePolicyChanged` signal expected by AcpAgentRegistry
// and other consumers in addition to the standard `<name>Changed` form.

static const char kAiDefaultAgentIdKey[]      = "Ai/DefaultAgentId";
static const char kAiAutoApprovePolicyKey[]   = "Ai/AutoApprovePermissions";
static const char kAiAgentsJsonKey[]          = "Ai/Agents";
static const char kAiAgentPreferencesJsonKey[] = "Ai/AgentPreferences";
static const char kWorkspaceTasksJsonKey[]    = "Terminal/WorkspaceTasks";

QString ApplicationSettings::defaultAiAgentId() const
{
    return value(QLatin1String(kAiDefaultAgentIdKey), QString()).toString();
}

void ApplicationSettings::setDefaultAiAgentId(const QString &id)
{
    setValue(QLatin1String(kAiDefaultAgentIdKey), id);
    emit defaultAiAgentIdChanged(id);
}

QString ApplicationSettings::aiAutoApprovePolicy() const
{
    return value(QLatin1String(kAiAutoApprovePolicyKey), QString()).toString();
}

void ApplicationSettings::setAiAutoApprovePolicy(const QString &policy)
{
    setValue(QLatin1String(kAiAutoApprovePolicyKey), policy);
    emit aiAutoApprovePolicyChanged(policy);
    emit autoApprovePolicyChanged(policy);
}

QString ApplicationSettings::aiAgentsJson() const
{
    return value(QLatin1String(kAiAgentsJsonKey), QString()).toString();
}

void ApplicationSettings::setAiAgentsJson(const QString &json)
{
    setValue(QLatin1String(kAiAgentsJsonKey), json);
    emit aiAgentsJsonChanged(json);
}

QString ApplicationSettings::aiAgentPreferencesJson() const
{
    return value(QLatin1String(kAiAgentPreferencesJsonKey), QString()).toString();
}

void ApplicationSettings::setAiAgentPreferencesJson(const QString &json)
{
    setValue(QLatin1String(kAiAgentPreferencesJsonKey), json);
    emit aiAgentPreferencesJsonChanged(json);
}

QString ApplicationSettings::workspaceTasksJson() const
{
    return value(QLatin1String(kWorkspaceTasksJsonKey), QString()).toString();
}

void ApplicationSettings::setWorkspaceTasksJson(const QString &json)
{
    setValue(QLatin1String(kWorkspaceTasksJsonKey), json);
}

// --- Mini Apps settings ----------------------------------------------------------

static const char kMiniAppsGlobalJsonKey[]    = "MiniApps/GlobalAppsJson";
static const char kMiniAppsWorkspaceJsonKey[] = "MiniApps/WorkspaceAppsJson";

QString ApplicationSettings::miniAppsGlobalJson() const
{
    return value(QLatin1String(kMiniAppsGlobalJsonKey), QString()).toString();
}

void ApplicationSettings::setMiniAppsGlobalJson(const QString &json)
{
    setValue(QLatin1String(kMiniAppsGlobalJsonKey), json);
    emit miniAppsGlobalJsonChanged(json);
}

QString ApplicationSettings::miniAppsWorkspaceJson() const
{
    return value(QLatin1String(kMiniAppsWorkspaceJsonKey), QString()).toString();
}

void ApplicationSettings::setMiniAppsWorkspaceJson(const QString &json)
{
    setValue(QLatin1String(kMiniAppsWorkspaceJsonKey), json);
    emit miniAppsWorkspaceJsonChanged(json);
}
