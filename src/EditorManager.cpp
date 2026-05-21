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
#include "HTMLAutoCompleteDecorator.h"
#include "JustfileRecipeHighlighter.h"


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

    connect(settings, &ApplicationSettings::fontNameChanged, this, [=](QString fontName){
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
            LineNumbers *decorator = editor->findChild<LineNumbers *>(QString(), Qt::FindDirectChildrenOnly);
            if (decorator) {
                decorator->setEnabled(b);
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

    purgeOldEditorPointers();

    for (ScintillaNext *editor : qAsConst(editors)) {
        if (editor->isFile() && editor->getFileInfo() == newInfo) {
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
    qInfo(Q_FUNC_INFO);

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

    applyThemeToEditor(editor, darkTheme, /*initialSetup=*/true);

    // Lua's SetLanguage writes per-style fore/back from the language definition,
    // which hardcodes white backgrounds. Re-skin after the language is fully
    // applied (lexerChanged is emitted *after* SetLanguage from
    // NotepadNextApplication::setEditorLanguage) so dark mode survives.
    connect(editor, &ScintillaNext::lexerChanged, this, [this, editor]() {
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

    // Decorators
    SmartHighlighter *s = new SmartHighlighter(editor);
    s->setEnabled(true);

    HighlightedScrollBarDecorator *h = new HighlightedScrollBarDecorator(editor);
    h->setEnabled(true);

    BraceMatch *b = new BraceMatch(editor);
    b->setEnabled(true);

    LineNumbers *l = new LineNumbers(editor);
    l->setEnabled(settings->showLineNumbers());

    SurroundSelection *ss = new SurroundSelection(editor);
    ss->setEnabled(true);

    BetterMultiSelection *bms = new BetterMultiSelection(editor);
    bms->setEnabled(true);

    AutoIndentation *ai = new AutoIndentation(editor);
    ai->setEnabled(true);

    AutoCompletion *ac = new AutoCompletion(editor);
    ac->setEnabled(true);

    URLFinder *uf = new URLFinder(editor);
    uf->setEnabled(settings->urlHighlighting());

    JustfileRecipeHighlighter *jrh = new JustfileRecipeHighlighter(editor);
    jrh->setEnabled(true);

    BookMarkDecorator *bm = new BookMarkDecorator(editor);
    bm->setEnabled(true);

    new HTMLAutoCompleteDecorator(editor);
}

void EditorManager::applyTheme(bool dark)
{
    const bool transitionToLight = darkTheme && !dark;
    darkTheme = dark;

    for (auto &editor : getEditors()) {
        if (!editor) continue;

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

int EditorManager::detectEOLMode(ScintillaNext *editor) const
{
    qInfo(Q_FUNC_INFO);

    const int MIN_LINE_THRESHOLD = 3;
    const int MAX_BYTES_TO_CHECK = 10*1024;

    int index = 0;
    int lf = 0;
    int cr = 0;
    int crlf = 0;
    int chPrev = ' ';
    int chNext = editor->charAt(index);

    for (int i = 0; i < qMin(MAX_BYTES_TO_CHECK, (int) editor->length()); ++i) {
        int ch = chNext;
        chNext = editor->charAt(i + 1);

        if (ch == '\r') {
            if (chNext == '\n') crlf++;
            else cr++;
        }
        else if (ch == '\n') {
            if (chPrev != '\r') lf++;
        }

        chPrev = ch;

        // If any meet some minimum threshold then just declare victory
        if (crlf == MIN_LINE_THRESHOLD) return SC_EOL_CRLF;
        else if (cr == MIN_LINE_THRESHOLD) return SC_EOL_CR;
        else if (lf == MIN_LINE_THRESHOLD) return SC_EOL_LF;
    }

    // There are either no lines or only a few, so make a best effort determination

    if (crlf > cr && crlf > lf) {
        return SC_EOL_CRLF;
    }
    else if (cr > lf) {
        return SC_EOL_CR;
    }
    else if (lf > cr) {
        return SC_EOL_LF;
    }
    else {
        return -1;
    }
}
