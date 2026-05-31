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


#include "PreferencesDialog.h"
#include "DataPaths.h"
#include "NotepadNextApplication.h"
#include "TranslationManager.h"
#include "ai/CredentialStore.h"
#include "ui_PreferencesDialog.h"
#include "ScintillaNext.h"

#include <QButtonGroup>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFontDialog>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSpinBox>
#include <QStandardPaths>


PreferencesDialog::PreferencesDialog(ApplicationSettings *settings, QWidget *parent) :
    QDialog(parent, Qt::Tool),
    ui(new Ui::PreferencesDialog),
    settings(settings)
{
    ui->setupUi(this);

    QIcon icon = style()->standardIcon(QStyle::SP_MessageBoxInformation);
    QPixmap pixmap = icon.pixmap(QSize(16, 16));
    ui->labelAppRestartIcon->setPixmap(pixmap);
    ui->labelAppRestartIcon->hide();
    ui->labelAppRestart->hide();

    MapSettingToCheckBox(ui->checkBoxMenuBar, &ApplicationSettings::showMenuBar, &ApplicationSettings::setShowMenuBar, &ApplicationSettings::showMenuBarChanged);
    MapSettingToCheckBox(ui->checkBoxToolBar, &ApplicationSettings::showToolBar, &ApplicationSettings::setShowToolBar, &ApplicationSettings::showToolBarChanged);
    MapSettingToCheckBox(ui->checkBoxStatusBar, &ApplicationSettings::showStatusBar, &ApplicationSettings::setShowStatusBar, &ApplicationSettings::showStatusBarChanged);
    MapSettingToCheckBox(ui->checkBoxRecenterSearchDialog, &ApplicationSettings::centerSearchDialog, &ApplicationSettings::setCenterSearchDialog, &ApplicationSettings::centerSearchDialogChanged);

    MapSettingToGroupBox(ui->gbxRestorePreviousSession, &ApplicationSettings::restorePreviousSession, &ApplicationSettings::setRestorePreviousSession, &ApplicationSettings::restorePreviousSessionChanged);
    connect(ui->gbxRestorePreviousSession, &QGroupBox::toggled, this, [=](bool checked) {
        if (!checked) {
            ui->checkBoxUnsavedFiles->setChecked(false);
            ui->checkBoxRestoreTempFiles->setChecked(false);
        }
        else {
            QMessageBox::warning(this, tr("Warning"), tr("This feature is experimental and it should not be considered safe for critically important work. It may lead to possible data loss. Use at your own risk."));
        }
    });

    MapSettingToCheckBox(ui->checkBoxUnsavedFiles, &ApplicationSettings::restoreUnsavedFiles, &ApplicationSettings::setRestoreUnsavedFiles, &ApplicationSettings::restoreUnsavedFilesChanged);
    MapSettingToCheckBox(ui->checkBoxRestoreTempFiles, &ApplicationSettings::restoreTempFiles, &ApplicationSettings::setRestoreTempFiles, &ApplicationSettings::restoreTempFilesChanged);

    MapSettingToCheckBox(ui->checkBoxCombineSearchResults, &ApplicationSettings::combineSearchResults, &ApplicationSettings::setCombineSearchResults, &ApplicationSettings::combineSearchResultsChanged);

    populateTranslationComboBox();
    connect(ui->comboBoxTranslation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int index) {
        settings->setTranslation(ui->comboBoxTranslation->itemData(index).toString());
        showApplicationRestartRequired();
    });

    ui->comboBoxTheme->addItem(tr("Follow System"), static_cast<int>(ApplicationSettings::System));
    ui->comboBoxTheme->addItem(tr("Light"),         static_cast<int>(ApplicationSettings::Light));
    ui->comboBoxTheme->addItem(tr("Dark"),          static_cast<int>(ApplicationSettings::Dark));
    {
        int themeIndex = ui->comboBoxTheme->findData(static_cast<int>(settings->theme()));
        ui->comboBoxTheme->setCurrentIndex(themeIndex == -1 ? 0 : themeIndex);
    }
    connect(ui->comboBoxTheme, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int index) {
        settings->setTheme(static_cast<ApplicationSettings::ThemeEnum>(
            ui->comboBoxTheme->itemData(index).toInt()));
    });
    connect(settings, &ApplicationSettings::themeChanged, this, [=](ApplicationSettings::ThemeEnum t) {
        int idx = ui->comboBoxTheme->findData(static_cast<int>(t));
        if (idx != -1) ui->comboBoxTheme->setCurrentIndex(idx);
    });

    MapSettingToCheckBox(ui->checkBoxExitOnLastTabClosed, &ApplicationSettings::exitOnLastTabClosed, &ApplicationSettings::setExitOnLastTabClosed, &ApplicationSettings::exitOnLastTabClosedChanged);

    ui->fcbDefaultFont->setCurrentFont(QFont(settings->fontName()));
    connect(ui->fcbDefaultFont, &QFontComboBox::currentFontChanged, this, [=](const QFont &f) {
        settings->setFontName(f.family());
    });
    connect(settings, &ApplicationSettings::fontNameChanged, this, [=](QString fontName){
        ui->fcbDefaultFont->setCurrentFont(QFont(fontName));
    });

    ui->spbDefaultFontSize->setValue(settings->fontSize());
    connect(ui->spbDefaultFontSize, QOverload<int>::of(&QSpinBox::valueChanged), settings, &ApplicationSettings::setFontSize);
    connect(settings, &ApplicationSettings::fontSizeChanged, ui->spbDefaultFontSize, &QSpinBox::setValue);

    ui->comboBoxLineEndings->addItem(tr("System Default"), QString(""));
    ui->comboBoxLineEndings->addItem(tr("Windows (CR LF)"), ScintillaNext::eolModeToString(SC_EOL_CRLF));
    ui->comboBoxLineEndings->addItem(tr("Linux (LF)"), ScintillaNext::eolModeToString(SC_EOL_LF));
    ui->comboBoxLineEndings->addItem(tr("Macintosh (CR)"), ScintillaNext::eolModeToString(SC_EOL_CR));

    // Select the current one
    int index = ui->comboBoxLineEndings->findData(settings->defaultEOLMode());
    ui->comboBoxLineEndings->setCurrentIndex(index == -1 ? 0 : index);

    connect(ui->comboBoxLineEndings, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int index) {
        settings->setDefaultEOLMode(ui->comboBoxLineEndings->itemData(index).toString());
    });
    connect(settings, &ApplicationSettings::defaultEOLModeChanged, this, [=](QString defaultEOLMode) {
        int index = ui->comboBoxLineEndings->findData(defaultEOLMode);
        ui->comboBoxLineEndings->setCurrentIndex(index == -1 ? 0 : index);
    });

    MapSettingToCheckBox(ui->checkBoxHighlightURLs, &ApplicationSettings::urlHighlighting, &ApplicationSettings::setURLHighlighting, &ApplicationSettings::urlHighlightingChanged);
    MapSettingToCheckBox(ui->checkBoxShowLineNumbers, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers, &ApplicationSettings::showLineNumbersChanged);
    MapSettingToCheckBox(ui->checkBoxFontHinting, &ApplicationSettings::fontHinting, &ApplicationSettings::setFontHinting, &ApplicationSettings::fontHintingChanged);


    QButtonGroup *buttonGroup = new QButtonGroup(this);
    buttonGroup->addButton(ui->radioFollowCurrentDirectory, ApplicationSettings::FollowCurrentDocument);
    buttonGroup->addButton(ui->radioLastUsedDirectory, ApplicationSettings::RememberLastUsed);
    buttonGroup->addButton(ui->radioHardCoded, ApplicationSettings::HardCoded);

    connect(buttonGroup, &QButtonGroup::idClicked, this, [=](int id) {
        ApplicationSettings::DefaultDirectoryBehaviorEnum e = static_cast<ApplicationSettings::DefaultDirectoryBehaviorEnum>(id);
        settings->setDefaultDirectoryBehavior(e);
    });

    connect(ui->radioHardCoded, &QRadioButton::toggled, this, [=](bool checked){
        ui->btnSelectHardCodedPath->setEnabled(checked);
        ui->txtHardCodedPath->setEnabled(checked);
    });

    connect(ui->btnSelectHardCodedPath, &QToolButton::clicked, this, [=]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Default Directory"));
        if (dir.isEmpty()) return; // user cancelled

        settings->setDefaultDirectory(QDir::fromNativeSeparators(dir));
        ui->txtHardCodedPath->setText(QDir::toNativeSeparators(dir));
    });

    connect(ui->txtHardCodedPath, &QLineEdit::editingFinished, this, [=]() {
        QString dir = ui->txtHardCodedPath->text();
        settings->setDefaultDirectory(QDir::fromNativeSeparators(dir));
        ui->txtHardCodedPath->setText(QDir::toNativeSeparators(dir));
    });

    if (auto b = buttonGroup->button(settings->defaultDirectoryBehavior())) {
        b->setChecked(true);
    }

    if (settings->defaultDirectoryBehavior() == ApplicationSettings::HardCoded) {
        ui->txtHardCodedPath->setText((QDir::toNativeSeparators(settings->defaultDirectory())));
    }
    else {
        ui->txtHardCodedPath->setText(QString());
    }

    // --- Shell setting UI ---
#ifdef Q_OS_WIN
    // Windows: combo box with detected shells + Custom option
    struct ShellEntry { const char *name; const char *exe; };
    static constexpr ShellEntry knownShells[] = {
        {"PowerShell 7 (pwsh)", "pwsh.exe"},
        {"Windows PowerShell",  "powershell.exe"},
        {"Command Prompt",      "cmd.exe"},
    };
    for (const auto &entry : knownShells) {
        if (!QStandardPaths::findExecutable(QString::fromLatin1(entry.exe)).isEmpty()) {
            ui->comboBoxShell->addItem(QString::fromLatin1(entry.name), QString::fromLatin1(entry.exe));
        }
    }
    ui->comboBoxShell->addItem(tr("Custom..."), QStringLiteral("__custom__"));

    auto syncCustomRowVisibility = [=]() {
        const bool isCustom = ui->comboBoxShell->currentData().toString() == QLatin1String("__custom__");
        ui->labelCustomShell->setVisible(isCustom);
        ui->lineEditShellCommand->setVisible(isCustom);
        ui->btnBrowseShell->setVisible(isCustom);
    };

    const QString currentShell = settings->shellCommand();
    int idx = ui->comboBoxShell->findData(currentShell);
    if (idx == -1) {
        idx = ui->comboBoxShell->findData(QStringLiteral("__custom__"));
        ui->lineEditShellCommand->setText(currentShell);
    }
    ui->comboBoxShell->setCurrentIndex(idx);
    syncCustomRowVisibility();

    connect(ui->comboBoxShell, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        const QString data = ui->comboBoxShell->currentData().toString();
        syncCustomRowVisibility();
        if (data != QLatin1String("__custom__")) {
            settings->setShellCommand(data);
        }
    });

    connect(ui->lineEditShellCommand, &QLineEdit::editingFinished, this, [=]() {
        if (ui->comboBoxShell->currentData().toString() == QLatin1String("__custom__")) {
            settings->setShellCommand(ui->lineEditShellCommand->text());
        }
    });
    connect(settings, &ApplicationSettings::shellCommandChanged, this, [=](const QString &s) {
        int i = ui->comboBoxShell->findData(s);
        if (i != -1) {
            ui->comboBoxShell->setCurrentIndex(i);
        } else {
            ui->comboBoxShell->setCurrentIndex(ui->comboBoxShell->findData(QStringLiteral("__custom__")));
            if (ui->lineEditShellCommand->text() != s) {
                ui->lineEditShellCommand->setText(s);
            }
        }
    });

    connect(ui->btnBrowseShell, &QToolButton::clicked, this, [=]() {
        const QString filter = tr("Executables (*.exe);;All files (*)");
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose Shell"), ui->lineEditShellCommand->text(), filter);
        if (!path.isEmpty()) {
            const QString native = QDir::toNativeSeparators(path);
            ui->lineEditShellCommand->setText(native);
            settings->setShellCommand(native);
        }
    });
#else
    // Non-Windows: hide combo, show only line edit + browse (old behavior)
    ui->comboBoxShell->setVisible(false);
    ui->labelShellCommand->setVisible(false);
    ui->labelCustomShell->setText(tr("Shell command"));

    ui->lineEditShellCommand->setText(settings->shellCommand());
    connect(ui->lineEditShellCommand, &QLineEdit::editingFinished, this, [=]() {
        settings->setShellCommand(ui->lineEditShellCommand->text());
    });
    connect(settings, &ApplicationSettings::shellCommandChanged, this, [=](const QString &s) {
        if (ui->lineEditShellCommand->text() != s) {
            ui->lineEditShellCommand->setText(s);
        }
    });

    connect(ui->btnBrowseShell, &QToolButton::clicked, this, [=]() {
        const QString filter = tr("All files (*)");
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose Shell"), ui->lineEditShellCommand->text(), filter);
        if (!path.isEmpty()) {
            const QString native = QDir::toNativeSeparators(path);
            ui->lineEditShellCommand->setText(native);
            settings->setShellCommand(native);
        }
    });
#endif

    connect(ui->btnChooseTerminalFont, &QPushButton::clicked, this, [=]() {
        QFont current;
        const QString stored = settings->terminalFont();
        if (stored.isEmpty() || !current.fromString(stored)) {
            current = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        }
        bool ok = false;
        const QFont chosen = QFontDialog::getFont(&ok, current, this, tr("Terminal Font"));
        if (ok) {
            settings->setTerminalFont(chosen.toString());
        }
    });

    // --- Data Directory section ------------------------------------------------

    ui->lineEditDataDir->setText(QDir::toNativeSeparators(DataPaths::appDataLocation()));
    ui->labelDataDirSourceValue->setText(DataPaths::sourceLabel());

    const bool isOverridden = (DataPaths::source() == DataPaths::Source::CLI
                               || DataPaths::source() == DataPaths::Source::Env
                               || DataPaths::source() == DataPaths::Source::Portable);
    ui->btnBrowseDataDir->setEnabled(!isOverridden);

    connect(ui->btnBrowseDataDir, &QToolButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Choose Data Directory"),
            QDir::toNativeSeparators(DataPaths::baseDir()));
        if (dir.isEmpty()) return;

        const QString newAppData = QDir::cleanPath(dir) + QStringLiteral("/NotepadAI");
        if (QDir::cleanPath(newAppData) == QDir::cleanPath(DataPaths::appDataLocation())) {
            return;
        }

        // Check if target already has data
        QDir targetDir(newAppData);
        if (targetDir.exists() && !targetDir.isEmpty()) {
            const int choice = QMessageBox::question(
                this, tr("Data Directory"),
                tr("The directory already contains data.\n\n"
                   "Use existing data (no copy), overwrite with current data, or cancel?"),
                tr("Use Existing"), tr("Overwrite"), tr("Cancel"),
                0, 2);
            if (choice == 2) return;
            if (choice == 0) {
                writeBootstrapDataDir(dir);
                offerRestart();
                return;
            }
        }

        // Copy current data to new location
        if (!targetDir.exists()) targetDir.mkpath(QStringLiteral("."));

        const QDir sourceDir(DataPaths::appDataLocation());
        bool copyOk = true;
        const auto entries = sourceDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            const QString srcPath = sourceDir.absoluteFilePath(entry);
            const QString dstPath = targetDir.absoluteFilePath(entry);
            QFileInfo fi(srcPath);
            if (fi.isDir()) {
                copyOk = QDir(dstPath).mkpath(QStringLiteral("."));
                if (copyOk) {
                    QDir subDir(srcPath);
                    const auto subEntries = subDir.entryList(QDir::Files);
                    for (const QString &subEntry : subEntries) {
                        QFile::remove(targetDir.absoluteFilePath(entry + QLatin1Char('/') + subEntry));
                        copyOk = QFile::copy(subDir.absoluteFilePath(subEntry),
                                             targetDir.absoluteFilePath(entry + QLatin1Char('/') + subEntry));
                        if (!copyOk) break;
                    }
                }
            } else {
                QFile::remove(dstPath);
                copyOk = QFile::copy(srcPath, dstPath);
            }
            if (!copyOk) {
                QMessageBox::warning(this, tr("Data Directory"),
                    tr("Failed to copy data to the new directory.\n"
                       "The data directory has not been changed."));
                return;
            }
        }

        writeBootstrapDataDir(dir);
        offerRestart();
    });

    // --- AI provider settings --------------------------------------------------

    // Add a note explaining which features use this configuration.
    {
        auto *noteLabel = new QLabel(
            tr("Used by: AI Commit Message, Prompt Improver. "
               "More features will use this provider in the future."),
            this);
        noteLabel->setWordWrap(true);
        noteLabel->setStyleSheet(QStringLiteral(
            "color: palette(placeholder-text); font-size: 11px; margin-bottom: 4px;"));
        ui->formLayoutAi->insertRow(0, noteLabel);
    }

    ui->lineEditAiUrl->setText(settings->commitMessageProviderUrl());
    connect(ui->lineEditAiUrl, &QLineEdit::editingFinished, this, [=]() {
        settings->setCommitMessageProviderUrl(ui->lineEditAiUrl->text().trimmed());
    });
    connect(settings, &ApplicationSettings::commitMessageProviderUrlChanged, this, [=](const QString &s) {
        if (ui->lineEditAiUrl->text() != s) ui->lineEditAiUrl->setText(s);
    });

    ui->lineEditAiModel->setText(settings->commitMessageModel());
    connect(ui->lineEditAiModel, &QLineEdit::editingFinished, this, [=]() {
        settings->setCommitMessageModel(ui->lineEditAiModel->text().trimmed());
    });
    connect(settings, &ApplicationSettings::commitMessageModelChanged, this, [=](const QString &s) {
        if (ui->lineEditAiModel->text() != s) ui->lineEditAiModel->setText(s);
    });

    // API key — the value itself never round-trips through the UI. The line
    // edit is write-only (Password mode + placeholder), and a status label
    // reports whether a key is configured + by which mechanism.
    NotepadNextApplication *npApp = qobject_cast<NotepadNextApplication *>(qApp);
    ai::CredentialStore *credStore = npApp ? npApp->getCredentialStore() : nullptr;

    auto refreshApiKeyStatus = [=]() {
        QString text;
        const bool envOverride = !qEnvironmentVariableIsEmpty("NOTEPADAI_COMMIT_API_KEY")
                                 || !qEnvironmentVariableIsEmpty("NOTEPADAI_COMMIT_API_KEY_FILE");
        const bool configured = settings->commitMessageApiKeyConfigured();
        const bool backendOk = credStore ? credStore->isBackendAvailable() : false;
        QString color = QStringLiteral("palette(mid)");
        if (envOverride) {
            text = tr("Using key from environment variable (NOTEPADAI_COMMIT_API_KEY[_FILE]).");
        } else if (configured && backendOk) {
            text = tr("Key stored in OS keychain.");
        } else if (configured && !backendOk) {
            text = tr("Key flagged as stored but OS keychain backend is unavailable.");
            color = QStringLiteral("#c0392b");
        } else if (!backendOk) {
            text = tr("OS keychain backend unavailable — set NOTEPADAI_COMMIT_API_KEY to use AI generation.");
            color = QStringLiteral("#c0392b");
        } else {
            text = tr("No key configured.");
        }
        ui->labelAiApiKeyStatus->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(color));
        ui->labelAiApiKeyStatus->setText(text);
        ui->btnAiClearApiKey->setEnabled(configured && backendOk);
    };
    refreshApiKeyStatus();
    if (credStore) {
        connect(credStore, &ai::CredentialStore::apiKeyConfiguredChanged,
                this, [=](bool) { refreshApiKeyStatus(); });
    }

    connect(ui->btnAiSaveApiKey, &QPushButton::clicked, this, [=]() {
        const QString value = ui->lineEditAiApiKey->text();
        if (value.isEmpty()) {
            QMessageBox::information(this, tr("API key"),
                                     tr("Enter a key before saving."));
            return;
        }
        if (!credStore) {
            QMessageBox::warning(this, tr("API key"),
                                 tr("Credential store is not available in this build."));
            return;
        }
        QString err;
        if (!credStore->storeApiKey(value, &err)) {
            QMessageBox::warning(this, tr("API key"),
                                 tr("Failed to store key: %1").arg(err));
            return;
        }
        ui->lineEditAiApiKey->clear();
        refreshApiKeyStatus();
    });

    connect(ui->btnAiClearApiKey, &QPushButton::clicked, this, [=]() {
        if (!credStore) return;
        if (QMessageBox::question(this, tr("Clear API key"),
                tr("Remove the stored API key from the OS keychain?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            return;
        }
        QString err;
        credStore->clearApiKey(&err);
        refreshApiKeyStatus();
    });

    ui->plainTextEditAiPromptTemplate->setPlainText(settings->commitMessagePromptTemplate());
    connect(ui->plainTextEditAiPromptTemplate, &QPlainTextEdit::textChanged, this, [=]() {
        const QString cur = ui->plainTextEditAiPromptTemplate->toPlainText();
        if (cur != settings->commitMessagePromptTemplate()) {
            settings->setCommitMessagePromptTemplate(cur);
        }
    });
    connect(settings, &ApplicationSettings::commitMessagePromptTemplateChanged, this, [=](const QString &s) {
        if (ui->plainTextEditAiPromptTemplate->toPlainText() != s) {
            ui->plainTextEditAiPromptTemplate->setPlainText(s);
        }
    });
    connect(ui->btnAiResetPromptTemplate, &QPushButton::clicked, this, [=]() {
        // setCommitMessagePromptTemplate("") then re-read the default —
        // ApplicationSettings substitutes the built-in default for empty values.
        settings->remove(QStringLiteral("Ai/CommitMessagePromptTemplate"));
        ui->plainTextEditAiPromptTemplate->setPlainText(settings->commitMessagePromptTemplate());
    });

    ui->spinBoxAiDiffBudget->setValue(settings->commitMessageDiffByteBudget());
    connect(ui->spinBoxAiDiffBudget, QOverload<int>::of(&QSpinBox::valueChanged),
            settings, &ApplicationSettings::setCommitMessageDiffByteBudget);
    connect(settings, &ApplicationSettings::commitMessageDiffByteBudgetChanged,
            ui->spinBoxAiDiffBudget, &QSpinBox::setValue);

    ui->spinBoxAiRulesBudget->setValue(settings->commitMessageRulesByteBudget());
    connect(ui->spinBoxAiRulesBudget, QOverload<int>::of(&QSpinBox::valueChanged),
            settings, &ApplicationSettings::setCommitMessageRulesByteBudget);
    connect(settings, &ApplicationSettings::commitMessageRulesByteBudgetChanged,
            ui->spinBoxAiRulesBudget, &QSpinBox::setValue);

    ui->spinBoxAiIdleTimeout->setValue(settings->commitMessageStreamIdleTimeoutSec());
    connect(ui->spinBoxAiIdleTimeout, QOverload<int>::of(&QSpinBox::valueChanged),
            settings, &ApplicationSettings::setCommitMessageStreamIdleTimeoutSec);
    connect(settings, &ApplicationSettings::commitMessageStreamIdleTimeoutSecChanged,
            ui->spinBoxAiIdleTimeout, &QSpinBox::setValue);

    ui->keySequenceEditAiShortcut->setKeySequence(QKeySequence(settings->commitMessageGenerateShortcut()));
    connect(ui->keySequenceEditAiShortcut, &QKeySequenceEdit::keySequenceChanged, this, [=](const QKeySequence &ks) {
        settings->setCommitMessageGenerateShortcut(ks.toString(QKeySequence::PortableText));
    });
    connect(settings, &ApplicationSettings::commitMessageGenerateShortcutChanged, this, [=](const QString &s) {
        const QKeySequence ks(s);
        if (ui->keySequenceEditAiShortcut->keySequence() != ks) {
            ui->keySequenceEditAiShortcut->setKeySequence(ks);
        }
    });
}

PreferencesDialog::~PreferencesDialog()
{
    delete ui;
}

void PreferencesDialog::showApplicationRestartRequired() const
{
    ui->labelAppRestartIcon->show();
    ui->labelAppRestart->show();
}

template<typename Func1, typename Func2, typename Func3>
void PreferencesDialog::MapSettingToCheckBox(QCheckBox *checkBox, Func1 getter, Func2 setter, Func3 notifier) const
{
    // Get the value and set the checkbox state
    checkBox->setChecked(std::bind(getter, settings)());

    // Set up two way connection
    connect(settings, notifier, checkBox, &QCheckBox::setChecked);
    connect(checkBox, &QCheckBox::toggled, settings, setter);
}

template<typename Func1, typename Func2, typename Func3>
void PreferencesDialog::MapSettingToGroupBox(QGroupBox *groupBox, Func1 getter, Func2 setter, Func3 notifier) const
{
    // Get the value and set the checkbox state
    groupBox->setChecked(std::bind(getter, settings)());

    // Set up two way connection
    connect(settings, notifier, groupBox, &QGroupBox::setChecked);
    connect(groupBox, &QGroupBox::toggled, settings, setter);
}

void PreferencesDialog::populateTranslationComboBox()
{
    NotepadNextApplication *app = qobject_cast<NotepadNextApplication *>(qApp);

    // Add the system default at the top
    ui->comboBoxTranslation->addItem(tr("<System Default>"), QStringLiteral(""));

    // Under test harnesses qApp is a plain QApplication; skip enumerating translations.
    if (!app) return;

    // TODO: sort this list and keep the system default at the top
    for (const auto &localeName : app->getTranslationManager()->availableTranslations())
    {
        QLocale locale(localeName);
        const QString localeDisplay = TranslationManager::FormatLocaleTerritoryAndLanguage(locale);
        ui->comboBoxTranslation->addItem(localeDisplay, localeName);
    }

    // Select the current one
    int index = ui->comboBoxTranslation->findData(settings->translation());
    if (index != -1) {
        ui->comboBoxTranslation->setCurrentIndex(index);
    }
}

void PreferencesDialog::writeBootstrapDataDir(const QString &baseDir)
{
    QSettings bootstrap(QSettings::IniFormat, QSettings::UserScope,
                        QStringLiteral("NotepadAI"), QStringLiteral("NotepadAI"));
    bootstrap.setValue(QStringLiteral("App/DataDir"), baseDir);
    if (bootstrap.status() != QSettings::NoError) {
        QMessageBox::warning(this, tr("Data Directory"),
            tr("Cannot save data directory preference to the default settings file.\n"
               "Use --data-dir flag or NOTEPADAI_DATA_DIR environment variable instead."));
    }
}

void PreferencesDialog::offerRestart()
{
    const int result = QMessageBox::question(
        this, tr("Restart Required"),
        tr("The data directory has been changed. Restart now to apply?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (result == QMessageBox::Yes) {
        const QString exe = QCoreApplication::applicationFilePath();
        QProcess::startDetached(exe, QCoreApplication::arguments().mid(1));
        QCoreApplication::quit();
    } else {
        showApplicationRestartRequired();
    }
}
