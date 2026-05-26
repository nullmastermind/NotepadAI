/*
 * This file is part of Notepad Next.
 * Copyright 2019 Justin Dailey
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


#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "MarkerAppDecorator.h"
#include "RecentFilesListManager.h"
#include "EditorManager.h"
#include "LuaExtension.h"
#include "DebugManager.h"
#include "SessionManager.h"
#include "TranslationManager.h"
#include "ApplicationSettings.h"
#include "AcpAgentManager.h"
#include "PreviewTabManager.h"
#include "MarkdownPreviewWidget.h"
#include "ai/CommitMessageGenerator.h"
#include "ai/CredentialStore.h"
#include "ThemeResolver.h"

#include "LuaState.h"
#include "lua.hpp"
#include "EditorConfigAppDecorator.h"
#include "ProfileScope.h"
#include "ShutdownDiagnostics.h"

#include "ILexer.h"
#include "Lexilla.h"

#include <chrono>

#include <QCommandLineParser>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QUuid>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

const SingleApplication::Options opts = SingleApplication::ExcludeAppPath | SingleApplication::ExcludeAppVersion | SingleApplication::SecondaryNotification;

void parseCommandLine(QCommandLineParser &parser, const QStringList &args)
{
    parser.setApplicationDescription("NotepadAI");
    parser.addHelpOption();
    parser.addVersionOption();

    parser.addPositionalArgument("files", "Files to open.");

    parser.addOptions({
        {"translation", "Overrides the system default translation.", "translation"},
        {"reset-settings", "Resets all application settings."},
        {"n", "Places the cursor on the line number for the first file specified", "line number"},
        {"workspace", "Opens the specified folder as a workspace", "workspace path"},
        {"new-window", "Forces a fresh independent window instead of forwarding to an existing instance"},
        {"data-dir", "Use a custom data directory for settings, sessions, and history", "path"}
    });

    parser.process(args);
}

static QString toLocalFileName(const QString file)
{
    QUrl fileUrl(file);
    return fileUrl.isValid() && fileUrl.isLocalFile() ? fileUrl.toLocalFile() : file;
}

// Drop entries that can never be valid workspaces (empty, relative like ".",
// or paths whose target directory no longer exists) and resolve any survivor
// to its absolute form. Stale relative paths can land in App/RecentWorkspacesList
// or FolderAsWorkspace/Workspaces from older builds that didn't normalize on
// write — without this filter they'd keep showing up in the recent menu and
// would re-spawn empty docks on session restore.
static QStringList sanitizeWorkspacePaths(const QStringList &raw)
{
    QStringList out;
    out.reserve(raw.size());
    for (const QString &p : raw) {
        if (p.isEmpty()) continue;
        const QString abs = QDir(p).absolutePath();
        if (abs.isEmpty()) continue;
        if (!QFileInfo(abs).isDir()) continue;
        if (out.contains(abs)) continue;
        out.append(abs);
    }
    return out;
}

// Detect --new-window from raw argv before SingleApplication runs its primary-instance
// check. When set we hand the base ctor a unique userData so this process gets its own
// block-server identity and isn't intercepted as a secondary of the existing instance.
// Skipped when a custom data dir provides its own userData (--data-dir wins).
static QString detectNewWindowUserData(int argc, char **argv, const QString &dataDirUserData)
{
    if (!dataDirUserData.isEmpty())
        return dataDirUserData;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--new-window") == 0 || qstrcmp(argv[i], "-new-window") == 0) {
            return QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
    }
    return {};
}

NotepadNextApplication::NotepadNextApplication(int &argc, char **argv, const QString &userData)
    : SingleApplication(argc, argv, true, opts, 1000, detectNewWindowUserData(argc, argv, userData))
{
#ifdef Q_OS_WIN
    // Create a system-wide mutex so the installer can detect if it is running
    CreateMutex(NULL, false, L"NotepadNextMutex");
#endif
    parseCommandLine(parser, arguments());

    DebugManager::manageDebugOutput();
    DebugManager::pauseDebugOutput();
}

bool NotepadNextApplication::init()
{
    PROFILE_SCOPE("NotepadNextApplication::init");
    qInfo(Q_FUNC_INFO);

    {
    PROFILE_SCOPE("NotepadNextApplication::init.preInit");

#ifndef Q_OS_MACOS
    setWindowIcon(QIcon(QStringLiteral(":/icons/NotepadNext.png")));
#endif

    settings = new ApplicationSettings(this);

    if (parser.isSet("reset-settings")) {
        QFileInfo original(settings->fileName());
        const QString backup = original.canonicalFilePath() + QStringLiteral(".backup");

        qInfo("Resetting application settings");
        qInfo("Backuping up %s to %s", qUtf8Printable(settings->fileName()), qUtf8Printable(backup));

        QFile::remove(backup);
        QFile::rename(settings->fileName(), backup);

        settings->clear();
    }

    // Translation files are stored as a qresource
    translationManager = new TranslationManager(this, QStringLiteral(":/i18n/"));

    // The command line overrides the settings
    if (!parser.value("translation").isEmpty()) {
        translationManager->loadTranslation(parser.value("translation"));
    }
    else if (!settings->translation().isEmpty()){
        translationManager->loadTranslation(settings->translation());
    }
    else {
        translationManager->loadSystemDefaultTranslation();
    }

    } // init.preInit

    // This connection isn't needed since the application can not appropriately retranslate the UI at runtime
    //connect(settings, &ApplicationSettings::translationChanged, translationManager, &TranslationManager::loadTranslationByName);

    {
        PROFILE_SCOPE("NotepadNextApplication::initLua");
        luaState = new LuaState();
    }

    {
        PROFILE_SCOPE("NotepadNextApplication::initManagers");
        recentFilesListManager = new RecentFilesListManager(this);
        recentWorkspacesListManager = new RecentFilesListManager(this);
        editorManager = new EditorManager(settings, this);
        aiAgentManager_ = new AcpAgentManager(settings, this);
        credentialStore_ = new ai::CredentialStore(this);
        commitMessageGenerator_ = new ai::CommitMessageGenerator(settings, credentialStore_, this);
        sessionManager = new SessionManager(this);
    }

    connect(editorManager, &EditorManager::editorCreated, recentFilesListManager, [this](ScintillaNext *editor) {
        if (editor->isFile()) {
            recentFilesListManager->removeFile(editor->getFilePath());
        }
    });

    connect(editorManager, &EditorManager::editorClosed, recentFilesListManager, [this](ScintillaNext *editor) {
        if (editor->isFile()) {
            recentFilesListManager->addFile(editor->getFilePath());
        }
    });

    {
        PROFILE_SCOPE("NotepadNextApplication::loadSettings");
        loadSettings();
    }

    {
        PROFILE_SCOPE("NotepadNextApplication::applyTheme");
        applyTheme();
    }
    connect(settings, &ApplicationSettings::themeChanged, this, [this](ApplicationSettings::ThemeEnum) {
        applyTheme();
    });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
        if (settings->theme() == ApplicationSettings::System) {
            applyTheme();
        }
    });

    connect(this, &NotepadNextApplication::aboutToQuit, this, &NotepadNextApplication::saveSettings);
    connect(this, &NotepadNextApplication::aboutToQuit, this, [this]() {
        PROFILE_SCOPE("NotepadNextApplication::aboutToQuit::acpShutdown");
        if (aiAgentManager_) {
            aiAgentManager_->shutdown();
        }
    });

#ifndef NDEBUG
    // Apply current setting + slow-event threshold defaults.
    ShutdownDiagnostics::setCollectionEnabled(settings->shutdownDiagnosticsEnabled());
    connect(settings, &ApplicationSettings::shutdownDiagnosticsEnabledChanged,
            this, [](bool enabled) {
                ShutdownDiagnostics::setCollectionEnabled(enabled);
            });
    connect(this, &NotepadNextApplication::aboutToQuit, this, []() {
        ShutdownDiagnostics::writeReport();
    });
#endif

    {
    PROFILE_SCOPE("NotepadNextApplication::init.appDecorators");
    EditorConfigAppDecorator *ecad = new EditorConfigAppDecorator(this);
    ecad->setEnabled(true);
    MarkerAppDecorator *mad = new MarkerAppDecorator(this);
    mad->setEnabled(true);
    }

    {
        PROFILE_SCOPE("NotepadNextApplication::initLuaScripts");
        luaState->executeFile(":/scripts/init.lua");
        LuaExtension::Instance().Initialise(luaState->L, Q_NULLPTR);
    }

    {
        PROFILE_SCOPE("NotepadNextApplication::createNewWindow");
        createNewWindow();
    }
    connect(editorManager, &EditorManager::editorCreated, window, &MainWindow::addEditor);

    {
        previewTabManager = new PreviewTabManager(this, window->getDockedEditor(), this);
        previewTabManager->registerType(QStringLiteral("markdown"), {
            {QStringLiteral("md"), QStringLiteral("markdown"), QStringLiteral("mdown"),
             QStringLiteral("mkd"), QStringLiteral("mkdn"), QStringLiteral("mdwn")},
            QStringLiteral(":/icons/markdown-preview.svg"),
            [this](QWidget *parent) -> PreviewContentWidget * {
                return new MarkdownPreviewWidget(this, parent);
            }
        });
    }

    // If the application is activated (e.g. user switching to another program and them back) the focus
    // needs to be reset on whatever object previously had focus (e.g. the find dialog)
    connect(this, &NotepadNextApplication::focusChanged, this, [&](QWidget *old, QWidget *now) {
        Q_UNUSED(old);
        if (now) {
            currentlyFocusedWidget = now;
        }
    });

    connect(this, &SingleApplication::instanceStarted, window, &MainWindow::bringWindowToForeground);
    connect(this, &SingleApplication::receivedMessage, this, &NotepadNextApplication::receiveInfoFromSecondaryInstance, Qt::QueuedConnection);

    connect(this, &NotepadNextApplication::applicationStateChanged, this, [&](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) {

            // Make sure it is active...
            // The application can be active without the main window being show e.g. if there is a
            // message box that pops up before the main window
            if (window->isActiveWindow()) {
                window->focusIn();
            }

            if (!currentlyFocusedWidget.isNull()) {
                currentlyFocusedWidget->activateWindow();
            }
        }
    });

    const bool isNewWindow = parser.isSet("new-window");

    {
        PROFILE_SCOPE("NotepadNextApplication::restoreWindowGeometry");
        window->restoreWindowGeometry();
    }

    if (!isNewWindow) {
        // One-shot migration from the pre-multi-workspace single-folder setting.
        // Builds before the multi-workspace refactor only persisted
        // FolderAsWorkspace/RootPath; seed the new Workspaces list from it so
        // users upgrading don't lose their open folder. After the first save
        // following migration, Workspaces becomes authoritative and the legacy
        // key is no longer read or written.
        if (settings->value("FolderAsWorkspace/Workspaces").toStringList().isEmpty()) {
            const QString legacyRoot = settings->value("FolderAsWorkspace/RootPath").toString();
            const QStringList sanitized = sanitizeWorkspacePaths(QStringList{legacyRoot});
            if (!sanitized.isEmpty()) {
                settings->setValue("FolderAsWorkspace/Workspaces", sanitized);
            }
        }

        if (settings->restorePreviousSession()) {
            PROFILE_SCOPE("NotepadNextApplication::restoreSession");
            qInfo("Restoring previous session");

            sessionManager->loadSession(window);
        }

        // Workspaces always restore (matching the pre-multi-workspace behavior
        // where the single open folder was always reopened), independent of the
        // editor-session setting. --new-window instances skip this entirely since
        // they are ephemeral by design.
        window->restoreOpenWorkspaces();
    }

    {
        PROFILE_SCOPE("NotepadNextApplication::openCliFiles");
        openFiles(parser.positionalArguments());
    }

    if (parser.isSet("n") && parser.positionalArguments().length() > 0) {
        QString firstFile = parser.positionalArguments()[0];
        ScintillaNext *editor = editorManager->getEditorByFilePath(toLocalFileName(firstFile));

        if (editor) {
            int n = parser.value("n").toInt();
            editor->gotoLine(n - 1);
        }
    }

    // If the window does not have any editors (meaning the no files were
    // specified on the command line) then create a new empty file
    if (window->editorCount() == 0) {
        PROFILE_SCOPE("NotepadNextApplication::init.fallbackNewFile");
        window->newFile();
    }

    // Everything should be ready at this point

    // Check this after restoring the state, as the state contains the previous visibility state of the FAW dock
    if (parser.isSet("workspace")) {
        const QString dir = parser.value("workspace");
        qInfo("Opening folder as workspace: %s", qUtf8Printable(dir));
        window->setFolderAsWorkspacePath(dir);
    }

    {
    PROFILE_SCOPE("NotepadNextApplication::init.windowShow");
    window->showMaximized();
    }

    {
        PROFILE_SCOPE("NotepadNextApplication::restoreWindowState");
        window->restoreWindowState();
    }

    if (!isNewWindow) {
        window->raiseSavedActiveWorkspace();
    }

    DebugManager::resumeDebugOutput();

    return true;
}

SessionManager *NotepadNextApplication::getSessionManager() const
{
    SessionManager::SessionFileTypes fileTypes;

    if (settings->restorePreviousSession()) {
        fileTypes |= SessionManager::SavedFile;
    }

    if (settings->restoreUnsavedFiles()) {
        fileTypes |= SessionManager::UnsavedFile;
    }

    if (settings->restoreTempFiles()) {
        fileTypes |= SessionManager::TempFile;
    }

    // Update the file types supported in case something has changed in the settings
    sessionManager->setSessionFileTypes(fileTypes);

    return sessionManager;
}

QString NotepadNextApplication::getFileDialogFilter() const
{
    return getLuaState()->executeAndReturn<QString>("return DialogFilters()");
}

QString NotepadNextApplication::getFileDialogFilterForLanguage(const QString &language) const
{
    getLuaState()->setVariable("langForFilter", language);
    return getLuaState()->executeAndReturn<QString>("return FilterForLanguage(langForFilter)");
}

QStringList NotepadNextApplication::getLanguages() const
{
    return getLuaState()->executeAndReturn<QStringList>(
                R"(
                local names = {}
                for k in pairs(languages) do table.insert(names, k) end
                table.sort(names, function (a, b) return string.lower(a) < string.lower(b) end)
                return names
                )");
}

void NotepadNextApplication::setEditorLanguage(ScintillaNext *editor, const QString &languageName) const
{
    LuaExtension::Instance().setEditor(editor);

    getLuaState()->setVariable("languageName", languageName);
    const QString lexer = getLuaState()->executeAndReturn<QString>("return languages[languageName].lexer");

    editor->languageName = languageName;
    editor->languageSingleLineComment = getLuaState()->executeAndReturn<QString>("return languages[languageName].singleLineComment or \"\"").toUtf8();

    auto lexerInstance = CreateLexer(lexer.toLatin1().constData());
    editor->setILexer((sptr_t) lexerInstance);
    editor->clearDocumentStyle(); // Remove all previous style information, setting the lexer does not guarantee styling information is cleared

    // Dynamic properties can be used to skip part of the default initialization. The value in the
    // property doesn't currently matter, but may be used at a later point.
    bool skipTabs = editor->QObject::property("nn_skip_usetabs").isValid();
    bool skipTabWidth = editor->QObject::property("nn_skip_tabwidth").isValid();

    getLuaState()->setVariable("skip_tabs", skipTabs);
    getLuaState()->setVariable("skip_tabwidth", skipTabWidth);

    getLuaState()->execute("SetLanguage(languageName)");

    // Emitted after SetLanguage so listeners (notably EditorManager's theme
    // re-skin) see the fully-populated lexer styles. Manual emit because
    // setILexer() doesn't trigger one.
    emit editor->lexerChanged();
}

QString NotepadNextApplication::detectLanguage(ScintillaNext *editor) const
{
    qInfo(Q_FUNC_INFO);

    QString language_name = QStringLiteral("Text");

    if (editor->isFile()) {
        const QFileInfo fileInfo = editor->getFileInfo();
        language_name = detectLanguageFromExtension(fileInfo.suffix(), fileInfo.fileName());
    }

    if (language_name == QStringLiteral("Text")) {
        language_name = detectLanguageFromContents(editor);
    }

    return language_name;
}

QString NotepadNextApplication::detectLanguageFromExtension(const QString &extension, const QString &fileName) const
{
    qInfo(Q_FUNC_INFO);

    getLuaState()->setVariable("ext", extension);
    getLuaState()->setVariable("fileName", fileName);

    return getLuaState()->executeAndReturn<QString>(R"(
    -- Filename match is checked first so it always beats fallback
    -- extensions (e.g. text.lua claims the empty extension "" which
    -- would otherwise win for bare files like 'justfile' / 'Makefile').
    if fileName ~= "" then
        for name, L in pairs(languages) do
            if L.filenames then
                for _, v in ipairs(L.filenames) do
                    if v == fileName then
                        return name
                    end
                end
            end
        end
        for name, L in pairs(languages) do
            if L.filename_patterns then
                for _, pat in ipairs(L.filename_patterns) do
                    -- pcall contains pattern errors so one bad pattern
                    -- in a language file can't break detection for the rest
                    local ok, result = pcall(string.match, fileName, pat)
                    if ok and result then
                        return name
                    elseif not ok then
                        print("filename_patterns error in " .. name .. ": " .. tostring(result))
                    end
                end
            end
        end
    end
    for name, L in pairs(languages) do
        if L.extensions then
            for _, v in ipairs(L.extensions) do
                if v == ext then
                    return name
                end
            end
        end
    end
    return "Text"
    )");
}

QString NotepadNextApplication::detectLanguageFromContents(ScintillaNext *editor) const
{
    qInfo(Q_FUNC_INFO);

    LuaExtension::Instance().setEditor(editor);

    return getLuaState()->executeAndReturn<QString>(R"(
    -- Grab a small chunk
    if editor.Length > 0 then
        editor:SetTargetRange(0, math.min(64, editor.Length))
        return DetectLanguageFromContents(editor.TargetText)
    end

    return "Text"
    )");
}

QString NotepadNextApplication::detectLanguageFromPath(const QString &relPath) const
{
    if (relPath.isEmpty()) return QStringLiteral("Text");
    const int slashFwd = relPath.lastIndexOf('/');
    const int slashBwd = relPath.lastIndexOf('\\');
    const int slash = slashFwd > slashBwd ? slashFwd : slashBwd;
    const QString fileName = (slash >= 0) ? relPath.mid(slash + 1) : relPath;
    const int dot = fileName.lastIndexOf('.');
    const QString ext = (dot >= 0 && dot < fileName.size() - 1)
                            ? fileName.mid(dot + 1)
                            : QString();
    return detectLanguageFromExtension(ext, fileName);
}

QString NotepadNextApplication::resolveLexerName(const QString &languageName) const
{
    if (languageName.isEmpty()) return QString();
    getLuaState()->setVariable("languageName", languageName);
    return getLuaState()->executeAndReturn<QString>(
        "local L = languages[languageName]; if L then return L.lexer or \"\" else return \"\" end");
}

void NotepadNextApplication::sendInfoToPrimaryInstance()
{
    qInfo(Q_FUNC_INFO);

    QStringList argsToSend;
    const QStringList positionalArguments = parser.positionalArguments();

    // Any positional arguments need translated into an absolute file path relative to this instance
    for (const QString &arg : arguments()) {
        if (positionalArguments.contains(arg)) {
            QFileInfo fileInfo(arg);
            argsToSend.append(fileInfo.absoluteFilePath());
        } else {
            argsToSend.append(arg);
        }
    }

    QByteArray buffer;
    QDataStream stream(&buffer, QIODevice::WriteOnly);
    stream << argsToSend;

    const bool success = sendMessage(buffer);
    if (!success) {
        qWarning("sendMessage() unsuccessful");
    }
}

void NotepadNextApplication::receiveInfoFromSecondaryInstance(quint32 instanceId, QByteArray message)
{
    qInfo(Q_FUNC_INFO);

    Q_UNUSED(instanceId)

    QDataStream stream(&message, QIODevice::ReadOnly);
    QStringList args;

    stream >> args;

    QCommandLineParser parser;
    parseCommandLine(parser, args);

    openFiles(parser.positionalArguments());
}

bool NotepadNextApplication::isRunningAsAdmin() const
{
    static bool initialized = false;
    static bool isAdmin = false;

    if (!initialized) {
        initialized = true;

#ifdef Q_OS_WIN
        BOOL isMember;
        SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
        PSID administratorsGroupSid = NULL;

        // Create a SID for the Administrators group
        if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &administratorsGroupSid)) {
            if (CheckTokenMembership(NULL, administratorsGroupSid, &isMember)) {
                isAdmin = isMember;
            }
            FreeSid(administratorsGroupSid);
        }
#endif
    }

    return isAdmin;
}

bool NotepadNextApplication::event(QEvent *event)
{
    // Handle the QFileOpenEvent to open files on MacOS X.
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *fileOpenEvent = static_cast<QFileOpenEvent*>(event);

        qInfo("QFileOpenEvent %s", qUtf8Printable(fileOpenEvent->file()));

        openFiles(QStringList() << fileOpenEvent->file());

        return true;
    }

    return SingleApplication::event(event);
}

#ifndef NDEBUG
bool NotepadNextApplication::notify(QObject *receiver, QEvent *event)
{
    // Fast-path: skip overhead entirely when collection is off.
    if (!ShutdownDiagnostics::isCollectionEnabled()) {
        return SingleApplication::notify(receiver, event);
    }

    const auto start = std::chrono::steady_clock::now();
    const bool result = SingleApplication::notify(receiver, event);
    const auto end = std::chrono::steady_clock::now();
    const qint64 ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - start).count();

    ShutdownDiagnostics::Detail::recordEvent(receiver, event, ns);
    return result;
}
#endif

void NotepadNextApplication::openFiles(const QStringList &files)
{
    qInfo(Q_FUNC_INFO);

    for (const QString &file : files) {
        window->openFile(toLocalFileName(file));
    }
}

void NotepadNextApplication::loadSettings()
{
    recentFilesListManager->setFileList(getSettings()->value("App/RecentFilesList").toStringList());
    recentWorkspacesListManager->setFileList(
        sanitizeWorkspacePaths(getSettings()->value("App/RecentWorkspacesList").toStringList()));
}

void NotepadNextApplication::saveSettings()
{
    getSettings()->setValue("App/RecentFilesList", recentFilesListManager->fileList());
    getSettings()->setValue("App/RecentWorkspacesList", recentWorkspacesListManager->fileList());
}

void NotepadNextApplication::saveSession()
{
    PROFILE_SCOPE("NotepadNextApplication::saveSession");
    // Iterate all the opened editors and add them to the recent file list
    for (const auto &editor : window->editors()) {
        if (editor->isFile()) {
            recentFilesListManager->addFile(editor->getFilePath());
        }
    }
    saveSettings();

    getSessionManager()->saveSession(window);
}

MainWindow *NotepadNextApplication::createNewWindow()
{
    Q_ASSERT(window == Q_NULLPTR);

    window = new MainWindow(this);

    // Keep Lua's editor reference up to date
    connect(window, &MainWindow::editorActivated, this, [](ScintillaNext *editor) {
        LuaExtension::Instance().setEditor(editor);
    });

    // --new-window instances are ephemeral wrt session — skip both close-time and
    // autosave persistence so they can't overwrite the parent instance's session.
    if (!parser.isSet("new-window")) {
        // TODO: this shouldn't be dependent on a MainWindow closing, but this works for now
        // since the assumption is MainWindow::aboutToClose() infers the application is shutting
        // down but the editors are still active.
        connect(window, &MainWindow::aboutToClose, this, &NotepadNextApplication::saveSession);

        // Timer to autosave the session
        connect(&autoSaveTimer, &QTimer::timeout, this, &NotepadNextApplication::saveSession);

        // Also flush per-workspace UI state (tree expand/collapse, inner tab,
        // current item) when the dirty bit is set. Decoupled from saveSession
        // so it can run cheaply — no MainWindow/geometry or windowState
        // re-serialisation, just the WorkspaceStates array. Crash-resilient:
        // a hang or kill between two timer fires loses at most ~60s of UI state.
        connect(&autoSaveTimer, &QTimer::timeout, window, [w = window]() {
            if (w && w->isWorkspaceStateDirty()) {
                w->saveWorkspaceStatesOnly();
                w->clearWorkspaceStateDirty();
            }
        });

        autoSaveTimer.start(60 * 1000);
    }

    return window;
}

bool NotepadNextApplication::isEffectiveThemeDark() const
{
    return resolveThemeIsDark(settings->theme(), QGuiApplication::styleHints()->colorScheme());
}

void NotepadNextApplication::applyTheme()
{
    qInfo(Q_FUNC_INFO);

    const bool dark = isEffectiveThemeDark();

    // Fusion is the most consistent style across platforms for palette-driven theming
    setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    if (dark) {
        palette.setColor(QPalette::Window,          QColor(45, 45, 45));
        palette.setColor(QPalette::WindowText,      QColor(220, 220, 220));
        palette.setColor(QPalette::Base,            QColor(30, 30, 30));
        palette.setColor(QPalette::AlternateBase,   QColor(45, 45, 45));
        palette.setColor(QPalette::ToolTipBase,     QColor(60, 60, 60));
        palette.setColor(QPalette::ToolTipText,     QColor(220, 220, 220));
        palette.setColor(QPalette::Text,            QColor(220, 220, 220));
        palette.setColor(QPalette::PlaceholderText, QColor(140, 140, 140));
        palette.setColor(QPalette::Button,          QColor(53, 53, 53));
        palette.setColor(QPalette::ButtonText,      QColor(220, 220, 220));
        palette.setColor(QPalette::BrightText,      Qt::red);
        palette.setColor(QPalette::Link,            QColor(80, 160, 220));
        palette.setColor(QPalette::Highlight,       QColor(38, 110, 180));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        // Mid/Midlight are unset by default and Qt derives them from Window —
        // for a dark Window that yields near-black, making any QSS rule using
        // `palette(mid)` (subtle labels, char counts, rulers) invisible.
        palette.setColor(QPalette::Mid,             QColor(140, 140, 140));
        palette.setColor(QPalette::Midlight,        QColor(80, 80, 80));
        palette.setColor(QPalette::Disabled, QPalette::Text,            QColor(120, 120, 120));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText,      QColor(120, 120, 120));
        palette.setColor(QPalette::Disabled, QPalette::WindowText,      QColor(120, 120, 120));
        palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(180, 180, 180));
    }
    else {
        // Fusion's standardPalette() consults QGuiApplication::styleHints()
        // ->colorScheme() since Qt 6.5, so on a system in dark mode it hands
        // back a dark palette even when the user explicitly chose Light here.
        // Build the light palette by hand so Light means light, period.
        palette.setColor(QPalette::Window,          QColor(240, 240, 240));
        palette.setColor(QPalette::WindowText,      Qt::black);
        palette.setColor(QPalette::Base,            Qt::white);
        palette.setColor(QPalette::AlternateBase,   QColor(247, 247, 247));
        palette.setColor(QPalette::ToolTipBase,     QColor(255, 255, 220));
        palette.setColor(QPalette::ToolTipText,     Qt::black);
        palette.setColor(QPalette::Text,            Qt::black);
        palette.setColor(QPalette::PlaceholderText, QColor(120, 120, 120));
        palette.setColor(QPalette::Button,          QColor(240, 240, 240));
        palette.setColor(QPalette::ButtonText,      Qt::black);
        palette.setColor(QPalette::BrightText,      Qt::red);
        palette.setColor(QPalette::Link,            QColor(0, 102, 204));
        palette.setColor(QPalette::Highlight,       QColor(48, 140, 198));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::Mid,             QColor(140, 140, 140));
        palette.setColor(QPalette::Midlight,        QColor(220, 220, 220));
        palette.setColor(QPalette::Disabled, QPalette::Text,            QColor(160, 160, 160));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText,      QColor(160, 160, 160));
        palette.setColor(QPalette::Disabled, QPalette::WindowText,      QColor(160, 160, 160));
        palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(220, 220, 220));
    }

    setPalette(palette);

    editorManager->applyTheme(dark);

    emit effectiveThemeChanged();
}

QStringList NotepadNextApplication::debugInfo() const
{
    QStringList info;

    info.append(QStringLiteral("%1 v%2 %3").arg(applicationDisplayName(), applicationVersion(), APP_DISTRIBUTION));
    info.append(QStringLiteral("Build Date/Time: %1 %2").arg(__DATE__, __TIME__));
    info.append(QStringLiteral("Qt: %1").arg(qVersion()));
    info.append(QStringLiteral("OS: %1").arg(QSysInfo::prettyProductName()));
    info.append(QStringLiteral("Locale: %1").arg(QLocale::system().name()));
    info.append(QStringLiteral("CPU: %1").arg(QSysInfo::currentCpuArchitecture()));
    info.append(QStringLiteral("File Path: %1").arg(applicationFilePath()));
    info.append(QStringLiteral("Arguments: %1").arg(arguments().join(' ')));
    info.append(QStringLiteral("Config File: %1").arg(ApplicationSettings().fileName()));

    return info;
}
