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


#include <QDebug>
#include <QDir>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QSettings>
#include <QSysInfo>
#include <QApplication>
#include <QDataStream>

#include <cstring>
#include <cstdlib>

#include <sentry.h>

#include "CrashHandler.h"
#include "DataPaths.h"
#include "GoalVerdictMcp.h"
#include "NotepadNextApplication.h"
#include "ShutdownDiagnostics.h"

int main(int argc, char *argv[])
try
{
    // Install crash handlers before anything else so faults during early init
    // (Qt plugin load, settings parse, ...) still produce a crash_report.txt.
    CrashHandler::install();

    // Stamp startup time for debug-build shutdown diagnostics. No-op in Release.
    ShutdownDiagnostics::install();

    // Test-only CLI flag wired up to scripts/test-crash-handler.sh. Parsed
    // before QApplication so we don't depend on SingleApplication forwarding.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], GoalVerdictMcp::kCliFlag) == 0)
            return GoalVerdictMcp::run();
        const char *prefix = "--__trigger-crash=";
        if (std::strncmp(argv[i], prefix, std::strlen(prefix)) == 0) {
            CrashHandler::triggerCrashForTest(argv[i] + std::strlen(prefix));
        }
    }

    qSetMessagePattern("[%{time process}] %{if-debug}D%{endif}%{if-info}I%{endif}%{if-warning}W%{endif}%{if-critical}C%{endif}%{if-fatal}F%{endif}: %{message}");

    // Set these since other parts of the app references these
    QApplication::setOrganizationName("NotepadAI");
    QApplication::setApplicationName("NotepadAI");
    QGuiApplication::setApplicationDisplayName("NotepadAI");
    QGuiApplication::setApplicationVersion(APP_VERSION);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    // Default settings format
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // --- Resolve data directory (priority: CLI > env > portable > bootstrap INI > default) ---
    QString dataDir;
    DataPaths::Source dataSource = DataPaths::Source::Default;

    auto resolveRelative = [&](const QString &path) -> QString {
        QDir d(path);
        if (d.isAbsolute())
            return QDir::cleanPath(path);
        const QString exeDir = QCoreApplication::applicationDirPath();
        return QDir::cleanPath(exeDir + QLatin1Char('/') + path);
    };

    // 1) CLI: --data-dir=<path>
    for (int i = 1; i < argc; ++i) {
        const char *prefix = "--data-dir=";
        if (std::strncmp(argv[i], prefix, std::strlen(prefix)) == 0) {
            const char *val = argv[i] + std::strlen(prefix);
            if (val[0] != '\0') {
                dataDir = resolveRelative(QString::fromUtf8(val));
                dataSource = DataPaths::Source::CLI;
            }
            break;
        }
    }

    // 2) Env: NOTEPADAI_DATA_DIR
    if (dataDir.isEmpty()) {
        const char *envVal = std::getenv("NOTEPADAI_DATA_DIR");
        if (envVal && envVal[0] != '\0') {
            dataDir = resolveRelative(QString::fromUtf8(envVal));
            dataSource = DataPaths::Source::Env;
        }
    }

    // 3) Portable: file named "portable" next to exe
    if (dataDir.isEmpty()) {
        const QString exeDir = QCoreApplication::applicationDirPath();
        if (QFileInfo::exists(exeDir + QStringLiteral("/portable"))) {
            dataDir = exeDir;
            dataSource = DataPaths::Source::Portable;
        }
    }

    // 4) Bootstrap INI at default location: key App/DataDir
    if (dataDir.isEmpty()) {
        QSettings bootstrap(QSettings::IniFormat, QSettings::UserScope,
                            QStringLiteral("NotepadAI"), QStringLiteral("NotepadAI"));
        const QString stored = bootstrap.value(QStringLiteral("App/DataDir")).toString();
        if (!stored.isEmpty()) {
            const QString resolved = resolveRelative(stored);
            QDir d(resolved + QStringLiteral("/NotepadAI"));
            if (d.exists() || d.mkpath(QStringLiteral("."))) {
                dataDir = resolved;
                dataSource = DataPaths::Source::Setting;
            }
        }
    }

    // 5) Default
    if (dataDir.isEmpty()) {
        dataDir = QSettings(QSettings::IniFormat, QSettings::UserScope,
                            QStringLiteral("NotepadAI"), QStringLiteral("NotepadAI"))
                      .fileName();
        // fileName() returns full path to INI; we need the parent of org dir
        // e.g. "C:/Users/X/AppData/Roaming/NotepadAI/NotepadAI.ini" → "C:/Users/X/AppData/Roaming"
        QFileInfo fi(dataDir);
        dataDir = fi.absolutePath();           // .../NotepadAI
        dataDir = QFileInfo(dataDir).absolutePath(); // .../Roaming
        dataSource = DataPaths::Source::Default;
    }

    // Validate: ensure the data directory is writable
    {
        QDir d(dataDir + QStringLiteral("/NotepadAI"));
        if (!d.exists() && !d.mkpath(QStringLiteral("."))) {
            // Fallback to default if custom dir is not writable
            if (dataSource != DataPaths::Source::Default) {
                qWarning("Data directory '%s' is not writable, falling back to default",
                         qUtf8Printable(dataDir));
                QSettings fallback(QSettings::IniFormat, QSettings::UserScope,
                                   QStringLiteral("NotepadAI"), QStringLiteral("NotepadAI"));
                QFileInfo fi(fallback.fileName());
                dataDir = QFileInfo(fi.absolutePath()).absolutePath();
                dataSource = DataPaths::Source::Default;
            }
        }
    }

    DataPaths::init(dataDir, dataSource);

    // Redirect QSettings to the resolved data directory
    if (dataSource != DataPaths::Source::Default) {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dataDir);
    }

    // --- Sentry crash reporting ---
    sentry_options_t *sentryOptions = sentry_options_new();
    sentry_options_set_dsn(sentryOptions, "https://2cfaca32d5fe89c28270072dcee0c305@o294128.ingest.us.sentry.io/4511496856272896");
    const std::string sentryDbPath = (dataDir + QStringLiteral("/NotepadAI/.sentry-native")).toStdString();
    sentry_options_set_database_path(sentryOptions, sentryDbPath.c_str());
    const std::string sentryRelease = std::string("NotepadAI@") + APP_VERSION;
    sentry_options_set_release(sentryOptions, sentryRelease.c_str());
    if (const char *dmp = CrashHandler::minidumpPath(); dmp && dmp[0])
        sentry_options_add_attachment(sentryOptions, dmp);
#ifndef NDEBUG
    sentry_options_set_debug(sentryOptions, 1);
#endif
    sentry_init(sentryOptions);
    auto sentryClose = qScopeGuard([] { sentry_close(); });

    // --- SingleApplication userData based on data dir ---
    // When using a custom data dir, hash the path to create a unique identity.
    // This replaces --new-window behavior when --data-dir is present.
    QString singleAppUserData;
    if (dataSource != DataPaths::Source::Default) {
        QString normalized = QDir::cleanPath(dataDir);
#ifdef Q_OS_WIN
        normalized = QDir::toNativeSeparators(normalized).toCaseFolded();
#endif
        const QByteArray hash = QCryptographicHash::hash(
            normalized.toUtf8(), QCryptographicHash::Sha256);
        singleAppUserData = QString::fromLatin1(hash.toHex());
    }

    NotepadNextApplication app(argc, argv, singleAppUserData);

    // Log some debug info
    qInfo("=============================");
    for(const auto &d : app.debugInfo()){
        qInfo("%s", qUtf8Printable(d));
    }
    qInfo("=============================");


    if(app.isPrimary()) {
        app.init();

        return app.exec();
    }
    else {
        qInfo() << "Primary instance already running. PID:" << app.primaryPid();

        app.sendInfoToPrimaryInstance();

        qInfo() << "Secondary instance closing...";

        app.exit(0);

        return 0;
    }
}
catch (const std::exception &e) {
    qCritical("Unhandled exception in main: %s", e.what());
    return 1;
}
catch (...) {
    qCritical("Unhandled unknown exception in main");
    return 1;
}
