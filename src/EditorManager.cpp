/*
 * This file is part of Notepad Next.
 * Copyright 2021 Justin Dailey
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

#include <QApplication>
#include <QFileInfo>
#include <QMainWindow>
#include <QPointer>
#include <QTimer>

#include "ApplicationSettings.h"

#include "DarkPalette.h"
#include "EditorManager.h"
#include "ScintillaNext.h"
#include "Scintilla.h"

// Editor decorators
#include "BraceMatch.h"
#include "HighlightedScrollBar.h"
#include "SmartHighlighter.h"
#include "SurroundSelection.h"
#include "LineNumbers.h"
#include "BetterMultiSelection.h"
#include "AutoIndentation.h"
#include "AutoCompletion.h"
#include "URLFinder.h"
#include "BookMarkDecorator.h"
#include "GitGutterDecorator.h"
#include "InlineBlameDecorator.h"
#include "EditorMinimap.h"
#include "HTMLAutoCompleteDecorator.h"
#include "JustfileRecipeHighlighter.h"
#include "ProfileScope.h"
#include "TaskRunnerGutter.h"
#include "TerminalManager.h"

// Glyph-hinting toggle lives in Scintilla's Qt platform layer (vendored). Read
// when a QFont is realised. Forward-declared rather than including PlatQt.h to
// avoid pulling Scintilla's internal platform headers into this translation unit.
namespace Scintilla::Internal { void SetEditorFontHintingEnabled(bool enabled) noexcept; }


const int MARK_HIDELINESBEGIN = 23;
const int MARK_HIDELINESEND = 22;
const int MARK_HIDELINESUNDERLINE = 21;

// Property key used to stash the per-style lexer foregrounds before the
// dark-mode DarkPalette transform mutates them in-place. Restored on a
// dark→light transition so the user sees the lexer's original light-mode
// colors instead of dark-lightened ones on a white background.
namespace { constexpr const char *kLexerFgCacheProp = "nade.lexerFg"; }


EditorManager::EditorManager(ApplicationSettings *settings, QObject *parent)
    : QObject(parent), settings(settings)
{
    // Seed the Scintilla platform-layer hinting flag before any editor (and thus
    // any QFont) is realised, so the very first paint already honours the setting.
    Scintilla::Internal::SetEditorFontHintingEnabled(settings->fontHinting());

    connect(this, &EditorManager::editorCreated, this, [=](ScintillaNext *editor) {
        connect(editor, &ScintillaNext::closed, this, [=]() {
            emit editorClosed(editor);
        });
    });

    connect(settings, &ApplicationSettings::showWrapSymbolChanged, this, [=](bool b) {
        for (auto &editor : getEditors()) {
            editor->setWrapVisualFlags(b ? SC_WRAPVISUALFLAG_END : SC_WRAPVISUALFLAG_NONE);
        }
    });


    connect(settings, &ApplicationSettings::showWhitespaceChanged, this, [=](bool b) {
        // TODO: could make SCWS_VISIBLEALWAYS configurable via settings. Probably not worth
        // taking up menu space e.g. show all, show leading, show trailing
        for (auto &editor : getEditors()) {
            editor->setViewWS(b ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
        }
    });

    connect(settings, &ApplicationSettings::showEndOfLineChanged, this, [=](bool b) {
        for (auto &editor : getEditors()) {
            editor->setViewEOL(b);
        }
    });

    connect(settings, &ApplicationSettings::showIndentGuideChanged, this, [=](bool b) {
        for (auto &editor : getEditors()) {
            editor->setIndentationGuides(b ? SC_IV_LOOKBOTH : SC_IV_NONE);
        }
    });

    connect(settings, &ApplicationSettings::wordWrapChanged, this, [=](bool b) {
        if (b) {
            for (auto &editor : getEditors()) {
                editor->setWrapMode(SC_WRAP_WORD);
            }
        }
        else {
            for (auto &editor : getEditors()) {
                // Store the top line and restore it after the lines have been unwrapped
                int topLine = editor->docLineFromVisible(editor->firstVisibleLine());
                editor->setWrapMode(SC_WRAP_NONE);
                editor->setFirstVisibleLine(topLine);
            }
        }
    });

    connect(settings, &ApplicationSettings::fontNameChanged, this, [=](const QString &fontName){
        for (auto &editor : getEditors()) {
            for (int i = 0; i <= STYLE_MAX; ++i) {
                editor->styleSetFont(i, fontName.toUtf8().data());
            }
        }
    });

    connect(settings, &ApplicationSettings::fontSizeChanged, this, [=](int fontSize){
        for (auto &editor : getEditors()) {
            for (int i = 0; i <= STYLE_MAX; ++i) {
                editor->styleSetSize(i, fontSize);
            }
        }
    });

    connect(settings, &ApplicationSettings::fontHintingChanged, this, [=](bool enabled){
        // Update the process-wide flag, then force Scintilla to re-realise every
        // font: hinting is baked into the QFont at allocation time, so the new
        // value only takes effect when the font cache is rebuilt. setFontQuality
        // with the current value is a semantic no-op that triggers
        // InvalidateStyleRedraw -> DropGraphics, which discards and recreates all
        // realised fonts on the next paint. Cheaper than re-issuing styleSetFont
        // across every style, and correct for all of them at once.
        Scintilla::Internal::SetEditorFontHintingEnabled(enabled);
        for (auto &editor : getEditors()) {
            editor->setFontQuality(editor->fontQuality());
        }
    });

    connect(settings, &ApplicationSettings::urlHighlightingChanged, this, [=](bool b){
        for (auto &editor : getEditors()) {
            URLFinder *decorator = editor->findChild<URLFinder *>(QString(), Qt::FindDirectChildrenOnly);
            if (decorator) {
                decorator->setEnabled(b);
            }
        }
    });

    connect(settings, &ApplicationSettings::showLineNumbersChanged, this, [=](bool b){
        for (auto &editor : getEditors()) {
            // Diff views own margin 0 themselves (per-row old/new file line numbers
            // rendered by GitDiffPainter). Their LineNumbers decorator was disabled
            // at creation; don't let the user's "Show Line Numbers" toggle clobber it.
            if (isDiffView(editor)) continue;
            LineNumbers *decorator = editor->findChild<LineNumbers *>(QString(), Qt::FindDirectChildrenOnly);
            if (decorator) {
                decorator->setEnabled(b);
            }
        }
    });

    connect(settings, &ApplicationSettings::gitGutterEnabledChanged, this, [=](bool b){
        for (auto &editor : getEditors()) {
            if (isDiffView(editor)) continue;
            GitGutterDecorator *d = editor->findChild<GitGutterDecorator *>(QString(), Qt::FindDirectChildrenOnly);
            if (!d) continue;
            d->setEnabled(b);
            if (b && editor->isFile()) d->refresh();
        }
    });

    connect(settings, &ApplicationSettings::inlineBlameEnabledChanged, this, [=](bool b){
        for (auto &editor : getEditors()) {
            if (isDiffView(editor)) continue;
            InlineBlameDecorator *d = editor->findChild<InlineBlameDecorator *>(QString(), Qt::FindDirectChildrenOnly);
            if (!d) continue;
            d->setEnabled(b);
            if (b && editor->isFile()) d->refresh();
        }
    });

    connect(settings, &ApplicationSettings::minimapEnabledChanged, this, [=](bool b){
        for (auto &editor : getEditors()) {
            if (isDiffView(editor)) continue;
            EditorMinimap *m = editor->findChild<EditorMinimap *>(QString(), Qt::FindDirectChildrenOnly);
            if (b) {
                if (!m) new EditorMinimap(editor); // self-installs
            } else if (m) {
                m->deleteLater();
            }
        }
    });
}

ScintillaNext *EditorManager::createEditor(const QString &name)
{
    ScintillaNext *editor = new ScintillaNext(name);

    manageEditor(editor);

    return editor;
}

ScintillaNext *EditorManager::createEditorFromFile(const QString &filePath, bool tryToCreate)
{
    PROFILE_SCOPE("EditorManager::createEditorFromFile");
    ScintillaNext *editor = ScintillaNext::fromFile(filePath, tryToCreate);

    if (editor) {
        manageEditor(editor);
    }

    return editor;
}

ScintillaNext *EditorManager::getEditorByFilePath(const QString &filePath)
{
    QFileInfo newInfo(filePath);
    newInfo.makeAbsolute();
    const QString canonicalNew = newInfo.absoluteFilePath();

    purgeOldEditorPointers();

    for (ScintillaNext *editor : qAsConst(editors)) {
        if (!editor->isFile()) continue;
        // Fast path: case-insensitive string compare avoids the expensive
        // GetFileInformationByHandle syscall that QFileInfo::operator== uses.
        if (editor->getFileInfo().absoluteFilePath().compare(canonicalNew, Qt::CaseInsensitive) == 0) {
            return editor;
        }
    }

    return Q_NULLPTR;
}

void EditorManager::manageEditor(ScintillaNext *editor)
{
    editors.append(QPointer<ScintillaNext>(editor));

    setupEditor(editor);

    emit editorCreated(editor);
}

void EditorManager::setupEditor(ScintillaNext *editor)
{
    PROFILE_SCOPE("EditorManager::setupEditor");
    qInfo(Q_FUNC_INFO);

    {
    PROFILE_SCOPE("EditorManager::setupEditor.scintillaConfig");

    editor->clearCmdKey(SCK_INSERT);

    editor->setFoldMarkers(QStringLiteral("box"));

    editor->setIdleStyling(SC_IDLESTYLING_TOVISIBLE);
    editor->setEndAtLastLine(false);

    editor->setMultipleSelection(true);
    editor->setAdditionalSelectionTyping(true);
    editor->setMultiPaste(SC_MULTIPASTE_EACH);
    editor->setVirtualSpaceOptions(SCVS_RECTANGULARSELECTION);

    editor->setMarginLeft(2);

    editor->setMarginWidthN(0, 30);
    editor->setMarginMaskN(1, (1<<MARK_HIDELINESBEGIN) | (1<<MARK_HIDELINESEND) | (1<<MARK_HIDELINESUNDERLINE));
    editor->setMarginMaskN(2, SC_MASK_FOLDERS);
    editor->setMarginWidthN(2, 14);

    editor->markerDefine(MARK_HIDELINESUNDERLINE, SC_MARK_UNDERLINE);

    editor->markerDefine(MARK_HIDELINESBEGIN, SC_MARK_ARROW);
    editor->markerDefine(MARK_HIDELINESEND, SC_MARK_ARROWDOWN);

    editor->setMarginSensitiveN(2, true);

    editor->setFoldFlags(SC_FOLDFLAG_LINEAFTER_CONTRACTED);
    editor->setScrollWidthTracking(true);
    editor->setScrollWidth(1);

    editor->setTabDrawMode(SCTD_STRIKEOUT);
    editor->setTabWidth(4);
    editor->setBackSpaceUnIndents(true);

    editor->setCaretLineVisible(true);
    editor->setCaretLineVisibleAlways(true);
    editor->setCaretWidth(2);

    editor->setWhitespaceSize(2);

    editor->setAutomaticFold(SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_CHANGE);
    editor->markerEnableHighlight(true);

    editor->setCharsDefault();
    editor->setWordChars(editor->wordChars() + settings->additionalWordChars().toLatin1());

    } // EditorManager::setupEditor.scintillaConfig

    {
    PROFILE_SCOPE("EditorManager::setupEditor.applyTheme");
    applyThemeToEditor(editor, darkTheme, /*initialSetup=*/true);
    }

    // Lua's SetLanguage writes per-style fore/back from the language definition,
    // which hardcodes white backgrounds. Re-skin after the language is fully
    // applied (lexerChanged is emitted *after* SetLanguage from
    // NotepadNextApplication::setEditorLanguage) so dark mode survives.
    connect(editor, &ScintillaNext::lexerChanged, this, [this, editor]() {
        // Diff views own their palette (GitDiffPainter); the chrome theme's
        // defaultBack does not match canvasBg, so applying it would corrupt
        // the diff colors until the next configureEditor call.
        if (isDiffView(editor)) return;
        applyThemeToEditor(editor, darkTheme, /*initialSetup=*/false);
    });

    editor->setViewWS(settings->showWhitespace() ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
    editor->setViewEOL(settings->showEndOfLine());
    editor->setWrapVisualFlags(settings->showWrapSymbol() ? SC_WRAPVISUALFLAG_END : SC_WRAPVISUALFLAG_NONE);
    editor->setIndentationGuides(settings->showIndentGuide() ? SC_IV_LOOKBOTH : SC_IV_NONE);
    editor->setWrapMode(settings->wordWrap() ? SC_WRAP_WORD : SC_WRAP_NONE);

    int detectedEOLMode = detectEOLMode(editor);
    if (detectedEOLMode == -1) {
        if (!settings->defaultEOLMode().isEmpty()) {
            const int eol = ScintillaNext::stringToEolMode(settings->defaultEOLMode().toLower());

            if (eol != -1)
                editor->setEOLMode(eol);
            else
                qWarning("Unexpected DefaultEOLMode: %s", qUtf8Printable(settings->defaultEOLMode()));
        }
        // else it will just stay whatever EOL mode it was when it was created
    }
    else {
        editor->setEOLMode(detectedEOLMode);
    }

    // Decorators — critical path (must exist before first paint)
    PROFILE_SCOPE("EditorManager::setupEditor.decorators");

    LineNumbers *l = new LineNumbers(editor);
    l->setEnabled(settings->showLineNumbers());

    HighlightedScrollBarDecorator *h = new HighlightedScrollBarDecorator(editor);
    h->setEnabled(true);

    BookMarkDecorator *bm = new BookMarkDecorator(editor);
    bm->setEnabled(true);

    // Defer non-essential decorators to the next event-loop tick so the
    // editor tab appears instantly. These decorators enhance editing but
    // are not needed for the initial paint.
    QPointer<ScintillaNext> guard(editor);
    const bool urlHighlighting = settings->urlHighlighting();
    const bool gitGutterEnabled = settings->gitGutterEnabled();
    const bool inlineBlameEnabled = settings->inlineBlameEnabled();
    const bool minimapEnabled = settings->minimapEnabled();
    const bool isFile = editor->isFile();

    QTimer::singleShot(0, editor, [this, guard, urlHighlighting, gitGutterEnabled,
                                    inlineBlameEnabled, minimapEnabled, isFile]() {
        if (!guard) return;
        ScintillaNext *editor = guard.data();

        SmartHighlighter *s = new SmartHighlighter(editor);
        s->setEnabled(true);

        BraceMatch *b = new BraceMatch(editor);
        b->setEnabled(true);

        SurroundSelection *ss = new SurroundSelection(editor);
        ss->setEnabled(true);

        BetterMultiSelection *bms = new BetterMultiSelection(editor);
        bms->setEnabled(true);

        AutoIndentation *ai = new AutoIndentation(editor);
        ai->setEnabled(true);

        AutoCompletion *ac = new AutoCompletion(editor);
        ac->setEnabled(true);

        URLFinder *uf = new URLFinder(editor);
        uf->setEnabled(urlHighlighting);

        JustfileRecipeHighlighter *jrh = new JustfileRecipeHighlighter(editor);
        jrh->setEnabled(true);

        const bool isDiff = isDiffView(editor);

        GitGutterDecorator *gg = nullptr;
        InlineBlameDecorator *blame = nullptr;
        if (!isDiff) {
            gg = new GitGutterDecorator(editor);
            gg->setEnabled(gitGutterEnabled);

            blame = new InlineBlameDecorator(editor);
            blame->setEnabled(inlineBlameEnabled);
            connect(blame, &InlineBlameDecorator::commitClicked,
                    this, &EditorManager::blameCommitClicked);
        }

        // Defer git decorator refreshes further so the deferred batch
        // itself doesn't block on QProcess spawns.
        if (isFile && !isDiff) {
            QTimer::singleShot(0, editor, [gg, blame, gitGutterEnabled, inlineBlameEnabled]() {
                if (gitGutterEnabled) gg->refresh();
                if (inlineBlameEnabled) blame->refresh();
            });
        }

        if (minimapEnabled) {
            new EditorMinimap(editor);
        }

        new HTMLAutoCompleteDecorator(editor);

        // Task runner gutter: create if the file is a supported task file.
        evaluateTaskRunner(editor);

        // Re-evaluate on rename (Save As to different filename)
        connect(editor, &ScintillaNext::renamed, this, [this, guard]() {
            if (!guard) return;
            evaluateTaskRunner(guard.data());
        });
    });
}

void EditorManager::applyTheme(bool dark)
{
    const bool transitionToLight = darkTheme && !dark;
    darkTheme = dark;

    for (auto &editor : getEditors()) {
        if (!editor) continue;

        // Diff views own their palette via GitDiffPainter::configureEditor and
        // are re-skinned by GitDiffViewController::setDarkPalette on the
        // effectiveThemeChanged signal. Running applyThemeToEditor on them
        // would overwrite canvasBg/canvasFg with the chrome's defaultBack
        // (visible as gray banding under the diff content) until the diff
        // controller's slot fires next tick.
        if (isDiffView(editor)) continue;

        if (transitionToLight) {
            // Coming from dark, where applyThemeToEditor lightened the lexer
            // foregrounds in-place. The original lexer-written values were
            // cached on entry to the dark transform — restore them now so
            // the user gets the lexer's light-mode palette instead of the
            // dark-lightened one painted on a white background.
            //
            // ScintillaEdit shadows QObject::setProperty with its own Scintilla
            // string-property overload, so we qualify with QObject:: to reach
            // Qt's dynamic-property API.
            const QVariant cached = editor->QObject::property(kLexerFgCacheProp);
            if (cached.isValid()) {
                const QByteArray bytes = cached.toByteArray();
                if (bytes.size() == static_cast<int>((STYLE_MAX + 1) * sizeof(int))) {
                    const int *p = reinterpret_cast<const int*>(bytes.constData());
                    for (int i = 0; i <= STYLE_MAX; ++i) {
                        editor->styleSetFore(i, p[i]);
                    }
                }
                editor->QObject::setProperty(kLexerFgCacheProp, QVariant());
            }
        }

        applyThemeToEditor(editor, dark, /*initialSetup=*/false);
    }
}

void EditorManager::applyThemeToEditor(ScintillaNext *editor, bool dark, bool initialSetup)
{
    // Scintilla colors are 0xBBGGRR; setElementColour takes 0xAABBGGRR.
    const int defaultFore     = dark ? 0xD4D4D4 : 0x000000;
    const int defaultBack     = dark ? 0x1E1E1E : 0xFFFFFF;
    const int lineNumberFore  = dark ? 0x858585 : 0x808080;
    const int lineNumberBack  = dark ? 0x252526 : 0xE4E4E4;
    const int braceLightFore  = dark ? 0x00A5FF : 0x0000FF;
    const int braceBadFore    = dark ? 0x4040FF : 0x000080;
    const int indentGuideFore = dark ? 0x404040 : 0xC0C0C0;
    const int foldMarginColour   = dark ? 0x252526 : 0xFFFFFF;
    const int foldMarginHiColour = dark ? 0x2D2D30 : 0xE9E9E9;
    const int foldMarkerFore = dark ? 0x1E1E1E : 0xF3F3F3;
    const int foldMarkerBack = dark ? 0xB0B0B0 : 0x808080;
    const int edgeColour     = dark ? 0x404040 : 0x80FFFF;
    const int hideLinesUnderlineBack = dark ? 0x4D9D4D : 0x77CC77;

    const unsigned int caretLineBack   = dark ? 0xFF2E2D2AU : 0xFFFFE8E8U;
    const unsigned int selInactiveBack = dark ? 0xFF504030U : 0xFFE0E0E0U;
    const unsigned int whiteSpaceFore  = dark ? 0xFF606060U : 0xFFD0D0D0U;
    const unsigned int foldLine        = dark ? 0xFF5A5A5AU : 0xFFA0A0A0U;
    // Caret: Scintilla defaults to opaque black, which is invisible on the
    // dark editor background. Match defaultFore so it stays visible in both
    // modes. Format is 0xAABBGGRR.
    const unsigned int caretFore           = dark ? 0xFFD4D4D4U : 0xFF000000U;
    const unsigned int caretAdditionalFore = dark ? 0xFFA0A0A0U : 0xFF606060U;
    // Selection: Scintilla defaults to a washed-out grey/white in dark mode
    // that buries text under it. Use VSCode's dark-selection blue (#264F78)
    // so highlighted text stays readable; light mode gets the pale blue we
    // already use elsewhere (#ADD6FF). Format is 0xAABBGGRR.
    const unsigned int selectionBack           = dark ? 0xFF784F26U : 0xFFFFD6ADU;
    const unsigned int selectionAdditionalBack = dark ? 0xFF6A4520U : 0xFFEFCBA3U;

    editor->styleSetFore(STYLE_DEFAULT, defaultFore);
    editor->styleSetBack(STYLE_DEFAULT, defaultBack);
    editor->styleSetSize(STYLE_DEFAULT, settings->fontSize());
    editor->styleSetFont(STYLE_DEFAULT, settings->fontName().toUtf8().data());

    if (initialSetup) {
        // Propagate the default style to all other styles. Destructive — only
        // safe before a lexer's Lua SetStyle runs.
        editor->styleClearAll();
    } else {
        // Lexer styles already carry meaningful foregrounds; only force the
        // background so dark mode is preserved.
        for (int i = 0; i <= STYLE_MAX; ++i) {
            editor->styleSetBack(i, defaultBack);
        }
    }

    // Per-language Lua files hard-code foregrounds for a white background.
    // After the lexer runs, rewrite every per-style foreground through the
    // pure transform so e.g. dark-green comments and pure-black identifiers
    // become readable on #1E1E1E. Chrome styles (line number, brace, indent
    // guide) are re-asserted below so this loop never wins over them.
    //
    // Snapshot the pre-transform values onto the editor first: that's the
    // original lexer fg (lexerChanged is the only path that mutates style
    // fgs between calls), and we'll restore it on the dark→light transition
    // so the lexer's light-mode palette is recovered without re-running it.
    if (dark && !initialSetup) {
        QByteArray cache((STYLE_MAX + 1) * static_cast<int>(sizeof(int)), Qt::Uninitialized);
        int *p = reinterpret_cast<int*>(cache.data());
        for (int i = 0; i <= STYLE_MAX; ++i) {
            p[i] = static_cast<int>(editor->styleFore(i));
        }
        editor->QObject::setProperty(kLexerFgCacheProp, cache);
        for (int i = 0; i <= STYLE_MAX; ++i) {
            editor->styleSetFore(i, DarkPalette::lightenSciForeground(p[i]));
        }
    }

    editor->styleSetFore(STYLE_LINENUMBER, lineNumberFore);
    editor->styleSetBack(STYLE_LINENUMBER, lineNumberBack);
    editor->styleSetBold(STYLE_LINENUMBER, false);

    editor->styleSetFore(STYLE_BRACELIGHT, braceLightFore);
    editor->styleSetBack(STYLE_BRACELIGHT, defaultBack);

    editor->styleSetFore(STYLE_BRACEBAD, braceBadFore);
    editor->styleSetBack(STYLE_BRACEBAD, defaultBack);

    editor->styleSetFore(STYLE_INDENTGUIDE, indentGuideFore);
    editor->styleSetBack(STYLE_INDENTGUIDE, defaultBack);

    for (int i = SC_MARKNUM_FOLDEREND; i <= SC_MARKNUM_FOLDEROPEN; ++i) {
        editor->markerSetFore(i, foldMarkerFore);
        editor->markerSetBack(i, foldMarkerBack);
        editor->markerSetBackSelected(i, 0x0000FF);
    }

    editor->markerSetBack(MARK_HIDELINESUNDERLINE, hideLinesUnderlineBack);

    editor->setEdgeColour(edgeColour);

    // https://www.scintilla.org/ScintillaDoc.html#ElementColours
    editor->setElementColour(SC_ELEMENT_SELECTION_BACK, selectionBack);
    editor->setElementColour(SC_ELEMENT_SELECTION_ADDITIONAL_BACK, selectionAdditionalBack);
    editor->setElementColour(SC_ELEMENT_SELECTION_INACTIVE_BACK, selInactiveBack);
    editor->setElementColour(SC_ELEMENT_CARET_LINE_BACK, caretLineBack);
    editor->setElementColour(SC_ELEMENT_CARET, caretFore);
    editor->setElementColour(SC_ELEMENT_CARET_ADDITIONAL, caretAdditionalFore);
    editor->setElementColour(SC_ELEMENT_WHITE_SPACE, whiteSpaceFore);
    editor->setElementColour(SC_ELEMENT_FOLD_LINE, foldLine);

    editor->setFoldMarginColour(true, foldMarginColour);
    editor->setFoldMarginHiColour(true, foldMarginHiColour);
}

void EditorManager::evaluateTaskRunner(ScintillaNext *editor)
{
    if (!editor) return;
    if (!editor->isFile()) return;
    if (isDiffView(editor)) return;

    const QString filePath = editor->getFilePath();
    const QString filename = QFileInfo(filePath).fileName();
    const bool shouldHave = TaskRunnerGutter::isTaskFile(filename);

    auto *existing = editor->findChild<TaskRunnerGutter *>(QString(), Qt::FindDirectChildrenOnly);

    if (shouldHave && !existing) {
        // Find TerminalManager via the main window
        TerminalManager *termMgr = nullptr;
        const auto topLevels = QApplication::topLevelWidgets();
        for (QWidget *w : topLevels) {
            if (auto *mw = qobject_cast<QMainWindow *>(w)) {
                termMgr = mw->findChild<TerminalManager *>();
                if (termMgr) break;
            }
        }
        if (termMgr) {
            new TaskRunnerGutter(editor, termMgr);
        }
    } else if (!shouldHave && existing) {
        existing->deleteLater();
    }
}

void EditorManager::purgeOldEditorPointers()
{
    QMutableListIterator<QPointer<ScintillaNext>> it(editors);

    while (it.hasNext()) {
        QPointer<ScintillaNext> pointer = it.next();
        if (pointer.isNull())
            it.remove();
    }
}

QList<QPointer<ScintillaNext> > EditorManager::getEditors()
{
    purgeOldEditorPointers();
    return editors;
}

void EditorManager::registerAsDiffView(ScintillaNext *editor)
{
    if (!editor) return;
    m_diffViews.insert(editor);
    // Auto-untrack on destruction. Capturing the raw pointer is safe because
    // QObject::destroyed fires before the pointer is invalidated for QSet ops.
    connect(editor, &QObject::destroyed, this, [this](QObject *obj) {
        m_diffViews.remove(static_cast<const ScintillaNext *>(obj));
    });
}

bool EditorManager::isDiffView(const ScintillaNext *editor) const
{
    return m_diffViews.contains(editor);
}

int EditorManager::detectEOLMode(ScintillaNext *editor) const
{
    qInfo(Q_FUNC_INFO);

    const int MAX_BYTES_TO_CHECK = 10 * 1024;
    const int MIN_LINE_THRESHOLD = 3;

    const int len = qMin(MAX_BYTES_TO_CHECK, static_cast<int>(editor->length()));
    if (len == 0) return -1;

    const QByteArray buf = editor->get_text_range(0, len);
    const char *data = buf.constData();
    const int n = buf.size();

    int lf = 0, cr = 0, crlf = 0;

    for (int i = 0; i < n; ++i) {
        if (data[i] == '\r') {
            if (i + 1 < n && data[i + 1] == '\n') {
                ++crlf;
                ++i; // skip the \n
            } else {
                ++cr;
            }
        } else if (data[i] == '\n') {
            ++lf;
        }

        if (crlf >= MIN_LINE_THRESHOLD) return SC_EOL_CRLF;
        if (cr >= MIN_LINE_THRESHOLD) return SC_EOL_CR;
        if (lf >= MIN_LINE_THRESHOLD) return SC_EOL_LF;
    }

    if (crlf > cr && crlf > lf) return SC_EOL_CRLF;
    if (cr > lf) return SC_EOL_CR;
    if (lf > 0) return SC_EOL_LF;

    return -1;
}
