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
#include <QStandardPaths>

#define CREATE_SETTING(group, name, lname, type, default) \
ApplicationSetting<type> name{#group "/" #name, (default)}; /* NOLINT(bugprone-macro-parentheses) */\
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
    // The Claude usage chip used to track one config folder; it now takes a list.
    const QString legacyClaudeDir = QStringLiteral("Ai/UsageClaudeConfigDir");
    if (contains(legacyClaudeDir)) {
        const QString dir = value(legacyClaudeDir).toString().trimmed();
        const QString dirsKey = QStringLiteral("Ai/UsageClaudeConfigDirs");
        if (!dir.isEmpty() && !contains(dirsKey))
            setValue(dirsKey, QStringList{dir});
        remove(legacyClaudeDir);
    }
}

// File-scope ApplicationSetting<T> objects and by-value QString setters are the
// established CREATE_SETTING shape. QString's constructor is not noexcept, and
// the setter copies — neither is a hot path (settings load once / write on
// change). NOLINT covers the whole expansion rather than rewriting the macro.
// NOLINTBEGIN(bugprone-throwing-static-initialization,performance-unnecessary-value-param)
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

CREATE_SETTING(App, Theme, theme, ApplicationSettings::ThemeEnum, ApplicationSettings::Dark)

CREATE_SETTING(Editor, ShowWhitespace, showWhitespace, bool, false);
CREATE_SETTING(Editor, ShowEndOfLine, showEndOfLine, bool, false);
CREATE_SETTING(Editor, ShowWrapSymbol, showWrapSymbol, bool, false);
CREATE_SETTING(Editor, ShowIndentGuide, showIndentGuide, bool, true);
CREATE_SETTING(Editor, WordWrap, wordWrap, bool, false)
CREATE_SETTING(Editor, FontName, fontName, QString, QStringLiteral("Consolas"))
CREATE_SETTING(Editor, FontSize, fontSize, int, 10)
// Glyph hinting toggle. Default true (sharp): snaps stems to the pixel grid so
// thin/light fonts (e.g. Lilex) aren't blurred by Qt's grayscale antialiasing.
// Off preserves the typeface's exact outline. Plumbed into Scintilla's Qt
// platform layer via Scintilla::Internal::SetEditorFontHintingEnabled.
CREATE_SETTING(Editor, FontHinting, fontHinting, bool, true)
CREATE_SETTING(Editor, AdditionalWordChars, additionalWordChars, QString, QStringLiteral(""));
CREATE_SETTING(Editor, DefaultEOLMode, defaultEOLMode, QString, QStringLiteral(""))
CREATE_SETTING(Editor, URLHighlighting, urlHighlighting, bool, true)
CREATE_SETTING(Editor, ShowLineNumbers, showLineNumbers, bool, true)
CREATE_SETTING(Editor, AutoCompletion, autoCompletion, bool, true)

// AI chat-view font. UseDefault=true follows the editor's Default Font (current
// behavior); when false the chat uses its own Family/SizePt/Sharpen. The
// Family/SizePt fallbacks are the application's default UI font (what the picker
// shows the first time the user unchecks "Use default font") — NOT the editor
// font. See AcpSessionView::chatFont().
CREATE_SETTING(ChatFont, ChatFontUseDefault, chatFontUseDefault, bool, true)
CREATE_SETTING(ChatFont, ChatFontFamily, chatFontFamily, QString, []() {
    return QApplication::font().family();
})
CREATE_SETTING(ChatFont, ChatFontSizePt, chatFontSizePt, int, []() {
    return QApplication::font().pointSize();
})
CREATE_SETTING(ChatFont, ChatFontSharpen, chatFontSharpen, bool, true)

static QString defaultShellCommand()
{
#ifdef Q_OS_WIN
    static const char *candidates[] = {"pwsh.exe", "powershell.exe", "cmd.exe"};
    for (const char *name : candidates) {
        if (!QStandardPaths::findExecutable(QString::fromLatin1(name)).isEmpty())
            return QString::fromLatin1(name);
    }
    return QStringLiteral("cmd.exe");
#else
    return qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
#endif
}

CREATE_SETTING(Terminal, ShellCommand, shellCommand, QString, []() { return defaultShellCommand(); })
// First installed of the usual terminal faces; Courier New (the Windows
// system fixed font) only as a last resort.
static QString findDefaultTerminalFont()
{
    static const char *const families[] = {
        "Cascadia Mono", "Consolas", "SF Mono", "Menlo", "DejaVu Sans Mono", "Liberation Mono",
    };
    const QStringList installed = QFontDatabase::families();
    for (const char *name : families) {
        const QString family = QString::fromLatin1(name);
        if (installed.contains(family)) {
            QFont f(family, kDefaultTerminalPointSize);
            f.setStyleHint(QFont::Monospace);
            return f.toString();
        }
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).toString();
}

// The settings layer evaluates defaults on every read; enumerating installed
// fonts is not free, so it happens once per process.
static QString defaultTerminalFont()
{
    static const QString font = findDefaultTerminalFont();
    return font;
}

CREATE_SETTING(Terminal, TerminalFont, terminalFont, QString, []() { return defaultTerminalFont(); })
CREATE_SETTING(Terminal, TerminalChildMemoryLimitGb, terminalChildMemoryLimitGb, int, 16)
CREATE_SETTING(Terminal, TerminalLineHeightPct, terminalLineHeightPct, int, 120)
CREATE_SETTING(Terminal, TerminalColorScheme, terminalColorScheme, QString, QStringLiteral("app"))
CREATE_SETTING(Terminal, TerminalMinimumContrast, terminalMinimumContrast, int, 45)
CREATE_SETTING(Terminal, TerminalCursorShape, terminalCursorShape, QString, QStringLiteral("block"))
CREATE_SETTING(Terminal, TerminalCursorBlink, terminalCursorBlink, QString, QStringLiteral("terminal"))
CREATE_SETTING(Terminal, TerminalScrollbackLines, terminalScrollbackLines, int, 10000)
CREATE_SETTING(Terminal, TerminalCopyOnSelect, terminalCopyOnSelect, bool, false)
CREATE_SETTING(Terminal, TerminalBellSound, terminalBellSound, bool, false)
CREATE_SETTING(Terminal, TerminalShowEmptyPanel, terminalShowEmptyPanel, bool, true)

// --- Quick Browser last-used proxy settings ------------------------------------

CREATE_SETTING(QuickBrowser, LastProxyType, lastProxyType, int, 0)
CREATE_SETTING(QuickBrowser, LastProxyHost, lastProxyHost, QString, QStringLiteral(""))
CREATE_SETTING(QuickBrowser, LastProxyPort, lastProxyPort, int, 0)
CREATE_SETTING(QuickBrowser, LastProxyBypassList, lastProxyBypassList, QString, QStringLiteral(""))

CREATE_SETTING(Git, SyntaxHighlightDiffEnabled, syntaxHighlightDiffEnabled, bool, true)
CREATE_SETTING(Git, DiffIgnoreWhitespace, diffIgnoreWhitespace, bool, false)
CREATE_SETTING(Git, DiffWrapLines, diffWrapLines, bool, false)
CREATE_SETTING(Git, DiffTreeWidth, diffTreeWidth, int, 256)
CREATE_SETTING(Git, DiffTreeVisible, diffTreeVisible, bool, true)

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
        "- Header: <type>(<scope>): <subject>, no more than 50 characters in total, imperative mood, no trailing period.\n"
        "- Body (optional): explain WHY, not WHAT. Wrap at 72 characters. Use bullet points if multiple concerns.\n"
        "- No trailing whitespace.\n"
        "- Deliver the message ONLY by calling the submit_commit_message tool (type, optional scope, subject, optional body). Never write it as text.\n"
        "\n"
        "{{rules}}\n"
        "{{subject_hint}}\n"
        "Diff:\n"
        "```\n"
        "{{diff}}\n"
        "```\n");
}

CREATE_SETTING(Ai, CommitMessageProviderUrl, commitMessageProviderUrl, QString, QStringLiteral(""))
CREATE_SETTING(Ai, CommitMessageModel, commitMessageModel, QString, QStringLiteral(""))
CREATE_SETTING(Ai, CommitMessageApiFormat, commitMessageApiFormat, ApplicationSettings::AiApiFormatEnum, ApplicationSettings::OpenAiCompatible)
CREATE_SETTING(Ai, CommitMessageApiKeyConfigured, commitMessageApiKeyConfigured, bool, false)
CREATE_SETTING(Ai, CommitMessagePromptTemplate, commitMessagePromptTemplate, QString, []() {
    return defaultCommitMessagePromptTemplate();
})
CREATE_SETTING(Ai, CommitMessageDiffByteBudget, commitMessageDiffByteBudget, int, 20000)
CREATE_SETTING(Ai, CommitMessageRulesByteBudget, commitMessageRulesByteBudget, int, 4000)
CREATE_SETTING(Ai, CommitMessageStreamIdleTimeoutSec, commitMessageStreamIdleTimeoutSec, int, 60)
CREATE_SETTING(Ai, CommitMessageGenerateShortcut, commitMessageGenerateShortcut, QString, QStringLiteral("Ctrl+Alt+G"))

CREATE_SETTING(Ai, SyncWorkspaceOnAiSwitch, syncWorkspaceOnAiSwitch, bool, true)
CREATE_SETTING(Ai, UsagePercentRemaining, usagePercentRemaining, bool, false)
CREATE_SETTING(Ai, UsageClaudeConfigDirs, usageClaudeConfigDirs, QStringList, QStringList())
// NOLINTEND(bugprone-throwing-static-initialization,performance-unnecessary-value-param)

// --- AI / ACP agent settings ---------------------------------------------------
//
// Declared by hand (not via CREATE_SETTING) because the auto-approve setter
// emits an extra `autoApprovePolicyChanged` signal expected by AcpAgentRegistry
// and other consumers in addition to the standard `<name>Changed` form.

static const char kAiDefaultAgentIdKey[]      = "Ai/DefaultAgentId";
static const char kAiLastUsedAgentIdKey[]     = "Ai/LastUsedAgentId";
static const char kAiAutoApprovePolicyKey[]   = "Ai/AutoApprovePermissions";
static const char kAiAgentsJsonKey[]          = "Ai/Agents";
static const char kAiAgentPreferencesJsonKey[] = "Ai/AgentPreferences";
static const char kScheduledTasksJsonKey[]    = "Ai/ScheduledTasksJson";
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

QString ApplicationSettings::lastUsedAiAgentId() const
{
    return value(QLatin1String(kAiLastUsedAgentIdKey), QString()).toString();
}

void ApplicationSettings::setLastUsedAiAgentId(const QString &id)
{
    if (lastUsedAiAgentId() == id) {
        return;
    }
    setValue(QLatin1String(kAiLastUsedAgentIdKey), id);
    emit lastUsedAiAgentIdChanged(id);
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

QString ApplicationSettings::scheduledTasksJson() const
{
    return value(QLatin1String(kScheduledTasksJsonKey), QString()).toString();
}

void ApplicationSettings::setScheduledTasksJson(const QString &json)
{
    setValue(QLatin1String(kScheduledTasksJsonKey), json);
    emit scheduledTasksJsonChanged(json);
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
