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
    void submitCurrentLine();      // Enter 與空白鍵（非 Lisp、非等待輸入時）共用的送出邏輯
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
