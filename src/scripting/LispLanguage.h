/**
 * @file LispLanguage.h
 * @brief Lisp 語言支援核心：詞法分析、括號配對、頂層表達式切分、縮排計算、符號字典
 *
 * 這一層「完全不依賴 ECL，也不依賴任何 GUI 類別」，只用 QString / QVector：
 *   - 編輯器（LispCodeEdit / LispSyntaxHighlighter）用它做語法上色、括號配對、
 *     Lisp 風格自動縮排、游標所在表達式的擷取、參數提示。
 *   - 即使 ECL 沒有編譯進來（HAVE_ECL 未定義）、引擎初始化失敗，編輯器仍可正常編輯。
 *   - 可以在沒有 Qt Widgets 的環境下做單元測試。
 */

#ifndef AICAD_SCRIPTING_LISPLANGUAGE_H
#define AICAD_SCRIPTING_LISPLANGUAGE_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QPair>

namespace aicad {
namespace scripting {

// ═══════════════════════════════════════════════════════════════════════════
// 詞法分析
// ═══════════════════════════════════════════════════════════════════════════

enum class LispTokenKind : unsigned char {
    LineComment,    ///< ; ... 到行尾
    BlockComment,   ///< #| ... |#（可巢狀、可跨行）
    String,         ///< "..."（可跨行）
    Char,           ///< #\a  #\Space  #\(
    Number,         ///< 12  -3.5  1/2  1.5d0  #xFF
    Keyword,        ///< :foo
    Symbol,         ///< 其他符號（含 |a b| 與 #:foo）
    OpenParen,      ///< (
    CloseParen,     ///< )
    Prefix          ///< '  `  ,  ,@  #'  #  #+  #-   （附著在後一個資料上）
};

struct LispToken {
    int           start  = 0;
    int           length = 0;
    LispTokenKind kind   = LispTokenKind::Symbol;
    int           depth  = 0;   ///< 僅括號有意義：該括號的巢狀深度（最外層 = 0）；配對的 ( ) 相同
    int end() const { return start + length; }
};

/**
 * 詞法狀態（可存進 QTextBlock::userState，讓語法上色能逐行進行）
 *   bit 0-1  : 模式 — 0 一般、1 字串中、2 區塊註解中
 *   bit 2-7  : 區塊註解巢狀深度（0..63）
 *   bit 8-30 : 行首的括號深度
 */
namespace LispLexState {
constexpr int Normal         = 0;
constexpr int InString       = 1;
constexpr int InBlockComment = 2;

inline int mode(int s)       { return s & 3; }
inline int blockDepth(int s) { return (s >> 2) & 0x3F; }
inline int parenDepth(int s) { return (s >> 8) & 0x3FFFFF; }
inline int pack(int mode, int blockDepth, int parenDepth) {
    return (mode & 3) | ((blockDepth & 0x3F) << 2) | ((parenDepth & 0x3FFFFF) << 8);
}
} // namespace LispLexState

class LispLexer {
public:
    /**
     * @brief 對 text 做詞法分析
     * @param text       可以是一整份文件，也可以只是一行（給 QSyntaxHighlighter 用）
     * @param startState 起始狀態（文件開頭給 0；逐行時給上一行結尾的狀態）
     * @param out        輸出 token（可為 nullptr，只要結尾狀態時）
     * @return           結尾狀態（見 LispLexState）
     */
    static int lex(const QString& text, int startState, QVector<LispToken>* out);

    /// 判斷一段符號文字是否為數值（整數、浮點、有理數、#x/#b/#o）
    static bool looksLikeNumber(const QString& s);

    /// 是否為符號的終止字元（空白、括號、引號、;、' ` ,）
    static bool isTerminator(QChar c);
};

// ═══════════════════════════════════════════════════════════════════════════
// 符號字典（上色 / 自動補全 / 簽名提示 / 縮排規則）
// ═══════════════════════════════════════════════════════════════════════════

enum class LispSymbolCategory : unsigned char {
    SpecialForm,    ///< quote、let、if…（特殊運算子）
    Macro,          ///< defun、when、loop…
    Function,       ///< car、mapcar、format…
    Aicad           ///< AICAD 綁定的 API（line、sketch-create…）
};

struct LispSymbolInfo {
    QString            name;        ///< 小寫
    LispSymbolCategory category = LispSymbolCategory::Function;
    QString            signature;   ///< 例如 "(line x1 y1 x2 y2)"；內建符號可為空
    QString            doc;         ///< 簡短說明；可為空
};

class LispDictionary {
public:
    static const LispDictionary& instance();

    /// 以小寫名稱查詢（會去掉 "cl:" "cl-user::" 之類的套件前綴）；找不到回傳 nullptr
    const LispSymbolInfo* find(const QString& name) const;

    /// 所有已知名稱（小寫、已排序、不重複），給自動補全用
    const QStringList& allNames() const { return m_allNames; }

    /**
     * 縮排規則：回傳「特殊參數」個數 n。
     *   前 n 個參數（distinguished）縮 4 格，其後的 body 縮 2 格；
     *   回傳 -1 表示一般函式呼叫（對齊第一個參數）。
     * 例：defun → 2、let → 1、when → 1、progn → 0、if → -1。
     * 名稱以 def / define- 開頭 → 2；with- / do- 開頭 → 1。
     */
    int bodyIndent(const QString& name) const;

private:
    LispDictionary();
    void add(const QString& names, LispSymbolCategory cat);

    QHash<QString, LispSymbolInfo> m_symbols;
    QHash<QString, int>            m_bodyIndent;
    QStringList                    m_allNames;
};

// ═══════════════════════════════════════════════════════════════════════════
// 文件索引
// ═══════════════════════════════════════════════════════════════════════════

/// 游標所在的函式呼叫（供參數提示）
struct LispCallContext {
    bool    valid      = false;
    QString op;                 ///< 運算子名稱（小寫）
    int     openPos    = -1;    ///< 該呼叫的 "(" 位置
    int     paramIndex = 0;     ///< 游標目前位於第幾個參數（0 起算，不含運算子）
};

/// 頂層定義（(defun foo …) 之類）
struct LispDefinition {
    QString kind;       ///< "defun"、"defvar"、…
    QString name;       ///< 名稱（保留原大小寫）
    QString signature;  ///< defun / defmacro 的 "(name a b &optional c)"；其他為空
    int     pos = 0;    ///< 該頂層表達式起點（"(" 的位置）
    int     line = 0;   ///< 0 起算的行號
};

/**
 * @brief 一份文字的結構索引
 *
 * rebuild() 對整份文字做一次詞法分析，之後所有查詢（括號配對、頂層表達式、
 * 縮排、呼叫內容、定義清單）都是對 token 陣列的 O(log n)～O(n) 操作。
 * 位置一律是 QString 的 UTF-16 索引，與 QPlainTextEdit 的游標位置一致。
 */
class LispDocumentIndex {
public:
    void rebuild(const QString& text);

    const QString&            text()   const { return m_text; }
    const QVector<LispToken>& tokens() const { return m_tokens; }

    /// 含有 pos 的 token 索引（pos 在 [start, end) 內）；沒有回傳 -1
    int tokenIndexAt(int pos) const;

    /// pos 是否在字串或註解內部（不含起始與結尾的引號／註解符號之外的位置）
    bool isInStringOrComment(int pos) const;

    /// pos 位置上的括號的配對括號位置；pos 不是括號或沒有配對回傳 -1
    int matchingParen(int pos) const;

    /// pos 位置是否為沒有配對的括號
    bool isUnmatchedParen(int pos) const;

    /// 沒有配對的括號總數（> 0 表示括號不平衡）
    int unmatchedParenCount() const { return m_unmatched; }

    /// 所有頂層表達式的 [start, end) 範圍（依序）
    const QVector<QPair<int, int>>& topLevelForms() const { return m_forms; }

    /**
     * 游標所在的頂層表達式：
     *   1. 含有 pos 的表達式；
     *   2. 否則，同一行上緊鄰 pos 之前的表達式（游標在 ")" 後面）；
     *   3. 否則，同一行上緊接 pos 之後的表達式。
     * 都沒有回傳 false。
     */
    bool formAt(int pos, int* start, int* end) const;

    /**
     * @brief 計算以 lineStart 為行首的那一行「應有的縮排欄數」
     * @return 欄數；若該行起點在字串／區塊註解內（不應改動）回傳 -1
     */
    int indentFor(int lineStart) const;

    /// 游標位置的函式呼叫內容（參數提示用）
    LispCallContext callContextAt(int pos) const;

    /// 頂層定義清單
    QVector<LispDefinition> definitions() const;

    /// 取得 token 文字
    QString tokenText(int index) const;

    /// pos 所在行的起點
    int lineStartOf(int pos) const;
    /// pos 的 0 起算行號
    int lineOf(int pos) const;

private:
    struct Frame;
    QVector<Frame> buildFrames(int limitPos, bool* insideAtom) const;

    QString                  m_text;
    QVector<LispToken>       m_tokens;
    QVector<int>             m_mate;         ///< token 索引 → 配對 token 索引（-1 = 無）
    QVector<int>             m_lineStarts;
    QVector<QPair<int, int>> m_forms;
    int                      m_unmatched = 0;
};

} // namespace scripting
} // namespace aicad

#endif // AICAD_SCRIPTING_LISPLANGUAGE_H
