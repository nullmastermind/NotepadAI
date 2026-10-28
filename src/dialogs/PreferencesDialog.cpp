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
#include "MainWindow.h"
#include "DataPaths.h"
#include "NotepadNextApplication.h"
#include "TranslationManager.h"
#include "UnfocusedWheelFilter.h"
#include "ai/CredentialStore.h"
#include "ui_PreferencesDialog.h"
#include "ScintillaNext.h"
#include "TerminalColorScheme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFontDialog>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>


PreferencesDialog::PreferencesDialog(ApplicationSettings *settings, QWidget *parent) :
    QDialog(parent, Qt::Tool),
    ui(new Ui::PreferencesDialog),
    settings(settings),
    m_pending(settings)
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
    connect(ui->comboBoxTranslation, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        m_pending.markDirty();
        showApplicationRestartRequired();
    });
    m_pending.addApply([=]() {
        settings->setTranslation(ui->comboBoxTranslation->currentData().toString());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->comboBoxTranslation);
        const int index = ui->comboBoxTranslation->findData(settings->translation());
        ui->comboBoxTranslation->setCurrentIndex(index != -1 ? index : 0);
        hideApplicationRestartRequired();
    });

    ui->comboBoxTheme->addItem(tr("Follow System"), static_cast<int>(ApplicationSettings::System));
    ui->comboBoxTheme->addItem(tr("Light"),         static_cast<int>(ApplicationSettings::Light));
    ui->comboBoxTheme->addItem(tr("Dark"),          static_cast<int>(ApplicationSettings::Dark));
    {
        int themeIndex = ui->comboBoxTheme->findData(static_cast<int>(settings->theme()));
        ui->comboBoxTheme->setCurrentIndex(themeIndex == -1 ? 0 : themeIndex);
    }
    connect(ui->comboBoxTheme, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setTheme(static_cast<ApplicationSettings::ThemeEnum>(
            ui->comboBoxTheme->currentData().toInt()));
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->comboBoxTheme);
        int idx = ui->comboBoxTheme->findData(static_cast<int>(settings->theme()));
        ui->comboBoxTheme->setCurrentIndex(idx == -1 ? 0 : idx);
    });

    MapSettingToCheckBox(ui->checkBoxExitOnLastTabClosed, &ApplicationSettings::exitOnLastTabClosed, &ApplicationSettings::setExitOnLastTabClosed, &ApplicationSettings::exitOnLastTabClosedChanged);

    ui->fcbDefaultFont->setCurrentFont(QFont(settings->fontName()));
    connect(ui->fcbDefaultFont, &QFontComboBox::currentFontChanged, this, [=](const QFont &) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setFontName(ui->fcbDefaultFont->currentFont().family());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->fcbDefaultFont);
        ui->fcbDefaultFont->setCurrentFont(QFont(settings->fontName()));
    });

    ui->spbDefaultFontSize->setValue(settings->fontSize());
    connect(ui->spbDefaultFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setFontSize(ui->spbDefaultFontSize->value());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->spbDefaultFontSize);
        ui->spbDefaultFontSize->setValue(settings->fontSize());
    });

    ui->comboBoxLineEndings->addItem(tr("System Default"), QString(""));
    ui->comboBoxLineEndings->addItem(tr("Windows (CR LF)"), ScintillaNext::eolModeToString(SC_EOL_CRLF));
    ui->comboBoxLineEndings->addItem(tr("Linux (LF)"), ScintillaNext::eolModeToString(SC_EOL_LF));
    ui->comboBoxLineEndings->addItem(tr("Macintosh (CR)"), ScintillaNext::eolModeToString(SC_EOL_CR));

    // Select the current one
    int index = ui->comboBoxLineEndings->findData(settings->defaultEOLMode());
    ui->comboBoxLineEndings->setCurrentIndex(index == -1 ? 0 : index);

    connect(ui->comboBoxLineEndings, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setDefaultEOLMode(ui->comboBoxLineEndings->currentData().toString());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->comboBoxLineEndings);
        int index = ui->comboBoxLineEndings->findData(settings->defaultEOLMode());
        ui->comboBoxLineEndings->setCurrentIndex(index == -1 ? 0 : index);
    });

    MapSettingToCheckBox(ui->checkBoxHighlightURLs, &ApplicationSettings::urlHighlighting, &ApplicationSettings::setURLHighlighting, &ApplicationSettings::urlHighlightingChanged);
    MapSettingToCheckBox(ui->checkBoxShowLineNumbers, &ApplicationSettings::showLineNumbers, &ApplicationSettings::setShowLineNumbers, &ApplicationSettings::showLineNumbersChanged);
    MapSettingToCheckBox(ui->checkBoxAutoCompletion, &ApplicationSettings::autoCompletion, &ApplicationSettings::setAutoCompletion, &ApplicationSettings::autoCompletionChanged);
    MapSettingToCheckBox(ui->checkBoxFontHinting, &ApplicationSettings::fontHinting, &ApplicationSettings::setFontHinting, &ApplicationSettings::fontHintingChanged);

    // --- Chat Font section -----------------------------------------------------
    // Mirrors the Default Font wiring above. "Use default font" (checked by
    // default) makes the AI chat follow the editor's Default Font; unchecking it
    // enables a separate family/size/sharpen for the chat only.
    MapSettingToCheckBox(ui->checkBoxChatUseDefaultFont, &ApplicationSettings::chatFontUseDefault, &ApplicationSettings::setChatFontUseDefault, &ApplicationSettings::chatFontUseDefaultChanged);

    ui->fcbChatFont->setCurrentFont(QFont(settings->chatFontFamily()));
    connect(ui->fcbChatFont, &QFontComboBox::currentFontChanged, this, [=](const QFont &) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setChatFontFamily(ui->fcbChatFont->currentFont().family());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->fcbChatFont);
        ui->fcbChatFont->setCurrentFont(QFont(settings->chatFontFamily()));
    });

    ui->spbChatFontSize->setValue(settings->chatFontSizePt());
    connect(ui->spbChatFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setChatFontSizePt(ui->spbChatFontSize->value());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->spbChatFontSize);
        ui->spbChatFontSize->setValue(settings->chatFontSizePt());
    });

    MapSettingToCheckBox(ui->checkBoxChatFontHinting, &ApplicationSettings::chatFontSharpen, &ApplicationSettings::setChatFontSharpen, &ApplicationSettings::chatFontSharpenChanged);

    // The 3 custom controls (+ their labels) are only meaningful when NOT
    // following the editor font. Keep them in sync from both sides (the checkbox
    // itself and any external settings change) so the enabled state never drifts.
    auto syncChatFontEnabled = [=]() {
        const bool custom = !ui->checkBoxChatUseDefaultFont->isChecked();
        ui->fcbChatFont->setEnabled(custom);
        ui->spbChatFontSize->setEnabled(custom);
        ui->checkBoxChatFontHinting->setEnabled(custom);
        ui->labelChatFont->setEnabled(custom);
        ui->labelChatFontSize->setEnabled(custom);
    };
    syncChatFontEnabled();
    connect(ui->checkBoxChatUseDefaultFont, &QCheckBox::toggled, this, syncChatFontEnabled);

    QButtonGroup *buttonGroup = new QButtonGroup(this);
    buttonGroup->addButton(ui->radioFollowCurrentDirectory, ApplicationSettings::FollowCurrentDocument);
    buttonGroup->addButton(ui->radioLastUsedDirectory, ApplicationSettings::RememberLastUsed);
    buttonGroup->addButton(ui->radioHardCoded, ApplicationSettings::HardCoded);

    connect(buttonGroup, &QButtonGroup::idClicked, this, [=](int) {
        m_pending.markDirty();
    });

    connect(ui->radioHardCoded, &QRadioButton::toggled, this, [=](bool checked){
        ui->btnSelectHardCodedPath->setEnabled(checked);
        ui->txtHardCodedPath->setEnabled(checked);
    });

    connect(ui->btnSelectHardCodedPath, &QToolButton::clicked, this, [=]() {
        QString dir = QFileDialog::getExistingDirectory(this, tr("Default Directory"));
        if (dir.isEmpty()) return; // user cancelled

        ui->txtHardCodedPath->setText(QDir::toNativeSeparators(dir));
        m_pending.markDirty();
    });

    connect(ui->txtHardCodedPath, &QLineEdit::editingFinished, this, [=]() {
        ui->txtHardCodedPath->setText(QDir::toNativeSeparators(
            QDir::fromNativeSeparators(ui->txtHardCodedPath->text())));
        m_pending.markDirty();
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

    m_pending.addApply([=]() {
        settings->setDefaultDirectoryBehavior(
            static_cast<ApplicationSettings::DefaultDirectoryBehaviorEnum>(buttonGroup->checkedId()));
        settings->setDefaultDirectory(QDir::fromNativeSeparators(ui->txtHardCodedPath->text()));
    });
    m_pending.addRevert([=]() {
        if (auto b = buttonGroup->button(settings->defaultDirectoryBehavior())) {
            QSignalBlocker blocker(b);
            b->setChecked(true);
        }
        QSignalBlocker pathBlocker(ui->txtHardCodedPath);
        if (settings->defaultDirectoryBehavior() == ApplicationSettings::HardCoded) {
            ui->txtHardCodedPath->setText(QDir::toNativeSeparators(settings->defaultDirectory()));
        } else {
            ui->txtHardCodedPath->setText(QString());
        }
        ui->btnSelectHardCodedPath->setEnabled(ui->radioHardCoded->isChecked());
        ui->txtHardCodedPath->setEnabled(ui->radioHardCoded->isChecked());
    });

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
        syncCustomRowVisibility();
        m_pending.markDirty();
    });

    connect(ui->lineEditShellCommand, &QLineEdit::editingFinished, this, [=]() {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        const QString data = ui->comboBoxShell->currentData().toString();
        if (data == QLatin1String("__custom__")) {
            settings->setShellCommand(ui->lineEditShellCommand->text());
        } else {
            settings->setShellCommand(data);
        }
    });
    m_pending.addRevert([=]() {
        QSignalBlocker comboBlocker(ui->comboBoxShell);
        QSignalBlocker lineBlocker(ui->lineEditShellCommand);
        const QString currentShell = settings->shellCommand();
        int i = ui->comboBoxShell->findData(currentShell);
        if (i != -1) {
            ui->comboBoxShell->setCurrentIndex(i);
        } else {
            ui->comboBoxShell->setCurrentIndex(ui->comboBoxShell->findData(QStringLiteral("__custom__")));
            ui->lineEditShellCommand->setText(currentShell);
        }
        syncCustomRowVisibility();
    });

    connect(ui->btnBrowseShell, &QToolButton::clicked, this, [=]() {
        const QString filter = tr("Executables (*.exe);;All files (*)");
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose Shell"), ui->lineEditShellCommand->text(), filter);
        if (!path.isEmpty()) {
            ui->lineEditShellCommand->setText(QDir::toNativeSeparators(path));
            m_pending.markDirty();
        }
    });
#else
    // Non-Windows: hide combo, show only line edit + browse (old behavior)
    ui->comboBoxShell->setVisible(false);
    ui->labelShellCommand->setVisible(false);
    ui->labelCustomShell->setText(tr("Shell command"));

    ui->lineEditShellCommand->setText(settings->shellCommand());
    connect(ui->lineEditShellCommand, &QLineEdit::editingFinished, this, [=]() {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setShellCommand(ui->lineEditShellCommand->text());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->lineEditShellCommand);
        ui->lineEditShellCommand->setText(settings->shellCommand());
    });

    connect(ui->btnBrowseShell, &QToolButton::clicked, this, [=]() {
        const QString filter = tr("All files (*)");
        const QString path = QFileDialog::getOpenFileName(this, tr("Choose Shell"), ui->lineEditShellCommand->text(), filter);
        if (!path.isEmpty()) {
            ui->lineEditShellCommand->setText(QDir::toNativeSeparators(path));
            m_pending.markDirty();
        }
    });
#endif

    m_pendingTerminalFont = settings->terminalFont();
    // The button names the font it would change, e.g. "Cascadia Mono, 10 pt".
    auto showTerminalFont = [=, this]() {
        QFont f;
        if (!m_pendingTerminalFont.isEmpty() && f.fromString(m_pendingTerminalFont)) {
            ui->btnChooseTerminalFont->setText(tr("%1, %2 pt").arg(f.family()).arg(f.pointSizeF()));
        } else {
            ui->btnChooseTerminalFont->setText(tr("Choose font..."));
        }
    };
    showTerminalFont();
    connect(ui->btnChooseTerminalFont, &QPushButton::clicked, this, [=]() {
        QFont current;
        const QString stored = m_pendingTerminalFont.isEmpty() ? settings->terminalFont() : m_pendingTerminalFont;
        if (stored.isEmpty() || !current.fromString(stored)) {
            current = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        }
        bool ok = false;
        const QFont chosen = QFontDialog::getFont(&ok, current, this, tr("Terminal Font"));
        if (ok) {
            m_pendingTerminalFont = chosen.toString();
            showTerminalFont();
            m_pending.markDirty();
        }
    });
    m_pending.addApply([=]() {
        settings->setTerminalFont(m_pendingTerminalFont);
    });
    m_pending.addRevert([=]() {
        m_pendingTerminalFont = settings->terminalFont();
        showTerminalFont();
    });

    // --- Terminal appearance and behaviour (applied live to open terminals) ---
    // Rows go between the font and the memory limit: shell + limit frame the
    // look-and-feel settings, which apply to every open terminal on Apply.
    QFormLayout *terminalForm = ui->formLayoutTerminal;
    // New rows go right after the font row, before the memory limit.
    int terminalRow = 0;
    terminalForm->getWidgetPosition(ui->btnChooseTerminalFont, &terminalRow, nullptr);
    ++terminalRow;
    auto addTerminalRow = [&](const QString &label, QWidget *field, const QString &toolTip) {
        auto *labelWidget = new QLabel(label, ui->groupBoxTerminal);
        labelWidget->setBuddy(field);
        labelWidget->setToolTip(toolTip);
        field->setToolTip(toolTip);
        terminalForm->insertRow(terminalRow++, labelWidget, field);
    };

    auto *lineHeight = new QSpinBox(ui->groupBoxTerminal);
    lineHeight->setRange(100, 200);
    lineHeight->setSingleStep(5);
    lineHeight->setSuffix(QStringLiteral("%"));
    addTerminalRow(tr("Line height"), lineHeight,
                   tr("Row height as a percentage of the font height."));

    auto *colorScheme = new QComboBox(ui->groupBoxTerminal);
    colorScheme->addItem(tr("Follow app theme"), QStringLiteral("app"));
    for (const QString &id : TerminalColorScheme::presetIds()) {
        colorScheme->addItem(TerminalColorScheme::presetName(id), id);
    }
    addTerminalRow(tr("Color scheme"), colorScheme,
                   tr("Terminal text colours. Following the app theme uses the editor's background "
                      "and switches with dark/light mode; a preset keeps its own colours."));

    auto *contrast = new QSpinBox(ui->groupBoxTerminal);
    contrast->setRange(0, 100);
    contrast->setSpecialValueText(tr("Off"));
    addTerminalRow(tr("Minimum contrast"), contrast,
                   tr("Text in the 16 basic colours is lifted until it reaches this APCA contrast "
                      "against its background (45 is comfortable). Colours a program picks exactly "
                      "are left alone."));

    auto *cursorShape = new QComboBox(ui->groupBoxTerminal);
    cursorShape->addItem(tr("Block"), QStringLiteral("block"));
    cursorShape->addItem(tr("Bar"), QStringLiteral("bar"));
    cursorShape->addItem(tr("Underline"), QStringLiteral("underline"));
    addTerminalRow(tr("Cursor"), cursorShape,
                   tr("Default cursor shape. Programs such as vim may change it while they run."));

    auto *cursorBlink = new QComboBox(ui->groupBoxTerminal);
    cursorBlink->addItem(tr("When the program asks"), QStringLiteral("terminal"));
    cursorBlink->addItem(tr("Always"), QStringLiteral("on"));
    cursorBlink->addItem(tr("Never"), QStringLiteral("off"));
    addTerminalRow(tr("Cursor blink"), cursorBlink, tr("Whether the cursor blinks while the terminal has focus."));

    auto *scrollback = new QSpinBox(ui->groupBoxTerminal);
    scrollback->setRange(0, 100000);
    scrollback->setSingleStep(1000);
    scrollback->setGroupSeparatorShown(true);
    scrollback->setSpecialValueText(tr("None"));
    scrollback->setSuffix(tr(" lines"));
    addTerminalRow(tr("Scrollback"), scrollback,
                   tr("Lines of history kept per terminal. Task terminals always keep the maximum."));

    auto *copyOnSelect = new QCheckBox(tr("Copy selected text to the clipboard"), ui->groupBoxTerminal);
    addTerminalRow(tr("Copy on select"), copyOnSelect, tr("Selecting text also copies it, without Ctrl+C."));

    auto *bellSound = new QCheckBox(tr("Play the system sound"), ui->groupBoxTerminal);
    addTerminalRow(tr("Bell"), bellSound,
                   tr("A bell in a tab you are not looking at always marks the tab with a dot."));

    auto *showEmptyPanel = new QCheckBox(tr("Show the empty panel when a workspace opens"),
                                         ui->groupBoxTerminal);
    addTerminalRow(tr("Empty panel"), showEmptyPanel,
                   tr("Reserves the terminal area with a panel offering a new terminal and the "
                      "workspace's tasks, until a terminal is opened."));

    lineHeight->setObjectName(QStringLiteral("spinBoxTerminalLineHeight"));
    colorScheme->setObjectName(QStringLiteral("comboBoxTerminalColorScheme"));
    contrast->setObjectName(QStringLiteral("spinBoxTerminalMinimumContrast"));
    cursorShape->setObjectName(QStringLiteral("comboBoxTerminalCursorShape"));
    cursorBlink->setObjectName(QStringLiteral("comboBoxTerminalCursorBlink"));
    scrollback->setObjectName(QStringLiteral("spinBoxTerminalScrollback"));
    copyOnSelect->setObjectName(QStringLiteral("checkBoxTerminalCopyOnSelect"));
    bellSound->setObjectName(QStringLiteral("checkBoxTerminalBellSound"));
    showEmptyPanel->setObjectName(QStringLiteral("checkBoxTerminalShowEmptyPanel"));

    auto setComboData = [](QComboBox *combo, const QString &value) {
        const int i = combo->findData(value);
        combo->setCurrentIndex(i < 0 ? 0 : i);
    };
    auto loadTerminalAppearance = [=, this]() {
        const QSignalBlocker b1(lineHeight), b2(colorScheme), b3(contrast), b4(cursorShape),
            b5(cursorBlink), b6(scrollback), b7(copyOnSelect), b8(bellSound), b9(showEmptyPanel);
        lineHeight->setValue(settings->terminalLineHeightPct());
        setComboData(colorScheme, settings->terminalColorScheme());
        contrast->setValue(settings->terminalMinimumContrast());
        setComboData(cursorShape, settings->terminalCursorShape());
        setComboData(cursorBlink, settings->terminalCursorBlink());
        scrollback->setValue(settings->terminalScrollbackLines());
        copyOnSelect->setChecked(settings->terminalCopyOnSelect());
        bellSound->setChecked(settings->terminalBellSound());
        showEmptyPanel->setChecked(settings->terminalShowEmptyPanel());
    };
    loadTerminalAppearance();

    auto markDirty = [=, this]() { m_pending.markDirty(); };
    connect(lineHeight, QOverload<int>::of(&QSpinBox::valueChanged), this, markDirty);
    connect(contrast, QOverload<int>::of(&QSpinBox::valueChanged), this, markDirty);
    connect(scrollback, QOverload<int>::of(&QSpinBox::valueChanged), this, markDirty);
    for (QComboBox *combo : {colorScheme, cursorShape, cursorBlink}) {
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, markDirty);
    }
    connect(copyOnSelect, &QCheckBox::toggled, this, markDirty);
    connect(bellSound, &QCheckBox::toggled, this, markDirty);
    connect(showEmptyPanel, &QCheckBox::toggled, this, markDirty);

    m_pending.addApply([=, this]() {
        settings->setTerminalLineHeightPct(lineHeight->value());
        settings->setTerminalColorScheme(colorScheme->currentData().toString());
        settings->setTerminalMinimumContrast(contrast->value());
        settings->setTerminalCursorShape(cursorShape->currentData().toString());
        settings->setTerminalCursorBlink(cursorBlink->currentData().toString());
        settings->setTerminalScrollbackLines(scrollback->value());
        settings->setTerminalCopyOnSelect(copyOnSelect->isChecked());
        settings->setTerminalBellSound(bellSound->isChecked());
        settings->setTerminalShowEmptyPanel(showEmptyPanel->isChecked());
    });
    m_pending.addRevert(loadTerminalAppearance);

    ui->spinBoxTerminalMemoryLimit->setValue(settings->terminalChildMemoryLimitGb());
    connect(ui->spinBoxTerminalMemoryLimit, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setTerminalChildMemoryLimitGb(ui->spinBoxTerminalMemoryLimit->value());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->spinBoxTerminalMemoryLimit);
        ui->spinBoxTerminalMemoryLimit->setValue(settings->terminalChildMemoryLimitGb());
    });

    // --- Claude usage config folders -------------------------------------------

    ui->labelClaudeConfigDirsHint->setForegroundRole(QPalette::PlaceholderText);
    auto fillClaudeConfigDirs = [this](const QStringList &dirs) {
        ui->listClaudeConfigDirs->clear();
        for (const QString &dir : dirs) {
            auto *item = new QListWidgetItem(QDir::toNativeSeparators(dir), ui->listClaudeConfigDirs);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
    };
    auto claudeConfigDirsFromUi = [this]() {
        QStringList dirs;
        for (int i = 0; i < ui->listClaudeConfigDirs->count(); ++i) {
            const QString dir = ui->listClaudeConfigDirs->item(i)->text().trimmed();
            if (!dir.isEmpty())
                dirs.append(QDir::fromNativeSeparators(dir));
        }
        return dirs;
    };
    fillClaudeConfigDirs(settings->usageClaudeConfigDirs());
    ui->btnRemoveClaudeConfigDir->setEnabled(false);
    connect(ui->listClaudeConfigDirs, &QListWidget::itemSelectionChanged, this, [this]() {
        ui->btnRemoveClaudeConfigDir->setEnabled(!ui->listClaudeConfigDirs->selectedItems().isEmpty());
    });
    connect(ui->listClaudeConfigDirs, &QListWidget::itemChanged, this, [=](QListWidgetItem *) {
        m_pending.markDirty();
    });
    connect(ui->listClaudeConfigDirs->model(), &QAbstractItemModel::rowsMoved, this, [=]() {
        m_pending.markDirty();
    });
    connect(ui->btnAddClaudeConfigDir, &QPushButton::clicked, this, [=]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Claude Config Folder"), QDir::homePath());
        if (dir.isEmpty())
            return;
        const QString native = QDir::toNativeSeparators(dir);
        const QList<QListWidgetItem *> existing = ui->listClaudeConfigDirs->findItems(native, Qt::MatchFixedString);
        if (!existing.isEmpty()) {
            ui->listClaudeConfigDirs->setCurrentItem(existing.first());
            return;
        }
        auto *item = new QListWidgetItem(native, ui->listClaudeConfigDirs);
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        ui->listClaudeConfigDirs->setCurrentItem(item);
        m_pending.markDirty();
    });
    connect(ui->btnRemoveClaudeConfigDir, &QPushButton::clicked, this, [=]() {
        const QList<QListWidgetItem *> selected = ui->listClaudeConfigDirs->selectedItems();
        if (selected.isEmpty())
            return;
        qDeleteAll(selected);
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        // The setter always emits; an unchanged list keeps every account as is anyway.
        const QStringList dirs = claudeConfigDirsFromUi();
        if (dirs != settings->usageClaudeConfigDirs())
            settings->setUsageClaudeConfigDirs(dirs);
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->listClaudeConfigDirs);
        fillClaudeConfigDirs(settings->usageClaudeConfigDirs());
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
            tr("Used by: AI Commit Message, Prompt Improver, Mini App copilot. "
               "Goal Agent Custom API is a separate Anthropic /v1/messages config."),
            this);
        noteLabel->setWordWrap(true);
        noteLabel->setStyleSheet(QStringLiteral(
            "color: palette(placeholder-text); font-size: 11px; margin-bottom: 4px;"));
        ui->formLayoutAi->insertRow(0, noteLabel);
    }

    ui->comboBoxAiApiFormat->addItem(tr("OpenAI-compatible"),
                                     static_cast<int>(ApplicationSettings::OpenAiCompatible));
    ui->comboBoxAiApiFormat->addItem(tr("Anthropic"),
                                     static_cast<int>(ApplicationSettings::Anthropic));
    {
        const int idx = ui->comboBoxAiApiFormat->findData(
            static_cast<int>(settings->commitMessageApiFormat()));
        ui->comboBoxAiApiFormat->setCurrentIndex(idx == -1 ? 0 : idx);
    }
    auto refreshAiEndpointChrome = [=]() {
        const bool anthropic = ui->comboBoxAiApiFormat->currentData().toInt()
            == static_cast<int>(ApplicationSettings::Anthropic);
        if (anthropic) {
            ui->labelAiUrl->setText(tr("Anthropic endpoint"));
            ui->lineEditAiUrl->setPlaceholderText(QStringLiteral("https://api.anthropic.com"));
            ui->lineEditAiUrl->setToolTip(tr(
                "Base URL of an Anthropic Messages API (e.g. https://api.anthropic.com). "
                "The /v1/messages path is appended automatically."));
            ui->lineEditAiModel->setPlaceholderText(QStringLiteral("claude-opus-5"));
        } else {
            ui->labelAiUrl->setText(tr("OpenAI-compatible endpoint"));
            ui->lineEditAiUrl->setPlaceholderText(QStringLiteral("https://api.openai.com/v1"));
            ui->lineEditAiUrl->setToolTip(tr(
                "Base URL of any OpenAI-compatible Chat Completions API (e.g. https://api.openai.com/v1). "
                "The /chat/completions path is appended automatically."));
            ui->lineEditAiModel->setPlaceholderText(QStringLiteral("gpt-4o-mini"));
        }
    };
    connect(ui->comboBoxAiApiFormat, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int) {
        m_pending.markDirty();
        refreshAiEndpointChrome();
    });
    m_pending.addApply([=]() {
        settings->setCommitMessageApiFormat(static_cast<ApplicationSettings::AiApiFormatEnum>(
            ui->comboBoxAiApiFormat->currentData().toInt()));
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->comboBoxAiApiFormat);
        const int idx = ui->comboBoxAiApiFormat->findData(
            static_cast<int>(settings->commitMessageApiFormat()));
        ui->comboBoxAiApiFormat->setCurrentIndex(idx == -1 ? 0 : idx);
        refreshAiEndpointChrome();
    });
    refreshAiEndpointChrome();

    ui->lineEditAiUrl->setText(settings->commitMessageProviderUrl());
    connect(ui->lineEditAiUrl, &QLineEdit::textEdited, this, [=](const QString &) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setCommitMessageProviderUrl(ui->lineEditAiUrl->text().trimmed());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->lineEditAiUrl);
        ui->lineEditAiUrl->setText(settings->commitMessageProviderUrl());
    });

    ui->lineEditAiModel->setText(settings->commitMessageModel());
    connect(ui->lineEditAiModel, &QLineEdit::textEdited, this, [=](const QString &) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setCommitMessageModel(ui->lineEditAiModel->text().trimmed());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->lineEditAiModel);
        ui->lineEditAiModel->setText(settings->commitMessageModel());
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
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setCommitMessagePromptTemplate(ui->plainTextEditAiPromptTemplate->toPlainText());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->plainTextEditAiPromptTemplate);
        ui->plainTextEditAiPromptTemplate->setPlainText(settings->commitMessagePromptTemplate());
    });
    connect(ui->btnAiResetPromptTemplate, &QPushButton::clicked, this, [=]() {
        // setCommitMessagePromptTemplate("") then re-read the default —
        // ApplicationSettings substitutes the built-in default for empty values.
        const QString previous = settings->commitMessagePromptTemplate();
        settings->remove(QStringLiteral("Ai/CommitMessagePromptTemplate"));
        const QString def = settings->commitMessagePromptTemplate();
        settings->setCommitMessagePromptTemplate(previous);
        ui->plainTextEditAiPromptTemplate->setPlainText(def);
        m_pending.markDirty();
    });

    ui->spinBoxAiDiffBudget->setValue(settings->commitMessageDiffByteBudget());
    connect(ui->spinBoxAiDiffBudget, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setCommitMessageDiffByteBudget(ui->spinBoxAiDiffBudget->value());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->spinBoxAiDiffBudget);
        ui->spinBoxAiDiffBudget->setValue(settings->commitMessageDiffByteBudget());
    });

    ui->spinBoxAiRulesBudget->setValue(settings->commitMessageRulesByteBudget());
    connect(ui->spinBoxAiRulesBudget, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setCommitMessageRulesByteBudget(ui->spinBoxAiRulesBudget->value());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->spinBoxAiRulesBudget);
        ui->spinBoxAiRulesBudget->setValue(settings->commitMessageRulesByteBudget());
    });

    ui->spinBoxAiIdleTimeout->setValue(settings->commitMessageStreamIdleTimeoutSec());
    connect(ui->spinBoxAiIdleTimeout, QOverload<int>::of(&QSpinBox::valueChanged), this, [=](int) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setCommitMessageStreamIdleTimeoutSec(ui->spinBoxAiIdleTimeout->value());
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->spinBoxAiIdleTimeout);
        ui->spinBoxAiIdleTimeout->setValue(settings->commitMessageStreamIdleTimeoutSec());
    });

    ui->keySequenceEditAiShortcut->setKeySequence(QKeySequence(settings->commitMessageGenerateShortcut()));
    connect(ui->keySequenceEditAiShortcut, &QKeySequenceEdit::keySequenceChanged, this, [=](const QKeySequence &) {
        m_pending.markDirty();
    });
    m_pending.addApply([=]() {
        settings->setCommitMessageGenerateShortcut(
            ui->keySequenceEditAiShortcut->keySequence().toString(QKeySequence::PortableText));
    });
    m_pending.addRevert([=]() {
        QSignalBlocker blocker(ui->keySequenceEditAiShortcut);
        ui->keySequenceEditAiShortcut->setKeySequence(QKeySequence(settings->commitMessageGenerateShortcut()));
    });

    UnfocusedWheelFilter::installOnInputs(this);
    m_pending.clearDirty();
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

void PreferencesDialog::hideApplicationRestartRequired() const
{
    ui->labelAppRestartIcon->hide();
    ui->labelAppRestart->hide();
}

void PreferencesDialog::accept()
{
    if (m_pending.isDirty())
        m_pending.apply();
    QDialog::accept();
}

void PreferencesDialog::reject()
{
    if (m_pending.isDirty()) {
        const auto result = QMessageBox::question(
            this,
            tr("Unsaved Changes"),
            tr("Discard unsaved changes?"),
            QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (result != QMessageBox::Discard)
            return;
        m_pending.revert();
    }
    QDialog::reject();
}

template<typename Func1, typename Func2, typename Func3>
void PreferencesDialog::MapSettingToCheckBox(QCheckBox *checkBox, Func1 getter, Func2 setter, Func3)
{
    m_pending.mapCheckBox(checkBox, getter, setter);
}

template<typename Func1, typename Func2, typename Func3>
void PreferencesDialog::MapSettingToGroupBox(QGroupBox *groupBox, Func1 getter, Func2 setter, Func3)
{
    m_pending.mapGroupBox(groupBox, getter, setter);
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
        QWidget *owner = parentWidget();
        while (owner && owner->parentWidget())
            owner = owner->parentWidget();
        if (auto *mainWindow = qobject_cast<MainWindow *>(owner))
            mainWindow->requestRestart();
    } else {
        showApplicationRestartRequired();
    }
}
