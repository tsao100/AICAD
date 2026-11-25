/**
 * @file LispLanguage.cpp
 * @brief Lisp 語言支援核心實作（詞法 / 括號配對 / 縮排 / 字典）
 */

#include "LispLanguage.h"

#include <QRegularExpression>
#include <algorithm>

namespace aicad {
namespace scripting {

// ═══════════════════════════════════════════════════════════════════════════
// LispLexer
// ═══════════════════════════════════════════════════════════════════════════

bool LispLexer::isTerminator(QChar c)
{
    if (c.isSpace()) return true;
    switch (c.unicode()) {
    case '(': case ')': case '"': case ';': case '\'': case '`': case ',':
        return true;
    default:
        return false;
    }
}

bool LispLexer::looksLikeNumber(const QString& s)
{
    static const QRegularExpression decimal(
        QStringLiteral("^[+-]?(?:\\d+/\\d+|(?:\\d+\\.?\\d*|\\.\\d+)(?:[eEdDfFsSlL][+-]?\\d+)?)$"));
    static const QRegularExpression radix(
        QStringLiteral("^#[xXbBoO][+-]?[0-9a-fA-F/]+$"));
    return decimal.match(s).hasMatch() || radix.match(s).hasMatch();
}

int LispLexer::lex(const QString& text, int startState, QVector<LispToken>* out)
{
    int mode   = LispLexState::mode(startState);
    int bdepth = LispLexState::blockDepth(startState);
    int pdepth = LispLexState::parenDepth(startState);

    const int n = text.size();
    int i = 0;

    auto addToken = [&](int s, int len, LispTokenKind k, int depth = 0) {
        if (out) {
            LispToken t;
            t.start = s; t.length = len; t.kind = k; t.depth = depth;
            out->append(t);
        }
    };

    // 掃描區塊註解內容：從 from 開始、目前巢狀深度 depth；回傳結束位置，depth 以參考更新。
    // 結束（depth 歸零）時回傳 |# 之後的位置；到文字結尾仍未結束則回傳 n 且 depth > 0。
    auto scanBlock = [&](int from, int& depth) -> int {
        int j = from;
        while (j < n && depth > 0) {
            if (text.at(j) == QLatin1Char('|') && j + 1 < n && text.at(j + 1) == QLatin1Char('#')) {
                --depth; j += 2;
            } else if (text.at(j) == QLatin1Char('#') && j + 1 < n && text.at(j + 1) == QLatin1Char('|')) {
                ++depth; j += 2;
            } else {
                ++j;
            }
        }
        return j;
    };

    // 掃描字串內容：from 在開頭引號「之後」；回傳結束引號之後的位置，closed 表示是否找到結尾引號
    auto scanString = [&](int from, bool& closed) -> int {
        int j = from;
        closed = false;
        while (j < n) {
            const QChar c = text.at(j);
            if (c == QLatin1Char('\\')) { j += 2; continue; }
            if (c == QLatin1Char('"'))  { ++j; closed = true; break; }
            ++j;
        }
        return qMin(j, n);
    };

    // ── 接續上一行的狀態 ────────────────────────────────────────────────
    if (mode == LispLexState::InString) {
        bool closed = false;
        const int j = scanString(0, closed);
        if (j > 0) addToken(0, j, LispTokenKind::String);
        i = j;
        if (!closed) return LispLexState::pack(LispLexState::InString, 0, pdepth);
        mode = LispLexState::Normal;
    } else if (mode == LispLexState::InBlockComment) {
        int depth = qMax(1, bdepth);
        const int j = scanBlock(0, depth);
        if (j > 0) addToken(0, j, LispTokenKind::BlockComment);
        i = j;
        if (depth > 0) return LispLexState::pack(LispLexState::InBlockComment, depth, pdepth);
        mode = LispLexState::Normal;
        bdepth = 0;
    }

    // ── 主迴圈 ──────────────────────────────────────────────────────────
    while (i < n) {
        const QChar c = text.at(i);

        if (c.isSpace()) { ++i; continue; }

        if (c == QLatin1Char(';')) {
            int j = text.indexOf(QLatin1Char('\n'), i);
            if (j < 0) j = n;
            addToken(i, j - i, LispTokenKind::LineComment);
            i = j;
            continue;
        }

        if (c == QLatin1Char('(')) {
            addToken(i, 1, LispTokenKind::OpenParen, pdepth);
            ++pdepth; ++i;
            continue;
        }
        if (c == QLatin1Char(')')) {
            if (pdepth > 0) --pdepth;
            addToken(i, 1, LispTokenKind::CloseParen, pdepth);
            ++i;
            continue;
        }

        if (c == QLatin1Char('"')) {
            bool closed = false;
            const int j = scanString(i + 1, closed);
            addToken(i, j - i, LispTokenKind::String);
            i = j;
            if (!closed) return LispLexState::pack(LispLexState::InString, 0, pdepth);
            continue;
        }

        if (c == QLatin1Char('\'') || c == QLatin1Char('`')) {
            addToken(i, 1, LispTokenKind::Prefix);
            ++i;
            continue;
        }
        if (c == QLatin1Char(',')) {
            const int len = (i + 1 < n && text.at(i + 1) == QLatin1Char('@')) ? 2 : 1;
            addToken(i, len, LispTokenKind::Prefix);
            i += len;
            continue;
        }

        if (c == QLatin1Char('#') && i + 1 < n) {
            const QChar d = text.at(i + 1);

            if (d == QLatin1Char('|')) {                       // #| 區塊註解
                int depth = 1;
                const int j = scanBlock(i + 2, depth);
                addToken(i, j - i, LispTokenKind::BlockComment);
                i = j;
                if (depth > 0) return LispLexState::pack(LispLexState::InBlockComment, depth, pdepth);
                continue;
            }
            if (d == QLatin1Char('\\')) {                      // #\a  #\Space  #\(
                int j = i + 2;
                if (j < n) {
                    if (text.at(j).isHighSurrogate() && j + 1 < n && text.at(j + 1).isLowSurrogate()) j += 2;
                    else ++j;
                    while (j < n && !isTerminator(text.at(j))) ++j;
                }
                addToken(i, j - i, LispTokenKind::Char);
                i = j;
                continue;
            }
            if (d == QLatin1Char('\'')) {                      // #'
                addToken(i, 2, LispTokenKind::Prefix);
                i += 2;
                continue;
            }
            if (d == QLatin1Char('(')) {                       // #( 向量：# 當前綴，( 照一般括號處理
                addToken(i, 1, LispTokenKind::Prefix);
                ++i;
                continue;
            }
            if (d == QLatin1Char('+') || d == QLatin1Char('-')) {   // #+feature / #-feature
                addToken(i, 2, LispTokenKind::Prefix);
                i += 2;
                continue;
            }
            // 其他 # 開頭：#:foo、#xFF、#b101… 走下面的符號掃描
        }

        // ── 符號 / 數字 / 關鍵字 ───────────────────────────────────────
        int j = i;
        while (j < n) {
            const QChar ch = text.at(j);
            if (ch == QLatin1Char('\\')) { j += 2; continue; }
            if (ch == QLatin1Char('|')) {                      // |multiple escape|（單行內）
                ++j;
                while (j < n && text.at(j) != QLatin1Char('|')) ++j;
                if (j < n) ++j;
                continue;
            }
            if (isTerminator(ch)) break;
            ++j;
        }
        j = qMin(qMax(j, i + 1), n);

        const QString word = text.mid(i, j - i);
        LispTokenKind kind = LispTokenKind::Symbol;
        if (word.size() > 1 && word.at(0) == QLatin1Char(':'))      kind = LispTokenKind::Keyword;
        else if (looksLikeNumber(word))                             kind = LispTokenKind::Number;
        addToken(i, j - i, kind);
        i = j;
    }

    return LispLexState::pack(LispLexState::Normal, 0, pdepth);
}

// ═══════════════════════════════════════════════════════════════════════════
// LispDictionary
// ═══════════════════════════════════════════════════════════════════════════

namespace {

struct AicadEntry { const char* name; const char* sig; const char* doc; };

// 與 LispBindings.cpp 內註冊的函式一一對應（簽名取自該檔案的註解）。
const AicadEntry kAicadApi[] = {
    // 文件 / 特徵
    {"sketch-create",       "(sketch-create plane-name sketch-name)",          "在指定平面（如 \"XY\"）建立草圖"},
    {"extrude-create",      "(extrude-create sketch-id height extrude-name)",  "將草圖拉伸成實體特徵"},
    {"feature-delete",      "(feature-delete feature-id)",                     "刪除特徵"},
    {"chamfer-edges",       "(chamfer-edges loft-id distance '((loop-index corner-index) ...))", "對 Loft 實體的指定角點倒角"},
    {"loft-loop-count",     "(loft-loop-count loft-id)",                       "Loft 特徵的獨立封閉迴圈數"},
    {"loft-corner-count",   "(loft-corner-count loft-id loop-index)",          "指定迴圈的斷面角點數"},
    // 幾何
    {"line",                "(line x1 y1 x2 y2)",                              "繪製直線"},
    {"rectangle",           "(rectangle x1 y1 x2 y2)",                         "繪製矩形（對角兩點）"},
    {"circle",              "(circle x y radius)",                             "繪製圓"},
    {"arc",                 "(arc x y radius start-angle end-angle)",          "繪製圓弧"},
    {"polyline",            "(polyline x1 y1 x2 y2 x3 y3 ...)",                "繪製多段線"},
    // 視圖
    {"view-set",            "(view-set view-name)",                            "設定視圖：\"TOP\" \"FRONT\" \"RIGHT\" \"ISOMETRIC\""},
    {"view-fit",            "(view-fit)",                                      "縮放至全部顯示"},
    {"view-zoom",           "(view-zoom factor)",                              "縮放視圖"},
    // 查詢
    {"feature-list",        "(feature-list)",                                  "取得所有特徵"},
    {"feature-info",        "(feature-info feature-id)",                       "取得特徵資訊"},
    {"feature-count",       "(feature-count)",                                 "特徵數量"},
    {"sketch-polylines",    "(sketch-polylines sketch-id)",                    "取得草圖的折線"},
    // 工具
    {"print-message",       "(print-message message)",                         "在訊息區顯示文字"},
    {"distance",            "(distance x1 y1 x2 y2)",                          "兩點距離"},
    {"angle",               "(angle x1 y1 x2 y2)",                             "兩點連線角度"},
    {"polar",               "(polar x y distance angle)",                      "極座標計算"},
    // OSnap
    {"osnap-enable",        "(osnap-enable t/nil)",                            "開關物件鎖點"},
    {"osnap-set",           "(osnap-set type1 type2 ...)",                     "設定鎖點類型，如 (osnap-set \"endpoint\" \"midpoint\")"},
    // 草圖約束
    {"sk-constrain-coincident",   "(sk-constrain-coincident geom-uuid-1 handle-1 geom-uuid-2 handle-2)", "重合約束"},
    {"sk-constrain-horizontal",   "(sk-constrain-horizontal line-uuid)",       "水平約束"},
    {"sk-constrain-vertical",     "(sk-constrain-vertical line-uuid)",         "垂直約束"},
    {"sk-constrain-parallel",     "(sk-constrain-parallel line-uuid-1 line-uuid-2)",      "平行約束"},
    {"sk-constrain-perpendicular","(sk-constrain-perpendicular line-uuid-1 line-uuid-2)", "垂直（正交）約束"},
    {"sk-constrain-tangent",      "(sk-constrain-tangent geom-uuid-1 geom-uuid-2)",       "相切約束"},
    {"sk-constrain-concentric",   "(sk-constrain-concentric uuid-1 uuid-2)",   "同心約束"},
    {"sk-constrain-equal-length", "(sk-constrain-equal-length line-uuid-1 line-uuid-2)",  "等長約束"},
    {"sk-constrain-equal-radius", "(sk-constrain-equal-radius uuid-1 uuid-2)", "等半徑約束"},
    {"sk-constrain-collinear",    "(sk-constrain-collinear line-uuid-1 line-uuid-2)",     "共線約束"},
    {"sk-constrain-midpoint",     "(sk-constrain-midpoint point-uuid line-uuid)",         "中點約束"},
    {"sk-constrain-symmetric",    "(sk-constrain-symmetric uuid-a handle-a uuid-b handle-b axis-uuid)", "對稱約束"},
    {"sk-constrain-fixed",        "(sk-constrain-fixed geom-uuid)",            "固定約束"},
    {"sk-constrain-dist",         "(sk-constrain-dist uuid-1 handle-1 uuid-2 handle-2 dist)", "距離約束"},
    {"sk-constrain-radius",       "(sk-constrain-radius geom-uuid radius-or-expr)",       "半徑約束"},
    {"sk-constrain-angle",        "(sk-constrain-angle uuid-1 h1 uuid-2 h2 angle-deg)",   "角度約束（度）"},
    {"sk-constrain-fix-x",        "(sk-constrain-fix-x geom-uuid x-val)",      "固定 X 座標"},
    {"sk-constrain-fix-y",        "(sk-constrain-fix-y geom-uuid y-val)",      "固定 Y 座標"},
    {"sk-remove-constraint",      "(sk-remove-constraint uuid)",               "移除約束"},
    {"sk-list-constraints",       "(sk-list-constraints)",                     "列出約束"},
    {"sk-dof",                    "(sk-dof)",                                  "草圖剩餘自由度"},
    {"sk-solve",                  "(sk-solve)",                                "求解草圖約束，成功回傳 t"},
    {"sk-edit-constraint",        "(sk-edit-constraint uuid new-value-or-expr)", "修改約束值或運算式"},
};

const char* const kSpecialForms =
    "block catch eval-when flet function go if labels let let* load-time-value locally macrolet "
    "multiple-value-call multiple-value-prog1 progn progv quote return-from setq symbol-macrolet "
    "tagbody the throw unwind-protect declare";

const char* const kMacros =
    "and assert case ccase check-type cond ctypecase declaim decf defclass defconstant defgeneric "
    "define-condition define-modify-macro defmacro defmethod defpackage defparameter defsetf defstruct "
    "deftype defun defvar destructuring-bind do do* do-all-symbols do-external-symbols do-symbols dolist "
    "dotimes ecase etypecase handler-bind handler-case ignore-errors in-package incf lambda loop "
    "loop-finish multiple-value-bind multiple-value-list multiple-value-setq nth-value or otherwise pop "
    "prog prog* prog1 prog2 psetf psetq push pushnew remf restart-bind restart-case return rotatef setf "
    "shiftf step time trace typecase unless untrace when with-accessors with-input-from-string "
    "with-open-file with-open-stream with-output-to-string with-slots with-standard-io-syntax";

const char* const kFunctions =
    // 數值
    "+ - * / = /= < > <= >= 1+ 1- abs acos asin atan ceiling cos exp expt floor float floatp gcd "
    "integerp isqrt lcm log max min minusp mod numberp oddp evenp plusp random rational realp rem "
    "round signum sin sqrt tan truncate zerop ash logand logior logxor lognot "
    "numerator denominator complex realpart imagpart phase cis sinh cosh tanh ffloor fceiling ftruncate fround "
    // cons / list
    "acons adjoin append apply assoc assoc-if butlast car cdr caar cadr cdar cddr caddr cdddr cadddr cons "
    "consp copy-list copy-tree endp eighth fifth first fourth getf last ldiff length list list* "
    "list-length listp make-list mapc mapcan mapcar mapcon mapl maplist member member-if nconc nreverse "
    "nth nthcdr null pairlis rassoc remove remove-duplicates remove-if remove-if-not reverse rest "
    "revappend second seventh sixth subst sublis tailp tenth third union intersection set-difference "
    "set-exclusive-or subsetp tree-equal rplaca rplacd nsubst nunion nintersection "
    // 序列 / 陣列
    "aref coerce concatenate copy-seq count count-if delete delete-duplicates delete-if elt every fill "
    "find find-if find-if-not map map-into merge notany notevery position position-if reduce replace "
    "search some sort stable-sort subseq mismatch substitute make-array vector vectorp svref "
    "array-dimension array-dimensions array-total-size adjust-array vector-push vector-push-extend vector-pop "
    "bit sbit arrayp make-sequence "
    // 字元 / 字串
    "char char-code char-downcase char-upcase char= char< char> characterp code-char digit-char-p "
    "alpha-char-p alphanumericp upper-case-p lower-case-p graphic-char-p both-case-p "
    "make-string parse-integer prin1-to-string princ-to-string string string-capitalize string-downcase "
    "string-equal string-left-trim string-right-trim string-trim string-upcase string= string< string> "
    "string-lessp string-greaterp stringp symbol-name intern make-symbol gensym keywordp symbolp "
    "string/= string<= string>= digit-char char-int char-name name-char "
    // 控制 / 函式
    "boundp complement constantly eq eql equal equalp fboundp funcall function-lambda-expression "
    "identity not symbol-function symbol-value values values-list eval macroexpand macroexpand-1 "
    "functionp compiled-function-p fdefinition makunbound fmakunbound "
    // 型別
    "typep type-of atom subtypep "
    // 輸入 / 輸出
    "format print prin1 princ terpri fresh-line write write-line write-string write-char write-to-string "
    "read read-line read-from-string read-char peek-char finish-output force-output pprint "
    "y-or-n-p yes-or-no-p open close with-open-file file-position probe-file directory "
    "make-string-output-stream get-output-stream-string make-string-input-stream "
    "load compile compile-file "
    // 條件
    "error warn signal cerror invoke-restart find-restart make-condition abort continue "
    "condition-p simple-condition-format-control "
    // 雜湊表 / property list / 符號
    "make-hash-table gethash remhash clrhash maphash hash-table-count hash-table-p "
    "get remprop symbol-plist symbol-package "
    // 時間 / 其他
    "get-universal-time get-internal-real-time get-internal-run-time sleep room "
    "documentation describe inspect apropos apropos-list machine-type lisp-implementation-type "
    "lisp-implementation-version software-type random-state-p make-random-state "
    // 物件系統
    "make-instance slot-value slot-boundp class-of class-name find-class "
    "initialize-instance print-object change-class shared-initialize "
    "next-method-p call-next-method"
    ;

const char* const kConstants = "t nil pi most-positive-fixnum most-negative-fixnum "
                               "most-positive-double-float most-negative-double-float "
                               "double-float-epsilon single-float-epsilon";

struct BodyIndentEntry { const char* names; int n; };
const BodyIndentEntry kBodyIndent[] = {
    // 前 n 個參數為 distinguished（縮 4），其後 body 縮 2
    {"progn tagbody ignore-errors", 0},
    {"let let* when unless dolist dotimes lambda flet labels macrolet symbol-macrolet block case ecase "
     "ccase typecase etypecase ctypecase catch prog1 handler-case handler-bind restart-case "
     "restart-bind unwind-protect with-open-file with-open-stream with-output-to-string "
     "with-input-from-string with-slots with-accessors with-standard-io-syntax multiple-value-prog1 "
     "multiple-value-call locally eval-when return-from progv do-symbols do-external-symbols "
     "do-all-symbols defstruct deftype defvar defparameter defconstant in-package declaim", 1},
    {"defun defmacro defmethod defgeneric destructuring-bind multiple-value-bind do do* "
     "define-condition define-modify-macro defsetf defpackage prog prog*", 2},
    {"defclass", 3},
};

QStringList splitWords(const QString& s)
{
    // QString::SkipEmptyParts 在 Qt6 已移除；Qt::SkipEmptyParts 要 Qt 5.14+
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    return s.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
#else
    return s.split(QRegularExpression(QStringLiteral("\\s+")), QString::SkipEmptyParts);
#endif
}

} // anonymous namespace

const LispDictionary& LispDictionary::instance()
{
    static const LispDictionary dict;
    return dict;
}

void LispDictionary::add(const QString& names, LispSymbolCategory cat)
{
    const QStringList list = splitWords(names);
    for (const QString& nm : list) {
        LispSymbolInfo info;
        info.name     = nm;
        info.category = cat;
        m_symbols.insert(nm, info);
    }
}

LispDictionary::LispDictionary()
{
    add(QString::fromLatin1(kFunctions),    LispSymbolCategory::Function);
    add(QString::fromLatin1(kConstants),    LispSymbolCategory::Function);
    add(QString::fromLatin1(kMacros),       LispSymbolCategory::Macro);
    add(QString::fromLatin1(kSpecialForms), LispSymbolCategory::SpecialForm);

    for (const AicadEntry& e : kAicadApi) {
        LispSymbolInfo info;
        info.name      = QString::fromLatin1(e.name);
        info.category  = LispSymbolCategory::Aicad;
        info.signature = QString::fromLatin1(e.sig);
        info.doc       = QString::fromUtf8(e.doc);
        m_symbols.insert(info.name, info);
    }

    // 少數常用形式附簽名，讓參數提示更有用
    struct Sig { const char* name; const char* sig; };
    static const Sig sigs[] = {
        {"defun",   "(defun name (params...) body...)"},
        {"defmacro","(defmacro name (params...) body...)"},
        {"defvar",  "(defvar name [value [doc]])"},
        {"defparameter", "(defparameter name value [doc])"},
        {"let",     "(let ((var value)...) body...)"},
        {"let*",    "(let* ((var value)...) body...)"},
        {"lambda",  "(lambda (params...) body...)"},
        {"if",      "(if test then [else])"},
        {"when",    "(when test body...)"},
        {"unless",  "(unless test body...)"},
        {"cond",    "(cond (test body...)...)"},
        {"case",    "(case key (keys body...)...)"},
        {"dolist",  "(dolist (var list [result]) body...)"},
        {"dotimes", "(dotimes (var count [result]) body...)"},
        {"setf",    "(setf place value ...)"},
        {"setq",    "(setq var value ...)"},
        {"loop",    "(loop [for var in/from/below ...] [do|collect|sum ...])"},
        {"format",  "(format destination control-string args...)"},
        {"mapcar",  "(mapcar function list...)"},
        {"apply",   "(apply function args... list)"},
        {"funcall", "(funcall function args...)"},
        {"handler-case", "(handler-case form (condition (var) body...)...)"},
        {"ignore-errors","(ignore-errors body...)"},
        {"format",  "(format destination control-string args...)"},
    };
    for (const Sig& s : sigs) {
        auto it = m_symbols.find(QString::fromLatin1(s.name));
        if (it != m_symbols.end() && it->signature.isEmpty())
            it->signature = QString::fromLatin1(s.sig);
    }

    for (const BodyIndentEntry& e : kBodyIndent) {
        const QStringList list = splitWords(QString::fromLatin1(e.names));
        for (const QString& nm : list) m_bodyIndent.insert(nm, e.n);
    }

    m_allNames = m_symbols.keys();
    std::sort(m_allNames.begin(), m_allNames.end());
}

static QString normalizeName(const QString& name)
{
    QString n = name.toLower();
    if (n.startsWith(QLatin1Char(':'))) return n;           // 關鍵字不剝除
    const int c = n.lastIndexOf(QLatin1Char(':'));
    if (c >= 0) n = n.mid(c + 1);                            // cl:car、cl-user::foo → car、foo
    return n;
}

const LispSymbolInfo* LispDictionary::find(const QString& name) const
{
    auto it = m_symbols.constFind(normalizeName(name));
    return it == m_symbols.constEnd() ? nullptr : &it.value();
}

int LispDictionary::bodyIndent(const QString& name) const
{
    const QString n = normalizeName(name);
    auto it = m_bodyIndent.constFind(n);
    if (it != m_bodyIndent.constEnd()) return it.value();
    if (n.startsWith(QLatin1String("define-"))) return 2;
    if (n.startsWith(QLatin1String("with-")))    return 1;
    return -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// LispDocumentIndex
// ═══════════════════════════════════════════════════════════════════════════

namespace {
bool isComment(LispTokenKind k)
{
    return k == LispTokenKind::LineComment || k == LispTokenKind::BlockComment;
}
} // anonymous namespace

void LispDocumentIndex::rebuild(const QString& text)
{
    m_text = text;
    m_tokens.clear();
    m_mate.clear();
    m_lineStarts.clear();
    m_forms.clear();
    m_unmatched = 0;

    LispLexer::lex(m_text, 0, &m_tokens);

    // 行起點
    m_lineStarts.append(0);
    for (int i = 0; i < m_text.size(); ++i)
        if (m_text.at(i) == QLatin1Char('\n')) m_lineStarts.append(i + 1);

    // 括號配對
    const int N = m_tokens.size();
    m_mate.fill(-1, N);
    QVector<int> stack;
    for (int i = 0; i < N; ++i) {
        const LispToken& t = m_tokens.at(i);
        if (t.kind == LispTokenKind::OpenParen) {
            stack.append(i);
        } else if (t.kind == LispTokenKind::CloseParen) {
            if (stack.isEmpty()) {
                ++m_unmatched;
            } else {
                const int o = stack.takeLast();
                m_mate[o] = i;
                m_mate[i] = o;
            }
        }
    }
    m_unmatched += stack.size();

    // 頂層表達式
    int i = 0;
    while (i < N) {
        const LispToken* t = &m_tokens.at(i);
        if (isComment(t->kind)) { ++i; continue; }
        if (t->kind == LispTokenKind::CloseParen) { ++i; continue; }   // 多餘的 )

        const int formStart = t->start;
        int formEnd = t->end();

        while (t->kind == LispTokenKind::Prefix) {
            formEnd = t->end();
            ++i;
            while (i < N && isComment(m_tokens.at(i).kind)) ++i;
            if (i >= N) break;
            t = &m_tokens.at(i);
        }
        if (i >= N) { m_forms.append(qMakePair(formStart, formEnd)); break; }

        if (t->kind == LispTokenKind::OpenParen) {
            const int mate = m_mate.at(i);
            if (mate < 0) {                       // 未關閉：延伸到文件結尾
                formEnd = m_text.size();
                i = N;
            } else {
                formEnd = m_tokens.at(mate).end();
                i = mate + 1;
            }
        } else if (t->kind == LispTokenKind::CloseParen) {
            // 前綴後面直接跟 )：前綴自成一個（壞的）表達式
            m_forms.append(qMakePair(formStart, formEnd));
            continue;
        } else {
            formEnd = t->end();
            ++i;
        }
        m_forms.append(qMakePair(formStart, formEnd));
    }
}

int LispDocumentIndex::tokenIndexAt(int pos) const
{
    // 最後一個 start <= pos 的 token
    int lo = 0, hi = m_tokens.size() - 1, ans = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (m_tokens.at(mid).start <= pos) { ans = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    if (ans >= 0 && pos < m_tokens.at(ans).end()) return ans;
    return -1;
}

bool LispDocumentIndex::isInStringOrComment(int pos) const
{
    int ti = tokenIndexAt(pos);
    if (ti < 0) {
        // pos 剛好在 token 結尾：行註解、以及文件尾端尚未結束的字串 / 區塊註解，
        // 游標停在結尾時仍然是「在其中」（使用者正在註解或字串裡打字）。
        ti = tokenIndexAt(pos - 1);
        if (ti < 0 || m_tokens.at(ti).end() != pos) return false;
        const LispToken& t = m_tokens.at(ti);
        if (t.kind == LispTokenKind::LineComment) return pos > t.start;
        if (pos != m_text.size()) return false;
        if (t.kind == LispTokenKind::String)
            return t.length == 1 || m_text.at(pos - 1) != QLatin1Char('"')
                   || (t.length >= 2 && m_text.at(pos - 2) == QLatin1Char('\\'));
        if (t.kind == LispTokenKind::BlockComment)
            return !m_text.endsWith(QLatin1String("|#"));
        return false;
    }
    const LispToken& t = m_tokens.at(ti);
    if (t.kind == LispTokenKind::String) return pos > t.start;          // 開頭引號之前不算
    if (isComment(t.kind))               return pos > t.start;
    return false;
}

int LispDocumentIndex::matchingParen(int pos) const
{
    const int ti = tokenIndexAt(pos);
    if (ti < 0) return -1;
    const LispToken& t = m_tokens.at(ti);
    if (t.kind != LispTokenKind::OpenParen && t.kind != LispTokenKind::CloseParen) return -1;
    const int mate = m_mate.at(ti);
    return mate < 0 ? -1 : m_tokens.at(mate).start;
}

bool LispDocumentIndex::isUnmatchedParen(int pos) const
{
    const int ti = tokenIndexAt(pos);
    if (ti < 0) return false;
    const LispToken& t = m_tokens.at(ti);
    return (t.kind == LispTokenKind::OpenParen || t.kind == LispTokenKind::CloseParen)
           && m_mate.at(ti) < 0;
}

int LispDocumentIndex::lineStartOf(int pos) const
{
    auto it = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), pos);
    if (it == m_lineStarts.begin()) return 0;
    return *(it - 1);
}

int LispDocumentIndex::lineOf(int pos) const
{
    auto it = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), pos);
    return int(it - m_lineStarts.begin()) - 1;
}

QString LispDocumentIndex::tokenText(int index) const
{
    if (index < 0 || index >= m_tokens.size()) return QString();
    const LispToken& t = m_tokens.at(index);
    return m_text.mid(t.start, t.length);
}

bool LispDocumentIndex::formAt(int pos, int* start, int* end) const
{
    pos = qBound(0, pos, int(m_text.size()));
    auto onlySpaceBetween = [&](int a, int b) {
        if (a > b || b > m_text.size()) return false;
        for (int i = a; i < b; ++i) {
            const QChar c = m_text.at(i);
            if (!c.isSpace() || c == QLatin1Char('\n')) return false;
        }
        return true;
    };

    // 1. 含有 pos
    for (const auto& f : m_forms) {
        if (f.first <= pos && pos < f.second) {
            if (start) *start = f.first;
            if (end)   *end   = f.second;
            return true;
        }
    }
    // 2. 同一行上、緊鄰 pos 之前
    for (int k = m_forms.size() - 1; k >= 0; --k) {
        const auto& f = m_forms.at(k);
        if (f.second <= pos) {
            if (onlySpaceBetween(f.second, pos)) {
                if (start) *start = f.first;
                if (end)   *end   = f.second;
                return true;
            }
            break;
        }
    }
    // 3. 同一行上、緊接 pos 之後
    for (const auto& f : m_forms) {
        if (f.first >= pos) {
            if (onlySpaceBetween(pos, f.first)) {
                if (start) *start = f.first;
                if (end)   *end   = f.second;
                return true;
            }
            break;
        }
    }
    return false;
}

// ── 縮排 / 呼叫內容共用的括號堆疊 ────────────────────────────────────────

struct LispDocumentIndex::Frame {
    int           openPos     = -1;      ///< "(" 位置；-1 = 頂層
    bool          quoted      = false;   ///< 前面有 ' ` #' # —— 當資料列表處理
    int           count       = 0;       ///< 已開始的元素數（含運算子）
    int           firstStart  = -1;
    int           secondStart = -1;
    int           firstTok    = -1;
    LispTokenKind firstKind   = LispTokenKind::Symbol;
    int           elemIndex   = -1;      ///< 這個列表在父列表中是第幾個元素
};

QVector<LispDocumentIndex::Frame>
LispDocumentIndex::buildFrames(int limitPos, bool* touching) const
{
    QVector<Frame> st;
    st.append(Frame());                               // 頂層

    bool pendingPrefix = false;
    bool pendingQuote  = false;                       // 前綴是 ' ` #' 或 #（資料）
    if (touching) *touching = false;

    auto beginElement = [&](int pos, LispTokenKind kind, int tokIdx) {
        Frame& f = st.last();
        if (f.count == 0)      { f.firstStart = pos; f.firstKind = kind; f.firstTok = tokIdx; }
        else if (f.count == 1) { f.secondStart = pos; }
        ++f.count;
    };

    for (int i = 0; i < m_tokens.size(); ++i) {
        const LispToken& t = m_tokens.at(i);
        if (t.start >= limitPos) break;
        if (isComment(t.kind)) continue;

        if (touching) *touching = (t.kind != LispTokenKind::OpenParen
                                   && t.kind != LispTokenKind::CloseParen
                                   && t.end() >= limitPos);

        switch (t.kind) {
        case LispTokenKind::Prefix: {
            if (!pendingPrefix) {
                beginElement(t.start, LispTokenKind::Prefix, i);
                pendingPrefix = true;
                pendingQuote  = false;
            }
            const QString p = m_text.mid(t.start, t.length);
            if (p == QLatin1String("'") || p == QLatin1String("`") ||
                p == QLatin1String("#'") || p == QLatin1String("#"))
                pendingQuote = true;
            break;
        }
        case LispTokenKind::OpenParen: {
            if (!pendingPrefix) beginElement(t.start, LispTokenKind::OpenParen, i);
            Frame f;
            f.openPos = t.start;
            f.quoted  = pendingQuote;
            f.elemIndex = st.last().count - 1;
            st.append(f);
            pendingPrefix = false;
            pendingQuote  = false;
            break;
        }
        case LispTokenKind::CloseParen:
            if (st.size() > 1) st.removeLast();
            pendingPrefix = false;
            pendingQuote  = false;
            break;
        default:                                       // 原子
            if (!pendingPrefix) beginElement(t.start, t.kind, i);
            pendingPrefix = false;
            pendingQuote  = false;
            break;
        }
    }
    return st;
}

int LispDocumentIndex::indentFor(int lineStart) const
{
    // 行首落在字串 / 區塊註解內：不動
    const int ti = tokenIndexAt(lineStart);
    if (ti >= 0) {
        const LispToken& t = m_tokens.at(ti);
        if ((t.kind == LispTokenKind::String || t.kind == LispTokenKind::BlockComment)
            && t.start < lineStart)
            return -1;
    }

    const QVector<Frame> st = buildFrames(lineStart, nullptr);
    const Frame& f = st.last();
    if (st.size() == 1) return 0;                     // 頂層

    auto col = [&](int pos) { return pos - lineStartOf(pos); };

    const int openCol = col(f.openPos);

    if (f.count == 0)                                  // "(" 後面還沒有任何東西
        return openCol + (f.quoted ? 1 : 2);

    // 資料列表 / 第一個元素不是符號：對齊第一個元素
    if (f.quoted || f.firstKind != LispTokenKind::Symbol)
        return col(f.firstStart);

    // case / handler-case 的子句 (error (e) body...)：body 縮 2，而不是對齊 (e)
    {
        const Frame& parent = st.at(st.size() - 2);
        if (parent.firstKind == LispTokenKind::Symbol && parent.firstTok >= 0 && f.elemIndex >= 2) {
            static const QStringList clauseForms = {
                QStringLiteral("handler-case"), QStringLiteral("restart-case"), QStringLiteral("case"),
                QStringLiteral("ecase"), QStringLiteral("ccase"), QStringLiteral("typecase"),
                QStringLiteral("etypecase"), QStringLiteral("ctypecase") };
            QString pop = tokenText(parent.firstTok).toLower();
            const int colon = pop.lastIndexOf(QLatin1Char(':'));
            if (colon >= 0) pop = pop.mid(colon + 1);
            if (clauseForms.contains(pop)) return openCol + 2;
        }
    }

    const QString op = tokenText(f.firstTok);
    const int n = LispDictionary::instance().bodyIndent(op);
    const int argsSoFar = f.count - 1;

    if (n >= 0)
        return argsSoFar < n ? openCol + 4 : openCol + 2;

    // 一般函式呼叫：對齊第一個參數；運算子後面還沒有參數（參數換到下一行）→ 括號右邊一格，
    // 與 Emacs 相同：(t\n 'z)、(message\n \"…\")
    if (f.count >= 2) return col(f.secondStart);
    return openCol + 1;
}

LispCallContext LispDocumentIndex::callContextAt(int pos) const
{
    LispCallContext ctx;
    if (isInStringOrComment(pos)) return ctx;

    bool touching = false;
    const QVector<Frame> st = buildFrames(pos, &touching);
    if (st.size() < 2) return ctx;

    const Frame& f = st.last();
    if (f.quoted || f.count == 0 || f.firstKind != LispTokenKind::Symbol) return ctx;

    // 游標還停在運算子本身上（(lin|）：沒有參數可言
    if (f.count == 1 && touching) return ctx;

    ctx.valid      = true;
    ctx.op         = tokenText(f.firstTok).toLower();
    ctx.openPos    = f.openPos;
    ctx.paramIndex = qMax(0, f.count - 1 - (touching ? 1 : 0));
    return ctx;
}

QVector<LispDefinition> LispDocumentIndex::definitions() const
{
    static const QStringList kinds = {
        QStringLiteral("defun"), QStringLiteral("defmacro"), QStringLiteral("defvar"),
        QStringLiteral("defparameter"), QStringLiteral("defconstant"), QStringLiteral("defstruct"),
        QStringLiteral("defclass"), QStringLiteral("defgeneric"), QStringLiteral("defmethod"),
        QStringLiteral("deftype")
    };

    QVector<LispDefinition> out;
    for (const auto& f : m_forms) {
        int ti = tokenIndexAt(f.first);
        if (ti < 0 || m_tokens.at(ti).kind != LispTokenKind::OpenParen) continue;

        auto nextSignificant = [&](int from) {
            int k = from + 1;
            while (k < m_tokens.size() && isComment(m_tokens.at(k).kind)) ++k;
            return k;
        };

        int k = nextSignificant(ti);
        if (k >= m_tokens.size() || m_tokens.at(k).kind != LispTokenKind::Symbol) continue;
        const QString kind = tokenText(k).toLower();
        if (!kinds.contains(kind)) continue;

        k = nextSignificant(k);
        if (k < m_tokens.size() && m_tokens.at(k).kind == LispTokenKind::OpenParen) {
            k = nextSignificant(k);                                  // (defstruct (name ...)) / (defun (setf x) ...)
            if (k < m_tokens.size() && m_tokens.at(k).kind == LispTokenKind::Symbol
                && tokenText(k).compare(QLatin1String("setf"), Qt::CaseInsensitive) == 0)
                k = nextSignificant(k);                              // (defun (setf thing) ...) → thing
        }
        if (k >= m_tokens.size() || m_tokens.at(k).kind != LispTokenKind::Symbol) continue;

        LispDefinition d;
        d.kind = kind;
        d.name = tokenText(k);
        d.pos  = f.first;
        d.line = lineOf(f.first);

        // defun / defmacro：把 lambda list 做成簽名，供參數提示使用
        if (kind == QLatin1String("defun") || kind == QLatin1String("defmacro")) {
            const int ll = nextSignificant(k);
            if (ll < m_tokens.size() && m_tokens.at(ll).kind == LispTokenKind::OpenParen && m_mate.at(ll) > ll) {
                const int a = m_tokens.at(ll).start + 1;
                const int b = m_tokens.at(m_mate.at(ll)).start;
                const QString inner = m_text.mid(a, b - a).simplified();
                d.signature = QLatin1Char('(') + d.name + (inner.isEmpty() ? QString() : QLatin1Char(' ') + inner)
                              + QLatin1Char(')');
            }
        }
        out.append(d);
    }
    return out;
}

} // namespace scripting
} // namespace aicad
