#include <QTimer>

#include "JustfileRecipeHighlighter.h"


JustfileRecipeHighlighter::JustfileRecipeHighlighter(ScintillaNext *editor) :
    EditorDecorator(editor),
    timer(new QTimer(this))
{
    indicator = editor->allocateIndicator("justfile_recipe");

    // INDIC_TEXTFORE overrides the foreground colour of the text the
    // indicator covers, sitting on top of the lexer's styling. Pure red is
    // pre-lighten — EditorManager's dark-theme transform doesn't touch
    // indicator colours, so this lands as-is. Bright red reads on both
    // light and dark backgrounds.
    editor->indicSetStyle(indicator, INDIC_TEXTFORE);
    editor->indicSetFore(indicator, 0x4040FF); // 0xBBGGRR — a bright red

    timer->setInterval(150);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, &JustfileRecipeHighlighter::rescan);

    // Language can change mid-session (user picks a different language from
    // the menu, or detection re-runs after rename). Re-evaluate whenever
    // the lexer is swapped.
    connect(editor, &ScintillaNext::lexerChanged, this, &JustfileRecipeHighlighter::refreshForLanguage);
    connect(editor, &ScintillaNext::resized, timer, qOverload<>(&QTimer::start));

    connect(this, &EditorDecorator::stateChanged, this, [=](bool b) {
        if (b) {
            refreshForLanguage();
        } else {
            clear();
        }
    });
}

void JustfileRecipeHighlighter::refreshForLanguage()
{
    if (editor->languageName == QStringLiteral("Justfile")) {
        timer->start();
    } else {
        clear();
    }
}

void JustfileRecipeHighlighter::rescan()
{
    clear();

    if (editor->languageName != QStringLiteral("Justfile")) {
        return;
    }

    // Recipe target line:
    //   name:                       -> match
    //   name arg1 arg2:             -> match
    //   @name:                      -> match (quiet recipe)
    //   [private] name:             -> match (attribute on prev line, name still highlights)
    //   FOO := "bar"                -> NO match (we exclude := via the (?!=) lookahead)
    //   set shell := [...]          -> NO match (same)
    // The capture group is the bare identifier; we only colour that, not
    // parameter list or trailing chars.
    const QByteArray reg =
        QByteArrayLiteral(R"(^[\t ]*@?([A-Za-z_][A-Za-z0-9_-]*)(?:[\t ]+[^:\r\n]*)?[\t ]*:(?!=))");

    const int flags = SCFIND_REGEXP | SCFIND_CXX11REGEX;
    Sci_TextToFind ttf{};
    ttf.chrg = {0, static_cast<Sci_PositionCR>(editor->length())};
    ttf.lpstrText = reg.constData();
    ttf.chrgText = {-1, -1};

    editor->setIndicatorCurrent(indicator);

    while (editor->send(SCI_FINDTEXT, flags, reinterpret_cast<sptr_t>(&ttf)) != -1) {
        // FINDTEXT returns whole-match range in chrgText. To get just the
        // target name (capture group 1), scan forward from match start past
        // any leading whitespace / '@' to the first identifier char, then
        // run while it's an identifier char.
        int p = ttf.chrgText.cpMin;
        const int matchEnd = ttf.chrgText.cpMax;

        while (p < matchEnd) {
            const char c = static_cast<char>(editor->charAt(p));
            if (c == ' ' || c == '\t' || c == '@') {
                ++p;
                continue;
            }
            break;
        }
        const int nameStart = p;
        while (p < matchEnd) {
            const char c = static_cast<char>(editor->charAt(p));
            const bool isIdent = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                 (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!isIdent) break;
            ++p;
        }
        const int nameEnd = p;

        if (nameEnd > nameStart) {
            editor->indicatorFillRange(nameStart, nameEnd - nameStart);
        }

        // Advance past this match to avoid an infinite loop on zero-width
        // edge cases.
        ttf.chrg.cpMin = (matchEnd > ttf.chrg.cpMin) ? matchEnd : ttf.chrg.cpMin + 1;
    }
}

void JustfileRecipeHighlighter::clear()
{
    editor->setIndicatorCurrent(indicator);
    editor->indicatorClearRange(0, editor->length());
}

void JustfileRecipeHighlighter::notify(const Scintilla::NotificationData *pscn)
{
    if (editor->languageName != QStringLiteral("Justfile")) {
        return;
    }

    if (pscn->nmhdr.code == Scintilla::Notification::Modified) {
        if (FlagSet(pscn->modificationType, Scintilla::ModificationFlags::InsertText) ||
            FlagSet(pscn->modificationType, Scintilla::ModificationFlags::DeleteText)) {
            timer->start();
        }
    }
}
