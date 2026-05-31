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


#pragma once

#include <cstdint>
#include <functional>

#include <QObject>
#include <QSettings>
#include <QString>
#include <QMetaEnum>


template<typename T>
class ApplicationSetting
{
public:
    ApplicationSetting(const char * const key, T defaultValue = T())
        : mKey(key)
        , mDefault(defaultValue)
        , mCallable(Q_NULLPTR)
    {}

    ApplicationSetting(const char * const key, std::function<T()> callable)
        : mKey(key)
        , mDefault(T())
        , mCallable(callable)
    {}

    inline T getDefault() const { return mCallable ? mCallable() : mDefault ; }
    inline const char * key() const { return mKey; }

private:
    const char * const mKey;
    const T mDefault;
    std::function<T()> mCallable;
};


#define DEFINE_SETTING(name, lname, type)\
public:\
    type lname() const;\
public slots:\
    void set##name(type lname);\
Q_SIGNAL\
    void lname##Changed(type lname);\


class ApplicationSettings : public QSettings
{
    Q_OBJECT

public:
    explicit ApplicationSettings(QObject *parent = nullptr);

    enum DefaultDirectoryBehaviorEnum {
        FollowCurrentDocument,
        RememberLastUsed,
        HardCoded
    };
    Q_ENUM(DefaultDirectoryBehaviorEnum)

    enum ThemeEnum : std::uint8_t {
        System,
        Light,
        Dark
    };
    Q_ENUM(ThemeEnum)

    template <typename T>
    T get(const char *key, const T &defaultValue) const
    { return value(QLatin1String(key), defaultValue).template value<T>(); }

    template <typename T>
    T get(const ApplicationSetting<T> &setting) const
    { return get(setting.key(), setting.getDefault()); }

    template <typename T>
    void set(const ApplicationSetting<T> &setting, const T &value)
    { setValue(QLatin1String(setting.key()), value); }

    DEFINE_SETTING(ShowMenuBar, showMenuBar, bool)
    DEFINE_SETTING(ShowToolBar, showToolBar, bool)
    DEFINE_SETTING(ShowTabBar, showTabBar, bool)
    DEFINE_SETTING(ShowStatusBar, showStatusBar, bool)
    DEFINE_SETTING(CenterSearchDialog, centerSearchDialog, bool)

    DEFINE_SETTING(TabsClosable, tabsClosable, bool)
    DEFINE_SETTING(ExitOnLastTabClosed, exitOnLastTabClosed, bool)

    DEFINE_SETTING(CombineSearchResults, combineSearchResults, bool)

    DEFINE_SETTING(RestorePreviousSession, restorePreviousSession, bool)
    DEFINE_SETTING(RestoreUnsavedFiles, restoreUnsavedFiles, bool)
    DEFINE_SETTING(RestoreTempFiles, restoreTempFiles, bool)

    DEFINE_SETTING(DataDir, dataDir, QString)

    DEFINE_SETTING(DefaultDirectoryBehavior, defaultDirectoryBehavior, DefaultDirectoryBehaviorEnum)
    DEFINE_SETTING(DefaultDirectory, defaultDirectory, QString)

    DEFINE_SETTING(Translation, translation, QString)

    DEFINE_SETTING(Theme, theme, ThemeEnum)

    DEFINE_SETTING(ShowWhitespace, showWhitespace, bool);
    DEFINE_SETTING(ShowEndOfLine, showEndOfLine, bool);
    DEFINE_SETTING(ShowWrapSymbol, showWrapSymbol, bool)
    DEFINE_SETTING(ShowIndentGuide, showIndentGuide, bool)
    DEFINE_SETTING(WordWrap, wordWrap, bool);
    DEFINE_SETTING(FontName, fontName, QString);
    DEFINE_SETTING(FontSize, fontSize, int);
    DEFINE_SETTING(FontHinting, fontHinting, bool);
    DEFINE_SETTING(AdditionalWordChars, additionalWordChars, QString);
    DEFINE_SETTING(DefaultEOLMode, defaultEOLMode, QString)
    DEFINE_SETTING(URLHighlighting, urlHighlighting, bool)
    DEFINE_SETTING(ShowLineNumbers, showLineNumbers, bool)

    DEFINE_SETTING(ShellCommand, shellCommand, QString)
    DEFINE_SETTING(TerminalFont, terminalFont, QString)

    DEFINE_SETTING(LastProxyType, lastProxyType, int)
    DEFINE_SETTING(LastProxyHost, lastProxyHost, QString)
    DEFINE_SETTING(LastProxyPort, lastProxyPort, int)
    DEFINE_SETTING(LastProxyBypassList, lastProxyBypassList, QString)

    // Per-workspace task registry. Stored as a single JSON object:
    // { "<cleanPath>": [{"name":"...","command":"..."},...], ... }
    // Never deleted — tasks survive workspace close/removal.
public:
    QString workspaceTasksJson() const;
public slots:
    void setWorkspaceTasksJson(const QString &json);

    // Mini Apps persistence. Global apps stored as a JSON array; workspace
    // apps stored as a JSON object keyed by normalized workspace path.
public:
    QString miniAppsGlobalJson() const;
    QString miniAppsWorkspaceJson() const;
public slots:
    void setMiniAppsGlobalJson(const QString &json);
    void setMiniAppsWorkspaceJson(const QString &json);
signals:
    void miniAppsGlobalJsonChanged(const QString &json);
    void miniAppsWorkspaceJsonChanged(const QString &json);

    DEFINE_SETTING(SyntaxHighlightDiffEnabled, syntaxHighlightDiffEnabled, bool)

    // Files-tab git decoration master toggle. When false, the workspace
    // tree shows default text colour regardless of git status.
    DEFINE_SETTING(FileTreeGitColors, fileTreeGitColors, bool)

    // Editor-margin git change markers (added/modified/deleted). When false,
    // the gutter shows no decoration. Default true. Storage key
    // Editor/GitGutterEnabled.
    DEFINE_SETTING(GitGutterEnabled, gitGutterEnabled, bool)

    // Inline git-blame annotation at the caret's line (EOL annotation
    // "Author, when • summary ↗ sha"). On by default. Toggle via
    // Ctrl+Alt+B or INI. Storage key Editor/InlineBlameEnabled.
    DEFINE_SETTING(InlineBlameEnabled, inlineBlameEnabled, bool)

    // Editor minimap (VS Code / Sublime-style thumbnail strip on the right).
    // Shows code-density tint + git markers + viewport indicator; click/drag
    // to navigate. Off by default — opt-in via INI. Storage key
    // Editor/MinimapEnabled.
    DEFINE_SETTING(MinimapEnabled, minimapEnabled, bool)

#ifndef NDEBUG
    DEFINE_SETTING(ShutdownDiagnosticsEnabled, shutdownDiagnosticsEnabled, bool)
#endif

    // Commit-message AI generation settings. All stored under the "Ai/" group.
    // The API key value itself is NEVER stored here — only a boolean flag
    // tracking whether the OS keychain holds one. See ai/CredentialStore.h.
    DEFINE_SETTING(CommitMessageProviderUrl,        commitMessageProviderUrl,        QString)
    DEFINE_SETTING(CommitMessageModel,              commitMessageModel,              QString)
    DEFINE_SETTING(CommitMessageApiKeyConfigured,   commitMessageApiKeyConfigured,   bool)
    DEFINE_SETTING(CommitMessagePromptTemplate,     commitMessagePromptTemplate,     QString)
    DEFINE_SETTING(CommitMessageDiffByteBudget,     commitMessageDiffByteBudget,     int)
    DEFINE_SETTING(CommitMessageRulesByteBudget,    commitMessageRulesByteBudget,    int)
    DEFINE_SETTING(CommitMessageStreamIdleTimeoutSec, commitMessageStreamIdleTimeoutSec, int)
    DEFINE_SETTING(CommitMessageGenerateShortcut,   commitMessageGenerateShortcut,   QString)

    DEFINE_SETTING(SyncWorkspaceOnAiSwitch, syncWorkspaceOnAiSwitch, bool)

    // AI / ACP agent settings. Stored under the "Ai/" group.
public:
    QString defaultAiAgentId() const;
    QString lastUsedAiAgentId() const;
    QString aiAutoApprovePolicy() const;
    QString aiAgentsJson() const;
    QString aiAgentPreferencesJson() const;
    QString scheduledTasksJson() const;

public slots:
    void setDefaultAiAgentId(const QString &id);
    void setLastUsedAiAgentId(const QString &id);
    void setAiAutoApprovePolicy(const QString &policy);
    void setAiAgentsJson(const QString &json);
    void setAiAgentPreferencesJson(const QString &json);
    void setScheduledTasksJson(const QString &json);

signals:
    void defaultAiAgentIdChanged(const QString &id);
    void lastUsedAiAgentIdChanged(const QString &id);
    void aiAutoApprovePolicyChanged(const QString &policy);
    void autoApprovePolicyChanged(const QString &policy);
    void aiAgentsJsonChanged(const QString &json);
    void aiAgentPreferencesJsonChanged(const QString &json);
    void scheduledTasksJsonChanged(const QString &json);
};
