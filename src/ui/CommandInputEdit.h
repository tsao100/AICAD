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
    void    rebuildDocument();
    QString anchorAtPos(const QPoint& pos) const; // 轉換座標後呼叫 anchorAt
    void    updateLeftMargin();

    QTextDocument* m_promptDoc   = nullptr;
    QString        m_promptPrefix;
    QList<command::InputParser::ParsedOption> m_options;
    QString        m_hoveredAnchor;   // 目前 hover 的 href 值
    int            m_docWidth = 0;    // chips 區塊實際寬度，用於 setTextMargins

    QStringList m_history;
    int         m_historyIndex = -1;   // -1 = 目前輸入
    QString     m_savedInput;          // 暫存當前輸入（歷程瀏覽時）
};

} // namespace ui
} // namespace aicad

#endif
