#ifndef JUSTFILERECIPEHIGHLIGHTER_H
#define JUSTFILERECIPEHIGHLIGHTER_H

#include "EditorDecorator.h"

class QTimer;

// Justfile uses the bash lexer (recipe bodies are shell, so bash gives the
// most useful highlighting). The bash lexer doesn't know about justfile
// recipe target syntax (`name:`, `name param:`, `@name:`), so target lines
// look like ordinary code. This decorator overlays an INDIC_TEXTFORE
// indicator on the target name to colour it, mimicking what the makefile
// lexer would give us for free if we used it instead.
class JustfileRecipeHighlighter : public EditorDecorator
{
    Q_OBJECT

public:
    JustfileRecipeHighlighter(ScintillaNext *editor);

public slots:
    void notify(const Scintilla::NotificationData *pscn) override;

private slots:
    void rescan();
    void clear();
    void refreshForLanguage();

private:
    QTimer *timer;
    int indicator;
};

#endif // JUSTFILERECIPEHIGHLIGHTER_H
