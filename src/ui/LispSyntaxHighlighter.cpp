/**
 * @file LispSyntaxHighlighter.cpp
 */
#include "LispSyntaxHighlighter.h"
#include "scripting/LispLanguage.h"

namespace aicad {
namespace ui {

using namespace scripting;

LispEditorTheme LispEditorTheme::light()
{
    LispEditorTheme t;
    t.background = QColor("#ffffff");  t.foreground = QColor("#1f2328");
    t.currentLine = QColor("#f3f6fb"); t.lineNumberBg = QColor("#f4f4f4"); t.lineNumberFg = QColor("#8a8f98");
    t.comment = QColor("#6a737d");     t.string = QColor("#0a7d33");   t.number = QColor("#b35900");
    t.keyword = QColor("#7b2cbf");     t.special = QColor("#7b2cbf");  t.builtin = QColor("#005cc5");
    t.api = QColor("#c2185b");         t.variable = QColor("#00796b"); t.constant = QColor("#b35900");
    t.defName = QColor("#8a4b00");     t.symbol = QColor("#1f2328");   t.prefix = QColor("#c2185b");
    t.parens[0] = QColor("#6a737d"); t.parens[1] = QColor("#1f7ae0");
    t.parens[2] = QColor("#2e9d4f"); t.parens[3] = QColor("#c27a00");
    t.matchBg = QColor("#c8e1ff");  t.mismatchBg = QColor("#ffb3b3");
    t.errorUnderline = QColor("#d32f2f"); t.flash = QColor("#fff3a8");
    return t;
}

LispEditorTheme LispEditorTheme::dark()
{
    LispEditorTheme t;
    t.background = QColor("#1e1f22");  t.foreground = QColor("#d7dae0");
    t.currentLine = QColor("#26282c"); t.lineNumberBg = QColor("#1a1b1e"); t.lineNumberFg = QColor("#6b7078");
    t.comment = QColor("#7f8791");     t.string = QColor("#8fcf8b");   t.number = QColor("#e5b567");
    t.keyword = QColor("#c792ea");     t.special = QColor("#c792ea");  t.builtin = QColor("#6cb6ff");
    t.api = QColor("#ff79a8");         t.variable = QColor("#56d4c4"); t.constant = QColor("#e5b567");
    t.defName = QColor("#ffcb6b");     t.symbol = QColor("#d7dae0");   t.prefix = QColor("#ff79a8");
    t.parens[0] = QColor("#8b929c"); t.parens[1] = QColor("#6cb6ff");
    t.parens[2] = QColor("#8fcf8b"); t.parens[3] = QColor("#e5b567");
    t.matchBg = QColor("#3a4a63");  t.mismatchBg = QColor("#7a2f2f");
    t.errorUnderline = QColor("#ff6b6b"); t.flash = QColor("#5a5320");
    return t;
}

LispEditorTheme LispEditorTheme::forBackground(const QColor& bg)
{
    return bg.lightness() < 128 ? dark() : light();
}

LispSyntaxHighlighter::LispSyntaxHighlighter(QTextDocument* doc, const LispEditorTheme& theme)
    : QSyntaxHighlighter(doc), m_theme(theme)
{
    buildFormats();
}

void LispSyntaxHighlighter::setTheme(const LispEditorTheme& theme)
{
    m_theme = theme;
    buildFormats();
    rehighlight();
}

void LispSyntaxHighlighter::buildFormats()
{
    auto mk = [](const QColor& c, bool bold = false, bool italic = false) {
        QTextCharFormat f;
        f.setForeground(c);
        if (bold)   f.setFontWeight(QFont::Bold);
        if (italic) f.setFontItalic(true);
        return f;
    };
    m_comment  = mk(m_theme.comment, false, true);
    m_string   = mk(m_theme.string);
    m_number   = mk(m_theme.number);
    m_keyword  = mk(m_theme.keyword);
    m_special  = mk(m_theme.special, true);
    m_builtin  = mk(m_theme.builtin);
    m_api      = mk(m_theme.api, true);
    m_variable = mk(m_theme.variable);
    m_constant = mk(m_theme.constant);
    m_defName  = mk(m_theme.defName, true);
    m_symbol   = mk(m_theme.symbol);
    m_prefix   = mk(m_theme.prefix);
    for (int i = 0; i < 4; ++i) m_parens[i] = mk(m_theme.parens[i], true);
}

void LispSyntaxHighlighter::highlightBlock(const QString& text)
{
    int state = previousBlockState();
    if (state < 0) state = 0;

    QVector<LispToken> tokens;
    const int endState = LispLexer::lex(text, state, &tokens);
    setCurrentBlockState(endState);

    static const QStringList defForms = {
        QStringLiteral("defun"), QStringLiteral("defmacro"), QStringLiteral("defvar"),
        QStringLiteral("defparameter"), QStringLiteral("defconstant"), QStringLiteral("defstruct"),
        QStringLiteral("defclass"), QStringLiteral("defgeneric"), QStringLiteral("defmethod"),
        QStringLiteral("deftype") };

    const LispDictionary& dict = LispDictionary::instance();
    bool prevWasDefForm = false;

    for (const LispToken& t : tokens) {
        const QString word = (t.kind == LispTokenKind::Symbol) ? text.mid(t.start, t.length) : QString();
        switch (t.kind) {
        case LispTokenKind::LineComment:
        case LispTokenKind::BlockComment: setFormat(t.start, t.length, m_comment); break;
        case LispTokenKind::String:       setFormat(t.start, t.length, m_string);  break;
        case LispTokenKind::Char:         setFormat(t.start, t.length, m_string);  break;
        case LispTokenKind::Number:       setFormat(t.start, t.length, m_number);  break;
        case LispTokenKind::Keyword:      setFormat(t.start, t.length, m_keyword); break;
        case LispTokenKind::Prefix:       setFormat(t.start, t.length, m_prefix);  break;
        case LispTokenKind::OpenParen:
        case LispTokenKind::CloseParen:   setFormat(t.start, t.length, m_parens[t.depth % 4]); break;
        case LispTokenKind::Symbol: {
            const QString lower = word.toLower();
            if (prevWasDefForm) {
                setFormat(t.start, t.length, m_defName);
            } else if (const LispSymbolInfo* info = dict.find(lower)) {
                switch (info->category) {
                case LispSymbolCategory::SpecialForm:
                case LispSymbolCategory::Macro:    setFormat(t.start, t.length, m_special);  break;
                case LispSymbolCategory::Function: setFormat(t.start, t.length, m_builtin);  break;
                case LispSymbolCategory::Aicad:    setFormat(t.start, t.length, m_api);      break;
                }
            } else if (word.size() > 2 && word.startsWith(QLatin1Char('*')) && word.endsWith(QLatin1Char('*'))) {
                setFormat(t.start, t.length, m_variable);
            } else if (word.size() > 2 && word.startsWith(QLatin1Char('+')) && word.endsWith(QLatin1Char('+'))) {
                setFormat(t.start, t.length, m_constant);
            } else {
                setFormat(t.start, t.length, m_symbol);
            }
            prevWasDefForm = defForms.contains(lower);
            continue;
        }
        }
        // (defstruct (name ...)) 的括號不打斷「下一個符號是名稱」
        if (t.kind != LispTokenKind::OpenParen) prevWasDefForm = false;
    }
}

} // namespace ui
} // namespace aicad
