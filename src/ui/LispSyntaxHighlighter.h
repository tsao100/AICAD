/**
 * @file LispSyntaxHighlighter.h
 * @brief Lisp 語法上色（註解 / 字串 / 數字 / 關鍵字 / 特殊形式 / AICAD API / 彩虹括號）
 */
#ifndef AICAD_UI_LISPSYNTAXHIGHLIGHTER_H
#define AICAD_UI_LISPSYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QColor>

namespace aicad {
namespace ui {

/// 編輯器配色；依背景明暗自動選擇淺色 / 深色
struct LispEditorTheme {
    QColor background, foreground, currentLine, lineNumberBg, lineNumberFg;
    QColor comment, string, number, keyword, special, builtin, api, variable, constant, defName, symbol, prefix;
    QColor parens[4];
    QColor matchBg, mismatchBg, errorUnderline, flash;

    static LispEditorTheme light();
    static LispEditorTheme dark();
    static LispEditorTheme forBackground(const QColor& bg);
};

class LispSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit LispSyntaxHighlighter(QTextDocument* doc, const LispEditorTheme& theme);
    void setTheme(const LispEditorTheme& theme);

protected:
    void highlightBlock(const QString& text) override;

private:
    void buildFormats();

    LispEditorTheme   m_theme;
    QTextCharFormat   m_comment, m_string, m_number, m_keyword, m_special, m_builtin, m_api,
                      m_variable, m_constant, m_defName, m_symbol, m_prefix, m_parens[4];
};

} // namespace ui
} // namespace aicad

#endif
