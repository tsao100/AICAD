/**
 * @file LispCodeEdit.h
 * @brief Lisp 程式碼編輯控制項：行號、括號配對高亮、Lisp 風格自動縮排、自動成對括號 / 引號、
 *        自動補全、參數提示、錯誤範圍標示
 */
#ifndef AICAD_UI_LISPCODEEDIT_H
#define AICAD_UI_LISPCODEEDIT_H

#include <QPlainTextEdit>
#include <QStringList>
#include "scripting/LispLanguage.h"
#include "ui/LispSyntaxHighlighter.h"

class QCompleter;
class QStringListModel;

namespace aicad {
namespace ui {

class LispCodeEdit : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit LispCodeEdit(QWidget* parent = nullptr);
    ~LispCodeEdit() override;

    // ── 檔案狀態（由 LispEditorWidget 維護）───────────────────────────
    QString filePath;               ///< 空字串 = 尚未存檔
    bool    crlf = false;           ///< 原檔使用 CRLF 換行，存檔時照原樣寫回
    QString untitledName;           ///< 未存檔時顯示的名稱

    // ── 結構查詢 ──────────────────────────────────────────────────────
    /// 目前文字的結構索引（文件改變時自動重建）
    const scripting::LispDocumentIndex& index() const;

    // ── 執行輔助 ──────────────────────────────────────────────────────
    /// 全部文字（換行一律為 \n）
    QString wholeText() const { return toPlainText(); }
    /// 有選取 → 選取範圍；否則 → 游標所在的頂層表達式。成功回傳 true。
    bool selectionOrFormToRun(QString* code, int* offset) const;

    // ── 標示 ──────────────────────────────────────────────────────────
    void setErrorRange(int start, int end);
    void clearErrorRange();
    void flashRange(int start, int end);

    // ── 編輯命令 ──────────────────────────────────────────────────────
    void reindentSelection();       ///< 重新縮排選取的行（無選取 = 目前行）
    void reindentAll();
    void indentSelection(bool outdent);   ///< 選取的行整體縮排 / 凸排 2 格（無選取 = 目前行）
    void toggleComment();
    void goToLine(int line1Based);
    void goToPosition(int pos);

    // ── 外觀 ──────────────────────────────────────────────────────────
    void setTheme(const LispEditorTheme& theme);
    void setEditorFont(const QFont& font);
    /// 設定編輯區文字大小（pt，限制 6–48）；大小有變才會發出 fontSizeChanged
    void setEditorFontSize(qreal pointSize);
    qreal editorFontSize() const { return font().pointSizeF(); }
    /// 預設大小（建構時的系統固定寬度字型大小），Ctrl+0 還原用
    qreal defaultFontSize() const { return m_defaultPointSize; }
    /// 額外的補全字詞（例如由 ECL 查詢到、目前已定義的函式）
    void setExtraCompletions(const QStringList& words);

    int  lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

Q_SIGNALS:
    void runFormRequested();                        ///< Ctrl+Enter
    void callHintChanged(const QString& html);      ///< 空字串 = 清除
    void cursorInfoChanged(int line, int column);   ///< 皆為 1 起算
    void fontSizeChanged(qreal pointSize);          ///< Ctrl+滾輪 / Ctrl+± / Ctrl+0 縮放後

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private Q_SLOTS:
    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect& rect, int dy);
    void onCursorMoved();
    void insertCompletion(const QString& completion);

private:
    void refreshSelections();
    void handleEnter();
    void handleTabIndent();
    void reindentBlocks(int firstBlock, int lastBlock);
    bool reindentBlock(QTextCursor& c, const QTextBlock& block);
    bool handleAutoPair(const QString& text);
    bool handleBackspacePair();
    QString wordBeforeCursor() const;
    void showCompletion(bool force);
    void rebuildCompletionWords();
    QString buildHint(const scripting::LispCallContext& ctx) const;
    QChar charAt(int pos) const;

    class LineNumberArea;
    LineNumberArea*        m_lineNumbers;
    LispSyntaxHighlighter* m_highlighter;
    LispEditorTheme        m_theme;

    mutable scripting::LispDocumentIndex m_index;
    mutable int                          m_indexRevision = -1;

    QCompleter*        m_completer;
    QStringListModel*  m_completionModel;
    QStringList        m_extraWords;
    int                m_wordsRevision = -2;

    qreal m_defaultPointSize = 10;
    int m_errorStart = -1, m_errorEnd = -1;
    int m_flashStart = -1, m_flashEnd = -1;
};

} // namespace ui
} // namespace aicad

#endif
