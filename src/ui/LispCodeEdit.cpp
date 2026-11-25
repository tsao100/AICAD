/**
 * @file LispCodeEdit.cpp
 */
#include "LispCodeEdit.h"

#include <climits>

#include <QAbstractItemView>
#include <QCompleter>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSet>
#include <QStringListModel>
#include <QTextBlock>
#include <QTimer>

namespace aicad {
namespace ui {

using namespace scripting;

// ═══════════════════════════════════════════════════════════════════════════
// 行號區
// ═══════════════════════════════════════════════════════════════════════════

class LispCodeEdit::LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(LispCodeEdit* e) : QWidget(e), m_edit(e) {}
    QSize sizeHint() const override { return QSize(m_edit->lineNumberAreaWidth(), 0); }
protected:
    void paintEvent(QPaintEvent* ev) override { m_edit->lineNumberAreaPaintEvent(ev); }
private:
    LispCodeEdit* m_edit;
};

namespace {

bool isSymChar(QChar c)
{
    if (c.isLetterOrNumber()) return true;
    switch (c.unicode()) {
    case '-': case '_': case '*': case '+': case '/': case '<': case '>': case '=':
    case '!': case '?': case '%': case '&': case '.': case ':': case '$': case '@': case '^': case '~':
        return true;
    default:
        return false;
    }
}

int leadingSpaces(const QString& s)
{
    int i = 0;
    while (i < s.size() && (s.at(i) == QLatin1Char(' ') || s.at(i) == QLatin1Char('\t'))) ++i;
    return i;
}

QString htmlEscape(const QString& s) { return s.toHtmlEscaped(); }

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════
// 建構
// ═══════════════════════════════════════════════════════════════════════════

LispCodeEdit::LispCodeEdit(QWidget* parent)
    : QPlainTextEdit(parent)
    , m_lineNumbers(new LineNumberArea(this))
    , m_theme(LispEditorTheme::forBackground(palette().color(QPalette::Base)))
{
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setUndoRedoEnabled(true);
    setEditorFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_defaultPointSize = font().pointSizeF() > 0 ? font().pointSizeF() : 10;

    m_highlighter = new LispSyntaxHighlighter(document(), m_theme);

    m_completionModel = new QStringListModel(this);
    m_completer = new QCompleter(m_completionModel, this);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setMaxVisibleItems(12);
    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
            this, &LispCodeEdit::insertCompletion);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &LispCodeEdit::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest,     this, &LispCodeEdit::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &LispCodeEdit::onCursorMoved);

    // 任何編輯都清掉錯誤底線
    // QTextDocument::revision() 在 setPlainText()/clear() 後會重新計數，可能撞到舊的值，
    // 不能單靠它判斷索引是否過期：任何內容變動都直接讓快取失效。
    connect(document(), &QTextDocument::contentsChange, this, [this]() {
        m_indexRevision = -1;
        m_wordsRevision = -2;
        if (m_errorStart >= 0) { m_errorStart = m_errorEnd = -1; refreshSelections(); }
    });

    setTheme(m_theme);
    updateLineNumberAreaWidth();
    refreshSelections();
}

LispCodeEdit::~LispCodeEdit() = default;

// ═══════════════════════════════════════════════════════════════════════════
// 外觀
// ═══════════════════════════════════════════════════════════════════════════

void LispCodeEdit::setTheme(const LispEditorTheme& theme)
{
    m_theme = theme;
    QPalette p = palette();
    p.setColor(QPalette::Base, theme.background);
    p.setColor(QPalette::Text, theme.foreground);
    setPalette(p);
    if (m_highlighter) m_highlighter->setTheme(theme);
    m_lineNumbers->update();
    refreshSelections();
}

void LispCodeEdit::setEditorFont(const QFont& font)
{
    setFont(font);
    setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 2);
    updateLineNumberAreaWidth();
}

void LispCodeEdit::setEditorFontSize(qreal pointSize)
{
    pointSize = qBound<qreal>(6, pointSize, 48);
    if (qFuzzyCompare(font().pointSizeF(), pointSize)) return;
    QFont f = font();
    f.setPointSizeF(pointSize);
    setEditorFont(f);                    // 同步更新 Tab 寬度與行號區寬度
    m_lineNumbers->update();
    Q_EMIT fontSizeChanged(pointSize);
}

void LispCodeEdit::wheelEvent(QWheelEvent* e)
{
    // Ctrl + 滾輪：縮放文字大小（每格 1pt）；其餘情況照一般捲動
    if (e->modifiers() & Qt::ControlModifier) {
        const int dy = e->angleDelta().y();
        if (dy != 0) setEditorFontSize(editorFontSize() + (dy > 0 ? 1 : -1));
        e->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(e);
}

void LispCodeEdit::setExtraCompletions(const QStringList& words)
{
    m_extraWords = words;
    m_wordsRevision = -2;
}

int LispCodeEdit::lineNumberAreaWidth() const
{
    int digits = 1;
    int mx = qMax(1, blockCount());
    while (mx >= 10) { mx /= 10; ++digits; }
    digits = qMax(digits, 3);
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void LispCodeEdit::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void LispCodeEdit::updateLineNumberArea(const QRect& rect, int dy)
{
    if (dy) m_lineNumbers->scroll(0, dy);
    else    m_lineNumbers->update(0, rect.y(), m_lineNumbers->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth();
}

void LispCodeEdit::resizeEvent(QResizeEvent* e)
{
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    m_lineNumbers->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void LispCodeEdit::focusOutEvent(QFocusEvent* e)
{
    if (m_completer->popup()) m_completer->popup()->hide();
    QPlainTextEdit::focusOutEvent(e);
}

void LispCodeEdit::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(m_lineNumbers);
    painter.fillRect(event->rect(), m_theme.lineNumberBg);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = int(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + int(blockBoundingRect(block).height());
    const int curLine = textCursor().blockNumber();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(blockNumber == curLine ? m_theme.foreground : m_theme.lineNumberFg);
            painter.drawText(0, top, m_lineNumbers->width() - 6, fontMetrics().height(),
                             Qt::AlignRight, QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + int(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 結構索引 / 查詢
// ═══════════════════════════════════════════════════════════════════════════

const LispDocumentIndex& LispCodeEdit::index() const
{
    const int rev = document()->revision();
    if (rev != m_indexRevision) {
        m_index.rebuild(toPlainText());
        m_indexRevision = rev;
    }
    return m_index;
}

QChar LispCodeEdit::charAt(int pos) const
{
    return (pos >= 0 && pos < document()->characterCount() - 1) ? document()->characterAt(pos) : QChar();
}

bool LispCodeEdit::selectionOrFormToRun(QString* code, int* offset) const
{
    const QTextCursor c = textCursor();
    if (c.hasSelection()) {
        QString s = c.selectedText();
        s.replace(QChar(0x2029), QLatin1Char('\n'));
        if (s.trimmed().isEmpty()) return false;
        if (code)   *code = s;
        if (offset) *offset = c.selectionStart();
        return true;
    }
    int s = 0, e = 0;
    if (!index().formAt(c.position(), &s, &e)) return false;
    if (code)   *code = index().text().mid(s, e - s);
    if (offset) *offset = s;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// 標示（目前行 / 括號配對 / 錯誤 / 閃爍）
// ═══════════════════════════════════════════════════════════════════════════

void LispCodeEdit::setErrorRange(int start, int end)
{
    m_errorStart = start; m_errorEnd = end;
    refreshSelections();
}

void LispCodeEdit::clearErrorRange()
{
    if (m_errorStart < 0) return;
    m_errorStart = m_errorEnd = -1;
    refreshSelections();
}

void LispCodeEdit::flashRange(int start, int end)
{
    m_flashStart = start; m_flashEnd = end;
    refreshSelections();
    QTimer::singleShot(450, this, [this]() {
        m_flashStart = m_flashEnd = -1;
        refreshSelections();
    });
}

void LispCodeEdit::refreshSelections()
{
    QList<QTextEdit::ExtraSelection> sels;

    auto rangeSel = [&](int a, int b) {
        QTextEdit::ExtraSelection s;
        s.cursor = QTextCursor(document());
        s.cursor.setPosition(qBound(0, a, document()->characterCount() - 1));
        s.cursor.setPosition(qBound(0, b, document()->characterCount() - 1), QTextCursor::KeepAnchor);
        return s;
    };

    // 目前行
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection s;
        s.format.setBackground(m_theme.currentLine);
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
        s.cursor = textCursor();
        s.cursor.clearSelection();
        sels.append(s);
    }

    // 閃爍（剛執行的範圍）
    if (m_flashStart >= 0 && m_flashEnd > m_flashStart) {
        QTextEdit::ExtraSelection s = rangeSel(m_flashStart, m_flashEnd);
        s.format.setBackground(m_theme.flash);
        sels.append(s);
    }

    // 括號配對：游標後方 / 前方的括號
    const LispDocumentIndex& idx = index();
    const int pos = textCursor().position();
    int parenPos = -1;
    for (int p : {pos, pos - 1}) {
        if (p < 0) continue;
        const int ti = idx.tokenIndexAt(p);
        if (ti >= 0) {
            const LispToken& t = idx.tokens().at(ti);
            if (t.kind == LispTokenKind::OpenParen || t.kind == LispTokenKind::CloseParen) { parenPos = t.start; break; }
        }
    }
    if (parenPos >= 0) {
        const int mate = idx.matchingParen(parenPos);
        if (mate >= 0) {
            for (int p : {parenPos, mate}) {
                QTextEdit::ExtraSelection s = rangeSel(p, p + 1);
                s.format.setBackground(m_theme.matchBg);
                s.format.setFontWeight(QFont::Bold);
                sels.append(s);
            }
        } else {
            QTextEdit::ExtraSelection s = rangeSel(parenPos, parenPos + 1);
            s.format.setBackground(m_theme.mismatchBg);
            sels.append(s);
        }
    }

    // 錯誤範圍：紅色波浪底線
    if (m_errorStart >= 0 && m_errorEnd >= m_errorStart) {
        QTextEdit::ExtraSelection s = rangeSel(m_errorStart, qMax(m_errorEnd, m_errorStart + 1));
        s.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        s.format.setUnderlineColor(m_theme.errorUnderline);
        sels.append(s);
    }

    setExtraSelections(sels);
}

void LispCodeEdit::onCursorMoved()
{
    refreshSelections();

    const QTextCursor c = textCursor();
    Q_EMIT cursorInfoChanged(c.blockNumber() + 1, c.positionInBlock() + 1);

    const LispCallContext ctx = index().callContextAt(c.position());
    Q_EMIT callHintChanged(ctx.valid ? buildHint(ctx) : QString());
}

// ── 參數提示 ────────────────────────────────────────────────────────────

QString LispCodeEdit::buildHint(const LispCallContext& ctx) const
{
    QString sig, doc;
    if (const LispSymbolInfo* info = LispDictionary::instance().find(ctx.op)) {
        sig = info->signature;
        doc = info->doc;
    }
    if (sig.isEmpty()) {                                  // 使用者在本檔案中定義的函式
        for (const LispDefinition& d : index().definitions()) {
            if (d.name.compare(ctx.op, Qt::CaseInsensitive) == 0 && !d.signature.isEmpty()) {
                sig = d.signature;
                break;
            }
        }
    }
    if (sig.isEmpty()) return QString();

    // 依括號 / 方括號深度切詞："(line x1 y1)" → [line, x1, y1]；"[value [doc]]" 視為一個詞
    QString body = sig.trimmed();
    if (body.startsWith(QLatin1Char('('))) body.remove(0, 1);
    if (body.endsWith(QLatin1Char(')')))   body.chop(1);
    QStringList parts;
    QString cur;
    int depth = 0;
    for (const QChar ch : body) {
        if (ch == QLatin1Char('(') || ch == QLatin1Char('[')) ++depth;
        if (ch == QLatin1Char(')') || ch == QLatin1Char(']')) --depth;
        if (ch.isSpace() && depth <= 0) {
            if (!cur.isEmpty()) { parts << cur; cur.clear(); }
        } else {
            cur += ch;
        }
    }
    if (!cur.isEmpty()) parts << cur;
    if (parts.isEmpty()) return QString();

    const QString name = parts.takeFirst();
    // &optional / &key / &rest 之類的 lambda-list 標記不是參數：計算位置時跳過
    int hi = -1, real = 0;
    for (int i = 0; i < parts.size(); ++i) {
        if (parts.at(i).startsWith(QLatin1Char('&'))) continue;
        if (real == ctx.paramIndex) { hi = i; break; }
        ++real;
    }
    if (hi < 0) {                                              // 超出：若簽名有 "..." 或 &rest，沿用最後一個
        for (int i = 0; i < parts.size(); ++i)
            if (parts.at(i).contains(QLatin1String("...")) || parts.at(i).compare(QLatin1String("&rest")) == 0)
                hi = parts.size() - 1;
    }

    QString html = QStringLiteral("<b>%1</b>").arg(htmlEscape(name));
    for (int i = 0; i < parts.size(); ++i) {
        html += QLatin1Char(' ');
        html += (i == hi) ? QStringLiteral("<span style=\"background:#ffe58a;color:#000;\"><b>%1</b></span>")
                                .arg(htmlEscape(parts.at(i)))
                          : htmlEscape(parts.at(i));
    }
    if (!doc.isEmpty()) html += QStringLiteral(" &nbsp;—&nbsp; <i>%1</i>").arg(htmlEscape(doc));
    return html;
}

// ═══════════════════════════════════════════════════════════════════════════
// 縮排
// ═══════════════════════════════════════════════════════════════════════════

bool LispCodeEdit::reindentBlock(QTextCursor& c, const QTextBlock& block)
{
    const QString text = block.text();
    const int lead = leadingSpaces(text);
    const QString trimmed = text.mid(lead);

    int want;
    if (trimmed.isEmpty()) {
        want = 0;                                              // 空白行：清成空行
    } else if (trimmed.startsWith(QLatin1String(";;;"))) {
        want = 0;                                              // ;;; 章節註解貼齊左邊
    } else {
        want = index().indentFor(block.position());
        if (want < 0) return false;                            // 在字串 / 區塊註解內：不動
    }
    if (want == lead && text.left(lead) == QString(lead, QLatin1Char(' ')))
        return false;                                          // 已經是正確縮排

    c.setPosition(block.position());
    c.setPosition(block.position() + lead, QTextCursor::KeepAnchor);
    c.insertText(QString(want, QLatin1Char(' ')));
    return true;
}

void LispCodeEdit::reindentBlocks(int firstBlock, int lastBlock)
{
    QTextCursor c(document());
    c.beginEditBlock();
    for (int b = firstBlock; b <= lastBlock; ++b) {
        const QTextBlock block = document()->findBlockByNumber(b);
        if (!block.isValid()) break;
        // beginEditBlock 期間 QTextDocument::revision() 不會變動，必須手動讓索引失效，
        // 否則後面的行會拿舊的 token 位置去算縮排。
        if (reindentBlock(c, block)) m_indexRevision = -1;
    }
    c.endEditBlock();
}

void LispCodeEdit::reindentSelection()
{
    const QTextCursor c = textCursor();
    int first = document()->findBlock(c.selectionStart()).blockNumber();
    int last  = document()->findBlock(c.selectionEnd()).blockNumber();
    if (c.hasSelection() && last > first && document()->findBlock(c.selectionEnd()).position() == c.selectionEnd())
        --last;                                                // 選取結束在下一行行首：不含該行
    reindentBlocks(first, last);
}

void LispCodeEdit::reindentAll()
{
    reindentBlocks(0, document()->blockCount() - 1);
}

void LispCodeEdit::indentSelection(bool outdent)
{
    constexpr int kStep = 2;
    QTextCursor sc = textCursor();
    int first = document()->findBlock(sc.selectionStart()).blockNumber();
    int last  = document()->findBlock(sc.selectionEnd()).blockNumber();
    if (sc.hasSelection() && last > first && document()->findBlock(sc.selectionEnd()).position() == sc.selectionEnd())
        --last;                                                // 選取結束在下一行行首：不含該行
    const bool multi = sc.hasSelection();

    QTextCursor c(document());
    c.beginEditBlock();
    for (int b = first; b <= last; ++b) {
        const QTextBlock blk = document()->findBlockByNumber(b);
        const QString t = blk.text();
        if (outdent) {
            int n = 0;
            if (!t.isEmpty() && t.at(0) == QLatin1Char('\t')) n = 1;
            else while (n < kStep && n < t.size() && t.at(n) == QLatin1Char(' ')) ++n;
            if (n == 0) continue;
            c.setPosition(blk.position());
            c.setPosition(blk.position() + n, QTextCursor::KeepAnchor);
            c.removeSelectedText();
        } else {
            if (t.trimmed().isEmpty() && multi) continue;      // 多行縮排時不替空白行加空格
            c.setPosition(blk.position());
            c.insertText(QString(kStep, QLatin1Char(' ')));
        }
    }
    c.endEditBlock();

    if (multi) {                                               // 保持選取整段，可以連按 Tab / Shift+Tab
        QTextCursor r(document());
        r.setPosition(document()->findBlockByNumber(first).position());
        const QTextBlock lb = document()->findBlockByNumber(last);
        r.setPosition(lb.position() + lb.length() - 1, QTextCursor::KeepAnchor);
        setTextCursor(r);
    }
}

void LispCodeEdit::handleTabIndent()
{
    QTextCursor c = textCursor();
    if (c.hasSelection()) { reindentSelection(); return; }

    const int col0 = c.positionInBlock();
    const int lead0 = leadingSpaces(c.block().text());
    reindentBlocks(c.blockNumber(), c.blockNumber());

    // 游標原本在行首空白內 → 移到新縮排之後；否則保持相對文字位置
    c = textCursor();
    const QString text = c.block().text();
    const int leadNow = leadingSpaces(text);
    if (col0 <= lead0) c.setPosition(c.block().position() + leadNow);
    else               c.setPosition(c.block().position() + qMin(text.size(), col0 + (leadNow - lead0)));
    setTextCursor(c);
}

void LispCodeEdit::handleEnter()
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (c.hasSelection()) c.removeSelectedText();
    c.insertBlock();

    // 新行殘留的前導空白先清掉，再填入計算出的縮排
    const QTextBlock nb = c.block();
    const int lead = leadingSpaces(nb.text());
    c.setPosition(nb.position());
    c.setPosition(nb.position() + lead, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    c.endEditBlock();

    int ind = index().indentFor(nb.position());
    if (ind < 0) ind = leadingSpaces(nb.previous().text());   // 字串 / 區塊註解內：沿用上一行
    c.insertText(QString(qMax(0, ind), QLatin1Char(' ')));
    setTextCursor(c);
    ensureCursorVisible();
}

// ═══════════════════════════════════════════════════════════════════════════
// 自動成對括號 / 引號
// ═══════════════════════════════════════════════════════════════════════════

bool LispCodeEdit::handleAutoPair(const QString& text)
{
    if (text.size() != 1) return false;
    const QChar ch = text.at(0);
    if (ch != QLatin1Char('(') && ch != QLatin1Char(')') && ch != QLatin1Char('"')) return false;

    QTextCursor c = textCursor();
    const int pos = c.position();
    const LispDocumentIndex& idx = index();
    const bool inLiteral = idx.isInStringOrComment(pos);
    const QChar next = charAt(pos);
    const QChar prev = charAt(pos - 1);
    const bool charLiteralContext = (prev == QLatin1Char('\\') && charAt(pos - 2) == QLatin1Char('#'));

    if (charLiteralContext) return false;                       // #\( #\" 照字面輸入

    // ── 結尾 ")"：平衡時蓋過既有的 ")" ─────────────────────────────
    if (ch == QLatin1Char(')')) {
        if (!c.hasSelection() && !inLiteral && next == QLatin1Char(')') && idx.unmatchedParenCount() == 0) {
            c.movePosition(QTextCursor::Right);
            setTextCursor(c);
            return true;
        }
        return false;
    }

    // ── 引號 ────────────────────────────────────────────────────
    if (ch == QLatin1Char('"')) {
        const int ti = idx.tokenIndexAt(pos);
        const bool inString = ti >= 0 && idx.tokens().at(ti).kind == LispTokenKind::String
                              && pos > idx.tokens().at(ti).start;
        if (inString) {
            if (next == QLatin1Char('"') && prev != QLatin1Char('\\')) {   // 蓋過結尾引號
                c.movePosition(QTextCursor::Right);
                setTextCursor(c);
                return true;
            }
            return false;
        }
        if (inLiteral) return false;                            // 註解中
    } else if (inLiteral) {                                     // "(" 在字串 / 註解內
        return false;
    }

    const QChar closer = (ch == QLatin1Char('(')) ? QLatin1Char(')') : QLatin1Char('"');

    if (c.hasSelection()) {                                     // 包住選取內容
        const QString sel = c.selectedText();
        const int start = c.selectionStart();
        c.insertText(ch + sel + closer);
        c.setPosition(start + 1);
        c.setPosition(start + 1 + sel.size(), QTextCursor::KeepAnchor);
        setTextCursor(c);
        return true;
    }

    // 只有在後面是結尾 / 空白 / 右括號時才自動補結尾，避免把 "(foo" 前面插入括號時多出一個
    if (next.isNull() || next.isSpace() || next == QLatin1Char(')')) {
        c.insertText(QString(ch) + closer);
        c.movePosition(QTextCursor::Left);
        setTextCursor(c);
        return true;
    }
    return false;
}

bool LispCodeEdit::handleBackspacePair()
{
    QTextCursor c = textCursor();
    if (c.hasSelection()) return false;
    const int pos = c.position();
    const QChar prev = charAt(pos - 1), next = charAt(pos);
    if ((prev == QLatin1Char('(') && next == QLatin1Char(')')) ||
        (prev == QLatin1Char('"') && next == QLatin1Char('"'))) {
        c.beginEditBlock();
        c.deleteChar();
        c.deletePreviousChar();
        c.endEditBlock();
        setTextCursor(c);
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════
// 註解切換 / 跳行
// ═══════════════════════════════════════════════════════════════════════════

void LispCodeEdit::toggleComment()
{
    const QTextCursor sc = textCursor();
    int first = document()->findBlock(sc.selectionStart()).blockNumber();
    int last  = document()->findBlock(sc.selectionEnd()).blockNumber();
    if (sc.hasSelection() && last > first && document()->findBlock(sc.selectionEnd()).position() == sc.selectionEnd())
        --last;

    bool allCommented = true;
    int minIndent = INT_MAX;
    for (int b = first; b <= last; ++b) {
        const QString t = document()->findBlockByNumber(b).text();
        const int lead = leadingSpaces(t);
        if (t.mid(lead).isEmpty()) continue;
        minIndent = qMin(minIndent, lead);
        if (!t.mid(lead).startsWith(QLatin1Char(';'))) allCommented = false;
    }
    if (minIndent == INT_MAX) return;                           // 全是空行

    QTextCursor c(document());
    c.beginEditBlock();
    for (int b = first; b <= last; ++b) {
        const QTextBlock blk = document()->findBlockByNumber(b);
        const QString t = blk.text();
        const int lead = leadingSpaces(t);
        if (t.mid(lead).isEmpty()) continue;
        if (allCommented) {
            int n = 0;
            while (lead + n < t.size() && t.at(lead + n) == QLatin1Char(';') && n < 2) ++n;
            if (lead + n < t.size() && t.at(lead + n) == QLatin1Char(' ')) ++n;
            c.setPosition(blk.position() + lead);
            c.setPosition(blk.position() + lead + n, QTextCursor::KeepAnchor);
            c.removeSelectedText();
        } else {
            c.setPosition(blk.position() + minIndent);
            c.insertText(QStringLiteral(";; "));
        }
    }
    c.endEditBlock();
}

void LispCodeEdit::goToLine(int line1Based)
{
    const QTextBlock b = document()->findBlockByNumber(qMax(0, line1Based - 1));
    if (!b.isValid()) return;
    QTextCursor c(b);
    setTextCursor(c);
    centerCursor();
    setFocus();
}

void LispCodeEdit::goToPosition(int pos)
{
    QTextCursor c(document());
    c.setPosition(qBound(0, pos, document()->characterCount() - 1));
    setTextCursor(c);
    centerCursor();
}

// ═══════════════════════════════════════════════════════════════════════════
// 自動補全
// ═══════════════════════════════════════════════════════════════════════════

QString LispCodeEdit::wordBeforeCursor() const
{
    const QTextCursor c = textCursor();
    const QString text = c.block().text();
    int i = c.positionInBlock();
    const int end = i;
    while (i > 0 && isSymChar(text.at(i - 1))) --i;
    return text.mid(i, end - i);
}

void LispCodeEdit::rebuildCompletionWords()
{
    const int rev = document()->revision();
    if (rev == m_wordsRevision) return;
    m_wordsRevision = rev;

    QSet<QString> words;
    for (const QString& w : LispDictionary::instance().allNames()) words.insert(w);
    for (const QString& w : m_extraWords) words.insert(w.toLower());

    const LispDocumentIndex& idx = index();
    for (int i = 0; i < idx.tokens().size(); ++i) {
        if (idx.tokens().at(i).kind == LispTokenKind::Symbol && idx.tokens().at(i).length >= 3)
            words.insert(idx.tokenText(i).toLower());
    }
    QStringList list = words.values();
    list.sort();
    m_completionModel->setStringList(list);
}

void LispCodeEdit::showCompletion(bool force)
{
    const QString prefix = wordBeforeCursor();
    const int pos = textCursor().position();

    if (prefix.isEmpty() || (!force && prefix.size() < 2) || index().isInStringOrComment(pos) ||
        prefix.startsWith(QLatin1Char(':'))) {
        m_completer->popup()->hide();
        return;
    }
    rebuildCompletionWords();
    if (prefix != m_completer->completionPrefix()) {
        m_completer->setCompletionPrefix(prefix);
        m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    }
    // 只剩一個且已完全輸入：不必再提示
    if (m_completer->completionCount() == 0 ||
        (m_completer->completionCount() == 1 &&
         m_completer->currentCompletion().compare(prefix, Qt::CaseInsensitive) == 0)) {
        m_completer->popup()->hide();
        return;
    }
    QRect cr = cursorRect();
    cr.setWidth(m_completer->popup()->sizeHintForColumn(0)
                + m_completer->popup()->verticalScrollBar()->sizeHint().width());
    m_completer->complete(cr);
}

void LispCodeEdit::insertCompletion(const QString& completion)
{
    QTextCursor c = textCursor();
    const int len = wordBeforeCursor().size();
    c.setPosition(c.position() - len);
    c.setPosition(c.position() + len, QTextCursor::KeepAnchor);
    c.insertText(completion);
    setTextCursor(c);
}

void LispCodeEdit::mouseDoubleClickEvent(QMouseEvent* e)
{
    // 雙擊括號：選取成對括號（含括號本身）之間的全部內容
    if (e->button() == Qt::LeftButton) {
        const int pos = cursorForPosition(e->pos()).position();
        const LispDocumentIndex& idx = index();
        // 點在括號左側或右側都算：先看游標後方，再看前方
        for (int p : {pos, pos - 1}) {
            if (p < 0) continue;
            const int ti = idx.tokenIndexAt(p);
            if (ti < 0) continue;
            const LispToken& t = idx.tokens().at(ti);
            if (t.kind != LispTokenKind::OpenParen && t.kind != LispTokenKind::CloseParen) continue;
            const int mate = idx.matchingParen(t.start);
            if (mate < 0) continue;                            // 沒有配對：交給預設行為
            const int a = qMin(t.start, mate), b = qMax(t.start, mate) + 1;
            QTextCursor c(document());
            c.setPosition(a);
            c.setPosition(b, QTextCursor::KeepAnchor);
            setTextCursor(c);
            e->accept();
            return;
        }
    }
    QPlainTextEdit::mouseDoubleClickEvent(e);
}

// ═══════════════════════════════════════════════════════════════════════════
// 按鍵
// ═══════════════════════════════════════════════════════════════════════════

void LispCodeEdit::keyPressEvent(QKeyEvent* e)
{
    // 補全視窗開啟時，這些鍵交給 QCompleter 處理
    if (m_completer->popup()->isVisible()) {
        switch (e->key()) {
        case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Escape:
        case Qt::Key_Tab:   case Qt::Key_Backtab:
            e->ignore();
            return;
        default: break;
        }
    }

    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool ctrl = mods & Qt::ControlModifier;
    const bool alt  = mods & Qt::AltModifier;

    if (ctrl && !alt && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        Q_EMIT runFormRequested();
        return;
    }
    if (ctrl && !alt && (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal)) { setEditorFontSize(editorFontSize() + 1); return; }
    if (ctrl && !alt && e->key() == Qt::Key_Minus) { setEditorFontSize(editorFontSize() - 1); return; }
    if (ctrl && !alt && e->key() == Qt::Key_0)     { setEditorFontSize(defaultFontSize());    return; }
    if (ctrl && !alt && e->key() == Qt::Key_Space) {
        showCompletion(true);
        return;
    }
    if (!ctrl && !alt && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        handleEnter();
        return;
    }
    // Tab：有選取 → 選取的行整體縮排 2 格；沒有選取 → 依 Lisp 規則重排目前行
    // Shift+Tab：選取的行（或目前行）凸排 2 格
    if (!ctrl && !alt && e->key() == Qt::Key_Tab) {
        if (textCursor().hasSelection()) indentSelection(false);
        else                             handleTabIndent();
        return;
    }
    if (!ctrl && !alt && e->key() == Qt::Key_Backtab) { indentSelection(true); return; }
    if (!ctrl && !alt && e->key() == Qt::Key_Backspace && handleBackspacePair()) return;
    if (!ctrl && !alt && handleAutoPair(e->text())) return;

    QPlainTextEdit::keyPressEvent(e);

    // 輸入符號字元後自動提示；其他按鍵（移動、刪除…）關閉提示
    const QString t = e->text();
    if (!ctrl && !alt && t.size() == 1 && isSymChar(t.at(0))) showCompletion(false);
    else if (m_completer->popup()->isVisible() && e->key() != Qt::Key_Shift) {
        if (e->key() == Qt::Key_Backspace) showCompletion(false);
        else m_completer->popup()->hide();
    }
}

} // namespace ui
} // namespace aicad
