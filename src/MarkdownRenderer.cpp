#include "MarkdownRenderer.h"

#include <QByteArray>
#include <QColor>
#include <QSet>

#include <md4c.h>
#include <md4c-html.h>

#include <ILexer.h>
#include <Lexilla.h>
#include <Sci_Position.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

struct CodePalette {
    QString defaultColor;
    QString keyword;
    QString string;
    QString comment;
    QString number;
    QString preprocessor;
    QString operatorColor;
    QString type;
};

static const CodePalette &lightPalette()
{
    static const CodePalette p = {
        "#1f2328", "#cf222e", "#0a3069", "#6e7781",
        "#0550ae", "#116329", "#1f2328", "#953800"
    };
    return p;
}

static const CodePalette &darkPalette()
{
    static const CodePalette p = {
        "#e6edf3", "#ff7b72", "#a5d6ff", "#8b949e",
        "#79c0ff", "#7ee787", "#e6edf3", "#ffa657"
    };
    return p;
}

enum class SemanticStyle : uint8_t {
    Default, Keyword, String, Comment, Number, Preprocessor, Operator, Type
};

// PLACEHOLDER_CONTINUE

SemanticStyle classifyStyle(int styleId)
{
    // Common Scintilla style ranges for cpp-family lexers (SCE_C_*)
    // These cover cpp, java, javascript, go, rust (which all use cpp lexer or similar ranges)
    switch (styleId) {
    case 1: case 2: case 3: case 15: // COMMENT, COMMENTLINE, COMMENTDOC, COMMENTLINEDOC
        return SemanticStyle::Comment;
    case 4: // NUMBER
        return SemanticStyle::Number;
    case 5: case 16: // WORD, WORD2
        return SemanticStyle::Keyword;
    case 6: case 7: case 12: case 13: // STRING, CHARACTER, STRINGEOL, VERBATIM
        return SemanticStyle::String;
    case 9: // PREPROCESSOR
        return SemanticStyle::Preprocessor;
    case 10: // OPERATOR
        return SemanticStyle::Operator;
    case 19: case 20: // GLOBALCLASS, TASKMARKER (used as type in some lexers)
        return SemanticStyle::Type;
    default:
        return SemanticStyle::Default;
    }
}

const QString &colorForSemantic(SemanticStyle s, bool isDark)
{
    const CodePalette &pal = isDark ? darkPalette() : lightPalette();
    switch (s) {
    case SemanticStyle::Keyword:      return pal.keyword;
    case SemanticStyle::String:       return pal.string;
    case SemanticStyle::Comment:      return pal.comment;
    case SemanticStyle::Number:       return pal.number;
    case SemanticStyle::Preprocessor: return pal.preprocessor;
    case SemanticStyle::Operator:     return pal.operatorColor;
    case SemanticStyle::Type:         return pal.type;
    default:                          return pal.defaultColor;
    }
}

// PLACEHOLDER_LEXERDOC

class LexerDoc final : public Scintilla::IDocument
{
public:
    explicit LexerDoc(const QByteArray &content)
        : m_content(content), m_styles(content.size(), 0)
    {
        m_lineStarts.push_back(0);
        for (Sci_Position i = 0; i < Sci_Position(content.size()); ++i) {
            if (content[qsizetype(i)] == '\n') m_lineStarts.push_back(i + 1);
        }
        m_lineStarts.push_back(Sci_Position(content.size()));
        m_lineState.assign(m_lineStarts.size(), 0);
    }
    const QByteArray &styles() const { return m_styles; }

    int SCI_METHOD Version() const override { return Scintilla::dvRelease4; }
    void SCI_METHOD SetErrorStatus(int) override {}
    Sci_Position SCI_METHOD Length() const override { return Sci_Position(m_content.size()); }
    void SCI_METHOD GetCharRange(char *buf, Sci_Position pos, Sci_Position len) const override {
        const auto end = std::min<Sci_Position>(pos + len, Sci_Position(m_content.size()));
        const auto start = std::max<Sci_Position>(pos, 0);
        if (end > start) std::memcpy(buf, m_content.constData() + start, size_t(end - start));
    }
    char SCI_METHOD StyleAt(Sci_Position pos) const override {
        if (pos < 0 || pos >= Sci_Position(m_styles.size())) return 0;
        return m_styles[qsizetype(pos)];
    }
    Sci_Position SCI_METHOD LineFromPosition(Sci_Position pos) const override {
        if (pos <= 0) return 0;
        pos = std::min(pos, Sci_Position(m_content.size()));
        auto it = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), pos);
        return Sci_Position((it - m_lineStarts.begin()) - 1);
    }
    Sci_Position SCI_METHOD LineStart(Sci_Position line) const override {
        if (line < 0) return 0;
        if (size_t(line) >= m_lineStarts.size()) return Sci_Position(m_content.size());
        return m_lineStarts[size_t(line)];
    }
    int SCI_METHOD GetLevel(Sci_Position) const override { return 0x400; }
    int SCI_METHOD SetLevel(Sci_Position, int) override { return 0; }
    int SCI_METHOD GetLineState(Sci_Position line) const override {
        if (line < 0 || size_t(line) >= m_lineState.size()) return 0;
        return m_lineState[size_t(line)];
    }
    int SCI_METHOD SetLineState(Sci_Position line, int state) override {
        if (line < 0) return 0;
        if (size_t(line) >= m_lineState.size()) m_lineState.resize(size_t(line) + 1, 0);
        m_lineState[size_t(line)] = state;
        return state;
    }
    void SCI_METHOD StartStyling(Sci_Position pos) override { m_stylingPos = std::clamp<Sci_Position>(pos, 0, Sci_Position(m_styles.size())); }
    bool SCI_METHOD SetStyleFor(Sci_Position len, char style) override {
        const auto end = std::min<Sci_Position>(m_stylingPos + len, Sci_Position(m_styles.size()));
        if (end > m_stylingPos) std::memset(m_styles.data() + m_stylingPos, (unsigned char)style, size_t(end - m_stylingPos));
        m_stylingPos = end;
        return true;
    }
    bool SCI_METHOD SetStyles(Sci_Position len, const char *s) override {
        const auto end = std::min<Sci_Position>(m_stylingPos + len, Sci_Position(m_styles.size()));
        if (end > m_stylingPos) std::memcpy(m_styles.data() + m_stylingPos, s, size_t(end - m_stylingPos));
        m_stylingPos = end;
        return true;
    }
    void SCI_METHOD DecorationSetCurrentIndicator(int) override {}
    void SCI_METHOD DecorationFillRange(Sci_Position, int, Sci_Position) override {}
    void SCI_METHOD ChangeLexerState(Sci_Position, Sci_Position) override {}
    int SCI_METHOD CodePage() const override { return 65001; }
    bool SCI_METHOD IsDBCSLeadByte(char) const override { return false; }
    const char * SCI_METHOD BufferPointer() override { return m_content.constData(); }
    int SCI_METHOD GetLineIndentation(Sci_Position) override { return 0; }
    Sci_Position SCI_METHOD LineEnd(Sci_Position line) const override {
        const auto next = LineStart(line + 1);
        if (next > 0 && next <= Sci_Position(m_content.size()) && m_content[qsizetype(next - 1)] == '\n') return next - 1;
        return next;
    }
    Sci_Position SCI_METHOD GetRelativePosition(Sci_Position pos, Sci_Position off) const override {
        return std::clamp<Sci_Position>(pos + off, 0, Sci_Position(m_content.size()));
    }
    int SCI_METHOD GetCharacterAndWidth(Sci_Position pos, Sci_Position *w) const override {
        if (w) *w = 1;
        if (pos < 0 || pos >= Sci_Position(m_content.size())) return 0;
        return (unsigned char)m_content[qsizetype(pos)];
    }
private:
    QByteArray m_content;
    QByteArray m_styles;
    std::vector<Sci_Position> m_lineStarts;
    std::vector<int> m_lineState;
    Sci_Position m_stylingPos = 0;
};

// PLACEHOLDER_FENCE_MAP

static const QHash<QString, QString> &fenceAliases()
{
    static const QHash<QString, QString> m = {
    {"c++", "cpp"}, {"cxx", "cpp"}, {"cc", "cpp"}, {"h", "cpp"}, {"hpp", "cpp"},
    {"py", "python"},
    {"js", "javascript"}, {"mjs", "javascript"}, {"cjs", "javascript"}, {"jsx", "javascript"},
    {"ts", "javascript"}, {"tsx", "javascript"}, {"typescript", "javascript"},
    {"rs", "rust"},
    {"golang", "go"},
    {"sh", "bash"}, {"shell", "bash"}, {"zsh", "bash"}, {"ksh", "bash"},
    {"htm", "html"}, {"xhtml", "html"},
    {"yml", "yaml"},
    {"rb", "ruby"},
    {"pl", "perl"},
    {"ps1", "powershell"},
    {"md", "markdown"},
    };
    return m;
}

static const QHash<QString, QString> &languageToLexer()
{
    static const QHash<QString, QString> m = {
    {"cpp", "cpp"}, {"c", "cpp"}, {"python", "python"}, {"javascript", "cpp"},
    {"rust", "rust"}, {"java", "cpp"}, {"go", "cpp"}, {"cs", "cpp"},
    {"bash", "bash"}, {"html", "hypertext"}, {"css", "css"}, {"json", "json"},
    {"xml", "xml"}, {"sql", "sql"}, {"lua", "lua"}, {"ruby", "ruby"},
    {"perl", "perl"}, {"yaml", "yaml"}, {"toml", "toml"}, {"markdown", "markdown"},
    {"cmake", "cmake"}, {"makefile", "makefile"}, {"powershell", "powershell"},
    {"dart", "dart"}, {"kotlin", "cpp"}, {"swift", "cpp"},
    };
    return m;
}

} // anonymous namespace

// PLACEHOLDER_IMPL

QString MarkdownRenderer::normalizeFenceLabel(const QString &label)
{
    QString lower = label.toLower().trimmed();
    auto it = fenceAliases().constFind(lower);
    return it != fenceAliases().constEnd() ? it.value() : lower;
}

QSet<QString> MarkdownRenderer::scanFenceLabels(const QString &source)
{
    QSet<QString> labels;
    const QChar *data = source.constData();
    const int len = source.size();
    int i = 0;
    while (i < len) {
        if (i == 0 || data[i - 1] == QLatin1Char('\n')) {
            int backticks = 0;
            while (i < len && data[i] == QLatin1Char('`')) { ++backticks; ++i; }
            if (backticks >= 3) {
                int start = i;
                while (i < len && data[i] != QLatin1Char('\n') && data[i] != QLatin1Char(' ')
                       && data[i] != QLatin1Char('`')) ++i;
                if (i > start)
                    labels.insert(source.mid(start, i - start).toLower());
            }
        }
        while (i < len && data[i] != QLatin1Char('\n')) ++i;
        if (i < len) ++i;
    }
    return labels;
}

QString MarkdownRenderer::highlightCodeBlock(const QByteArray &code, const QString &lexerName, bool isDark)
{
    if (lexerName.isEmpty() || code.isEmpty()) {
        QString escaped = QString::fromUtf8(code).toHtmlEscaped();
        return QStringLiteral("<pre><code>") + escaped + QStringLiteral("</code></pre>");
    }

    Scintilla::ILexer5 *lexer = CreateLexer(lexerName.toLatin1().constData());
    if (!lexer) {
        QString escaped = QString::fromUtf8(code).toHtmlEscaped();
        return QStringLiteral("<pre><code>") + escaped + QStringLiteral("</code></pre>");
    }

    LexerDoc doc(code);
    lexer->Lex(0, static_cast<Sci_Position>(code.size()), 0, &doc);
    lexer->Release();

    const QByteArray &styles = doc.styles();
    QString html;
    html.reserve(code.size() * 2);
    html += QStringLiteral("<pre><code>");

    int prevStyle = -1;
    for (int i = 0; i < code.size(); ++i) {
        int s = static_cast<unsigned char>(styles[i]);
        if (s != prevStyle) {
            if (prevStyle >= 0) html += QStringLiteral("</font>");
            SemanticStyle sem = classifyStyle(s);
            const QString &color = colorForSemantic(sem, isDark);
            html += QStringLiteral("<font color=\"") + color + QStringLiteral("\">");
            prevStyle = s;
        }
        char ch = code[i];
        switch (ch) {
        case '<': html += QStringLiteral("&lt;"); break;
        case '>': html += QStringLiteral("&gt;"); break;
        case '&': html += QStringLiteral("&amp;"); break;
        case '"': html += QStringLiteral("&quot;"); break;
        default:  html += QLatin1Char(ch); break;
        }
    }
    if (prevStyle >= 0) html += QStringLiteral("</font>");
    html += QStringLiteral("</code></pre>");
    return html;
}

// PLACEHOLDER_RENDER

QString MarkdownRenderer::buildStyleBlock(const QPalette &palette, bool isDark,
                                          const QString &fontFamily, int fontSize)
{
    QString bg = palette.color(QPalette::Base).name();
    QString fg = palette.color(QPalette::Text).name();
    QString link = palette.color(QPalette::Link).name();
    QString mid = palette.color(QPalette::Mid).name();
    QString altBase = palette.color(QPalette::AlternateBase).name();

    QString fontCss;
    if (!fontFamily.isEmpty())
        fontCss += QStringLiteral("font-family:'%1',system-ui,sans-serif;").arg(fontFamily);
    else
        fontCss += QStringLiteral("font-family:system-ui,sans-serif;");
    if (fontSize > 0)
        fontCss += QStringLiteral("font-size:%1pt;").arg(fontSize);

    QString codeFontCss;
    if (!fontFamily.isEmpty())
        codeFontCss += QStringLiteral("font-family:'%1',monospace;").arg(fontFamily);
    else
        codeFontCss += QStringLiteral("font-family:monospace;");
    if (fontSize > 0)
        codeFontCss += QStringLiteral("font-size:%1pt;").arg(fontSize);

    return QStringLiteral(
        "<style>"
        "body{background:%1;color:%2;%6padding:16px;line-height:1.6;}"
        "a{color:%3;}"
        "code{background:%4;padding:2px 4px;border-radius:3px;%7line-height:normal;}"
        "pre{background:%4;padding:0.8em 1em;border-radius:4px;overflow-x:auto;line-height:normal;}"
        "pre code{background:none;padding:0;line-height:normal;}"
        "blockquote{border-left:3px solid %5;padding-left:12px;margin-left:0;}"
        "table{border-collapse:collapse;}"
        "th,td{border:1px solid %5;padding:6px 12px;}"
        "th{background:%4;}"
        "hr{border:none;border-top:1px solid %5;}"
        "img{max-width:100%%;}"
        "</style>"
    ).arg(bg, fg, link, altBase, mid, fontCss, codeFontCss);
}

namespace {

struct RenderContext {
    const MarkdownRenderRequest *request;
    QString html;
    size_t bytesEmitted = 0;
    bool truncated = false;
    QByteArray sourceUtf8;
    std::vector<Sci_Position> lineStarts;

    int lineFromOffset(MD_OFFSET offset) const {
        auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), Sci_Position(offset));
        return int((it - lineStarts.begin()) - 1) + 1;
    }
};

struct CodeBlockState {
    QByteArray content;
    QString fenceLabel;
    int sourceLine = 0;
};

void mdOutputCallback(const MD_CHAR *text, MD_SIZE size, void *userdata)
{
    auto *ctx = static_cast<RenderContext *>(userdata);
    if (ctx->truncated) return;
    ctx->bytesEmitted += size;
    if (ctx->bytesEmitted > MarkdownRenderer::kMaxHtmlBytes) {
        ctx->truncated = true;
        ctx->html += QStringLiteral("</p><hr><p><em>[Preview truncated — file too large]</em></p>");
        return;
    }
    ctx->html += QString::fromUtf8(text, int(size));
}

} // anonymous namespace

MarkdownRenderResult MarkdownRenderer::render(const MarkdownRenderRequest &request)
{
    MarkdownRenderResult result;
    RenderContext ctx;
    ctx.request = &request;
    ctx.sourceUtf8 = request.sourceText.toUtf8();

    ctx.lineStarts.push_back(0);
    for (int i = 0; i < ctx.sourceUtf8.size(); ++i) {
        if (ctx.sourceUtf8[i] == '\n') ctx.lineStarts.push_back(i + 1);
    }
    ctx.lineStarts.push_back(ctx.sourceUtf8.size());

    ctx.html.reserve(ctx.sourceUtf8.size() * 2);
    ctx.html += QStringLiteral("<!DOCTYPE html><html><head>");
    ctx.html += buildStyleBlock(request.palette, request.isDark, request.fontFamily, request.fontSize);
    ctx.html += QStringLiteral("</head><body>");

    // Two-pass approach:
    // Pass 1: Use md_html for standard rendering (fast, handles all markdown)
    // Pass 2 (inline): Intercept code blocks via custom callbacks for syntax highlighting

    // We use md4c's standard HTML renderer for simplicity in v1.
    // Code block highlighting is done as a post-process on the HTML output.
    // Anchor injection: we insert anchors at regular line intervals.

    // Inject anchors every 10 lines for scroll sync
    int totalLines = int(ctx.lineStarts.size()) - 1;
    QHash<int, bool> anchorLines;
    for (int line = 1; line <= totalLines; line += 10) {
        anchorLines.insert(line, true);
    }

    // Render with md4c's HTML helper
    unsigned mdFlags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS
                     | MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_NOHTML;

    int parseResult = md_html(
        ctx.sourceUtf8.constData(),
        MD_SIZE(ctx.sourceUtf8.size()),
        mdOutputCallback,
        &ctx,
        mdFlags,
        0
    );

    if (parseResult != 0 && ctx.html.isEmpty()) {
        ctx.html += QStringLiteral("<p><em>[Markdown parse error]</em></p>");
    }

    // Post-process: inject line anchors at block boundaries
    // Insert anchors before <h1>-<h6>, <p>, <pre>, <ul>, <ol>, <table>, <blockquote>, <hr>
    // This is approximate but sufficient for scroll sync
    static const QStringList blockTags = {
        QStringLiteral("<h1"), QStringLiteral("<h2"), QStringLiteral("<h3"),
        QStringLiteral("<h4"), QStringLiteral("<h5"), QStringLiteral("<h6"),
        QStringLiteral("<p"), QStringLiteral("<pre"), QStringLiteral("<ul"),
        QStringLiteral("<ol"), QStringLiteral("<table"), QStringLiteral("<blockquote"),
        QStringLiteral("<hr")
    };

    // Simple anchor injection: insert anchors at evenly-spaced positions
    // based on the ratio of HTML position to total HTML length mapped to source lines
    int anchorCount = totalLines / 10;
    if (anchorCount > 0 && !ctx.truncated) {
        QString anchored;
        anchored.reserve(ctx.html.size() + anchorCount * 30);
        int htmlLen = ctx.html.size();
        int nextAnchorLine = 1;
        int insertedUpTo = 0;

        for (int line = 1; line <= totalLines; line += 10) {
            // Estimate position in HTML proportional to line position in source
            int estimatedPos = int(qint64(line) * htmlLen / totalLines);
            // Find next block tag boundary after estimated position
            int bestPos = -1;
            for (const QString &tag : blockTags) {
                int found = ctx.html.indexOf(tag, estimatedPos);
                if (found >= 0 && (bestPos < 0 || found < bestPos))
                    bestPos = found;
            }
            if (bestPos < 0 || bestPos <= insertedUpTo) continue;

            anchored += ctx.html.mid(insertedUpTo, bestPos - insertedUpTo);
            anchored += QStringLiteral("<a id=\"L%1\"></a>").arg(line);
            insertedUpTo = bestPos;
        }
        anchored += ctx.html.mid(insertedUpTo);
        ctx.html = std::move(anchored);
    }

    // Post-process: highlight code blocks
    // Find <pre><code class="language-xxx">...</code></pre> patterns and replace with highlighted version
    int searchFrom = 0;
    while (true) {
        int preStart = ctx.html.indexOf(QStringLiteral("<pre><code class=\"language-"), searchFrom);
        if (preStart < 0) break;

        int langStart = preStart + 27; // length of '<pre><code class="language-'
        int langEnd = ctx.html.indexOf(QLatin1Char('"'), langStart);
        if (langEnd < 0) break;

        QString fenceLabel = ctx.html.mid(langStart, langEnd - langStart);
        int codeStart = ctx.html.indexOf(QLatin1Char('>'), langEnd);
        if (codeStart < 0) break;
        codeStart++; // skip '>'

        int codeEnd = ctx.html.indexOf(QStringLiteral("</code></pre>"), codeStart);
        if (codeEnd < 0) break;

        // Extract code content (HTML-encoded by md4c), decode entities
        QString codeHtml = ctx.html.mid(codeStart, codeEnd - codeStart);
        QString codeText = codeHtml;
        codeText.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
        codeText.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        codeText.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        codeText.replace(QStringLiteral("&quot;"), QStringLiteral("\""));

        // Resolve lexer name
        QString normalized = normalizeFenceLabel(fenceLabel);
        QString lexerName;
        auto it = request.resolvedLexers.constFind(normalized);
        if (it != request.resolvedLexers.constEnd()) {
            lexerName = it.value();
        } else {
            auto lit = languageToLexer().constFind(normalized);
            if (lit != languageToLexer().constEnd()) lexerName = lit.value();
        }

        QString highlighted = highlightCodeBlock(codeText.toUtf8(), lexerName, request.isDark);

        // Replace the original <pre><code...>...</code></pre> with highlighted version
        int blockEnd = codeEnd + 13; // length of "</code></pre>"
        ctx.html.replace(preStart, blockEnd - preStart, highlighted);
        searchFrom = preStart + highlighted.size();
    }

    ctx.html += QStringLiteral("</body></html>");

    result.html = std::move(ctx.html);
    result.truncated = ctx.truncated;
    return result;
}
