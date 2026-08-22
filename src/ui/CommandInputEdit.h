#ifndef COMMANDINPUTEDIT_H
#define COMMANDINPUTEDIT_H

#include "command/InputParser.h"
#include <QLineEdit>
#include <QStringList>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>

namespace aicad {
namespace ui {

// 支援上下鍵歷程、空白鍵重覆、Tab 補全
class CommandInputEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit CommandInputEdit(QWidget* parent = nullptr);

    void setHistory(const QStringList& history);
    void addToHistory(const QString& cmd);    
    void setPromptOptions(const QString& prefix,
                          const QList<command::InputParser::ParsedOption>& options);
    void setPromptText(const QString& text);
    void clearPromptOptions();

    // 送出目前輸入框內容，等同「按下 Enter」的最終效果（見 .cpp 檔內完整
    // 說明）。公開給 CadView 的滑鼠右鍵處理呼叫，讓「右鍵＝Enter」這個
    // 既有慣例（見 CadView::tryEndGetGeomSelectionViaRightClick() 的
    // 註解）在 InputType::YesNo 等其他等待輸入型別下也能沿用同一份邏輯：
    // 右鍵時輸入框若已有內容（例如使用者先打了 "y"）會一併送出，而不是
    // 像過去只能送出空字串（無法送出使用者已打的內容）。
    void submitCurrentLine();

signals:
    void commandSubmitted(const QString& text);  // Enter 或空白鍵
    void optionChipClicked(const QString& optionKey);
    void escapePressed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event)    override;
    void mousePressEvent(QMouseEvent* e)   override;
    void mouseMoveEvent(QMouseEvent* e)    override;
    void leaveEvent(QEvent* e)             override;
    void resizeEvent(QResizeEvent* e)      override;

private:
    void historyUp();
    void historyDown();
    void repeatLastCommand();
    void    rebuildDocument();
    QString anchorAtPos(const QPoint& pos) const; // 轉換座標後呼叫 anchorAt
    void    updateLeftMargin();

    // Lisp 多行輸入（括弧未對應時）
    static bool isLispParensBalanced(const QString& text);
    void        updateLispContinuationHint();

    QTextDocument* m_promptDoc   = nullptr;
    QString        m_promptPrefix;
    QString m_promptText;
    QList<command::InputParser::ParsedOption> m_options;
    QString        m_hoveredAnchor;   // 目前 hover 的 href 值
    int            m_docWidth = 0;    // chips 區塊實際寬度，用於 setTextMargins

    QStringList m_history;
    int         m_historyIndex = -1;   // -1 = 目前輸入
    QString     m_savedInput;          // 暫存當前輸入（歷程瀏覽時）

    // 是否正在輸入一個左右括弧尚未對應的 Lisp 表達式（跨多次 Enter 累積）
    bool    m_lispMode = false;
    // 已累積的 Lisp 表達式內容（以真正的 '\n' 分隔每次按 Enter 前的那一行）
    QString m_lispBuffer;
};

} // namespace ui
} // namespace aicad

#endif
