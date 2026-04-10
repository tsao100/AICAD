#ifndef COMMANDINPUTEDIT_H
#define COMMANDINPUTEDIT_H

#include <QLineEdit>
#include <QStringList>

namespace aicad {
namespace ui {

// 支援上下鍵歷程、空白鍵重覆、Tab 補全
class CommandInputEdit : public QLineEdit {
    Q_OBJECT

public:
    explicit CommandInputEdit(QWidget* parent = nullptr);

    void setHistory(const QStringList& history);
    void addToHistory(const QString& cmd);

signals:
    void commandSubmitted(const QString& text);  // Enter 或空白鍵

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void historyUp();
    void historyDown();
    void repeatLastCommand();

    QStringList m_history;
    int         m_historyIndex = -1;   // -1 = 目前輸入
    QString     m_savedInput;          // 暫存當前輸入（歷程瀏覽時）
};

} // namespace ui
} // namespace aicad

#endif
